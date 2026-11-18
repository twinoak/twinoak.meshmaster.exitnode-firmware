// The board's two other halves of task 59's seams: the server, and where the record lives.
//
// `Fetch.h` names three seams and `src/platform/OtaSlot.h` is the first of them. These are the
// other two, and both are board-only:
//
//   ServerRoutes   ReleaseHttp over src/agent/AgentClient.h. **Every request goes down the one
//                  path every other call of this firmware goes down** - the kept connection, the
//                  verified certificate chain, the `Authorization: Bearer` header that
//                  AgentClient::exchange() builds and nothing else does, `X-Agent-Version`, and
//                  the 120-a-minute ceiling - so nothing of the token is in this file, in
//                  Fetch.cpp, or anywhere a fetch can reach. A 3xx comes back as the status it is:
//                  the HTTPClient is told not to follow one.
//
//   SettingsRecord RecordStore over src/settings/Store.h: the five keys of SlotRecord.h in the
//                  one NVS namespace the settings live in, written by the firmware and typed by
//                  nobody.

#pragma once

#include "Fetch.h"
#include "SlotRecord.h"
#include "settings/Store.h"

namespace mm {

class AgentClient;

class ServerRoutes final : public update::ReleaseHttp {
 public:
  explicit ServerRoutes(AgentClient& client) : client_(client) {}
  bool get(const update::HttpRequest& req, update::HttpAnswer& out, update::HttpBody& body) override;

 private:
  AgentClient& client_;
  char range_[48] = {0};
};

class SettingsRecord final : public update::RecordStore {
 public:
  explicit SettingsRecord(SettingsStore& settings) : settings_(settings) {}
  size_t get(const char* key, char* out, size_t cap) override;
  bool put(const char* key, const char* value) override;

 private:
  SettingsStore& settings_;
};

}  // namespace mm
