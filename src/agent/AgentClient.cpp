#include "AgentClient.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstdlib>
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
constexpr size_t kBodyCap = 2048;                  // the config and heartbeat answers are a few hundred bytes

// The two answer headers this firmware acts on. Retry-After is the wait a 429 names (API §1);
// X-RateLimit-Remaining is what is left of the 120 a minute, and is only ever logged.
// Not `const char* const[]`: HTTPClient::collectHeaders takes `const char*[]`.
const char* kWantedHeaders[] = {"Retry-After", "X-RateLimit-Remaining"};

void wipe(char* buf, size_t len) {
  volatile char* p = buf;
  for (size_t i = 0; i < len; i++) p[i] = 0;
}

// Retry-After is "a number of seconds" or an HTTP date. The server sends seconds (API §1, "the
// exact wait"); a date is honoured as a minute rather than parsed, which is the safe way round.
uint32_t parseRetryAfter(const String& value) {
  if (value.length() == 0) return 0;
  char* end = nullptr;
  const long seconds = std::strtol(value.c_str(), &end, 10);
  if (end != value.c_str() && *end == '\0' && seconds >= 0) {
    return seconds > 3600 ? 3600u : static_cast<uint32_t>(seconds);
  }
  return 60;
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
  lastCall_ = Call();
  hasConfig_ = false;
}

int AgentClient::request(Root root, const char* method, const char* path, const char* requestBody, size_t requestLen,
                         char* body, size_t bodyCap, size_t& bodyLen, int& tlsError, Call& out) {
  bodyLen = 0;
  tlsError = 0;

  WiFiClientSecure client;
  if (root == Root::IsrgRootX1) {
    client.setCACert(kIsrgRootX1Pem);
  } else {
    client.setCACertBundle(kCoreCertBundle);
  }
  client.setHandshakeTimeout(30);

  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.setUserAgent(String("meshmaster-agent/") + version_);
  if (!http.begin(client, path)) {
    std::snprintf(out.error, sizeof out.error, "the URL was refused by the client");
    return 0;
  }
  http.collectHeaders(kWantedHeaders, sizeof kWantedHeaders / sizeof kWantedHeaders[0]);

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
  if (requestBody != nullptr) http.addHeader("Content-Type", "application/json");

  const bool post = std::strcmp(method, "POST") == 0;
  const int code = post ? http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(requestBody)), requestLen)
                        : http.GET();
  if (code > 0) {
    // getString() reads a Content-Length or a chunked body alike; the answer is a few hundred
    // bytes and anything past bodyCap is cut, which the parser then reports.
    const String reply = http.getString();
    bodyLen = reply.length() < bodyCap - 1 ? reply.length() : bodyCap - 1;
    std::memcpy(body, reply.c_str(), bodyLen);
    body[bodyLen] = '\0';
    out.retryAfterS = parseRetryAfter(http.header("Retry-After"));
    const String remaining = http.header("X-RateLimit-Remaining");
    out.rateLimitRemaining = remaining.length() == 0 ? -1 : remaining.toInt();
  } else {
    char reason[96] = {0};
    tlsError = client.lastError(reason, sizeof reason);
    if (tlsError != 0) {
      std::snprintf(out.error, sizeof out.error, "%s (tls %d: %s)", http.errorToString(code).c_str(), tlsError, reason);
    } else {
      std::snprintf(out.error, sizeof out.error, "%s", http.errorToString(code).c_str());
    }
  }
  http.end();
  client.stop();
  return code;
}

bool AgentClient::call(const char* method, const char* endpoint, const char* requestBody, size_t requestLen,
                       char* body, size_t bodyCap, size_t& bodyLen, Call& out) {
  out = Call();
  out.atMillis = millis();
  out.atEpoch = clock_->now();
  out.root = root_;

  if (!configured()) {
    std::snprintf(out.error, sizeof out.error, "not configured: meshmaster.url and meshmaster.token are needed");
    lastCall_ = out;
    return false;
  }
  if (!limiter_.take(out.atMillis)) {
    const uint32_t wait = limiter_.waitMs(out.atMillis);
    std::snprintf(out.error, sizeof out.error, "held back by the 120-a-minute ceiling for %lu s",
                  static_cast<unsigned long>((wait + 999) / 1000));
    LOG_W("server: %s", out.error);
    lastCall_ = out;
    return false;
  }

  char path[kMaxValueLen + 48];
  std::snprintf(path, sizeof path, "%s%s", url_, endpoint);

  int tlsError = 0;
  int code = request(root_, method, path, requestBody, requestLen, body, bodyCap, bodyLen, tlsError, out);
  if (code <= 0 && tlsError == kMbedtlsCertVerifyFailed) {
    // The chain did not end at the root tried. Try the other one, once, and remember.
    const Root other = root_ == Root::IsrgRootX1 ? Root::CoreBundle : Root::IsrgRootX1;
    LOG_W("server: the certificate chain did not verify against %s; trying %s", rootName(root_), rootName(other));
    root_ = other;
    out.root = root_;
    out.error[0] = '\0';
    limiter_.took(millis());
    code = request(root_, method, path, requestBody, requestLen, body, bodyCap, bodyLen, tlsError, out);
  }
  out.atEpoch = clock_->now();
  out.httpStatus = code > 0 ? code : 0;
  out.reachable = code > 0;
  out.ok = code == 200;
  lastCall_ = out;
  return out.reachable;
}

bool AgentClient::fetchConfig() {
  static char body[kBodyCap];
  size_t bodyLen = 0;
  Call c;
  const uint32_t heapBefore = ESP.getFreeHeap();
  LOG_I("server: GET %s/agent/v1/config (root: %s)", url_, rootName(root_));
  const bool reachable = call("GET", "/agent/v1/config", nullptr, 0, body, sizeof body, bodyLen, c);

  Outcome o;
  o.attempted = true;
  o.atMillis = c.atMillis;
  o.atEpoch = c.atEpoch;
  o.root = c.root;
  o.heapAfter = ESP.getFreeHeap();
  std::snprintf(o.error, sizeof o.error, "%s", c.error);
  LOG_I("heap: %lu free after the call (%lu before), min ever %lu", static_cast<unsigned long>(o.heapAfter),
        static_cast<unsigned long>(heapBefore), static_cast<unsigned long>(ESP.getMinFreeHeap()));

  if (!reachable) {
    LOG_E("server: no reply: %s", o.error);
    last_ = o;
    return false;
  }

  o.httpStatus = c.httpStatus;
  if (!c.ok) {
    LOG_E("server: http %d from GET /agent/v1/config", c.httpStatus);
    if (bodyLen > 0) LOG_D("server: body (%u bytes): %.200s", static_cast<unsigned>(bodyLen), body);
    last_ = o;
    return false;
  }

  AgentConfig cfg;
  char err[64];
  if (!parseAgentConfig(body, bodyLen, cfg, err, sizeof err)) {
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

bool AgentClient::postHeartbeat(const Heartbeat& hb, HeartbeatReply& reply) {
  static char requestJson[1024];
  const size_t len = writeHeartbeat(hb, requestJson, sizeof requestJson);
  if (len == 0) {
    lastCall_ = Call();
    std::snprintf(lastCall_.error, sizeof lastCall_.error, "the heartbeat body did not fit in %u bytes",
                  static_cast<unsigned>(sizeof requestJson));
    LOG_E("server: %s", lastCall_.error);
    return false;
  }

  static char body[kBodyCap];
  size_t bodyLen = 0;
  Call c;
  if (!call("POST", "/agent/v1/heartbeat", requestJson, len, body, sizeof body, bodyLen, c)) {
    LOG_W("server: heartbeat: no reply: %s", c.error);
    return false;
  }
  if (!c.ok) {
    LOG_W("server: heartbeat: http %d%s", c.httpStatus, c.httpStatus == 401 ? " - the token was refused" : "");
    if (bodyLen > 0) LOG_D("server: body (%u bytes): %.200s", static_cast<unsigned>(bodyLen), body);
    return false;
  }

  char err[64];
  if (!parseHeartbeatReply(body, bodyLen, reply, err, sizeof err)) {
    lastCall_.ok = false;
    std::snprintf(lastCall_.error, sizeof lastCall_.error, "http 200 but the reply did not parse: %s", err);
    LOG_E("server: heartbeat: %s", lastCall_.error);
    return false;
  }
  return true;
}

bool AgentClient::postObservations(const char* requestBody, size_t len, int& accepted, bool& duplicate) {
  static char body[kBodyCap];
  size_t bodyLen = 0;
  Call c;
  accepted = 0;
  duplicate = false;
  if (!call("POST", "/agent/v1/observations", requestBody, len, body, sizeof body, bodyLen, c)) {
    LOG_W("server: observations: no reply: %s", c.error);
    return false;
  }
  if (!c.ok) {
    LOG_W("server: observations: http %d", c.httpStatus);
    if (bodyLen > 0) LOG_D("server: body (%u bytes): %.200s", static_cast<unsigned>(bodyLen), body);
    return false;
  }
  ObservationsReply r;
  char err[64];
  if (!parseObservationsReply(body, bodyLen, r, err, sizeof err)) {
    lastCall_.ok = false;
    std::snprintf(lastCall_.error, sizeof lastCall_.error, "http 200 but the reply did not parse: %s", err);
    LOG_E("server: observations: %s", lastCall_.error);
    return false;
  }
  accepted = r.accepted;
  duplicate = r.duplicate;
  return true;
}

bool AgentClient::claimJobs(ClaimedJob* jobs, int max, int& count) {
  count = 0;
  char request[32];
  const size_t requestLen = writeClaimRequest(max, request, sizeof request);

  // Two jobs with a label, a path and an idempotency key is about a kilobyte; this is room for
  // the claim the server may hand over and no more, and a longer one is cut and reported.
  static char body[3072];
  size_t bodyLen = 0;
  Call c;
  if (!call("POST", "/agent/v1/jobs/claim", request, requestLen, body, sizeof body, bodyLen, c)) {
    LOG_W("server: claim: no reply: %s", c.error);
    return false;
  }
  if (!c.ok) {
    LOG_W("server: claim: http %d", c.httpStatus);
    if (bodyLen > 0) LOG_D("server: body (%u bytes): %.200s", static_cast<unsigned>(bodyLen), body);
    return false;
  }
  uint32_t serverTime = 0;
  char err[80];
  if (!parseClaimResponse(body, bodyLen, jobs, max, count, serverTime, err, sizeof err)) {
    lastCall_.ok = false;
    std::snprintf(lastCall_.error, sizeof lastCall_.error, "http 200 but the claim did not parse: %s", err);
    LOG_E("server: claim: %s", lastCall_.error);
    return false;
  }
  int32_t difference = 0;
  clock_->takeServerTime(serverTime, difference);
  return true;
}

bool AgentClient::postJobResult(long long jobId, const char* requestBody, size_t len) {
  char endpoint[48];
  std::snprintf(endpoint, sizeof endpoint, "/agent/v1/jobs/%lld/result", jobId);

  static char body[256];
  size_t bodyLen = 0;
  Call c;
  if (!call("POST", endpoint, requestBody, len, body, sizeof body, bodyLen, c)) {
    LOG_W("server: result for job %lld: no reply: %s", jobId, c.error);
    return false;
  }
  if (!c.ok) {
    LOG_W("server: result for job %lld: http %d", jobId, c.httpStatus);
    if (bodyLen > 0) LOG_D("server: body (%u bytes): %.200s", static_cast<unsigned>(bodyLen), body);
    return false;
  }
  LOG_D("server: result for job %lld taken: %.200s", jobId, body);
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
