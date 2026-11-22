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
#include "platform/TlsHeap.h"

// The ESP32 core's certificate bundle: the Mozilla roots as libmbedtls.a of the arduino-esp32
// framework carries them (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL in its sdkconfig).
// The symbol is the blob's start; WiFiClientSecure::setCACertBundle() takes it as is.
extern const uint8_t kCoreCertBundle[] asm("_binary_x509_crt_bundle_start");

namespace mm {

namespace {

constexpr int kMbedtlsCertVerifyFailed = -0x2700;  // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
constexpr size_t kBodyCap = 2048;                  // the config and heartbeat answers are a few hundred bytes

// The lowest free heap seen during a call (task p1.8-30) is read from src/platform/TlsHeap.h's
// wrapper of mbedtls's allocator, which every allocation mbedtls makes passes through - the
// handshake's certificate chain, the content buffers, the session's keys - with a
// read of the free heap right after it: exact for mbedtls's share of the call, which is nearly
// all of it. The rest (lwIP's segments, HTTPClient's Strings and its 1 KB chunk buffer) is
// caught by the four samples request() and call() take, and by the board's minimum-ever: if
// that moved during the call, the call is what moved it and the figure is exact for everything.
// Since task p1.8-31 the same wrapper is where a board with PSRAM puts its TLS buffers.

// The larger of mbedtls' two TLS content buffers as one allocation
// (CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN 16384 in sdkconfig.defaults since task p1.9-36, which sizes
// the pair apart - 4096 out - plus its header room): what the largest free block has to hold for
// a handshake to be possible at all. The same figure as src/observer/Connection.h's
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

// The answer headers this firmware acts on. Retry-After is the wait a 429 names (API §1);
// X-RateLimit-Remaining is what is left of the 120 a minute, and is only ever logged. ETag and
// Content-Range are the firmware routes' (task p1.15-59, unit-protocol.md §4): the strong tag a
// resume sends back as If-Range, and the bytes a 206 really answered with.
// Not `const char* const[]`: HTTPClient::collectHeaders takes `const char*[]`.
const char* kWantedHeaders[] = {"Retry-After", "X-RateLimit-Remaining", "ETag", "Content-Range"};

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

// A Stream that hands every byte straight to an update::HttpBody and keeps none: what
// HTTPClient::writeToStream() is given for a firmware route, so that 64 kB of an image goes from
// the socket into the other application slot without a buffer of its own anywhere between.
// Returning 0 from write() is how writeToStream() is told to stop, which is what a sink that
// refused the bytes means.
class SinkStream : public Stream {
 public:
  explicit SinkStream(mm::update::HttpBody& body) : body_(body) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (refused_) return 0;
    if (!body_.take(data, size)) {
      refused_ = true;
      return 0;
    }
    len_ += size;
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  uint32_t length() const { return len_; }

 private:
  mm::update::HttpBody& body_;
  uint32_t len_ = 0;
  bool refused_ = false;
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
  linkConnectsSeen_ = wifi.connects();
  // The HTTP client outlives every call (one connection, kept: KeepAlive.h); the TLS client
  // under it is made at each handshake, in exchange(). The timeouts are what every call had.
  if (http_ == nullptr) {
    http_ = new HTTPClient();
    http_->setReuse(true);
    http_->setConnectTimeout(10000);
    http_->setTimeout(15000);
    http_->setUserAgent(String("meshmaster-agent/") + version_);
    // §4: nothing on the firmware routes is ever a redirect, and this firmware follows none on any
    // route. It is the HTTPClient's default and it is said here so that it is not a default.
    http_->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http_->collectHeaders(kWantedHeaders, sizeof kWantedHeaders / sizeof kWantedHeaders[0]);
  }
  tlsheap::begin();
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
  // A new URL or a new token: the kept connection was made for the old ones.
  line_.drop(*this);
}

// ---- the wire under KeepAlive ----------------------------------------------------------------

bool AgentClient::connected() { return client_ != nullptr && client_->connected(); }

void AgentClient::close() {
  if (client_ == nullptr) return;
  client_->stop();  // frees mbedtls's context, the chain and both content buffers
  delete client_;
  client_ = nullptr;
}

uplink::Attempt AgentClient::exchange(Root root) {
  uplink::Attempt a;
  Pending& p = pending_;
  if (client_ == nullptr) {
    // A handshake: a client with no history, verifying against the root this call wants.
    client_ = new WiFiClientSecure();
    if (root == Root::IsrgRootX1) {
      client_->setCACert(kIsrgRootX1Pem);
    } else {
      client_->setCACertBundle(kCoreCertBundle);
    }
    client_->setHandshakeTimeout(30);
  }
  if (!http_->begin(*client_, p.path)) {
    std::snprintf(p.error, sizeof p.error, "the URL was refused by the client");
    return a;  // code 0: no reply
  }

  {
    // The token: NVS -> this buffer -> the header, and wiped. HTTPClient keeps its own copy of
    // the header line until end(), which is the one copy this code cannot wipe.
    char token[kMaxValueLen + 1] = {0};
    char bearer[kMaxValueLen + 8] = {0};
    settings_->get(*findKey("meshmaster.token"), token, sizeof token);
    std::snprintf(bearer, sizeof bearer, "Bearer %s", token);
    http_->addHeader("Authorization", bearer);
    wipe(token, sizeof token);
    wipe(bearer, sizeof bearer);
  }
  http_->addHeader("X-Agent-Version", version_);
  http_->addHeader("Accept", p.sink != nullptr ? "*/*" : "application/json");
  if (p.requestBody != nullptr) http_->addHeader("Content-Type", "application/json");
  // The firmware routes' two request headers (§4): one closed range, and the tag the first range
  // came back with so that a file replaced under the same URL answers 200 and starts again.
  if (p.rangeHeader != nullptr) http_->addHeader("Range", p.rangeHeader);
  if (p.ifRange != nullptr) http_->addHeader("If-Range", p.ifRange);

  const bool post = std::strcmp(p.method, "POST") == 0;
  const int code = post ? http_->POST(reinterpret_cast<uint8_t*>(const_cast<char*>(p.requestBody)), p.requestLen)
                        : http_->GET();
  tlsheap::sample();  // the status line is in: the handshake, if there was one, is behind us
  if (code > 0) {
    if (p.sink != nullptr) {
      // A firmware route (§4): the two answer headers first, then the body straight into the
      // caller's sink - the other application slot, one range at a time, nothing held in RAM.
      // The sink is shown the status line before a byte of the body, because what a 206 means and
      // what a 200 means to a range are different things (update/Fetch.h).
      std::snprintf(p.etag, sizeof p.etag, "%s", http_->header("ETag").c_str());
      std::snprintf(p.contentRange, sizeof p.contentRange, "%s", http_->header("Content-Range").c_str());
      update::HttpAnswer head;
      head.reachable = true;
      head.status = code;
      std::snprintf(head.etag, sizeof head.etag, "%s", p.etag);
      std::snprintf(head.contentRange, sizeof head.contentRange, "%s", p.contentRange);
      if (p.sink->begin(head)) {
        SinkStream out(*p.sink);
        // **What the client returns is read** (task p1.19-68). Before 68 it was thrown away, and
        // a range that stopped short - three of Aarhus C's did on 20 November - said nothing at
        // all of why. writeToStream() answers with the bytes it moved, or one of HTTPClient's own
        // negative codes; the TLS layer's error for the same moment is the client's, and has to be
        // read before end() stops the socket.
        const int wrote = http_->writeToStream(&out);
        p.bodyLen = out.length();
        p.bodyReturn = static_cast<int32_t>(wrote);
        p.bodyReturnKnown = true;
        if (wrote < 0) {
          char reason[96] = {0};
          p.bodyTlsError = client_->lastError(reason, sizeof reason);
          // The same reading as a call that got no reply at all: mbedtls said an allocation
          // failed, or the largest free block can no longer hold a TLS content buffer.
          p.bodyOutOfMemory = failedForMemory(p.bodyTlsError);
        }
        p.largestFreeBlock = ESP.getMaxAllocHeap();
      }
      tlsheap::sample();
      p.retryAfterS = parseRetryAfter(http_->header("Retry-After"));
      const String remainingForFile = http_->header("X-RateLimit-Remaining");
      p.rateLimitRemaining = remainingForFile.length() == 0 ? -1 : remainingForFile.toInt();
      http_->end();
      a.code = code;
      return a;
    }
    // The body straight into the caller's buffer: writeToStream() reads a Content-Length or a
    // chunked body alike, and the sink below takes what fits in bodyCap and drops the rest, which
    // the parser then reports. No String is made of it - a claim carries a repeater's password,
    // and a String would be one more heap copy of it that nothing wipes (agent/Credentials.h).
    // The chunk buffer HTTPClient moves the bytes through on the way is the one transient this
    // code cannot wipe, the same shape as the .NET agent's HttpClient.
    BodySink sink(p.body, p.bodyCap);
    http_->writeToStream(&sink);
    tlsheap::sample();
    p.bodyLen = sink.length();
    p.body[p.bodyLen] = '\0';
    p.retryAfterS = parseRetryAfter(http_->header("Retry-After"));
    const String remaining = http_->header("X-RateLimit-Remaining");
    p.rateLimitRemaining = remaining.length() == 0 ? -1 : remaining.toInt();
  } else {
    char reason[96] = {0};
    a.tlsError = client_->lastError(reason, sizeof reason);
    if (a.tlsError != 0) {
      std::snprintf(p.error, sizeof p.error, "%s (tls %d: %s)", http_->errorToString(code).c_str(), a.tlsError, reason);
    } else {
      std::snprintf(p.error, sizeof p.error, "%s", http_->errorToString(code).c_str());
    }
  }
  // end() keeps the socket open when the server allowed it (setReuse(true) and no
  // `Connection: close`), and stops it otherwise; a failed request has already stopped it.
  http_->end();
  a.code = code;
  return a;
}

int AgentClient::request(Root root, const char* method, const char* path, const char* requestBody, size_t requestLen,
                         char* body, size_t bodyCap, size_t& bodyLen, int& tlsError, Call& out, FirmwareExtras* extras) {
  bodyLen = 0;
  tlsError = 0;

  // A WiFi link remade since the last call: the kept connection went with the old one, and
  // dropping it here is cheaper than letting a request find out.
  if (wifi_ != nullptr && wifi_->connects() != linkConnectsSeen_) {
    linkConnectsSeen_ = wifi_->connects();
    if (line_.kept()) LOG_I("uplink: the WiFi link was remade; the kept connection goes with it");
    line_.drop(*this);
  }

  pending_ = Pending();
  pending_.method = method;
  pending_.path = path;
  pending_.requestBody = requestBody;
  pending_.requestLen = requestLen;
  pending_.body = body;
  pending_.bodyCap = bodyCap;
  if (extras != nullptr) {
    pending_.sink = extras->sink;
    pending_.rangeHeader = extras->rangeHeader;
    pending_.ifRange = extras->ifRange;
  }

  const uplink::Attempt a = line_.call(*this, root, clock_->now());
  const uplink::Counters& c = line_.counters();
  if (line_.lastReopened()) {
    LOG_I("uplink: the kept connection was dead under the request; reopened inside the call (handshake %lu, call %lu)",
          static_cast<unsigned long>(c.handshakes), static_cast<unsigned long>(c.calls));
  } else if (line_.lastHandshook()) {
    LOG_I("uplink: handshake %lu for call %lu (%s)", static_cast<unsigned long>(c.handshakes),
          static_cast<unsigned long>(c.calls), uplink::KeepAlive::whyText(line_.why()));
  } else {
    LOG_D("uplink: call %lu on the connection kept since handshake %lu", static_cast<unsigned long>(c.calls),
          static_cast<unsigned long>(c.handshakes));
  }

  bodyLen = pending_.bodyLen;
  tlsError = a.tlsError;
  out.retryAfterS = pending_.retryAfterS;
  out.rateLimitRemaining = pending_.rateLimitRemaining;
  if (extras != nullptr) {
    std::snprintf(extras->etag, sizeof extras->etag, "%s", pending_.etag);
    std::snprintf(extras->contentRange, sizeof extras->contentRange, "%s", pending_.contentRange);
    extras->bodyReturn = pending_.bodyReturn;
    extras->bodyReturnKnown = pending_.bodyReturnKnown;
    extras->bodyTlsError = pending_.bodyTlsError;
    extras->bodyOutOfMemory = pending_.bodyOutOfMemory;
    extras->largestFreeBlock = pending_.largestFreeBlock;
  }
  if (pending_.error[0] != '\0') std::snprintf(out.error, sizeof out.error, "%s", pending_.error);
  return a.code;
}

bool AgentClient::call(const char* method, const char* endpoint, const char* requestBody, size_t requestLen,
                       char* body, size_t bodyCap, size_t& bodyLen, Call& out, FirmwareExtras* extras) {
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

  // What this call costs the heap (HeapCost): the allocator wrapper watches from here to the
  // end of the call, both requests of a root retry included.
  HeapCost cost;
  cost.freeBefore = ESP.getFreeHeap();
  const uint32_t minEverBefore = ESP.getMinFreeHeap();
  tlsheap::watchBegin(cost.freeBefore);

  int tlsError = 0;
  int code = request(root_, method, path, requestBody, requestLen, body, bodyCap, bodyLen, tlsError, out, extras);
  if (code <= 0 && tlsError == kMbedtlsCertVerifyFailed) {
    // The chain did not end at the root tried. Try the other one, once, and remember.
    const Root other = root_ == Root::IsrgRootX1 ? Root::CoreBundle : Root::IsrgRootX1;
    LOG_W("server: the certificate chain did not verify against %s; trying %s", rootName(root_), rootName(other));
    root_ = other;
    out.via = rootName(root_);
    out.error[0] = '\0';
    limiter_.took(millis());
    code = request(root_, method, path, requestBody, requestLen, body, bodyCap, bodyLen, tlsError, out, extras);
  }
  const uint32_t lowestSeen = tlsheap::watchEnd();
  cost.freeAfter = ESP.getFreeHeap();
  cost.lowest = lowestSeen < cost.freeAfter ? lowestSeen : cost.freeAfter;
  const uint32_t minEverAfter = ESP.getMinFreeHeap();
  if (minEverAfter < minEverBefore) cost.lowest = minEverAfter;  // the call moved the board's minimum: exact
  cost.atEpoch = clock_->now();
  cost.any = true;
  cost_ = cost;
  out.atEpoch = clock_->now();
  out.httpStatus = code > 0 ? code : 0;
  out.reachable = code > 0;
  out.ok = code == 200;
  if (code <= 0 && failedForMemory(tlsError)) {
    memoryFailures_++;
    LOG_W("server: the call failed for want of memory (tls %d, largest free block %lu); %lu such failure(s)", tlsError,
          static_cast<unsigned long>(ESP.getMaxAllocHeap()), static_cast<unsigned long>(memoryFailures_));
  } else if (extras != nullptr && extras->bodyOutOfMemory) {
    // Task p1.19-68, deliverable 4: a range whose body stopped for want of memory. The status
    // line came back, so the call "went through" and nothing above sees it - and the range still
    // ended where mbedtls could not take a record's buffer. It is counted here, with every other
    // memory failure of the uplink, which is what the observer's slots watch.
    memoryFailures_++;
    LOG_W("server: a streamed body stopped for want of memory (client %ld, tls %d, largest free block %lu); %lu such failure(s)",
          static_cast<long>(extras->bodyReturn), extras->bodyTlsError,
          static_cast<unsigned long>(extras->largestFreeBlock), static_cast<unsigned long>(memoryFailures_));
  }
  // A firmware route does **not** become "the server's last answer": `lastCall()` is what the
  // agent loop reads of its own call a line later (agent/ServerPolicy.h decides whether to halt
  // on it), and twenty-odd ranges of an image passing through would be answering a question
  // nobody asked. The heap cost is kept, because it really is what the last call cost and the
  // fetch's own line prints it (task p1.15-59, deliverable 5).
  if (extras == nullptr) lastCall_ = out;
  return out.reachable;
}

bool AgentClient::getStreamed(const char* endpoint, const char* rangeHeader, const char* ifRange,
                              StreamedAnswer& out, update::HttpBody& sink) {
  out = StreamedAnswer();
  FirmwareExtras extras;
  extras.sink = &sink;
  extras.rangeHeader = rangeHeader;
  extras.ifRange = ifRange;

  size_t bodyLen = 0;
  Call c;
  const bool reachable = call("GET", endpoint, nullptr, 0, nullptr, 0, bodyLen, c, &extras);
  out.reachable = reachable;
  out.status = c.httpStatus;
  out.bodyLen = static_cast<uint32_t>(bodyLen);
  std::snprintf(out.etag, sizeof out.etag, "%s", extras.etag);
  std::snprintf(out.contentRange, sizeof out.contentRange, "%s", extras.contentRange);
  std::snprintf(out.error, sizeof out.error, "%s", c.error);
  out.bodyReturn = extras.bodyReturn;
  out.bodyReturnKnown = extras.bodyReturnKnown;
  out.tlsError = extras.bodyTlsError;
  out.outOfMemory = extras.bodyOutOfMemory;
  out.largestFreeBlock = extras.largestFreeBlock;
  // The path is logged and the headers are not: one of them carries the token, and no line of
  // this firmware ever holds it (agent/Log.h).
  LOG_D("server: GET %s%s -> http %d, %lu bytes", endpoint, rangeHeader != nullptr ? " (a range)" : "", out.status,
        static_cast<unsigned long>(out.bodyLen));
  return reachable;
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
  LOG_I("heap: %lu free after the call (%lu before, lowest %lu during), min ever %lu", static_cast<unsigned long>(o.heapAfter),
        static_cast<unsigned long>(heapBefore), static_cast<unsigned long>(cost_.lowest),
        static_cast<unsigned long>(ESP.getMinFreeHeap()));

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
  // Two kilobytes since task p1.7-28, where one was enough: the heartbeat of a node that is also
  // an observer carries the `observer` object, and two slots of it at the bounds the server
  // accepts (a 63-character custom broker, a 95-character last error apiece) are about 700 bytes
  // on top of the ~720 this board's own fields come to. A heartbeat that does not fit is a
  // heartbeat that does not go, and an exit node whose heartbeat does not go is offline - so the
  // buffer is sized for the longest thing this firmware can produce and not for the common case.
  static char requestJson[2048];
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

bool AgentClient::claimJobs(ClaimedJob* jobs, int max, int& count, int transactionsLeft) {
  count = 0;
  // `{"max":2}`, and with the optional `transactionsLeft` of a wake's later claim about thirty
  // more characters (task p1.20-69).
  char request[64];
  const size_t requestLen = writeClaimRequest(max, request, sizeof request, transactionsLeft);

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

bool AgentClient::postJobResult(long long jobId, const char* requestBody, size_t len, bool& jobsWaiting) {
  jobsWaiting = false;
  size_t bodyLen = 0;
  if (!postToJob(jobId, "result", "result", requestBody, len, g_jobReply, sizeof g_jobReply, bodyLen)) return false;
  LOG_D("server: result for job %lld taken: %.200s", jobId, g_jobReply);
  // The one member of the answer this firmware acts on, read from the body the call already has
  // and nowhere else (task p1.20-69). A reply that does not carry it says nothing, and nothing is
  // false: it is written only to an exit node that proposed an interval of its own, which this
  // board never does - so on a Heltec this is false at every result and `loop()` asks nobody.
  jobsWaiting = resultReplySaysJobsWaiting(g_jobReply, bodyLen);
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

bool AgentClient::postUpdateResult(long long updateId, const char* requestBody, size_t len, bool& notFound) {
  notFound = false;
  // Not a job's route: an update belongs to the exit node itself, so this is the one POST of this
  // firmware that is not under /agent/v1/jobs (task p1.15-61).
  char endpoint[64];
  std::snprintf(endpoint, sizeof endpoint, "/agent/v1/updates/%lld/result", updateId);
  size_t bodyLen = 0;
  Call c;
  if (!call("POST", endpoint, requestBody, len, g_jobReply, sizeof g_jobReply, bodyLen, c)) {
    LOG_W("server: the result of update %lld: no reply: %s", updateId, c.error);
    return false;
  }
  if (!c.ok) {
    // The one 404 is an answer and not a glitch: the id is not this exit node's, or is no ask at
    // all, and the result stops being posted (API section 4's rule for every firmware route).
    notFound = c.httpStatus == 404;
    LOG_W("server: the result of update %lld: http %d%s", updateId, c.httpStatus,
          notFound ? " - the server does not know this update" : "");
    if (bodyLen > 0) LOG_D("server: body (%u bytes): %.200s", static_cast<unsigned>(bodyLen), g_jobReply);
    return false;
  }
  LOG_I("server: the result of update %lld was taken", updateId);
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

void AgentClient::describeUplink(char* out, size_t cap) const {
  if (!cost_.any) {
    std::snprintf(out, cap, "no call yet");
    return;
  }
  char kept[112];
  if (line_.kept()) {
    char since[24];
    if (line_.keptSince() != 0) {
      formatClock(line_.keptSince(), since, sizeof since);
    } else {
      std::snprintf(since, sizeof since, "boot");
    }
    std::snprintf(kept, sizeof kept, "connection kept since %s (root %s)", since, rootName(line_.keptRoot()));
  } else {
    std::snprintf(kept, sizeof kept, "no connection kept (%s)", uplink::KeepAlive::whyText(line_.why()));
  }
  const uplink::Counters& c = line_.counters();
  std::snprintf(out, cap,
                "%s; %lu handshake(s) for %lu call(s), %lu reused, %lu reopened in-call; last call %lu free before, "
                "lowest %lu during, %lu after",
                kept, static_cast<unsigned long>(c.handshakes), static_cast<unsigned long>(c.calls),
                static_cast<unsigned long>(c.reused), static_cast<unsigned long>(c.reopened),
                static_cast<unsigned long>(cost_.freeBefore), static_cast<unsigned long>(cost_.lowest),
                static_cast<unsigned long>(cost_.freeAfter));
}


}  // namespace mm
