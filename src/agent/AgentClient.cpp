#include "AgentClient.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

#include "Iso8601.h"
#include "Log.h"
#include "TlsRoots.h"

// The ESP32 core's certificate bundle: the Mozilla roots as libmbedtls.a of the arduino-esp32
// framework carries them (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL in its sdkconfig).
// The symbol is the blob's start; WiFiClientSecure::setCACertBundle() takes it as is.
extern const uint8_t kCoreCertBundle[] asm("_binary_x509_crt_bundle_start");

namespace mm {

namespace {

constexpr int kMbedtlsCertVerifyFailed = -0x2700;  // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
constexpr size_t kBodyCap = 2048;                  // the config answer is a few hundred bytes

void wipe(char* buf, size_t len) {
  volatile char* p = buf;
  for (size_t i = 0; i < len; i++) p[i] = 0;
}

}  // namespace

const char* AgentClient::rootName(Root r) { return r == Root::IsrgRootX1 ? "ISRG Root X1" : "the core's bundle"; }

void AgentClient::begin(SettingsStore& settings, AgentClock& clock, const char* agentVersion) {
  settings_ = &settings;
  clock_ = &clock;
  version_ = agentVersion;
  reconfigure();
}

void AgentClient::reconfigure() {
  settings_->get(*findKey("meshmaster.url"), url_, sizeof url_);
  // A trailing slash is not part of the origin.
  size_t n = std::strlen(url_);
  while (n > 0 && url_[n - 1] == '/') url_[--n] = '\0';
  tokenSet_ = settings_->isSet(*findKey("meshmaster.token"));
  last_ = Outcome();
  hasConfig_ = false;
}

int AgentClient::request(Root root, char* body, size_t bodyCap, size_t& bodyLen, int& tlsError, char* err,
                         size_t errCap) {
  bodyLen = 0;
  tlsError = 0;
  err[0] = '\0';

  WiFiClientSecure client;
  if (root == Root::IsrgRootX1) {
    client.setCACert(kIsrgRootX1Pem);
  } else {
    client.setCACertBundle(kCoreCertBundle);
  }
  client.setHandshakeTimeout(30);

  char path[kMaxValueLen + 32];
  std::snprintf(path, sizeof path, "%s/agent/v1/config", url_);

  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.setUserAgent(String("meshmaster-agent/") + version_);
  if (!http.begin(client, path)) {
    std::snprintf(err, errCap, "the URL was refused by the client");
    return 0;
  }

  {
    // The token: NVS -> this buffer -> the header, and wiped. HTTPClient keeps its own copy of
    // the header line until end(), which is the one copy this code cannot wipe.
    char token[kMaxValueLen + 1] = {0};
    char bearer[kMaxValueLen + 8] = {0};
    settings_->get(*findKey("meshmaster.token"), token, sizeof token);
    std::snprintf(bearer, sizeof bearer, "Bearer %s", token);
    http.addHeader("Authorization", bearer);
    wipe(token, sizeof token);
    wipe(bearer, sizeof bearer);
  }
  http.addHeader("X-Agent-Version", version_);
  http.addHeader("Accept", "application/json");

  const int code = http.GET();
  if (code > 0) {
    // getString() reads a Content-Length or a chunked body alike; the answer is a few hundred
    // bytes and anything past bodyCap is cut, which the parser then reports.
    const String reply = http.getString();
    bodyLen = reply.length() < bodyCap - 1 ? reply.length() : bodyCap - 1;
    std::memcpy(body, reply.c_str(), bodyLen);
    body[bodyLen] = '\0';
  } else {
    char reason[96] = {0};
    tlsError = client.lastError(reason, sizeof reason);
    if (tlsError != 0) {
      std::snprintf(err, errCap, "%s (tls %d: %s)", http.errorToString(code).c_str(), tlsError, reason);
    } else {
      std::snprintf(err, errCap, "%s", http.errorToString(code).c_str());
    }
  }
  http.end();
  client.stop();
  return code;
}

bool AgentClient::fetchConfig() {
  Outcome o;
  o.attempted = true;
  o.atMillis = millis();
  o.atEpoch = clock_->now();
  o.root = root_;

  if (!configured()) {
    std::snprintf(o.error, sizeof o.error, "not configured: meshmaster.url and meshmaster.token are needed");
    last_ = o;
    return false;
  }

  static char body[kBodyCap];
  size_t bodyLen = 0;
  int tlsError = 0;
  const uint32_t heapBefore = ESP.getFreeHeap();
  LOG_I("server: GET %s/agent/v1/config (root: %s)", url_, rootName(root_));
  int code = request(root_, body, sizeof body, bodyLen, tlsError, o.error, sizeof o.error);

  if (code <= 0 && tlsError == kMbedtlsCertVerifyFailed) {
    // The chain did not end at the root tried. Try the other one, once, and remember.
    const Root other = root_ == Root::IsrgRootX1 ? Root::CoreBundle : Root::IsrgRootX1;
    LOG_W("server: the certificate chain did not verify against %s; trying %s", rootName(root_), rootName(other));
    root_ = other;
    o.root = root_;
    code = request(root_, body, sizeof body, bodyLen, tlsError, o.error, sizeof o.error);
  }
  o.heapAfter = ESP.getFreeHeap();
  o.atEpoch = clock_->now();
  LOG_I("heap: %lu free after the call (%lu before), min ever %lu", static_cast<unsigned long>(o.heapAfter),
        static_cast<unsigned long>(heapBefore), static_cast<unsigned long>(ESP.getMinFreeHeap()));

  if (code <= 0) {
    o.ok = false;
    LOG_E("server: no reply: %s", o.error);
    last_ = o;
    return false;
  }

  o.httpStatus = code;
  if (code != 200) {
    o.ok = false;
    LOG_E("server: http %d from GET /agent/v1/config", code);
    if (bodyLen > 0) LOG_D("server: body (%u bytes): %.200s", static_cast<unsigned>(bodyLen), body);
    last_ = o;
    return false;
  }

  AgentConfig cfg;
  char err[64];
  if (!parseAgentConfig(body, bodyLen, cfg, err, sizeof err)) {
    o.ok = false;
    std::snprintf(o.error, sizeof o.error, "http 200 but the reply did not parse: %s", err);
    LOG_E("server: %s", o.error);
    LOG_D("server: body (%u bytes): %.200s", static_cast<unsigned>(bodyLen), body);
    last_ = o;
    return false;
  }

  int32_t diff = 0;
  const bool moved = clock_->takeServerTime(cfg.serverTimeUnix, diff);
  o.ok = true;
  o.atEpoch = clock_->now();
  config_ = cfg;
  hasConfig_ = true;
  last_ = o;
  LOG_I("server: config ok: agent %lld \"%s\", transport %s, budget %d/min gap %d s, poll %d s, serverTime %s%s",
        cfg.agentId, cfg.label, cfg.transport, cfg.budget.maxTransactionsPerMinute, cfg.budget.minGapSeconds,
        cfg.pollIntervalS, cfg.serverTime, moved ? " (clock set from it)" : "");
  if (std::strcmp(cfg.transport, "onboard") != 0) {
    LOG_W("server: the exit node is registered with transport '%s', this board is 'onboard'", cfg.transport);
  }
  return true;
}

void AgentClient::describe(char* out, size_t cap) const {
  if (!configured()) {
    std::snprintf(out, cap, "not configured (%s%s%s)", url_[0] == '\0' ? "meshmaster.url unset" : "",
                  (url_[0] == '\0' && !tokenSet_) ? ", " : "", tokenSet_ ? "" : "meshmaster.token unset");
    return;
  }
  if (!last_.attempted) {
    std::snprintf(out, cap, "%s: not called yet", url_);
    return;
  }
  char when[24];
  if (last_.atEpoch != 0) {
    formatClock(last_.atEpoch, when, sizeof when);
  } else {
    std::snprintf(when, sizeof when, "+%lus", static_cast<unsigned long>(last_.atMillis / 1000));
  }
  if (last_.ok) {
    std::snprintf(out, cap, "%s: config ok at %s, agent %lld \"%s\", transport %s, budget %d/min gap %d s, poll %d s, root %s",
                  url_, when, config_.agentId, config_.label, config_.transport, config_.budget.maxTransactionsPerMinute,
                  config_.budget.minGapSeconds, config_.pollIntervalS, rootName(last_.root));
  } else if (last_.httpStatus != 0) {
    std::snprintf(out, cap, "%s: http %d at %s%s%s, root %s", url_, last_.httpStatus, when, last_.error[0] ? ", " : "",
                  last_.error, rootName(last_.root));
  } else {
    std::snprintf(out, cap, "%s: failed at %s: %s (root %s)", url_, when, last_.error, rootName(last_.root));
  }
}

void AgentClient::oledLine(char* out, size_t cap) const {
  if (!configured()) {
    std::snprintf(out, cap, "no server set");
    return;
  }
  if (!last_.attempted) {
    std::snprintf(out, cap, "server: waiting");
    return;
  }
  if (last_.ok) {
    char when[12];
    if (last_.atEpoch != 0) {
      formatClock(last_.atEpoch, when, sizeof when);
    } else {
      std::snprintf(when, sizeof when, "+%lus", static_cast<unsigned long>(last_.atMillis / 1000));
    }
    std::snprintf(out, cap, "config ok %s", when);
    return;
  }
  if (last_.httpStatus != 0) {
    std::snprintf(out, cap, "http %d", last_.httpStatus);
    return;
  }
  std::snprintf(out, cap, "tls/net failed");
}

}  // namespace mm
