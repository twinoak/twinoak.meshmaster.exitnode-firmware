// The config read (task 04; src/agent/ConfigRead.h): the list and its price, the access-list
// reply read from the transcribed fixture bytes, the region helpers, and the ConfigReadResult
// held to the .NET fixture byte for byte - `aclRestored` included. Plus the JSON writer's
// escaping, which that fixture is the first to exercise (`\u003E`).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "agent/Airtime.h"
#include "agent/ConfigRead.h"
#include "agent/JsonWriter.h"
#include "mesh/ReplyCodec.h"

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

TEST(ConfigList, TheDefaultListIsTheServersFifteenKeysWithRegionsAndAclLast) {
  ASSERT_EQ(mm::kDefaultConfigKeyCount, 15);
  const char* expected[] = {"name",      "lat",  "lon",           "advert.interval", "flood.advert.interval",
                            "repeat",    "dutycycle", "flood.max", "flood.max.unscoped", "tx",
                            "path.hash.mode", "multi.acks", "allow.read.only", "regions", "acl"};
  for (int i = 0; i < 15; i++) EXPECT_STREQ(mm::kDefaultConfigKeys[i], expected[i]) << i;
  EXPECT_STREQ(mm::kConfigReadExtras[0], "ver");
  EXPECT_STREQ(mm::kConfigReadExtras[1], "board");
}

// ConfigReadPayload.Transactions: n + 2 + r. The default read is 18, plus the login.
TEST(ConfigList, ThePriceIsTheServersOwnArithmetic) {
  EXPECT_EQ(mm::configReadTransactions(nullptr, 0), 18);
  EXPECT_EQ(mm::configReadTransactions(mm::kDefaultConfigKeys, mm::kDefaultConfigKeyCount), 18);
  const char* three[] = {"name", "advert.interval", "flood.max"};  // the payload fixture's list
  EXPECT_EQ(mm::configReadTransactions(three, 3), 5);
  const char* withRegions[] = {"name", "regions"};
  EXPECT_EQ(mm::configReadTransactions(withRegions, 2), 5);
  const char* aclOnly[] = {"acl"};
  EXPECT_EQ(mm::configReadTransactions(aclOnly, 1), 3) << "the binary request costs the one transaction the get cost";
  EXPECT_EQ(mm::transactionsFor("config.read", mm::CredentialClass::Admin), 18);
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
