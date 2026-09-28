// The contract is the .NET agent's: what this firmware POSTs to /agent/v1/jobs/{id}/result is
// held against tests/fixtures/contracts **byte for byte**, for the samples MeshMaster.Contracts'
// own round-trip tests wrote those fixtures from. A difference is a bug in the firmware, not a
// second dialect.
//
// Two checks per record, and they are not the same one: the fixture says the *shape* - the field
// order, the camelCase, the nulls left out, the numbers as System.Text.Json writes them - and
// the second test says the *values*, built from the transcribed repeater bytes of
// tests/fixtures/companion and compared with what MeshMaster.Agent.Tests asserts for them.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "agent/Iso8601.h"
#include "agent/JobEnvelopes.h"
#include "agent/JobResults.h"

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif
#ifndef COMPANION_DIR
#error "COMPANION_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) {
  std::ifstream in(fs::path(FIXTURES_DIR) / (name + ".json"), std::ios::binary);
  EXPECT_TRUE(in.good()) << "no such contract fixture: " << name;
  std::ostringstream all;
  all << in.rdbuf();
  std::string text = all.str();
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::vector<uint8_t> companion(const std::string& name, std::size_t skip) {
  std::ifstream in(fs::path(COMPANION_DIR) / (name + ".hex"));
  EXPECT_TRUE(in.good()) << "no such companion fixture: " << name;
  std::string line;
  std::string hex;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    for (char c : line) {
      if (hexDigit(c) >= 0) hex.push_back(c);
    }
  }
  std::vector<uint8_t> bytes;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<uint8_t>((hexDigit(hex[i]) << 4) | hexDigit(hex[i + 1])));
  }
  EXPECT_GE(bytes.size(), skip);
  return {bytes.begin() + static_cast<long>(skip), bytes.end()};
}

uint32_t at(const char* iso) {
  uint32_t epoch = 0;
  EXPECT_TRUE(mm::parseUtc(iso, epoch)) << iso;
  return epoch;
}

// The StatusResult sample of ContractsTests, as a reading plus the two error counters it
// carries: `txFail` is not a counter this firmware has ever seen, and that is the point - the
// errors of a status result are an open key space on both ends.
mm::StatusReading contractSample() {
  mm::StatusReading s;
  s.batteryMv = 4012;
  s.noiseFloorDbm = -112;
  s.lastRssi = -97;
  s.lastSnr = 6.25;
  s.uptimeS = 864000;
  s.txQueueLen = 0;
  s.packetsRecv = 12345;
  s.packetsSent = 6789;
  s.recvFlood = 900;
  s.recvDirect = 11445;
  s.sentFlood = 12;
  s.sentDirect = 6777;
  s.airtimeS = 1234;
  s.floodDups = 3;
  s.directDups = 0;
  return s;
}

const char* kSampleStatusJson =
    "{\"batteryMv\":4012,\"noiseFloorDbm\":-112,\"lastRssi\":-97,\"lastSnr\":6.25,\"uptimeS\":864000,"
    "\"txQueueLen\":0,\"packets\":{\"recv\":12345,\"sent\":6789,\"recvFlood\":900,\"recvDirect\":11445,"
    "\"sentFlood\":12,\"sentDirect\":6777},\"airtimeS\":1234,\"dups\":{\"flood\":3,\"direct\":0},"
    "\"errors\":{\"eventsFull\":0,\"txFail\":2},\"firmware\":\"v1.17.1\"}";

// ------------------------------------------------------------------ the shapes

TEST(StatusResult, TheSameInputMakesTheDotNetFixtureByteForByte) {
  const mm::StatusReading s = contractSample();
  const mm::ErrorCounter errors[] = {{"eventsFull", 0}, {"txFail", 2}};
  char out[768];
  const std::size_t len = mm::writeStatusResult(s, errors, 2, "v1.17.1", out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), fixture("StatusResult"));
  EXPECT_EQ(std::string(out, len), std::string(kSampleStatusJson));
}

TEST(TelemetryResult, TheSameInputMakesTheDotNetFixtureByteForByte) {
  mm::LppReading readings[2];
  readings[0].channel = 3;
  std::snprintf(readings[0].type, sizeof readings[0].type, "voltage");
  readings[0].value = 4.012;
  readings[0].unit = "V";
  readings[1].channel = 4;
  std::snprintf(readings[1].type, sizeof readings[1].type, "temperature");
  readings[1].value = -1.5;
  readings[1].unit = "C";

  char out[512];
  const std::size_t len = mm::writeTelemetryResult(readings, 2, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), fixture("TelemetryResult"));
}

TEST(NeighboursResult, TheSameInputMakesTheDotNetFixtureByteForByte) {
  mm::Neighbour n[2];
  std::snprintf(n[0].prefix, sizeof n[0].prefix, "a1");
  n[0].lastHeardS = 42;
  n[0].snr = 7.25;
  std::snprintf(n[1].prefix, sizeof n[1].prefix, "7f");
  n[1].lastHeardS = 3600;
  n[1].snr = -2.5;

  char out[512];
  const std::size_t len = mm::writeNeighboursResult(n, 2, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), fixture("NeighboursResult"));
}

TEST(TraceResult, TheSameInputMakesTheDotNetFixtureByteForByte) {
  mm::TraceHop hops[2];
  std::snprintf(hops[0].prefix, sizeof hops[0].prefix, "a1");
  hops[0].hasSnr = true;
  hops[0].snr = 7.25;
  std::snprintf(hops[1].prefix, sizeof hops[1].prefix, "7f");
  hops[1].hasSnr = true;
  hops[1].snr = -2.5;

  char out[512];
  const std::size_t len = mm::writeTraceResult(hops, 2, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), fixture("TraceResult"));
}

TEST(JobResultReport, TheSameInputMakesTheDotNetFixtureByteForByte) {
  const char* path[] = {"a1", "7f"};
  mm::JobReport r;
  r.clientId = "e7c1";
  r.outcome = mm::Outcome::Ok;
  r.startedAt = at("2026-09-18T10:00:05Z");
  r.finishedAt = at("2026-09-18T10:00:09Z");
  r.hasPathUsed = true;
  r.pathUsed = path;
  r.pathCount = 2;
  r.hasHops = true;
  r.hops = 2;
  r.hasRttMs = true;
  r.rttMs = 4210;
  r.transactions = 2;
  r.data = kSampleStatusJson;
  r.log = "login ok (session reused); status req 1 try; 4.2 s";

  char out[1024];
  const std::size_t len = mm::writeJobResult(r, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), fixture("JobResultReport"));
}

TEST(JobResultReport, TheOutcomesAreTheEnumsCamelCaseNames) {
  EXPECT_STREQ(mm::outcomeName(mm::Outcome::Ok), "ok");
  EXPECT_STREQ(mm::outcomeName(mm::Outcome::Failed), "failed");
  EXPECT_STREQ(mm::outcomeName(mm::Outcome::Unreachable), "unreachable");
  EXPECT_STREQ(mm::outcomeName(mm::Outcome::Unauthorized), "unauthorized");
  EXPECT_STREQ(mm::outcomeName(mm::Outcome::Unsupported), "unsupported");
  EXPECT_STREQ(mm::outcomeName(mm::Outcome::Timeout), "timeout");
}

TEST(JobResultReport, AReportWithNothingToSayLeavesEveryOptionalFieldOut) {
  mm::JobReport r;
  r.clientId = "12-abc";
  r.outcome = mm::Outcome::Unreachable;
  r.startedAt = at("2026-09-18T10:00:05Z");
  r.finishedAt = at("2026-09-18T10:00:09Z");
  r.transactions = 2;

  char out[256];
  const std::size_t len = mm::writeJobResult(r, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len),
            "{\"clientId\":\"12-abc\",\"outcome\":\"unreachable\",\"startedAt\":\"2026-09-18T10:00:05Z\","
            "\"finishedAt\":\"2026-09-18T10:00:09Z\",\"transactions\":2}");
}

TEST(JobResultReport, ALogIsBoundedAtFourThousandCharacters) {
  const std::string log(6000, 'x');
  mm::JobReport r;
  r.clientId = "1-a";
  r.outcome = mm::Outcome::Ok;
  r.log = log.c_str();

  std::vector<char> out(8192);
  const std::size_t len = mm::writeJobResult(r, out.data(), out.size());
  ASSERT_GT(len, 0u);
  const std::string body(out.data(), len);
  const std::size_t quoted = body.find("\"log\":\"");
  ASSERT_NE(quoted, std::string::npos);
  EXPECT_EQ(body.size() - quoted - 7 - 2, mm::kMaxJobLogChars);
}

TEST(JobResultReport, ABodyThatDoesNotFitIsRefusedRatherThanCutInHalf) {
  mm::JobReport r;
  r.clientId = "1-a";
  r.outcome = mm::Outcome::Ok;
  char out[32];
  EXPECT_EQ(mm::writeJobResult(r, out, sizeof out), 0u);
}

// ------------------------------------------------------------------ the values, from the wire

// What the .NET agent's StatusHandler makes of tests/fixtures/companion/status-response.hex, as
// HandlerTests asserts it: every counter, and `recvErrors` beside `eventsFull` in the errors.
TEST(StatusResult, TheTranscribedReplyMakesTheResultTheDotNetAgentMakes) {
  const auto stats = companion("status-response", 8);
  mm::StatusReading s;
  ASSERT_TRUE(mm::parseRepeaterStats(stats.data(), stats.size(), s));

  mm::ErrorCounter errors[mm::kMaxStatusErrors];
  const int n = mm::statusErrors(s, errors, mm::kMaxStatusErrors);
  ASSERT_EQ(n, 3);
  EXPECT_STREQ(errors[0].name, "eventsFull");
  EXPECT_EQ(errors[0].value, 0);
  EXPECT_STREQ(errors[1].name, "recvErrors");
  EXPECT_EQ(errors[1].value, 2);
  EXPECT_STREQ(errors[2].name, "rxAirtimeS");
  EXPECT_EQ(errors[2].value, 5678);

  char out[768];
  const std::size_t len = mm::writeStatusResult(s, errors, n, nullptr, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len),
            "{\"batteryMv\":4012,\"noiseFloorDbm\":-112,\"lastRssi\":-97,\"lastSnr\":6.25,\"uptimeS\":864000,"
            "\"txQueueLen\":0,\"packets\":{\"recv\":12345,\"sent\":6789,\"recvFlood\":9000,\"recvDirect\":3345,"
            "\"sentFlood\":4000,\"sentDirect\":2789},\"airtimeS\":1234,\"dups\":{\"flood\":3,\"direct\":0},"
            "\"errors\":{\"eventsFull\":0,\"recvErrors\":2,\"rxAirtimeS\":5678}}")
      << "no `firmware`: it is a second request and only an acl-class read makes it";
}

TEST(StatusResult, AnAclReadCarriesTheFirmwareTheOwnerInfoRequestFetched) {
  const auto stats = companion("status-response", 8);
  mm::StatusReading s;
  ASSERT_TRUE(mm::parseRepeaterStats(stats.data(), stats.size(), s));
  const auto owner = companion("binary-response-owner-info", 6);
  char firmware[mm::kMaxFirmwareVersionLen + 1] = {0};
  ASSERT_TRUE(mm::parseOwnerInfoFirmware(owner.data(), owner.size(), firmware, sizeof firmware));

  mm::ErrorCounter errors[mm::kMaxStatusErrors];
  const int n = mm::statusErrors(s, errors, mm::kMaxStatusErrors);
  char out[768];
  const std::size_t len = mm::writeStatusResult(s, errors, n, firmware, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_NE(std::string(out, len).find("\"firmware\":\"v1.17.1\"}"), std::string::npos);
}

TEST(TelemetryResult, TheTranscribedReplyMakesTheFourChannelsTheDotNetAgentMakes) {
  const auto lpp = companion("telemetry-response", 8);
  mm::LppReading readings[mm::kMaxLppReadings];
  std::size_t undecodedFrom = 0;
  const int n = mm::decodeLpp(lpp.data(), lpp.size(), readings, mm::kMaxLppReadings, undecodedFrom);
  ASSERT_EQ(n, 4);
  EXPECT_EQ(undecodedFrom, lpp.size());

  char out[512];
  const std::size_t len = mm::writeTelemetryResult(readings, n, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len),
            "{\"channels\":[{\"channel\":1,\"type\":\"voltage\",\"value\":4.01,\"unit\":\"V\"},"
            "{\"channel\":2,\"type\":\"humidity\",\"value\":55.5,\"unit\":\"%\"},"
            "{\"channel\":2,\"type\":\"pressure\",\"value\":1013.2,\"unit\":\"hPa\"},"
            "{\"channel\":1,\"type\":\"temperature\",\"value\":21.5,\"unit\":\"C\"}]}");
}

// The padded reply of 26 September 2026: two readings, and the four zero bytes are nothing.
TEST(TelemetryResult, ThePaddedReplyDecodesToTwoReadingsAndNoPhantomChannelZero) {
  const auto lpp = companion("binary-response-telemetry-padded", 6);
  mm::LppReading readings[mm::kMaxLppReadings];
  std::size_t undecodedFrom = 0;
  const int n = mm::decodeLpp(lpp.data(), lpp.size(), readings, mm::kMaxLppReadings, undecodedFrom);
  ASSERT_EQ(n, 2);
  EXPECT_EQ(undecodedFrom, lpp.size());

  char out[512];
  const std::size_t len = mm::writeTelemetryResult(readings, n, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len),
            "{\"channels\":[{\"channel\":1,\"type\":\"voltage\",\"value\":4.11,\"unit\":\"V\"},"
            "{\"channel\":1,\"type\":\"temperature\",\"value\":16.7,\"unit\":\"C\"}]}");
  EXPECT_EQ(std::string(out, len).find("lpp.0"), std::string::npos);
  EXPECT_EQ(std::string(out, len).find("\"channel\":0"), std::string::npos);
}

TEST(NeighboursResult, TheTranscribedPageMakesTheResultTheDotNetAgentMakes) {
  const auto data = companion("binary-response-neighbours", 6);
  mm::NeighboursPage page;
  ASSERT_TRUE(mm::parseNeighboursPage(data.data(), data.size(), mm::kNeighbourPrefixLen, page));

  char out[512];
  const std::size_t len = mm::writeNeighboursResult(page.entries, page.count, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len),
            "{\"neighbours\":[{\"prefix\":\"7f8e9dacbbcad9e8\",\"lastHeardS\":60,\"snr\":7.5},"
            "{\"prefix\":\"c0ffee0011223344\",\"lastHeardS\":3600,\"snr\":-2.25}]}");
}

// A trace is a **round trip** (task 24), and what the agent reports is all of it with its own
// hash appended: out, the repeater, back, and this exit node. The server takes the outward half
// - (count - 1) / 2 hops - and stores that; the agent halves nothing, because the SNR of every
// leg is a measurement and the result is where measurements go.
TEST(TraceResult, TheWholeRoundTripAndThisBoardsOwnHashAreWhatTheResultCarries) {
  // trace-data.hex: hashes a1 b2 a1, SNRs 7.25, -2.5, 6.5 - out to b2 and back to a1 - and the
  // final SNR 7.5 is what this board measured on the packet coming home.
  const auto frame = companion("trace-data", 1);
  const std::size_t pathLen = frame[1];
  const uint8_t flags = frame[2];
  const std::size_t hops = pathLen >> (flags & 0x03);
  mm::TraceReading trace;
  ASSERT_TRUE(mm::parseTrace(flags, &frame[11], pathLen, &frame[11 + pathLen], hops, trace));
  ASSERT_EQ(trace.count, 3);
  const double finalSnr = static_cast<int8_t>(frame[11 + pathLen + hops]) / 4.0;
  EXPECT_DOUBLE_EQ(finalSnr, 7.5);

  // The exit node's own hash, one byte of its key at this hash size, with the final SNR.
  ASSERT_LT(trace.count, mm::kMaxTraceHops);
  mm::TraceHop& self = trace.hops[trace.count];
  std::snprintf(self.prefix, sizeof self.prefix, "ff");
  self.hasSnr = true;
  self.snr = finalSnr;

  char out[512];
  const std::size_t len = mm::writeTraceResult(trace.hops, trace.count + 1, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len),
            "{\"hops\":[{\"prefix\":\"a1\",\"snr\":7.25},{\"prefix\":\"b2\",\"snr\":-2.5},"
            "{\"prefix\":\"a1\",\"snr\":6.5},{\"prefix\":\"ff\",\"snr\":7.5}]}");
}

TEST(LoginBelowClassResult, ItSaysWhatWasGrantedAndWhatWasNeeded) {
  char out[128];
  const std::size_t len = mm::writeLoginBelowClassResult("guest", "read-only", out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), "{\"loggedInAs\":\"guest\",\"needed\":\"read-only\"}");
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
