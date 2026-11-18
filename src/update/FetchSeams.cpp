#include "FetchSeams.h"

#include <cstdio>
#include <cstring>

#include "agent/AgentClient.h"

namespace mm {

bool ServerRoutes::get(const update::HttpRequest& req, update::HttpAnswer& out, update::HttpBody& body) {
  const char* range = nullptr;
  if (req.hasRange) {
    // §4: `Range: bytes=a-b`, the first byte being `bytes=0-0`. This firmware asks for one closed
    // range and never a multi-range, which that section answers with the whole file and a 200.
    std::snprintf(range_, sizeof(range_), "bytes=%lu-%lu", static_cast<unsigned long>(req.from),
                  static_cast<unsigned long>(req.to));
    range = range_;
  }
  AgentClient::StreamedAnswer answer;
  client_.getStreamed(req.path, range, req.ifRange, answer, body);

  out.reachable = answer.reachable;
  out.status = answer.status;
  out.bodyLen = answer.bodyLen;
  std::snprintf(out.etag, sizeof(out.etag), "%s", answer.etag);
  std::snprintf(out.contentRange, sizeof(out.contentRange), "%s", answer.contentRange);
  std::snprintf(out.error, sizeof(out.error), "%s", answer.error);
  return out.reachable;
}

size_t SettingsRecord::get(const char* key, char* out, size_t cap) {
  return settings_.getRecord(key, out, cap);
}

bool SettingsRecord::put(const char* key, const char* value) {
  return settings_.putRecord(key, value);
}

}  // namespace mm
