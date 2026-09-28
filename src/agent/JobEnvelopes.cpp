#include "JobEnvelopes.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "Iso8601.h"
#include "JsonWriter.h"

namespace mm {

namespace {

void say(char* err, size_t cap, const char* text) {
  if (err == nullptr || cap == 0) return;
  std::snprintf(err, cap, "%s", text);
}

void copyText(const char* from, char* to, size_t cap) {
  if (from == nullptr) {
    to[0] = '\0';
    return;
  }
  std::snprintf(to, cap, "%s", from);
}

}  // namespace

const char* const kJobTypes[kJobTypeCount] = {"status", "telemetry", "neighbours", "trace"};

bool isKnownJobType(const char* type) {
  if (type == nullptr) return false;
  for (int i = 0; i < kJobTypeCount; i++) {
    if (std::strcmp(type, kJobTypes[i]) == 0) return true;
  }
  return false;
}

const char* outcomeName(Outcome o) {
  switch (o) {
    case Outcome::Ok: return "ok";
    case Outcome::Unreachable: return "unreachable";
    case Outcome::Unauthorized: return "unauthorized";
    case Outcome::Unsupported: return "unsupported";
    case Outcome::Timeout: return "timeout";
    case Outcome::Failed:
    default: return "failed";
  }
}

bool canRun(const ClaimedJob& job, char* why, size_t whyCap) {
  if (!isKnownJobType(job.type)) {
    if (why != nullptr && whyCap > 0) {
      std::snprintf(why, whyCap, "no handler for job type '%s' in this firmware", job.type);
    }
    return false;
  }
  if (job.credentialClass == CredentialClass::Guest || job.credentialClass == CredentialClass::Admin) {
    if (why != nullptr && whyCap > 0) {
      std::snprintf(why, whyCap,
                    "this firmware logs in as itself and carries no password: class %s is not one it can ask for",
                    credentialClassName(job.credentialClass));
    }
    return false;
  }
  if (job.repeaterKey[0] == '\0') {
    if (why != nullptr && whyCap > 0) std::snprintf(why, whyCap, "the repeater has no public key; nothing to address");
    return false;
  }
  if (why != nullptr && whyCap > 0) why[0] = '\0';
  return true;
}

bool sharesLogin(const ClaimedJob& a, const ClaimedJob& b) {
  if (a.id == b.id) return false;
  for (int i = 0; i < a.groupCount; i++) {
    if (a.groupWith[i] == b.id) return true;
  }
  for (int i = 0; i < b.groupCount; i++) {
    if (b.groupWith[i] == a.id) return true;
  }
  return false;
}

size_t writeClaimRequest(int max, char* out, size_t cap) {
  JsonWriter w(out, cap);
  w.beginObject();
  w.key("max");
  w.integer(max);
  w.endObject();
  return w.overflowed() ? 0 : w.length();
}

bool parseClaimResponse(const char* json, size_t len, ClaimedJob* jobs, int max, int& count, uint32_t& serverTime,
                        char* err, size_t errCap) {
  count = 0;
  serverTime = 0;
  // Two jobs with a path, an idempotency key and a label is about a kilobyte; ArduinoJson sizes
  // its pool from what it actually reads, and this is the one reply of the protocol that is read
  // rather than written here.
  JsonDocument doc;
  const DeserializationError e = deserializeJson(doc, json, len);
  if (e) {
    char msg[96];
    std::snprintf(msg, sizeof msg, "not JSON: %s", e.c_str());
    say(err, errCap, msg);
    return false;
  }
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) {
    say(err, errCap, "not a JSON object");
    return false;
  }
  const char* when = root["serverTime"];
  if (when == nullptr || !parseUtc(when, serverTime)) {
    say(err, errCap, "missing or malformed: serverTime");
    return false;
  }
  JsonArrayConst list = root["jobs"];
  if (list.isNull()) {
    say(err, errCap, "missing: jobs");
    return false;
  }

  for (JsonObjectConst j : list) {
    if (count >= max) break;  // the server was asked for `max`; anything past it is not taken
    ClaimedJob job;

    JsonVariantConst id = j["id"];
    if (!id.is<long long>()) {
      say(err, errCap, "a job has no id");
      return false;
    }
    job.id = id.as<long long>();

    const char* type = j["type"];
    if (type == nullptr) {
      say(err, errCap, "a job has no type");
      return false;
    }
    copyText(type, job.type, sizeof job.type);

    JsonObjectConst repeater = j["repeater"];
    if (repeater.isNull()) {
      say(err, errCap, "a job has no repeater");
      return false;
    }
    job.repeaterId = repeater["id"] | 0LL;
    copyText(repeater["label"], job.repeaterLabel, sizeof job.repeaterLabel);
    copyText(repeater["publicKey"], job.repeaterKey, sizeof job.repeaterKey);

    if (!parseCredentialClass(j["credentialClass"] | "none", job.credentialClass)) {
      say(err, errCap, "a job has a credential class this firmware does not know");
      return false;
    }

    const char* lease = j["leaseUntil"];
    if (lease == nullptr || !parseUtc(lease, job.leaseUntil)) {
      say(err, errCap, "a job has no leaseUntil");
      return false;
    }

    JsonObjectConst timeouts = j["timeouts"];
    if (!timeouts.isNull()) {
      job.perHopMs = timeouts["perHopMs"] | job.perHopMs;
      job.loginMs = timeouts["loginMs"] | job.loginMs;
    }

    JsonObjectConst credential = j["credential"];
    if (!credential.isNull()) {
      JsonVariantConst acl = credential["acl"];
      if (acl.is<int>()) {
        job.hasAclLevel = true;
        job.aclLevel = acl.as<int>();
      } else if (acl.is<bool>() && acl.as<bool>()) {
        // The pre-task-60 shape, `{"acl": true}`: a grant whose level the server did not say.
        job.hasAclLevel = true;
        job.aclLevel = kPermReadOnly;
      }
    }

    JsonObjectConst path = j["path"];
    if (!path.isNull()) {
      JsonArrayConst hops = path["hops"];
      if (!hops.isNull()) {
        job.hasPath = true;
        for (JsonVariantConst hop : hops) {
          const char* text = hop.as<const char*>();
          if (text == nullptr || job.pathCount >= kMaxClaimPathHops) continue;
          copyText(text, job.pathHops[job.pathCount], sizeof job.pathHops[0]);
          job.pathCount++;
        }
      }
    }

    copyText(j["idempotencyKey"], job.idempotencyKey, sizeof job.idempotencyKey);

    JsonArrayConst group = j["groupWith"];
    if (!group.isNull()) {
      for (JsonVariantConst other : group) {
        if (job.groupCount >= kMaxGroupWith) break;
        job.groupWith[job.groupCount++] = other.as<long long>();
      }
    }

    jobs[count++] = job;
  }
  return true;
}

size_t writeJobResult(const JobReport& r, char* out, size_t cap) {
  JsonWriter w(out, cap);
  char when[24];
  w.beginObject();
  w.key("clientId");
  w.string(r.clientId);
  w.key("outcome");
  w.string(outcomeName(r.outcome));
  w.key("startedAt");
  formatUtc(r.startedAt, when, sizeof when);
  w.string(when);
  w.key("finishedAt");
  formatUtc(r.finishedAt, when, sizeof when);
  w.string(when);
  if (r.hasPathUsed) {
    w.key("pathUsed");
    w.beginArray();
    for (int i = 0; i < r.pathCount; i++) {
      w.comma();
      w.string(r.pathUsed[i]);
    }
    w.endArray();
  }
  if (r.hasHops) {
    w.key("hops");
    w.integer(r.hops);
  }
  if (r.hasRttMs) {
    w.key("rttMs");
    w.integer(r.rttMs);
  }
  w.key("transactions");
  w.integer(r.transactions);
  if (r.data != nullptr && r.data[0] != '\0') {
    w.key("data");
    w.raw(r.data);
  }
  if (r.log != nullptr) {
    // The **last** 4 000 characters, as the .NET runner bounds it: what a job did last is what
    // a reader of the job's page wants, and a job that went wrong says so at the end.
    w.key("log");
    const size_t n = std::strlen(r.log);
    w.string(n > kMaxJobLogChars ? r.log + (n - kMaxJobLogChars) : r.log, kMaxJobLogChars);
  }
  w.endObject();
  return w.overflowed() ? 0 : w.length();
}

}  // namespace mm
