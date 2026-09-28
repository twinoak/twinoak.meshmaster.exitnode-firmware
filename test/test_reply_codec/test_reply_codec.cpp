// The parsers that read what a repeater answered, against the bytes the .NET agent reads.
//
// The board **is** the companion, so a reply reaches `Mesh::onPeerDataRecv` as a four-byte tag
// and the repeater's own bytes; the transcribed fixtures under tests/fixtures/companion are the
// same bytes one layer out, wrapped in the push code, the reserved byte and a prefix or a tag
// that `MyMesh::onContactResponse` puts round them on their way to an app over a cable. Each
// test here strips exactly that documented wrapper - the table is in the fixtures' own README -
// and feeds the rest to src/mesh/ReplyCodec.h, then checks the values
// MeshMaster.Agent.Tests asserts for the same file (FrameFixtureTests.cs, HandlerTests.cs).
//
// The one fixture whose bytes are not the board's is the login reply: the companion does not
// pass the repeater's reply through, it *composes* a frame from it (the ver-7 branch of
// onContactResponse: data[6] to byte 1, the tag to bytes 8..11, data[7] to byte 12, data[12] to
// byte 13). That copy is inverted here, in the open, and the board's parser is held to what
// comes out.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mesh/ReplyCodec.h"

#ifndef COMPANION_DIR
#error "COMPANION_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// A fixture is comment lines beginning with '#' and then the frame as hex, wrapped at some
// width. What is loaded is the frame: the push code and its body.
std::vector<uint8_t> load(const std::string& name) {
  std::ifstream in(fs::path(COMPANION_DIR) / (name + ".hex"));
  EXPECT_TRUE(in.good()) << "no such fixture: " << name;
  std::string line;
  std::string hex;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    for (char c : line) {
      if (hexDigit(c) >= 0) hex.push_back(c);
    }
  }
  EXPECT_EQ(hex.size() % 2, 0u) << name << " has an odd number of hex digits";
  std::vector<uint8_t> bytes;
  bytes.reserve(hex.size() / 2);
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<uint8_t>((hexDigit(hex[i]) << 4) | hexDigit(hex[i + 1])));
  }
  return bytes;
}

// The wrappers of the three shapes, as tests/fixtures/companion/README.md tabulates them.
std::vector<uint8_t> afterPrefix(const std::string& name) {  // code, reserved, pubkey prefix[6]
  const auto frame = load(name);
  EXPECT_GE(frame.size(), 8u);
  return {frame.begin() + 8, frame.end()};
}

std::vector<uint8_t> afterTag(const std::string& name) {  // code, reserved, tag[4]
  const auto frame = load(name);
  EXPECT_GE(frame.size(), 6u);
  return {frame.begin() + 6, frame.end()};
}

std::vector<uint8_t> body(const std::string& name) {  // code
  const auto frame = load(name);
  EXPECT_GE(frame.size(), 1u);
  return {frame.begin() + 1, frame.end()};
}

std::vector<uint8_t> fromHex(const std::string& hex) {
  std::vector<uint8_t> bytes;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<uint8_t>((hexDigit(hex[i]) << 4) | hexDigit(hex[i + 1])));
  }
  return bytes;
}

std::string toHex(const uint8_t* bytes, std::size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (std::size_t i = 0; i < len; i++) {
    out.push_back(digits[bytes[i] >> 4]);
    out.push_back(digits[bytes[i] & 0x0F]);
  }
  return out;
}

// ------------------------------------------------------------------ the fixture set itself

TEST(CompanionFixtures, EveryListedFileIsHereWellFormedAndCitesItsFirmware) {
  std::ifstream manifest(fs::path(COMPANION_DIR) / "MANIFEST.txt");
  ASSERT_TRUE(manifest.good()) << "tests/fixtures/companion/MANIFEST.txt is missing";
  std::string line;
  int listed = 0;
  std::vector<std::string> names;
  while (std::getline(manifest, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream row(line);
    std::string sha;
    std::uintmax_t size = 0;
    std::string name;
    row >> sha >> size >> name;
    ASSERT_FALSE(name.empty()) << "unreadable MANIFEST.txt line: " << line;
    const fs::path file = fs::path(COMPANION_DIR) / name;
    ASSERT_TRUE(fs::exists(file)) << name << " is in MANIFEST.txt and not on disk";
    EXPECT_EQ(fs::file_size(file), size) << name << " is not the size MANIFEST.txt records";

    // Every transcribed fixture names the firmware function its bytes were read from, which is
    // the rule that caught two misread bytes in the .NET port (tasks p3.5-41 and 42).
    std::ifstream in(file);
    std::string fixtureLine;
    bool cited = false;
    while (std::getline(in, fixtureLine)) {
      if (fixtureLine.rfind("# fw ", 0) == 0) cited = true;
    }
    EXPECT_TRUE(cited) << name << " has no '# fw ' line naming the firmware function";
    names.push_back(name);
    listed++;
  }
  EXPECT_GT(listed, 0);

  int onDisk = 0;
  for (const auto& entry : fs::directory_iterator(COMPANION_DIR)) {
    if (entry.is_regular_file() && entry.path().extension() == ".hex") onDisk++;
  }
  EXPECT_EQ(onDisk, listed) << "a .hex in tests/fixtures/companion is not in MANIFEST.txt";
}

// ------------------------------------------------------------------ status

TEST(Status, TheTranscribedReplyReadsEveryCounter) {
  const auto stats = afterPrefix("status-response");
  ASSERT_EQ(stats.size(), 56u);

  mm::StatusReading s;
  ASSERT_TRUE(mm::parseRepeaterStats(stats.data(), stats.size(), s));
  EXPECT_EQ(s.batteryMv, 4012);
  EXPECT_EQ(s.txQueueLen, 0);
  EXPECT_EQ(s.noiseFloorDbm, -112);
  EXPECT_EQ(s.lastRssi, -97);
  EXPECT_EQ(s.packetsRecv, 12345u);
  EXPECT_EQ(s.packetsSent, 6789u);
  EXPECT_EQ(s.airtimeS, 1234u);
  EXPECT_EQ(s.uptimeS, 864000u);
  EXPECT_EQ(s.sentFlood, 4000u);
  EXPECT_EQ(s.sentDirect, 2789u);
  EXPECT_EQ(s.recvFlood, 9000u);
  EXPECT_EQ(s.recvDirect, 3345u);
  EXPECT_EQ(s.errEvents, 0);
  EXPECT_DOUBLE_EQ(s.lastSnr, 6.25);
  EXPECT_EQ(s.directDups, 0);
  EXPECT_EQ(s.floodDups, 3);
  EXPECT_TRUE(s.hasRxAirtime);
  EXPECT_EQ(s.rxAirtimeS, 5678u);
  EXPECT_TRUE(s.hasRecvErrors);
  EXPECT_EQ(s.recvErrors, 2u);
}

TEST(Status, AShorterStatsStructParsesAndLeavesTheLastTwoUnknown) {
  const auto stats = afterPrefix("status-response-48");
  ASSERT_EQ(stats.size(), 48u);

  mm::StatusReading s;
  ASSERT_TRUE(mm::parseRepeaterStats(stats.data(), stats.size(), s));
  EXPECT_EQ(s.batteryMv, 4012);
  EXPECT_EQ(s.noiseFloorDbm, -112);
  EXPECT_EQ(s.packetsRecv, 12345u);
  EXPECT_DOUBLE_EQ(s.lastSnr, 6.25);
  EXPECT_EQ(s.floodDups, 3);
  // Unknown, not zero: a zero would be reported as a real reading of no received errors.
  EXPECT_FALSE(s.hasRxAirtime);
  EXPECT_FALSE(s.hasRecvErrors);
}

TEST(Status, AStatsStructShorterThanTheFixedFieldsIsRefused) {
  const auto stats = afterPrefix("status-response");
  mm::StatusReading s;
  EXPECT_FALSE(mm::parseRepeaterStats(stats.data(), 47, s));
  EXPECT_FALSE(mm::parseRepeaterStats(nullptr, 56, s));
}

TEST(OwnerInfo, OnlyTheFirmwareVersionIsKeptAndTheNameAndOwnerAreNot) {
  const auto data = afterTag("binary-response-owner-info");
  char firmware[mm::kMaxFirmwareVersionLen + 1] = {0};
  ASSERT_TRUE(mm::parseOwnerInfoFirmware(data.data(), data.size(), firmware, sizeof firmware));
  EXPECT_STREQ(firmware, "v1.17.1");
  EXPECT_EQ(std::string(firmware).find("Grenaa"), std::string::npos);
  EXPECT_EQ(std::string(firmware).find("TwinOak"), std::string::npos);
}

// ------------------------------------------------------------------ telemetry

void expectTheFourChannels(const std::vector<uint8_t>& lpp) {
  mm::LppReading readings[mm::kMaxLppReadings];
  std::size_t undecodedFrom = 0;
  const int n = mm::decodeLpp(lpp.data(), lpp.size(), readings, mm::kMaxLppReadings, undecodedFrom);
  ASSERT_EQ(n, 4);
  // The order is the firmware's: the battery before querySensors(), the MCU temperature after
  // it, so a channel-1 temperature always follows the channel-2 sensors (task p3.5-42).
  EXPECT_EQ(readings[0].channel, 1);
  EXPECT_STREQ(readings[0].type, "voltage");
  EXPECT_NEAR(readings[0].value, 4.01, 1e-9);
  EXPECT_STREQ(readings[0].unit, "V");
  EXPECT_EQ(readings[1].channel, 2);
  EXPECT_STREQ(readings[1].type, "humidity");
  EXPECT_NEAR(readings[1].value, 55.5, 1e-9);
  EXPECT_STREQ(readings[1].unit, "%");
  EXPECT_EQ(readings[2].channel, 2);
  EXPECT_STREQ(readings[2].type, "pressure");
  EXPECT_NEAR(readings[2].value, 1013.2, 1e-9);
  EXPECT_STREQ(readings[2].unit, "hPa");
  EXPECT_EQ(readings[3].channel, 1);
  EXPECT_STREQ(readings[3].type, "temperature");
  EXPECT_NEAR(readings[3].value, 21.5, 1e-9);
  EXPECT_STREQ(readings[3].unit, "C");
  EXPECT_EQ(undecodedFrom, lpp.size());
}

TEST(Telemetry, TheLppChannelsDecodeBigEndianFromThePushFrame) { expectTheFourChannels(afterPrefix("telemetry-response")); }

TEST(Telemetry, TheSameBytesDecodeFromABinaryResponse) { expectTheFourChannels(afterTag("binary-response-telemetry")); }

// Job 43's own telemetry reply, 26 September 2026: two readings and four zero bytes. Those four
// are the cipher block's padding, and channel 0 is not a channel - TELEM_CHANNEL_SELF is 1 - so
// the readings end there and nothing is left over (task p3.5-44).
TEST(Telemetry, ThePaddingAfterAReplyIsNotAChannelZeroReading) {
  const auto lpp = afterTag("binary-response-telemetry-padded");
  ASSERT_EQ(lpp.size(), 12u);

  mm::LppReading readings[mm::kMaxLppReadings];
  std::size_t undecodedFrom = 0;
  const int n = mm::decodeLpp(lpp.data(), lpp.size(), readings, mm::kMaxLppReadings, undecodedFrom);
  ASSERT_EQ(n, 2);
  EXPECT_EQ(readings[0].channel, 1);
  EXPECT_STREQ(readings[0].type, "voltage");
  EXPECT_NEAR(readings[0].value, 4.11, 1e-9);
  EXPECT_EQ(readings[1].channel, 1);
  EXPECT_STREQ(readings[1].type, "temperature");
  EXPECT_NEAR(readings[1].value, 16.7, 1e-9);
  EXPECT_EQ(undecodedFrom, lpp.size()) << "the four zero bytes are the padding and are nothing";
  EXPECT_EQ(toHex(lpp.data() + 8, 4), "00000000");
}

// The guard is "zeros, and fewer than a block of them", not "stop at the first zero". A byte
// that is not zero after the padding is a reply this table does not understand, and the whole
// tail from the zero channel byte is reported as undecoded rather than swallowed.
TEST(Telemetry, OnlyAShortRunOfZerosIsPadding) {
  struct Case {
    const char* lpp;
    const char* undecoded;
  };
  const Case cases[] = {
      {"0174019b016700a700000001", "00000001"},
      {"0174019b016700a70000010000", "0000010000"},
      {"0174019b016700a700", ""},
      {"0174019b016700a7000000000000000000000000000000000000", "000000000000000000000000000000000000"},
  };
  for (const Case& c : cases) {
    const auto lpp = fromHex(c.lpp);
    mm::LppReading readings[mm::kMaxLppReadings];
    std::size_t undecodedFrom = 0;
    const int n = mm::decodeLpp(lpp.data(), lpp.size(), readings, mm::kMaxLppReadings, undecodedFrom);
    EXPECT_EQ(n, 2) << c.lpp;
    EXPECT_EQ(toHex(lpp.data() + undecodedFrom, lpp.size() - undecodedFrom), std::string(c.undecoded)) << c.lpp;
  }
}

TEST(Telemetry, ATypeThisTableDoesNotKnowStopsTheDecodeAndIsReported) {
  const auto lpp = fromHex("017401910299deadbeef");  // 0x99 is not an LPP type
  mm::LppReading readings[mm::kMaxLppReadings];
  std::size_t undecodedFrom = 0;
  const int n = mm::decodeLpp(lpp.data(), lpp.size(), readings, mm::kMaxLppReadings, undecodedFrom);
  EXPECT_EQ(n, 1);
  EXPECT_EQ(undecodedFrom, 4u);
}

// ------------------------------------------------------------------ neighbours

TEST(Neighbours, ThePageReadsTheTotalAndEveryEntryAtTheAskedPrefixLength) {
  const auto data = afterTag("binary-response-neighbours");
  mm::NeighboursPage page;
  ASSERT_TRUE(mm::parseNeighboursPage(data.data(), data.size(), mm::kNeighbourPrefixLen, page));
  EXPECT_EQ(page.total, 2);
  ASSERT_EQ(page.count, 2);
  EXPECT_STREQ(page.entries[0].prefix, "7f8e9dacbbcad9e8");
  EXPECT_EQ(page.entries[0].lastHeardS, 60);
  EXPECT_DOUBLE_EQ(page.entries[0].snr, 7.5);
  EXPECT_STREQ(page.entries[1].prefix, "c0ffee0011223344");
  EXPECT_EQ(page.entries[1].lastHeardS, 3600);
  EXPECT_DOUBLE_EQ(page.entries[1].snr, -2.25);
}

TEST(Neighbours, NothingIsReadPastTheEndOfATruncatedPage) {
  auto data = afterTag("binary-response-neighbours");
  data.resize(data.size() - 3);  // the last entry is cut short
  mm::NeighboursPage page;
  ASSERT_TRUE(mm::parseNeighboursPage(data.data(), data.size(), mm::kNeighbourPrefixLen, page));
  EXPECT_EQ(page.count, 1);
  EXPECT_EQ(page.total, 2) << "the repeater still says how many it has";
}

TEST(Neighbours, ThePageSizeAndThePriceAgree) {
  // Nine a page is what the agent asks for and what the server's planned load prices with
  // (NeighboursPayload.PerPage), so the two cannot disagree.
  EXPECT_EQ(mm::kNeighboursPerPage, 9);
}

// ------------------------------------------------------------------ the login reply

// The ver-7 branch of MyMesh::onContactResponse (companion-v1.17.1, examples/companion_radio/
// MyMesh.cpp:690-702) composes its frame from the repeater's reply: byte 1 is the reply's
// data[6] (is_admin), bytes 2..7 the contact's prefix, 8..11 the tag, byte 12 the reply's
// data[7] (the ACL permission byte) and byte 13 its data[12] (FIRMWARE_VER_LEVEL). Inverting it
// here gives the bytes this board sees, which are the repeater's own.
std::vector<uint8_t> repeaterLoginReplyFrom(const std::string& fixture) {
  const auto b = body(fixture);  // is_admin(1), prefix(6), [ts(4), acl(1), level(1)]
  std::vector<uint8_t> reply;
  if (b.size() < 13) {
    // A legacy repeater's "OK", which the companion recognises before the new shape.
    reply = {'O', 'K'};
    return reply;
  }
  reply.resize(9);
  reply[0] = 0;      // RESP_SERVER_LOGIN_OK
  reply[1] = 0;      // the legacy keep-alive field
  reply[2] = b[0];   // is_admin
  reply[3] = b[11];  // the client entry's permission byte
  reply[4] = reply[5] = reply[6] = reply[7] = 0xAB;  // the random blob that makes the hash unique
  reply[8] = b[12];  // FIRMWARE_VER_LEVEL
  return reply;
}

TEST(Login, TheGrantIsReadFromAclPermsAndNotFromTheIsAdminFlag) {
  struct Case {
    const char* fixture;
    uint8_t isAdminFlag;
    uint8_t role;
    const char* tier;
    bool admin;
  };
  const Case cases[] = {
      {"login-success", 1, 3, "admin", true},
      {"login-success-readonly", 0, 1, "read-only", false},
      {"login-success-legacy", 0, 0, "guest", false},
      {"login-success-legacy-admin", 1, 3, "admin", true},
  };
  for (const Case& c : cases) {
    const auto reply = repeaterLoginReplyFrom(c.fixture);
    mm::LoginReply login;
    ASSERT_TRUE(mm::parseLoginReply(reply.data(), reply.size(), login)) << c.fixture;
    if (login.legacy) {
      // A pre-v7 repeater sends "OK" and nothing else, and the companion hard-codes is_admin 0;
      // an is_admin of 1 on such a reply cannot travel, so the two legacy fixtures read as guest
      // here and their flag is the companion's own.
      EXPECT_STREQ(mm::permissionName(login.role()), "guest") << c.fixture;
      continue;
    }
    EXPECT_EQ(login.isAdminFlag, c.isAdminFlag) << c.fixture;
    EXPECT_EQ(login.role(), c.role) << c.fixture;
    EXPECT_STREQ(mm::permissionName(login.role()), c.tier) << c.fixture;
    EXPECT_EQ(login.isAdmin(), c.admin) << c.fixture;
    EXPECT_TRUE(login.hasFirmwareLevel) << c.fixture;
    EXPECT_EQ(login.firmwareLevel, 2) << c.fixture;
  }
}

// The reply of 19:13 on 25 September 2026, as the firmware builds it for a correct admin
// password: is_admin 1 and the permission byte 3. Read the way this port read it before task 41
// - the flag as the role - it is "read-only" and no admin login through the agent was ever
// recognised as one.
TEST(Login, AnAdminLoginReadsAsAdminAndNotAsTheReadOnlyTheOldReadingMadeOfIt) {
  const auto reply = repeaterLoginReplyFrom("login-success");
  mm::LoginReply login;
  ASSERT_TRUE(mm::parseLoginReply(reply.data(), reply.size(), login));
  EXPECT_STREQ(mm::permissionName(login.role()), "admin");
  EXPECT_TRUE(login.isAdmin());
  // What the old reading made of the same bytes, for the record.
  EXPECT_STREQ(mm::permissionName(login.isAdminFlag), "read-only");
}

TEST(Login, AReplyThatIsNotALoginIsRefusedRatherThanRead) {
  const uint8_t notALogin[] = {0x7F, 0x01, 0x02};
  mm::LoginReply login;
  EXPECT_FALSE(mm::parseLoginReply(notALogin, sizeof notALogin, login));
  const uint8_t tooShort[] = {0x00};
  EXPECT_FALSE(mm::parseLoginReply(tooShort, sizeof tooShort, login));
}

TEST(Login, WhatEachClassNeedsIsWhatTheFirmwareGrades) {
  EXPECT_EQ(mm::roleNeededFor(mm::CredentialClass::Admin), mm::kPermAdmin);
  EXPECT_EQ(mm::roleNeededFor(mm::CredentialClass::Acl), mm::kPermReadOnly);
  EXPECT_EQ(mm::roleNeededFor(mm::CredentialClass::Guest), mm::kPermGuest);
  EXPECT_EQ(mm::roleNeededFor(mm::CredentialClass::None), mm::kPermGuest);
}

// ------------------------------------------------------------------ trace

// PUSH_CODE_TRACE_DATA is a faithful serialisation of what Mesh::onTraceRecv hands a subclass:
// reserved, path_len (bytes), flags, tag[4], auth[4], hashes[path_len], snrs[path_len >> path_sz],
// final snr. So the fixture's body is read back into the callback's arguments here.
struct TraceFrame {
  uint8_t flags = 0;
  std::vector<uint8_t> hashes;
  std::vector<uint8_t> snrs;
  int8_t finalSnr = 0;
};

TraceFrame traceFrom(const std::string& fixture) {
  const auto b = body(fixture);
  TraceFrame t;
  const std::size_t pathLen = b[1];
  t.flags = b[2];
  const std::size_t hops = pathLen >> (t.flags & 0x03);
  t.hashes.assign(b.begin() + 11, b.begin() + 11 + static_cast<long>(pathLen));
  t.snrs.assign(b.begin() + 11 + static_cast<long>(pathLen), b.begin() + 11 + static_cast<long>(pathLen + hops));
  t.finalSnr = static_cast<int8_t>(b[11 + pathLen + hops]);
  return t;
}

TEST(Trace, TheHopsTheirSnrsAndTheFinalOneAreRead) {
  const TraceFrame t = traceFrom("trace-data");
  EXPECT_EQ(t.flags, 0);

  mm::TraceReading trace;
  ASSERT_TRUE(mm::parseTrace(t.flags, t.hashes.data(), t.hashes.size(), t.snrs.data(), t.snrs.size(), trace));
  EXPECT_EQ(trace.hashSize, 1);
  ASSERT_EQ(trace.count, 3);
  EXPECT_STREQ(trace.hops[0].prefix, "a1");
  EXPECT_STREQ(trace.hops[1].prefix, "b2");
  EXPECT_STREQ(trace.hops[2].prefix, "a1");
  EXPECT_DOUBLE_EQ(trace.hops[0].snr, 7.25);
  EXPECT_DOUBLE_EQ(trace.hops[1].snr, -2.5);
  EXPECT_DOUBLE_EQ(trace.hops[2].snr, 6.5);
  EXPECT_DOUBLE_EQ(t.finalSnr / 4.0, 7.5);
}

// The trace flags byte is the hash size's **exponent**, not the size minus one (task p3.5-42):
// 0, 1, 2, 3 mean 1, 2, 4 and 8 bytes a hash - a different encoding from a packet path's length
// byte, which is (byte >> 6) + 1 and runs 1, 2, 3.
TEST(Trace, ATraceOfTwoByteHashesStatesTheExponentInItsFlagsByte) {
  const TraceFrame t = traceFrom("trace-data-2byte");
  EXPECT_EQ(t.flags, 1);

  mm::TraceReading trace;
  ASSERT_TRUE(mm::parseTrace(t.flags, t.hashes.data(), t.hashes.size(), t.snrs.data(), t.snrs.size(), trace));
  EXPECT_EQ(trace.hashSize, 2);
  ASSERT_EQ(trace.count, 3);
  EXPECT_STREQ(trace.hops[0].prefix, "a1b2");
  EXPECT_STREQ(trace.hops[1].prefix, "c3d4");
  EXPECT_STREQ(trace.hops[2].prefix, "a1b2");
  EXPECT_DOUBLE_EQ(trace.hops[0].snr, 7.25);
  EXPECT_DOUBLE_EQ(trace.hops[2].snr, 6.5);
}

TEST(Trace, ThePathHashSizesATracePacketCanCarryAreThePowersOfTwo) {
  EXPECT_TRUE(mm::isTraceableHashSize(1));
  EXPECT_TRUE(mm::isTraceableHashSize(2));
  EXPECT_FALSE(mm::isTraceableHashSize(3)) << "path_hash_mode 2's three-byte hashes have no encoding";
  EXPECT_TRUE(mm::isTraceableHashSize(4));
  EXPECT_TRUE(mm::isTraceableHashSize(8));
  EXPECT_EQ(mm::traceHashSizeFor(0), 1);
  EXPECT_EQ(mm::traceHashSizeFor(1), 2);
  EXPECT_EQ(mm::traceHashSizeFor(2), 4);
  EXPECT_EQ(mm::traceHashSizeFor(3), 8);
  EXPECT_EQ(mm::traceFlagsFor(1), 0);
  EXPECT_EQ(mm::traceFlagsFor(2), 1);
  EXPECT_EQ(mm::traceFlagsFor(4), 2);
  EXPECT_EQ(mm::traceFlagsFor(8), 3);
}

}  // namespace

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
