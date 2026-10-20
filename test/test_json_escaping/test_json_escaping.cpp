// How a string crosses the wire, clause by clause (task p1.6-24).
//
// meshmaster.dk writes its contracts with .NET's **relaxed** JSON encoder since its task
// p4.15-129 (`JavaScriptEncoder.UnsafeRelaxedJsonEscaping`), and `agent/JsonWriter.h` mirrors
// that encoder byte for byte - which is what lets every other suite here compare a body with a
// contract fixture. The rule is written down in eight clauses in
// tests/fixtures/contracts/README.md and in meshmaster.dk's docs/dev/unit-protocol.md §8, and
// `JsonEscaping.json` - a test-only record nothing sends, in the fixtures for this purpose -
// carries one character per clause.
//
// So this file is two things: that fixture, written by this writer and compared byte for byte,
// and a test per clause, so that a failure names the clause that moved instead of printing two
// long strings. The one clause this writer does not carry whole is 7's unassigned code points,
// which need Unicode's own table; the last test holds what the header promises instead - that a
// reader makes the same string of our bytes and of .NET's.

#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include <fstream>
#include <sstream>
#include <string>

#include <agent/JsonWriter.h>

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

std::string readFixture(const char* name) {
  std::ifstream in(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
  std::ostringstream all;
  all << in.rdbuf();
  std::string text = all.str();
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  return text;
}

// One string, written as the writer writes it - the quotes included, because the escaping is
// what is under test and a clause is one line of this file.
std::string json(const char* text) {
  char out[512];
  mm::JsonWriter w(out, sizeof out);
  w.string(text);
  EXPECT_FALSE(w.overflowed());
  return std::string(out, w.length());
}

// The decoded value of JsonEscaping.json's one member, byte for byte: every clause's character,
// as the input that produced the fixture. Written with hex escapes so that this file is ASCII
// and no compiler's idea of a source encoding can move a byte of it.
const char* const kEveryClause =
    "plus + html <>&' tick ` quote \" backslash \\ aring \xC3\x85 ellipsis \xE2\x80\xA6"
    " tab \t esc \x1B del \x7F ls \xE2\x80\xA8 ps \xE2\x80\xA9 emoji \xF0\x9F\x98\x80";

}  // namespace

// ---- the fixture, byte for byte ---------------------------------------------------------------

// The whole record, written as any envelope is: the key through the same `string()` and the value
// after it. If this fails and every clause below passes, the record moved rather than the rule.
TEST(JsonEscaping, TheFixtureIsWhatThisWriterMakesOfTheSameString) {
  const std::string fixture = readFixture("JsonEscaping.json");
  ASSERT_FALSE(fixture.empty())
      << "tests/fixtures/contracts/JsonEscaping.json is missing - run tools/sync-fixtures.ps1";

  char out[512];
  mm::JsonWriter w(out, sizeof out);
  w.beginObject();
  w.key("text");
  w.string(kEveryClause);
  w.endObject();
  ASSERT_FALSE(w.overflowed());
  EXPECT_EQ(fixture, std::string(out, w.length()));
}

// And the other direction: the fixture's bytes read back as the string they were made from, so
// that `kEveryClause` is held to the file and not to a reading of it.
TEST(JsonEscaping, TheFixtureReadsBackAsTheStringEveryClauseIsIn) {
  const std::string fixture = readFixture("JsonEscaping.json");
  ASSERT_FALSE(fixture.empty());
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, fixture.data(), fixture.size())) << fixture;
  EXPECT_EQ(std::string(kEveryClause), std::string(doc["text"].as<const char*>()));
}

// ---- one test per clause ----------------------------------------------------------------------

TEST(JsonEscaping, Clause1AQuoteAndABackslashTakeABackslash) {
  EXPECT_EQ("\"say \\\"hi\\\" c:\\\\site\\\\notes.txt\"", json("say \"hi\" c:\\site\\notes.txt"));
}

TEST(JsonEscaping, Clause2FiveControlsHaveShortForms) {
  EXPECT_EQ("\"a\\bb\\tc\\nd\\fe\\rf\"", json("a\bb\tc\nd\fe\rf"));
}

TEST(JsonEscaping, Clause3EveryOtherControlIsUppercaseHex) {
  // The C0 controls with no short form, DEL, and - above ASCII - the C1 controls and the no-break
  // space, which is the one character of this clause that looks printable. U+0000 is not here:
  // the writer takes NUL-terminated text, so a NUL ends the string rather than being escaped in
  // it, and nothing this firmware writes can carry one.
  EXPECT_EQ("\"\\u0001\\u001F\\u007F\"", json("\x01\x1F\x7F"));
  EXPECT_EQ("\"\\u0085\"", json("\xC2\x85"));  // U+0085, NEL, a C1 control
  EXPECT_EQ("\"\\u009F\"", json("\xC2\x9F"));  // U+009F, the last of them
  EXPECT_EQ("\"\\u00A0\"", json("\xC2\xA0"));  // U+00A0, the no-break space
}

TEST(JsonEscaping, Clause4TheLineAndParagraphSeparators) {
  EXPECT_EQ("\"\\u2028\\u2029\"", json("\xE2\x80\xA8\xE2\x80\xA9"));
}

// The clause this task is about: the whole of what the default encoder escaped for HTML's sake,
// and every assigned character of the BMP with it, is now itself.
TEST(JsonEscaping, Clause5AnAssignedBmpCharacterIsItself) {
  EXPECT_EQ("\"0.3.0+abc1234\"", json("0.3.0+abc1234"));
  EXPECT_EQ("\"> unknown command\"", json("> unknown command"));
  EXPECT_EQ("\"<set>\"", json("<set>"));
  EXPECT_EQ("\"a & b + c ' d ` e\"", json("a & b + c ' d ` e"));
  EXPECT_EQ("\"solidus / tilde ~\"", json("solidus / tilde ~"));
  EXPECT_EQ("\"Gren\xC3\xA5 Chimney #1\"", json("Gren\xC3\xA5 Chimney #1"));  // U+00E5
  EXPECT_EQ("\"Risskov \xC3\x86\"", json("Risskov \xC3\x86"));                // U+00C6
  EXPECT_EQ("\"\xE2\x80\x94\"", json("\xE2\x80\x94"));                        // U+2014, an em dash
  EXPECT_EQ("\"\xC2\xA7" "4.3\"", json("\xC2\xA7" "4.3"));                    // U+00A7, a section sign
  EXPECT_EQ("\"\xC2\xAD\"", json("\xC2\xAD"));                                // U+00AD, the soft hyphen
  EXPECT_EQ("\"\xE2\x80\x8D\"", json("\xE2\x80\x8D"));                        // U+200D, the ZWJ
  EXPECT_EQ("\"\xED\x95\x9C\"", json("\xED\x95\x9C"));                        // U+D55C, a Hangul syllable
}

TEST(JsonEscaping, Clause6ACodePointAboveTheBmpIsASurrogatePair) {
  EXPECT_EQ("\"\\uD83D\\uDE00\"", json("\xF0\x9F\x98\x80"));  // U+1F600
  EXPECT_EQ("\"\\uD83D\\uDCE1\"", json("\xF0\x9F\x93\xA1"));  // U+1F4E1
  EXPECT_EQ("\"\\uDBFF\\uDFFF\"", json("\xF4\x8F\xBF\xBF"));  // U+10FFFF, the last one there is
}

// What the encoder still escapes and a writer never has to emit. Every range of this clause is
// here; what is not a range is the next test.
TEST(JsonEscaping, Clause7WhatIsStillEscapedAboveAscii) {
  EXPECT_EQ("\"\\u1680\"", json("\xE1\x9A\x80"));  // U+1680, the Ogham space mark
  EXPECT_EQ("\"\\u2000\"", json("\xE2\x80\x80"));  // U+2000, the first of the Zs run
  EXPECT_EQ("\"\\u200A\"", json("\xE2\x80\x8A"));  // U+200A, the last of it
  EXPECT_EQ("\"\\u202F\"", json("\xE2\x80\xAF"));  // U+202F, the narrow no-break space
  EXPECT_EQ("\"\\u205F\"", json("\xE2\x81\x9F"));  // U+205F, the medium mathematical space
  EXPECT_EQ("\"\\u3000\"", json("\xE3\x80\x80"));  // U+3000, the ideographic space
  EXPECT_EQ("\"\\uE000\"", json("\xEE\x80\x80"));  // U+E000, the private-use area
  EXPECT_EQ("\"\\uF8FF\"", json("\xEF\xA3\xBF"));  // U+F8FF, the end of it
  EXPECT_EQ("\"\\uFDD0\"", json("\xEF\xB7\x90"));  // U+FDD0, a non-character
  EXPECT_EQ("\"\\uFEFF\"", json("\xEF\xBB\xBF"));  // U+FEFF, the byte-order mark
  EXPECT_EQ("\"\\uFFFE\"", json("\xEF\xBF\xBE"));  // U+FFFE, a non-character
  EXPECT_EQ("\"\\uFFFF\"", json("\xEF\xBF\xBF"));  // U+FFFF, the other one
  // The replacement character is itself a character, so it is clause 5's and not this one's.
  EXPECT_EQ("\"\xEF\xBF\xBD\"", json("\xEF\xBF\xBD"));  // U+FFFD
}

TEST(JsonEscaping, Clause8ASurrogateOrAByteThatIsNotUtf8IsTheReplacementCharacter) {
  EXPECT_EQ("\"cut \\uFFFD\"", json("cut \xC3"));         // a lead byte with nothing after it
  EXPECT_EQ("\"\\uFFFD(zz\"", json("\xC3\x28zz"));        // a continuation byte that is not one
  EXPECT_EQ("\"\\uFFFDzz\"", json("\x80zz"));             // a lone continuation byte
  EXPECT_EQ("\"\\uFFFD\"", json("\xED\xA0\x80"));         // U+D800, encoded as if it were a character
  EXPECT_EQ("\"\\uFFFD\"", json("\xED\xBF\xBF"));         // U+DFFF, the same
}

// ---- the one clause this writer does not carry whole ------------------------------------------

// Clause 7 covers every **unassigned** code point too, which .NET escapes because it carries
// Unicode's table of what is assigned and this writer does not (`JsonWriter.h`'s header says
// which way it differs, and why it will not grow one). What matters is that the difference is
// bytes and not meaning: a reader unescapes every JSON escape, so the server makes the same
// string of either form. This is the parse that holds that - a byte comparison could not.
TEST(JsonEscaping, AnUnassignedCodePointIsTheSameStringToAReaderEitherWay) {
  // What this writer makes of U+0378, by clause 5: the character itself.
  const std::string ours = "{\"text\":" + json("a \xCD\xB8 b") + "}";
  EXPECT_EQ("{\"text\":\"a \xCD\xB8 b\"}", ours);

  // What meshmaster.dk makes of it, by clause 7.
  const std::string theirs = "{\"text\":\"a \\u0378 b\"}";
  EXPECT_NE(theirs, ours) << "this is the one place the two writers' bytes differ";

  JsonDocument mine;
  ASSERT_FALSE(deserializeJson(mine, ours.data(), ours.size())) << ours;
  JsonDocument dotnet;
  ASSERT_FALSE(deserializeJson(dotnet, theirs.data(), theirs.size())) << theirs;
  EXPECT_EQ(std::string("a \xCD\xB8 b"), std::string(mine["text"].as<const char*>()));
  EXPECT_EQ(std::string(dotnet["text"].as<const char*>()), std::string(mine["text"].as<const char*>()))
      << "the two forms are one string, which is why the bytes may differ here and nowhere else";
}

// ---- and what the new rule costs a wake -------------------------------------------------------

// The point of the dialect from this end: the bytes a result no longer spends on escapes. A CLI
// reply that carries the firmware's prompt, a Danish repeater name and a version with build
// metadata are the three the board sends most often, and each was six bytes a character before.
TEST(JsonEscaping, TheBytesAWakeNoLongerSpendsOnEscapes) {
  EXPECT_EQ(19u, json("> unknown command").size());  // 24 under the default encoder
  EXPECT_EQ(8u, json("Gren\xC3\xA5").size());        // 12
  EXPECT_EQ(15u, json("0.3.0+abc1234").size());      // 20
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
