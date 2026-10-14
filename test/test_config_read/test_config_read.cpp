// The config read (task 04; agent/ConfigRead.h): the list and its price, the access-list
// reply read from the transcribed fixture bytes, the region helpers, and the ConfigReadResult
// held to the .NET fixture byte for byte - `aclRestored` included. Plus the JSON writer's
// escaping, which that fixture is the first to exercise (`\u003E`).

#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <agent/Airtime.h>
#include <agent/ConfigRead.h>
#include <agent/JsonWriter.h>
#include <agent/ReplyCodec.h>

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

// A companion fixture's frame after its wrapper: code, reserved, tag[4] for a binary response.
std::vector<uint8_t> afterTag(const std::string& name) {
  std::ifstream in(fs::path(COMPANION_DIR) / (name + ".hex"));
  EXPECT_TRUE(in.good()) << "no such companion fixture: " << name;
  std::string line;
  std::string hex;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    for (char c : line) {
      if (hexDigit(c) >= 0) hex.push_back(c);
    }
  }
  std::vector<uint8_t> bytes;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<uint8_t>((hexDigit(hex[i]) << 4) | hexDigit(hex[i + 1])));
  }
  EXPECT_GE(bytes.size(), 6u);
  return {bytes.begin() + 6, bytes.end()};
}

std::string json(const char* text) {
  char out[512];
  mm::JsonWriter w(out, sizeof out);
  w.string(text);
  return std::string(out, w.length());
}

// ------------------------------------------------------------------ the list and its price

// `ConfigReadPayload.DefaultKeys` in meshmaster.dk's `src/MeshMaster.Contracts/Payloads.cs`,
// which the contract fixtures do **not** carry - `ConfigReadPayload.json` is a three-key list,
// the one the payload's own sample uses - so this is the literal, with the file it was read from
// named here and in `ConfigRead.cpp`. `agc.reset.interval` is the fourteenth, which is what
// CONFIGURATION §2's paragraph on that key says ("the fourteenth key, before `regions` and
// `acl`"); task p1.1-08's harness found it missing and task p1.3-12 put it back.
TEST(ConfigList, TheDefaultListIsTheServersSixteenKeysInTheServersOrder) {
  ASSERT_EQ(mm::kDefaultConfigKeyCount, 16);
  const char* expected[] = {"name",      "lat",  "lon",           "advert.interval", "flood.advert.interval",
                            "repeat",    "dutycycle", "flood.max", "flood.max.unscoped", "tx",
                            "path.hash.mode", "multi.acks", "allow.read.only", "agc.reset.interval", "regions",
                            "acl"};
  ASSERT_EQ(static_cast<int>(sizeof expected / sizeof expected[0]), mm::kDefaultConfigKeyCount);
  for (int i = 0; i < mm::kDefaultConfigKeyCount; i++) EXPECT_STREQ(mm::kDefaultConfigKeys[i], expected[i]) << i;
  EXPECT_STREQ(mm::kDefaultConfigKeys[13], "agc.reset.interval") << "the server's fourteenth";
  EXPECT_STREQ(mm::kConfigReadExtras[0], "ver");
  EXPECT_STREQ(mm::kConfigReadExtras[1], "board");
}

// ConfigReadPayload.Transactions: n + 2 + r. The default read is 19 since the list became the
// server's sixteen (task p1.3-12), plus the login: EXIT-NODES §7's "19 + login = 20", and 21
// with the ACL restore this firmware pays after it.
TEST(ConfigList, ThePriceIsTheServersOwnArithmetic) {
  EXPECT_EQ(mm::configReadTransactions(nullptr, 0), 19);
  EXPECT_EQ(mm::configReadTransactions(mm::kDefaultConfigKeys, mm::kDefaultConfigKeyCount), 19);
  const char* three[] = {"name", "advert.interval", "flood.max"};  // the payload fixture's list
  EXPECT_EQ(mm::configReadTransactions(three, 3), 5);
  const char* withRegions[] = {"name", "regions"};
  EXPECT_EQ(mm::configReadTransactions(withRegions, 2), 5);
  const char* aclOnly[] = {"acl"};
  EXPECT_EQ(mm::configReadTransactions(aclOnly, 1), 3) << "the binary request costs the one transaction the get cost";
  EXPECT_EQ(mm::transactionsFor("config.read", mm::CredentialClass::Admin), 19);
  EXPECT_EQ(mm::transactionsFor("config.read", mm::CredentialClass::Admin, mm::kMaxConfigKeys), 35)
      << "a claim at the cap: 32 keys, the two extras, and regions priced as the ceiling";
  EXPECT_EQ(mm::transactionsFor("discover", mm::CredentialClass::Admin), 1);
  EXPECT_EQ(mm::transactionsFor("acl.set", mm::CredentialClass::Admin), 1);
  EXPECT_EQ(mm::kTransactionsForAclRestore, 1);
}

// ------------------------------------------------------------------ the access list

// The cited fixture: three entries at the three levels setperm knows, one with a sensor bit
// above the role mask set, and seven bytes of cipher padding after them.
TEST(AccessList, TheCitedFixtureReadsAsThreeEntriesWithTheRolesMasked) {
  const auto data = afterTag("access-list-response");
  mm::AccessListEntry entries[mm::kAccessListMaxEntries];
  int count = 0;
  ASSERT_TRUE(mm::parseAccessList(data.data(), data.size(), entries, mm::kAccessListMaxEntries, count));
  ASSERT_EQ(count, 3);
  EXPECT_STREQ(entries[0].prefix, "c0ffee001122");
  EXPECT_EQ(entries[0].role(), mm::kPermReadOnly);
  EXPECT_STREQ(entries[1].prefix, "7f8e9dacbbca");
  EXPECT_EQ(entries[1].role(), mm::kPermReadWrite);
  EXPECT_STREQ(entries[2].prefix, "5e5e5e5e5e5e");
  EXPECT_EQ(entries[2].permissions, 0x83);
  EXPECT_EQ(entries[2].role(), mm::kPermAdmin) << "131 to a reader that forgets the mask";

  char tokens[mm::kMaxAclValueLen + 1];
  mm::accessListTokens(entries, count, tokens, sizeof tokens);
  EXPECT_STREQ(tokens, "c0ffee001122:1 7f8e9dacbbca:2 5e5e5e5e5e5e:3");
}

// The parse stops at padding and refuses a non-zero tail.
TEST(AccessList, TheParseStopsAtThePaddingAndRefusesANonZeroTail) {
  auto data = afterTag("access-list-response");
  mm::AccessListEntry entries[mm::kAccessListMaxEntries];
  int count = 0;
  ASSERT_TRUE(mm::parseAccessList(data.data(), data.size(), entries, mm::kAccessListMaxEntries, count));
  EXPECT_EQ(count, 3);

  // One byte of the padding that is not zero: not the layout this reader knows, and nothing is
  // read out of it - a guessed ACL is what would revoke an association. (The byte flipped is a
  // prefix slot of a fourth entry that never was; its permissions slot stays zero, which is
  // where the reader stops, as the .NET reader does.)
  data[data.size() - 2] = 0x01;
  EXPECT_FALSE(mm::parseAccessList(data.data(), data.size(), entries, mm::kAccessListMaxEntries, count));
  EXPECT_EQ(count, 0);

  // A permissions byte of zero is where the padding starts, whatever follows in the prefix
  // slot; but a non-zero byte after it is still refused.
  std::vector<uint8_t> odd = {0xc0, 0xff, 0xee, 0x00, 0x11, 0x22, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ASSERT_TRUE(mm::parseAccessList(odd.data(), odd.size(), entries, mm::kAccessListMaxEntries, count));
  EXPECT_EQ(count, 1);
  odd[8] = 0x55;
  EXPECT_FALSE(mm::parseAccessList(odd.data(), odd.size(), entries, mm::kAccessListMaxEntries, count));
}

TEST(AccessList, AnEmptyBodyIsAnEmptyAclAndARealAnswer) {
  mm::AccessListEntry entries[mm::kAccessListMaxEntries];
  int count = 7;
  EXPECT_TRUE(mm::parseAccessList(nullptr, 0, entries, mm::kAccessListMaxEntries, count));
  EXPECT_EQ(count, 0);
  const uint8_t padding[16] = {0};
  EXPECT_TRUE(mm::parseAccessList(padding, sizeof padding, entries, mm::kAccessListMaxEntries, count));
  EXPECT_EQ(count, 0);
}

// The reply at its ceiling: 23 entries, which is the most one datagram can carry, and no
// evidence either way.
TEST(AccessList, TheFullFixtureIsTwentyThreeEntriesAndTheCeilingIsTheDatagramsArithmetic) {
  EXPECT_EQ(mm::kAccessListMaxEntries, 23);
  EXPECT_EQ(mm::kAccessListEntrySize, 7);
  const auto data = afterTag("access-list-response-full");
  mm::AccessListEntry entries[mm::kAccessListMaxEntries];
  int count = 0;
  ASSERT_TRUE(mm::parseAccessList(data.data(), data.size(), entries, mm::kAccessListMaxEntries, count));
  EXPECT_EQ(count, mm::kAccessListMaxEntries);
  EXPECT_STREQ(entries[0].prefix, "010101010101");
  EXPECT_EQ(entries[0].role(), 1);
  EXPECT_STREQ(entries[22].prefix, "171717171717");
  EXPECT_EQ(entries[22].role(), 2);

  char tokens[mm::kMaxAclValueLen + 1];
  const size_t len = mm::accessListTokens(entries, count, tokens, sizeof tokens);
  EXPECT_EQ(len, 23u * 14 + 22);
  EXPECT_LE(len, mm::kMaxAclValueLen);
  EXPECT_STREQ(mm::kAclFullLine, "acl: 23 entries, the firmware's reply is full and may be truncated");
}

// ------------------------------------------------------------------ the regions

TEST(Regions, AListingThatFilledTheBufferOrLostItsNewlineIsNoEvidence) {
  EXPECT_FALSE(mm::regionListingIsTruncated("*\n  dk\n    aarhus F\n"));
  EXPECT_TRUE(mm::regionListingIsTruncated("*\n  dk\n    aarhus F"));  // no newline: cut off
  EXPECT_TRUE(mm::regionListingIsTruncated(""));
  std::string full = "*\n";
  while (full.size() < 159) full += "x";
  EXPECT_TRUE(mm::regionListingIsTruncated(full.c_str()));
  EXPECT_EQ(mm::kRegionReplyLimit, 160u);
}

TEST(Regions, TheDefaultScopeIsReadFromTheReadingSentenceAndNotFromTheSettingOne) {
  char out[40];
  EXPECT_TRUE(mm::regionDefaultOf(" default scope is dk", out, sizeof out));
  EXPECT_STREQ(out, "dk");
  EXPECT_TRUE(mm::regionDefaultOf("default scope is <null>\n", out, sizeof out));
  EXPECT_STREQ(out, "<null>");
  EXPECT_FALSE(mm::regionDefaultOf("default scope is now dk", out, sizeof out)) << "the setting command's reply";
  EXPECT_FALSE(mm::regionDefaultOf("default scope is ", out, sizeof out));
  EXPECT_FALSE(mm::regionDefaultOf("Unknown command", out, sizeof out));
  EXPECT_FALSE(mm::regionDefaultOf(nullptr, out, sizeof out));
}

TEST(Regions, TheValueIsTheListingAndOneLineMeshMasterAdds) {
  char out[mm::kMaxConfigValueLen + 1];
  ASSERT_TRUE(mm::regionValue("*\n  dk\n    aarhus F\n", " default scope is dk", out, sizeof out));
  EXPECT_STREQ(out, "*\n  dk\n    aarhus F\ndefault dk");
  ASSERT_TRUE(mm::regionValue("*\n  dk\n", "Unknown command", out, sizeof out));
  EXPECT_STREQ(out, "*\n  dk\ndefault <null>");
  EXPECT_FALSE(mm::regionValue("*\n  dk", " default scope is dk", out, sizeof out)) << "cut off: no evidence";
  EXPECT_FALSE(mm::regionValue("", " default scope is dk", out, sizeof out));
  EXPECT_FALSE(mm::regionValue(nullptr, nullptr, out, sizeof out));
}

// ------------------------------------------------------------------ a get reply, a CLI reply

TEST(CliReply, AGetAnswersWithAnAngleBracketAndAnythingElseIsRaw) {
  EXPECT_STREQ(mm::cliReplyValue("> 240"), "240");
  EXPECT_STREQ(mm::cliReplyValue("  > Grenaa Chimney #1"), "Grenaa Chimney #1");
  EXPECT_EQ(mm::cliReplyValue("??: acl"), nullptr);
  EXPECT_EQ(mm::cliReplyValue("Unknown command"), nullptr);
  EXPECT_EQ(mm::cliReplyValue(">240"), nullptr);
  EXPECT_EQ(mm::cliReplyValue(nullptr), nullptr);
  char text[] = "  OK  \r\n";
  EXPECT_STREQ(mm::trimInPlace(text), "OK");
}

// The datagram a repeater answers a CLI line with: timestamp, the flags byte with the text type
// in its upper bits, the text - the bytes of contact-msg-recv-v3.hex's reply to `get
// advert.interval`, in the shape they have on the air rather than on the cable.
TEST(CliReply, TheTextIsReadAfterTheTimestampAndTheFlags) {
  std::vector<uint8_t> data = {0x50, 0x8d, 0xd2, 0x68, static_cast<uint8_t>(mm::kTxtTypeCliData << 2),
                               '>',  ' ',  '2',  '4',  '0', 0, 0, 0, 0, 0, 0};  // padded to the block
  char text[mm::kMaxCliReplyLen + 1];
  ASSERT_TRUE(mm::parseCliReply(data.data(), data.size(), text, sizeof text));
  EXPECT_STREQ(text, "> 240");
  EXPECT_STREQ(mm::cliReplyValue(text), "240");

  data[4] = 0;  // TXT_TYPE_PLAIN, a legacy repeater's CLI reply
  ASSERT_TRUE(mm::parseCliReply(data.data(), data.size(), text, sizeof text));
  EXPECT_STREQ(text, "> 240");

  data[4] = static_cast<uint8_t>(2 << 2);  // a signed plain text, which is not a CLI reply
  EXPECT_FALSE(mm::parseCliReply(data.data(), data.size(), text, sizeof text));
  EXPECT_FALSE(mm::parseCliReply(data.data(), 5, text, sizeof text)) << "no text at all";
}

// ------------------------------------------------------------------ the result

// ConfigReadResult(Values, Raw, Firmware, Board, AclRestored), byte for byte: the sample
// MeshMaster.Contracts' round-trip test wrote, with a `>` the .NET encoder escapes.
TEST(ConfigReadResult, TheFixtureIsMadeByteForByteAclRestoredIncluded) {
  mm::ConfigSnapshot s;
  s.clear();
  ASSERT_TRUE(s.put("name", "Grenaa Chimney #1"));
  ASSERT_TRUE(s.put("flood.advert.interval", "3"));
  ASSERT_TRUE(s.addRaw("> unknown command"));
  std::snprintf(s.firmware, sizeof s.firmware, "v1.17.1");
  std::snprintf(s.board, sizeof s.board, "heltec_v3");
  s.hasAclRestored = true;
  s.aclRestored = true;

  char out[1024];
  const size_t len = mm::writeConfigReadResult(s, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), fixture("ConfigReadResult"));
}

TEST(ConfigReadResult, ANullAclRestoredIsLeftOutAndAFalseOneIsWritten) {
  mm::ConfigSnapshot s;
  s.clear();
  s.put("name", "x");
  char out[512];
  size_t len = mm::writeConfigReadResult(s, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), "{\"values\":{\"name\":\"x\"},\"raw\":[]}");

  s.hasAclRestored = true;
  s.aclRestored = false;
  len = mm::writeConfigReadResult(s, out, sizeof out);
  EXPECT_EQ(std::string(out, len), "{\"values\":{\"name\":\"x\"},\"raw\":[],\"aclRestored\":false}");
}

// What the .NET handler's snapshot looks like after a default read against the fake: `regions`
// first, the gets in order, the ACL last, and the raw line of a full list.
TEST(ConfigReadResult, TheAclIsTheLastValueAndTheRawLinesFollowInOrder) {
  mm::ConfigSnapshot s;
  s.clear();
  s.put(mm::kRegionsKey, "*\n  dk\ndefault dk");
  s.put("name", "Risskov");
  mm::AccessListEntry e;
  std::snprintf(e.prefix, sizeof e.prefix, "c0ffee001122");
  e.permissions = 3;
  mm::accessListTokens(&e, 1, s.acl, sizeof s.acl);
  s.hasAcl = true;
  s.addRaw(mm::kAclFullLine);
  s.hasAclRestored = true;
  s.aclRestored = true;

  char out[1024];
  const size_t len = mm::writeConfigReadResult(s, out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len),
            "{\"values\":{\"regions\":\"*\\n  dk\\ndefault dk\",\"name\":\"Risskov\",\"acl\":\"c0ffee001122:3\"},"
            "\"raw\":[\"acl: 23 entries, the firmware\\u0027s reply is full and may be truncated\"],\"aclRestored\":true}");
}

TEST(ConfigReadResult, TheSnapshotIsBoundedAndSaysSo) {
  ASSERT_EQ(mm::kMaxConfigKeys, 32) << "the cap a claim is refused above (task p1.3-12)";
  ASSERT_EQ(mm::kMaxConfigRawLines, 12);
  mm::ConfigSnapshot s;
  s.clear();
  char key[8];
  for (int i = 0; i < mm::kMaxConfigKeys; i++) {
    std::snprintf(key, sizeof key, "k%d", i);
    EXPECT_TRUE(s.put(key, "v"));
  }
  EXPECT_FALSE(s.put("one-more", "v"));
  EXPECT_TRUE(s.put("k0", "changed")) << "a key already there is replaced, not appended";
  EXPECT_STREQ(s.values[0].value, "changed");
  for (int i = 0; i < mm::kMaxConfigRawLines; i++) EXPECT_TRUE(s.addRaw("r"));
  EXPECT_FALSE(s.addRaw("r"));
  EXPECT_EQ(s.valuesRead(), mm::kMaxConfigKeys);
}

// The thirteenth raw line: counted, not lost. A V3 asked the Configuration tab's whole list at
// v1.17 answers `??` to every key its firmware does not know, and six slots used to swallow
// them in silence (task p1.3-12, finding 5).
TEST(ConfigReadResult, AThirteenthRawLineIsCountedNotLost) {
  mm::ConfigSnapshot s;
  s.clear();
  char line[32];
  for (int i = 0; i < mm::kMaxConfigRawLines; i++) {
    std::snprintf(line, sizeof line, "get k%d: ??: k%d", i, i);
    ASSERT_TRUE(s.addRaw(line));
  }
  EXPECT_EQ(s.rawCount, mm::kMaxConfigRawLines);
  EXPECT_EQ(s.rawDropped, 0);

  EXPECT_FALSE(s.addRaw("get radio.rxgain: ??: radio.rxgain"));
  EXPECT_EQ(s.rawDropped, 1);
  EXPECT_EQ(s.rawCount, mm::kConfigRawSlots) << "the slot after the twelve is the count";
  EXPECT_STREQ(s.raw[mm::kMaxConfigRawLines], "raw: 1 line(s) the repeater said did not fit this snapshot");
  EXPECT_STREQ(s.raw[0], "get k0: ??: k0") << "nothing already kept is overwritten";
  EXPECT_STREQ(s.raw[11], "get k11: ??: k11");

  EXPECT_FALSE(s.addRaw("get fem.gain: ??: fem.gain"));
  EXPECT_EQ(s.rawDropped, 2);
  EXPECT_STREQ(s.raw[mm::kMaxConfigRawLines], "raw: 2 line(s) the repeater said did not fit this snapshot");

  // And the count reaches the result, as the last of its raw lines.
  char out[4096];
  const size_t len = mm::writeConfigReadResult(s, out, sizeof out);
  ASSERT_GT(len, 0u);
  const std::string json(out, len);
  EXPECT_NE(json.find("\"raw: 2 line(s) the repeater said did not fit this snapshot\"]"), std::string::npos) << json;

  s.clear();
  EXPECT_EQ(s.rawCount, 0);
  EXPECT_EQ(s.rawDropped, 0);
}

// The silent-ACL line: the .NET agent's sentence, em dash and section sign included, which the
// writer escapes as System.Text.Json does.
TEST(ConfigReadResult, TheSilentAclLineIsTheDotNetSentence) {
  const std::string line = mm::kAclSilentLine;
  EXPECT_EQ(line.find("acl: no reply \xE2\x80\x94 an ACL of 24 entries or more"), 0u);
  EXPECT_NE(line.find("(spec/EXIT-NODES.md \xC2\xA7" "4.3)"), std::string::npos);
  EXPECT_EQ(json(mm::kAclSilentLine),
            "\"acl: no reply \\u2014 an ACL of 24 entries or more, or a firmware without the request, is not answered "
            "(spec/EXIT-NODES.md \\u00A74.3)\"");
}

// ------------------------------------------------------------------ the full list (task p1.3-12)

// The read meshmaster.dk's task p4.9-102 makes: every key the Configuration tab shows, which is
// about twenty-six on a V3 at v1.17 and more than the twenty this firmware used to accept. The
// cap is 32 now, and this is a read at it - 29 keys the repeater answered, three it did not
// know, an `owner.info` holding a quote and a backslash and a full ACL - written into the
// runner's own buffer and parsed back.
//
// Task p1.1-08's host harness and its fake repeater would have run this over the air; that
// branch is not merged (tasks/phase-1.1/logs/STOPPED-08.md), so this is the snapshot and the
// writer, which is where every byte of the result is decided.
namespace {

constexpr size_t kRunnerDataCap = 8192;  // JobRunner.cpp's kDataCap

// What a V3 at v1.17 answers, in the order the read asks: the server's sixteen with
// `agc.reset.interval` among them, then the advanced keys and the read-only facts the
// Configuration tab shows. Three of them are keys that firmware does not know.
struct Answer {
  const char* key;
  const char* value;  // nullptr: the firmware answered `??: <key>`
};

const char kOwnerInfo[] =
    "Risskov Water Tower, \"the tall one\" - c:\\site\\notes.txt - ring <Thomas> first & bring the 13mm key, "
    "the hatch is stiff";

const Answer kFullList[] = {
    {"name", "Risskov Water Tower"},
    {"lat", "56.1899"},
    {"lon", "10.2255"},
    {"advert.interval", "240"},
    {"flood.advert.interval", "12"},
    {"repeat", "on"},
    {"dutycycle", "25.0%"},
    {"flood.max", "3"},
    {"flood.max.unscoped", "2"},
    {"tx", "22 dBm"},
    {"path.hash.mode", "1"},
    {"multi.acks", "0"},
    {"allow.read.only", "off"},
    {"agc.reset.interval", "4"},
    {"regions", nullptr},  // read before the gets and put below, not a `get`
    {"acl", nullptr},      // the binary request, below
    {"owner.info", kOwnerInfo},
    {"flood.max.advert", "3"},
    {"loop.detect", "moderate"},
    {"txdelay", "0"},
    {"direct.txdelay", "0.5"},
    {"rxdelay", "10"},
    {"int.thresh", "3"},
    {"cad", "on"},
    {"powersaving", "off"},
    {"public.key", "46f6a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e"},
    {"role", "2"},
    {"radio.rxgain", nullptr},                 // v1.17 on a V3 does not know it
    {"fem.rx.gain", nullptr},                   // nor this
    {"twinoak.pwr.kill.enabled", nullptr},      // nor a pack's key
    {"freq", "869.618"},
    {"bw", "62.500"},
};

}  // namespace

TEST(ConfigReadFullList, AReadAtTheCapOfThirtyTwoKeysIsWrittenWhole) {
  ASSERT_EQ(static_cast<int>(sizeof kFullList / sizeof kFullList[0]), mm::kMaxConfigKeys);

  mm::ConfigSnapshot s;
  s.clear();

  // `regions` first, as the .NET handler and JobRunner::runConfigRead both read it.
  const char kRegions[] = "*\n  dk\n    dk-east\n    dk-west\ndefault dk";
  ASSERT_TRUE(s.put(mm::kRegionsKey, kRegions));

  int unknown = 0;
  char line[mm::kMaxConfigRawLen + 1];
  for (const Answer& a : kFullList) {
    if (std::strcmp(a.key, mm::kRegionsKey) == 0 || std::strcmp(a.key, mm::kAclKey) == 0) continue;
    if (a.value != nullptr) {
      ASSERT_TRUE(s.put(a.key, a.value)) << a.key;
      continue;
    }
    // What the read loop does with a reply `cliReplyValue` refuses: kept verbatim, the read ok.
    std::snprintf(line, sizeof line, "get %.32s: ??: %.32s", a.key, a.key);
    ASSERT_TRUE(s.addRaw(line)) << a.key;
    unknown++;
  }
  EXPECT_EQ(unknown, 3);

  // A full ACL: the 23 entries the packet holds, which also gets the `acl` raw line.
  mm::AccessListEntry entries[mm::kAccessListMaxEntries];
  for (int i = 0; i < mm::kAccessListMaxEntries; i++) {
    std::snprintf(entries[i].prefix, sizeof entries[i].prefix, "%02xffee0011%02x", i + 1, i + 1);
    entries[i].permissions = static_cast<uint8_t>(1 + (i % 3));
  }
  const size_t aclLen = mm::accessListTokens(entries, mm::kAccessListMaxEntries, s.acl, sizeof s.acl);
  ASSERT_EQ(aclLen, std::strlen(s.acl));
  EXPECT_EQ(aclLen, 344u) << "23 tokens of twelve hex, a colon and a level, one space between";
  s.hasAcl = true;
  ASSERT_TRUE(s.addRaw(mm::kAclFullLine));

  std::snprintf(s.firmware, sizeof s.firmware, "v1.17.1 (Build: 27 Sep 2025)");
  std::snprintf(s.board, sizeof s.board, "Heltec v3");
  s.hasAclRestored = true;
  s.aclRestored = true;

  EXPECT_EQ(s.count, 28) << "the 27 the gets answered, and regions";
  EXPECT_EQ(s.rawCount, 4);
  EXPECT_EQ(s.rawDropped, 0);

  // The runner's own buffer, and the whole snapshot in it.
  static char out[kRunnerDataCap];
  const size_t len = mm::writeConfigReadResult(s, out, sizeof out);
  ASSERT_GT(len, 0u) << "a read that reached the repeater is never failed for want of room";
  EXPECT_LT(len, kRunnerDataCap);

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, out, len)) << std::string(out, len);
  JsonObjectConst values = doc["values"].as<JsonObjectConst>();
  ASSERT_FALSE(values.isNull());
  EXPECT_EQ(values.size(), 29u) << "the 28 values and the ACL";
  EXPECT_STREQ(values["name"].as<const char*>(), "Risskov Water Tower");
  EXPECT_STREQ(values["agc.reset.interval"].as<const char*>(), "4");
  EXPECT_STREQ(values[mm::kRegionsKey].as<const char*>(), kRegions) << "newlines and the default line";
  EXPECT_STREQ(values["owner.info"].as<const char*>(), kOwnerInfo) << "a quote, a backslash and an angle bracket";
  EXPECT_STREQ(values[mm::kAclKey].as<const char*>(), s.acl);
  for (const Answer& a : kFullList) {
    if (a.value == nullptr) {
      if (std::strcmp(a.key, mm::kRegionsKey) != 0 && std::strcmp(a.key, mm::kAclKey) != 0) {
        EXPECT_TRUE(values[a.key].isNull()) << a.key << " was never answered";
      }
      continue;
    }
    EXPECT_STREQ(values[a.key].as<const char*>(), a.value) << a.key;
  }

  JsonArrayConst raw = doc["raw"].as<JsonArrayConst>();
  ASSERT_FALSE(raw.isNull());
  ASSERT_EQ(raw.size(), 4u) << "three unknown keys and the full ACL; nothing cut";
  EXPECT_STREQ(raw[0].as<const char*>(), "get radio.rxgain: ??: radio.rxgain");
  EXPECT_STREQ(raw[1].as<const char*>(), "get fem.rx.gain: ??: fem.rx.gain");
  EXPECT_STREQ(raw[2].as<const char*>(), "get twinoak.pwr.kill.enabled: ??: twinoak.pwr.kill.enabled");
  EXPECT_STREQ(raw[3].as<const char*>(), mm::kAclFullLine);
  EXPECT_STREQ(doc["firmware"].as<const char*>(), "v1.17.1 (Build: 27 Sep 2025)");
  EXPECT_STREQ(doc["board"].as<const char*>(), "Heltec v3");
  EXPECT_TRUE(doc["aclRestored"].as<bool>());

  // The escaping is still System.Text.Json's, which is what the fixture holds both ends to.
  const std::string text(out, len);
  EXPECT_NE(text.find("\\\"the tall one\\\""), std::string::npos) << "a quote is a short escape";
  EXPECT_NE(text.find("c:\\\\site\\\\notes.txt"), std::string::npos);
  EXPECT_NE(text.find("\\u003CThomas\\u003E"), std::string::npos);
  EXPECT_NE(text.find("\\u0026"), std::string::npos);
}

// The same read into a buffer that cannot hold it: the raw lines go first and then the trailing
// values, and one more raw line says what went. Never 0, which would be a `failed` job for a
// repeater that answered everything it was asked (task p1.3-12).
TEST(ConfigReadFullList, ABufferTooSmallCostsRawLinesAndThenValuesNeverTheRead) {
  mm::ConfigSnapshot s;
  s.clear();
  char key[mm::kMaxConfigKeyLen + 1];
  char value[mm::kMaxConfigValueLen + 1];
  std::memset(value, 'v', sizeof value - 1);
  value[sizeof value - 1] = '\0';
  for (int i = 0; i < mm::kMaxConfigKeys; i++) {
    std::snprintf(key, sizeof key, "key.number.%02d.padded.out.to.32", i);
    ASSERT_TRUE(s.put(key, value));
  }
  char line[mm::kMaxConfigRawLen + 1];
  std::memset(line, 'r', sizeof line - 1);
  line[sizeof line - 1] = '\0';
  for (int i = 0; i < mm::kMaxConfigRawLines; i++) ASSERT_TRUE(s.addRaw(line));

  // 32 values of 240 characters and twelve raw lines of 200 is some 10.9 KB: more than the
  // runner's 8 192, which is why the writer degrades rather than answering 0.
  static char out[kRunnerDataCap];
  const size_t len = mm::writeConfigReadResult(s, out, sizeof out);
  ASSERT_GT(len, 0u);
  ASSERT_LE(len, kRunnerDataCap);

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, out, len)) << std::string(out, len);
  JsonArrayConst raw = doc["raw"].as<JsonArrayConst>();
  ASSERT_FALSE(raw.isNull());
  ASSERT_GT(raw.size(), 0u);
  const std::string cut = raw[raw.size() - 1].as<const char*>();
  EXPECT_EQ(cut.rfind("result: ", 0), 0u) << cut;
  EXPECT_NE(cut.find("did not fit 8192 bytes"), std::string::npos) << cut;
  // The values are what a read is for, so they are the last thing dropped: all 32 are here.
  EXPECT_EQ(doc["values"].as<JsonObjectConst>().size(), static_cast<size_t>(mm::kMaxConfigKeys));
  EXPECT_NE(cut.find("0 value(s)"), std::string::npos) << cut;

  // And at the floor the writer promises, something is still written.
  static char small[mm::kMinConfigReadResultCap];
  const size_t tiny = mm::writeConfigReadResult(s, small, sizeof small);
  ASSERT_GT(tiny, 0u) << "kMinConfigReadResultCap is the promise";
  JsonDocument doc2;
  ASSERT_FALSE(deserializeJson(doc2, small, tiny)) << std::string(small, tiny);
  EXPECT_EQ(doc2["raw"].as<JsonArrayConst>().size(), 1u) << "the cut note, and nothing else";
}

// ------------------------------------------------------------------ the writer's escaping

// JavaScriptEncoder.Default: the HTML-sensitive characters and every non-ASCII code point as
// \uXXXX with uppercase hex, the five short controls in their short forms, the rest \u00XX.
TEST(JsonWriterEscaping, TheBytesAreSystemTextJsonsForTheSameValue) {
  EXPECT_EQ(json("> unknown command"), "\"\\u003E unknown command\"");
  EXPECT_EQ(json("<set>"), "\"\\u003Cset\\u003E\"");
  EXPECT_EQ(json("a & b + c ' d ` e"), "\"a \\u0026 b \\u002B c \\u0027 d \\u0060 e\"");
  EXPECT_EQ(json("line\nnext\ttab\r\b\f"), "\"line\\nnext\\ttab\\r\\b\\f\"");
  EXPECT_EQ(json("\x01\x1f\x7f"), "\"\\u0001\\u001F\\u007F\"");
  EXPECT_EQ(json("quote \" slash \\ solidus /"), "\"quote \\\" slash \\\\ solidus /\"");
  EXPECT_EQ(json("Risskov \xC3\x86"), "\"Risskov \\u00C6\"");             // U+00C6
  EXPECT_EQ(json("\xE2\x80\x94"), "\"\\u2014\"");                         // U+2014
  EXPECT_EQ(json("\xF0\x9F\x93\xA1"), "\"\\uD83D\\uDCE1\"");             // U+1F4E1, a surrogate pair
  EXPECT_EQ(json("cut \xC3"), "\"cut \\uFFFD\"");                          // a lead byte with nothing after it
  EXPECT_EQ(json("Grenaa Chimney #1"), "\"Grenaa Chimney #1\"");
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
