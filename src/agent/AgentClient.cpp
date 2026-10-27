#include "AgentClient.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <agent/Iso8601.h>
#include <agent/Log.h>

#include "TlsRoots.h"

// The ESP32 core's certificate bundle: the Mozilla roots as libmbedtls.a of the arduino-esp32
// framework carries them (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL in its sdkconfig).
// The symbol is the blob's start; WiFiClientSecure::setCACertBundle() takes it as is.
extern const uint8_t kCoreCertBundle[] asm("_binary_x509_crt_bundle_start");

namespace mm {

namespace {

constexpr int kMbedtlsCertVerifyFailed = -0x2700;  // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
constexpr size_t kBodyCap = 2048;                  // the config and heartbeat answers are a few hundred bytes

// One of mbedtls' two TLS content buffers as one allocation (CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN
// 16384 in this core's sdkconfig, plus its header room): what the largest free block has to hold
// for a handshake to be possible at all. The same figure as src/observer/Connection.h's
// kTlsBufferBytes, written here because this file includes nothing of the observer.
constexpr uint32_t kTlsBufferBytes = 16 * 1024 + 1024;

// A call that failed for want of memory, as far as this side can tell: mbedtls' own allocation
// failures (ssl, x509, pk, ecp, mpi), or a heap whose largest block cannot hold a content buffer
// once the failed call's client is gone. src/observer/Connection.h acts on the count.
bool failedForMemory(int tlsError) {
  switch (tlsError) {
    case -0x7F00:  // MBEDTLS_ERR_SSL_ALLOC_FAILED
    case -0x2880:  // MBEDTLS_ERR_X509_ALLOC_FAILED
    case -0x3F80:  // MBEDTLS_ERR_PK_ALLOC_FAILED
    case -0x4D80:  // MBEDTLS_ERR_ECP_ALLOC_FAILED
    case -0x0010:  // MBEDTLS_ERR_MPI_ALLOC_FAILED
      return true;
    default:
      return ESP.getMaxAllocHeap() < kTlsBufferBytes;
  }
}

// The two answer headers this firmware acts on. Retry-After is the wait a 429 names (API §1);
// X-RateLimit-Remaining is what is left of the 120 a minute, and is only ever logged.
// Not `const char* const[]`: HTTPClient::collectHeaders takes `const char*[]`.
const char* kWantedHeaders[] = {"Retry-After", "X-RateLimit-Remaining"};

// A Stream that writes into a fixed buffer and reads nothing: what HTTPClient::writeToStream()
// is handed, so a reply lands in the caller's buffer and in no String.
class BodySink : public Stream {
 public:
  BodySink(char* buf, size_t cap) : buf_(buf), cap_(cap) {}
  size_t write(uint8_t c) override {
    if (len_ + 1 < cap_) buf_[len_++] = static_cast<char>(c);
    return 1;  // always "taken": what does not fit is dropped, and the parser reports the cut
  }
  size_t write(const uint8_t* data, size_t size) override {
    const size_t room = len_ + 1 < cap_ ? cap_ - 1 - len_ : 0;
    const size_t n = size < room ? size : room;
    if (n > 0) std::memcpy(buf_ + len_, data, n);
    len_ += n;
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t length() const { return len_; }

 private:
  char* buf_;
  size_t cap_;
  size_t len_ = 0;
};

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

void AgentClient::begin(SettingsStore& settings, AgentClock& clock, WifiLink& wifi, const char* agentVersion) {
  settings_ = &settings;
  clock_ = &clock;
  wifi_ = &wifi;
  version_ = agentVersion;
  reconfigure();
}

IUplink::LinkState AgentClient::link() const {
  LinkState state;
  if (wifi_ == nullptr) return state;
  state.up = wifi_->connected();
  state.connects = wifi_->connects();
  state.drops = wifi_->drops();
  return state;
}

void AgentClient::reconfigure() {
  // The stored URL, else the key's fallback (https://meshmaster.dk): this project has one server,
  // and a board that was given only its token talks to it.
  settings_->effective(*findKey("meshmaster.url"), url_, sizeof url_);
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
    // The body straight into the caller's buffer: writeToStream() reads a Content-Length or a
    // chunked body alike, and the sink below takes what fits in bodyCap and drops the rest, which
    // the parser then reports. No String is made of it - a claim carries a repeater's password,
    // and a String would be one more heap copy of it that nothing wipes (agent/Credentials.h).
    // The chunk buffer HTTPClient moves the bytes through on the way is the one transient this
    // code cannot wipe, the same shape as the .NET agent's HttpClient.
    BodySink sink(body, bodyCap);
    http.writeToStream(&sink);
    bodyLen = sink.length();
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
  out.via = rootName(root_);

  if (!configured()) {
    std::snprintf(out.error, sizeof out.error, "not configured: meshmaster.token is needed");
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
    out.via = rootName(root_);
    out.error[0] = '\0';
    limiter_.took(millis());
    code = request(root_, method, path, requestBody, requestLen, body, bodyCap, bodyLen, tlsError, out);
  }
  out.atEpoch = clock_->now();
  out.httpStatus = code > 0 ? code : 0;
  out.reachable = code > 0;
  out.ok = code == 200;
  if (code <= 0 && failedForMemory(tlsError)) {
    memoryFailures_++;
    LOG_W("server: the call failed for want of memory (tls %d, largest free block %lu); %lu such failure(s)", tlsError,
          static_cast<unsigned long>(ESP.getMaxAllocHeap()), static_cast<unsigned long>(memoryFailures_));
  }
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
  o.via = c.via;
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

  // Two jobs with a label, a path and an idempotency key is about a kilobyte, and a config.apply
  // whose plan holds the region loader's sequence (task 05: forty-odd lines of fifty bytes each
  // as JSON) is three more; this is room for the claim the server may hand over and no more,
  // and a longer one is cut and reported.
  static char body[6144];
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
  // The claim carries a repeater's password: it is parsed in place, the job keeps the one copy
  // it needs, and the reply's bytes are wiped here whether the parse took or not.
  const bool parsed = parseClaimResponse(body, bodyLen, jobs, max, count, serverTime, err, sizeof err);
  wipe(body, sizeof body);
  if (!parsed) {
    for (int i = 0; i < max; i++) jobs[i].credential.wipe();
    count = 0;
    lastCall_.ok = false;
    std::snprintf(lastCall_.error, sizeof lastCall_.error, "http 200 but the claim did not parse: %s", err);
    LOG_E("server: claim: %s", lastCall_.error);
    return false;
  }
  int32_t difference = 0;
  clock_->takeServerTime(serverTime, difference);
  return true;
}

bool AgentClient::postToJob(long long jobId, const char* action, const char* what, const char* requestBody, size_t len,
                            char* body, size_t bodyCap, size_t& bodyLen) {
  char endpoint[48];
  std::snprintf(endpoint, sizeof endpoint, "/agent/v1/jobs/%lld/%s", jobId, action);

  Call c;
  if (!call("POST", endpoint, requestBody, len, body, bodyCap, bodyLen, c)) {
    LOG_W("server: %s for job %lld: no reply: %s", what, jobId, c.error);
    return false;
  }
  if (!c.ok) {
    LOG_W("server: %s for job %lld: http %d", what, jobId, c.httpStatus);
    if (bodyLen > 0) LOG_D("server: body (%u bytes): %.200s", static_cast<unsigned>(bodyLen), body);
    return false;
  }
  return true;
}

namespace {
// The short replies of the three per-job POSTs - a result's `{jobId, duplicate, ...}`, an
// extend's new lease, a progress's `recordedAt` - share one buffer: the loop makes one call at
// a time, and three statics of 256 would be two more than the board had before task p1.5-15.
char g_jobReply[256];
}  // namespace

bool AgentClient::postJobResult(long long jobId, const char* requestBody, size_t len) {
  size_t bodyLen = 0;
  if (!postToJob(jobId, "result", "result", requestBody, len, g_jobReply, sizeof g_jobReply, bodyLen)) return false;
  LOG_D("server: result for job %lld taken: %.200s", jobId, g_jobReply);
  return true;
}

bool AgentClient::extendLease(long long jobId, int seconds, uint32_t& leaseUntil) {
  leaseUntil = 0;
  char request[32];
  const size_t requestLen = writeExtendRequest(seconds, request, sizeof request);
  size_t bodyLen = 0;
  if (!postToJob(jobId, "extend", "extend", request, requestLen, g_jobReply, sizeof g_jobReply, bodyLen)) return false;
  ExtendReply r;
  char err[64];
  if (!parseExtendReply(g_jobReply, bodyLen, r, err, sizeof err)) {
    lastCall_.ok = false;
    std::snprintf(lastCall_.error, sizeof lastCall_.error, "http 200 but the reply did not parse: %s", err);
    LOG_E("server: extend for job %lld: %s", jobId, lastCall_.error);
    return false;
  }
  leaseUntil = r.leaseUntilUnix;
  LOG_D("server: the lease of job %lld now ends at %s", jobId, r.leaseUntil);
  return true;
}

bool AgentClient::postProgress(long long jobId, const char* requestBody, size_t len) {
  size_t bodyLen = 0;
  if (!postToJob(jobId, "progress", "progress", requestBody, len, g_jobReply, sizeof g_jobReply, bodyLen)) return false;
  ProgressReply r;
  char err[64];
  if (!parseProgressReply(g_jobReply, bodyLen, r, err, sizeof err)) {
    lastCall_.ok = false;
    std::snprintf(lastCall_.error, sizeof lastCall_.error, "http 200 but the reply did not parse: %s", err);
    LOG_E("server: progress for job %lld: %s", jobId, lastCall_.error);
    return false;
  }
  LOG_D("server: progress of job %lld recorded at %s", jobId, r.recordedAt);
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
                  config_.budget.minGapSeconds, config_.pollIntervalS, last_.via);
  } else if (last_.httpStatus != 0) {
    std::snprintf(out, cap, "%s: http %d at %s%s%s, root %s", url_, last_.httpStatus, when, last_.error[0] ? ", " : "",
                  last_.error, last_.via);
  } else {
    std::snprintf(out, cap, "%s: failed at %s: %s (root %s)", url_, when, last_.error, last_.via);
  }
}


}  // namespace mm
