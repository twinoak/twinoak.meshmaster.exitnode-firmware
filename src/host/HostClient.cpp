#include "HostClient.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Log.h"
#include "agent/Iso8601.h"
#include "arduino/Arduino.h"
#include "settings/Keys.h"

namespace mm {

namespace {

constexpr size_t kBodyCap = 2048;
constexpr int kConnectTimeoutS = 5;
constexpr int kReadTimeoutS = 30;

void wipe(char* buf, size_t len) {
  volatile char* p = buf;
  for (size_t i = 0; i < len; i++) p[i] = 0;
}

// **The rail, on the address and not on the spelling.** A host build may open a plain-HTTP socket
// to a loopback or RFC 1918 address and to nothing else; the check is made on what the name
// *resolves to*, on every call, so `localhost`, `127.0.0.1` and `host.docker.internal` are all
// judged by the address behind them and a public name cannot be reached however it is written.
bool isPrivateAddress(const addrinfo* a, char* printed, size_t printedCap) {
  printed[0] = '\0';
  if (a->ai_family == AF_INET) {
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(a->ai_addr);
    ::inet_ntop(AF_INET, &in->sin_addr, printed, static_cast<socklen_t>(printedCap));
    const uint32_t v = ntohl(in->sin_addr.s_addr);
    const uint8_t b1 = static_cast<uint8_t>(v >> 24);
    const uint8_t b2 = static_cast<uint8_t>((v >> 16) & 0xFF);
    if (b1 == 127) return true;                          // 127.0.0.0/8
    if (b1 == 10) return true;                           // 10.0.0.0/8
    if (b1 == 172 && b2 >= 16 && b2 <= 31) return true;  // 172.16.0.0/12
    if (b1 == 192 && b2 == 168) return true;             // 192.168.0.0/16
    return false;
  }
  if (a->ai_family == AF_INET6) {
    const sockaddr_in6* in = reinterpret_cast<const sockaddr_in6*>(a->ai_addr);
    ::inet_ntop(AF_INET6, &in->sin6_addr, printed, static_cast<socklen_t>(printedCap));
    if (IN6_IS_ADDR_LOOPBACK(&in->sin6_addr)) return true;
    // A v4-mapped private address, which is what a dual-stack resolver may answer for those names.
    if (IN6_IS_ADDR_V4MAPPED(&in->sin6_addr)) {
      const uint8_t* bytes = in->sin6_addr.s6_addr + 12;
      if (bytes[0] == 127 || bytes[0] == 10) return true;
      if (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) return true;
      if (bytes[0] == 192 && bytes[1] == 168) return true;
    }
    return false;
  }
  return false;
}

// Blocking connect with a timeout, then blocking reads with one. Nothing clever: a request a few
// times a minute, one connection each, as the board's own TLS calls are.
int openSocket(const char* host, int port, char* err, size_t errCap) {
  char service[8];
  std::snprintf(service, sizeof service, "%d", port);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* list = nullptr;
  const int resolved = ::getaddrinfo(host, service, &hints, &list);
  if (resolved != 0 || list == nullptr) {
    std::snprintf(err, errCap, "%s:%d did not resolve: %s", host, port, ::gai_strerror(resolved));
    return -1;
  }
  // Every address the name answers with has to be a private one - not "the first": a name that
  // resolved to a loopback address *and* a public one would otherwise be a way round the rail.
  for (addrinfo* a = list; a != nullptr; a = a->ai_next) {
    char printed[INET6_ADDRSTRLEN] = {0};
    if (!isPrivateAddress(a, printed, sizeof printed)) {
      std::snprintf(err, errCap,
                    "%s resolves to %s, which is not a loopback or RFC 1918 address: the host build speaks plain "
                    "HTTP to nothing else (docs/host.md section 5)",
                    host, printed);
      ::freeaddrinfo(list);
      return -1;
    }
  }
  int fd = -1;
  for (addrinfo* a = list; a != nullptr; a = a->ai_next) {
    fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (fd < 0) continue;
    timeval connectTimeout{kConnectTimeoutS, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &connectTimeout, sizeof connectTimeout);
    timeval readTimeout{kReadTimeoutS, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &readTimeout, sizeof readTimeout);
    if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  ::freeaddrinfo(list);
  if (fd < 0) std::snprintf(err, errCap, "%s:%d refused the connection: %s", host, port, std::strerror(errno));
  return fd;
}

bool writeAll(int fd, const char* data, size_t len) {
  size_t at = 0;
  while (at < len) {
    const ssize_t n = ::send(fd, data + at, len - at, 0);
    if (n <= 0) return false;
    at += static_cast<size_t>(n);
  }
  return true;
}

// A header's value, case-insensitively, out of the response head.
bool headerValue(const char* head, const char* name, char* out, size_t cap) {
  const size_t nameLen = std::strlen(name);
  for (const char* line = head; line != nullptr && *line != '\0';) {
    const char* end = std::strstr(line, "\r\n");
    const size_t lineLen = end != nullptr ? static_cast<size_t>(end - line) : std::strlen(line);
    if (lineLen > nameLen && line[nameLen] == ':') {
      bool same = true;
      for (size_t i = 0; i < nameLen && same; i++) {
        char a = line[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        same = a == b;
      }
      if (same) {
        const char* value = line + nameLen + 1;
        while (*value == ' ') value++;
        const size_t valueLen = lineLen - static_cast<size_t>(value - line);
        std::snprintf(out, cap, "%.*s", static_cast<int>(valueLen), value);
        return true;
      }
    }
    if (end == nullptr) break;
    line = end + 2;
  }
  return false;
}

}  // namespace

void HostClient::begin(SettingsStore& settings, AgentClock& clock, const char* agentVersion) {
  settings_ = &settings;
  clock_ = &clock;
  version_ = agentVersion;
}

bool HostClient::setEndpoint(const char* url, char* why, size_t whyCap) {
  why[0] = '\0';
  endpointOk_ = false;
  if (url == nullptr || url[0] == '\0') {
    std::snprintf(why, whyCap, "--url is empty");
    return false;
  }
  if (std::strncmp(url, "https://", 8) == 0) {
    std::snprintf(why, whyCap,
                  "there is no TLS in the host build: use http:// to a loopback or RFC 1918 address. The board's "
                  "client is the one that speaks https:// and verifies the chain (src/agent/AgentClient.h)");
    return false;
  }
  if (std::strncmp(url, "http://", 7) != 0) {
    std::snprintf(why, whyCap, "--url must start with http://");
    return false;
  }
  std::snprintf(url_, sizeof url_, "%s", url);

  const char* authority = url + 7;
  const char* slash = std::strchr(authority, '/');
  const size_t authorityLen = slash != nullptr ? static_cast<size_t>(slash - authority) : std::strlen(authority);
  char authorityCopy[sizeof host_];
  std::snprintf(authorityCopy, sizeof authorityCopy, "%.*s", static_cast<int>(authorityLen), authority);
  std::snprintf(prefix_, sizeof prefix_, "%s", slash != nullptr ? slash : "");
  // A trailing slash would make every path a double one.
  const size_t prefixLen = std::strlen(prefix_);
  if (prefixLen > 0 && prefix_[prefixLen - 1] == '/') prefix_[prefixLen - 1] = '\0';

  port_ = 80;
  char* colon = std::strrchr(authorityCopy, ':');
  if (colon != nullptr && std::strchr(authorityCopy, ']') == nullptr) {
    *colon = '\0';
    port_ = std::atoi(colon + 1);
    if (port_ <= 0 || port_ > 65535) {
      std::snprintf(why, whyCap, "'%s' is not a port", colon + 1);
      return false;
    }
  }
  // `[::1]` as a URL authority.
  const char* hostStart = authorityCopy;
  char bare[sizeof host_];
  if (hostStart[0] == '[') {
    std::snprintf(bare, sizeof bare, "%s", hostStart + 1);
    char* close = std::strchr(bare, ']');
    if (close != nullptr) *close = '\0';
    hostStart = bare;
  }
  std::snprintf(host_, sizeof host_, "%s", hostStart);
  if (host_[0] == '\0') {
    std::snprintf(why, whyCap, "--url names no host");
    return false;
  }
  // The address rail is kept where the address is: openSocket() refuses every call whose name does
  // not resolve to a loopback or RFC 1918 address, every time, rather than once at start-up on a
  // spelling. A name whose answer changes cannot drift out of the rule.
  endpointOk_ = true;
  LOG_I("server: the host harness talks plain HTTP to %s:%d%s - and to nothing whose address is not a loopback or "
        "RFC 1918 one, checked on every call (docs/host.md section 5)",
        host_, port_, prefix_);
  return true;
}

void HostClient::reconfigure() {
  last_ = Outcome();
  hasConfig_ = false;
}

bool HostClient::configured() const {
  if (!endpointOk_ || settings_ == nullptr) return false;
  const KeyDef* token = findKey("meshmaster.token");
  return token != nullptr && settings_->isSet(*token);
}

bool HostClient::call(const char* method, const char* path, const char* requestBody, size_t requestLen, char* body,
                      size_t bodyCap, size_t& bodyLen, Call& out) {
  bodyLen = 0;
  body[0] = '\0';
  out = Call();
  out.root = Root::HostPlain;
  out.atMillis = millis();
  out.atEpoch = clock_ != nullptr ? clock_->now() : 0;

  if (!endpointOk_) {
    std::snprintf(out.error, sizeof out.error, "no endpoint: --url was refused");
    lastCall_ = out;
    return false;
  }
  // The server's `agent` policy is 120 a minute per token and this side keeps to it too, exactly as
  // the board does: a bug that retried in a tight loop costs a minute and not a token's budget.
  const uint32_t hold = limiter_.waitMs(millis());
  if (hold > 0) {
    LOG_W("server: the 120-a-minute ceiling: waiting %lu ms before %s %s", static_cast<unsigned long>(hold), method,
          path);
    delay(hold);
  }
  limiter_.took(millis());
  requests_++;

  int fd = openSocket(host_, port_, out.error, sizeof out.error);
  if (fd < 0) {
    lastCall_ = out;
    return false;
  }
  const int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

  // The token: read for this one call and wiped after, as the board reads it from NVS.
  char token[kMaxValueLen + 1] = {0};
  const KeyDef* tokenKey = findKey("meshmaster.token");
  if (tokenKey != nullptr && settings_ != nullptr) settings_->get(*tokenKey, token, sizeof token);

  char head[1024];
  const int headLen = std::snprintf(head, sizeof head,
                                    "%s %s%s HTTP/1.1\r\n"
                                    "Host: %s:%d\r\n"
                                    "Authorization: Bearer %s\r\n"
                                    "X-Agent-Version: %s\r\n"
                                    "User-Agent: meshmaster-agent/%s\r\n"
                                    "Accept: application/json\r\n"
                                    "Connection: close\r\n"
                                    "%s"
                                    "Content-Length: %zu\r\n"
                                    "\r\n",
                                    method, prefix_, path, host_, port_, token, version_, version_,
                                    requestBody != nullptr ? "Content-Type: application/json\r\n" : "", requestLen);
  wipe(token, sizeof token);
  if (headLen <= 0 || static_cast<size_t>(headLen) >= sizeof head) {
    std::snprintf(out.error, sizeof out.error, "the request head did not fit");
    ::close(fd);
    lastCall_ = out;
    return false;
  }
  const bool sent = writeAll(fd, head, static_cast<size_t>(headLen)) &&
                    (requestBody == nullptr || requestLen == 0 || writeAll(fd, requestBody, requestLen));
  wipe(head, sizeof head);  // a claim request carries nothing secret, but the header carried the token
  if (!sent) {
    std::snprintf(out.error, sizeof out.error, "the request could not be written: %s", std::strerror(errno));
    ::close(fd);
    lastCall_ = out;
    return false;
  }

  // The whole answer, head and body, into one buffer: a claim is a few kilobytes and this is a
  // workstation.
  static char raw[16384];
  size_t at = 0;
  while (at + 1 < sizeof raw) {
    const ssize_t n = ::recv(fd, raw + at, sizeof raw - 1 - at, 0);
    if (n <= 0) break;
    at += static_cast<size_t>(n);
  }
  ::close(fd);
  raw[at] = '\0';
  if (at == 0) {
    std::snprintf(out.error, sizeof out.error, "the server closed the connection without answering");
    lastCall_ = out;
    return false;
  }

  int status = 0;
  if (std::sscanf(raw, "HTTP/1.%*d %d", &status) != 1) {
    std::snprintf(out.error, sizeof out.error, "the answer is not HTTP: %.60s", raw);
    wipe(raw, sizeof raw);
    lastCall_ = out;
    return false;
  }
  char* split = std::strstr(raw, "\r\n\r\n");
  const char* payload = split != nullptr ? split + 4 : raw + at;
  if (split != nullptr) *split = '\0';

  char value[64];
  if (headerValue(raw, "Retry-After", value, sizeof value)) {
    const long seconds = std::strtol(value, nullptr, 10);
    out.retryAfterS = seconds > 0 ? (seconds > 3600 ? 3600u : static_cast<uint32_t>(seconds)) : 60u;
  }
  if (headerValue(raw, "X-RateLimit-Remaining", value, sizeof value)) {
    out.rateLimitRemaining = std::atoi(value);
  }
  // **Chunked, because Kestrel is.** The first run of this against meshmaster.dk's compose stack
  // found every call answered `Transfer-Encoding: chunked` and no Content-Length: ASP.NET Core does
  // not buffer a JSON answer to measure it. So the chunks are joined here. The board's HTTPClient
  // does this for it and nobody had to know; a harness that refused one could not talk to the real
  // server at all, which is exactly the kind of thing the harness is for.
  if (headerValue(raw, "Transfer-Encoding", value, sizeof value) && std::strstr(value, "chunked") != nullptr) {
    bodyLen = 0;
    const char* at = payload;
    while (true) {
      char* end = nullptr;
      const long size = std::strtol(at, &end, 16);
      if (end == at || size < 0) break;      // not a chunk header: the answer is cut
      const char* data = std::strstr(end, "\r\n");
      if (data == nullptr) break;
      data += 2;
      if (size == 0) break;                  // the last chunk, and the trailers after it
      size_t take = static_cast<size_t>(size);
      if (bodyLen + take > bodyCap - 1) take = bodyCap - 1 - bodyLen;
      std::memcpy(body + bodyLen, data, take);
      bodyLen += take;
      at = data + size;
      if (at[0] == '\r' && at[1] == '\n') at += 2;
    }
    body[bodyLen] = '\0';
    wipe(raw, sizeof raw);
    out.httpStatus = status;
    out.reachable = true;
    out.ok = status == 200;
    out.atEpoch = clock_ != nullptr ? clock_->now() : 0;
    lastCall_ = out;
    return true;
  }

  const size_t payloadLen = std::strlen(payload);
  bodyLen = payloadLen < bodyCap - 1 ? payloadLen : bodyCap - 1;
  std::memcpy(body, payload, bodyLen);
  body[bodyLen] = '\0';
  // The answer buffer held a claim, which carries a repeater's password: wiped the moment it has
  // been copied, exactly as src/agent/AgentClient.cpp wipes its own (agent/Credentials.h).
  wipe(raw, sizeof raw);

  out.httpStatus = status;
  out.reachable = true;
  out.ok = status == 200;
  out.atEpoch = clock_ != nullptr ? clock_->now() : 0;
  lastCall_ = out;
  return true;
}

bool HostClient::fetchConfig() {
  static char body[kBodyCap];
  size_t bodyLen = 0;
  Call c;
  LOG_I("server: GET %s/agent/v1/config (plain HTTP, host build)", url_);
  const bool reachable = call("GET", "/agent/v1/config", nullptr, 0, body, sizeof body, bodyLen, c);

  Outcome o;
  o.attempted = true;
  o.atMillis = c.atMillis;
  o.atEpoch = c.atEpoch;
  o.root = c.root;
  o.heapAfter = 0;  // there is no heap figure to report on a host
  std::snprintf(o.error, sizeof o.error, "%s", c.error);

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
    last_ = o;
    return false;
  }
  int32_t diff = 0;
  clock_->takeServerTime(cfg.serverTimeUnix, diff);
  o.ok = true;
  o.atEpoch = clock_->now();
  config_ = cfg;
  hasConfig_ = true;
  last_ = o;
  LOG_I("server: config ok: agent %lld \"%s\", transport %s, budget %d/min gap %d s, poll %d s, serverTime %s",
        cfg.agentId, cfg.label, cfg.transport, cfg.budget.maxTransactionsPerMinute, cfg.budget.minGapSeconds,
        cfg.pollIntervalS, cfg.serverTime);
  if (std::strcmp(cfg.transport, "onboard") != 0) {
    LOG_W("server: the exit node is registered with transport '%s', the harness heartbeats as 'onboard'", cfg.transport);
  }
  return true;
}

bool HostClient::postHeartbeat(const Heartbeat& hb, HeartbeatReply& reply) {
  static char requestJson[1024];
  const size_t len = writeHeartbeat(hb, requestJson, sizeof requestJson);
  if (len == 0) {
    lastCall_ = Call();
    std::snprintf(lastCall_.error, sizeof lastCall_.error, "the heartbeat body did not fit");
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

bool HostClient::postObservations(const char* requestBody, size_t len, int& accepted, bool& duplicate) {
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

bool HostClient::claimJobs(ClaimedJob* jobs, int max, int& count) {
  count = 0;
  char request[32];
  const size_t requestLen = writeClaimRequest(max, request, sizeof request);
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

bool HostClient::postJobResult(long long jobId, const char* requestBody, size_t len) {
  char endpoint[48];
  std::snprintf(endpoint, sizeof endpoint, "/agent/v1/jobs/%lld/result", jobId);
  static char body[512];
  size_t bodyLen = 0;
  Call c;
  if (!call("POST", endpoint, requestBody, len, body, sizeof body, bodyLen, c)) {
    LOG_W("server: result for job %lld: no reply: %s", jobId, c.error);
    return false;
  }
  if (!c.ok) {
    LOG_W("server: result for job %lld: http %d%s", jobId, c.httpStatus, bodyLen > 0 ? ":" : "");
    if (bodyLen > 0) LOG_W("server: %.300s", body);
    return false;
  }
  LOG_I("server: result for job %lld taken: %.200s", jobId, body);
  return true;
}

void HostClient::describe(char* out, size_t cap) const {
  if (!configured()) {
    std::snprintf(out, cap, "not configured (%s%s)", endpointOk_ ? "" : "--url was refused; ",
                  "meshmaster.token comes from --token");
    return;
  }
  if (!last_.attempted) {
    std::snprintf(out, cap, "%s: not called yet (plain HTTP, host build)", url_);
    return;
  }
  char when[24];
  if (last_.atEpoch != 0) {
    formatClock(last_.atEpoch, when, sizeof when);
  } else {
    std::snprintf(when, sizeof when, "+%lus", static_cast<unsigned long>(last_.atMillis / 1000));
  }
  if (last_.ok) {
    std::snprintf(out, cap,
                  "%s: config ok at %s, agent %lld \"%s\", transport %s, budget %d/min gap %d s, poll %d s, %s",
                  url_, when, config_.agentId, config_.label, config_.transport,
                  config_.budget.maxTransactionsPerMinute, config_.budget.minGapSeconds, config_.pollIntervalS,
                  rootName(last_.root));
  } else if (last_.httpStatus != 0) {
    std::snprintf(out, cap, "%s: http %d at %s%s%s", url_, last_.httpStatus, when, last_.error[0] != '\0' ? ", " : "",
                  last_.error);
  } else {
    std::snprintf(out, cap, "%s: failed at %s: %s", url_, when, last_.error);
  }
}

void HostClient::oledLine(char* out, size_t cap) const {
  if (!configured()) {
    std::snprintf(out, cap, "no server set");
    return;
  }
  if (!last_.attempted) {
    std::snprintf(out, cap, "server: waiting");
    return;
  }
  if (last_.ok) {
    std::snprintf(out, cap, "config ok");
    return;
  }
  std::snprintf(out, cap, "%s", last_.httpStatus != 0 ? "http error" : "net failed");
}

}  // namespace mm
