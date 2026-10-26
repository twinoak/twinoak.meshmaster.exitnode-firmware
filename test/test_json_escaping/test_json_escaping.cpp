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
// So this file is three things: that fixture, written by this writer and compared byte for byte;
// a test per clause, so that a failure names the clause that moved instead of printing two long
// strings; and - since task p1.7-25 - two seeded fuzz tests over clause 8, which is the clause
// that is not only a dialect but a rail. .NET's input is a UTF-16 string and the only thing its
// encoder has to replace is a lone surrogate; this writer's input is **bytes** from a stranger's
// advert or a repeater's console, and a document that is not UTF-8 is one System.Text.Json
// refuses - which on this end means a result retried at the head of the ordered queue for ever.
// The two fuzz tests say that nothing it writes is ill-formed UTF-8 and that nothing well-formed
// is changed, over input nobody typed.
//
// The one clause this writer does not carry whole is 7's unassigned code points, which need
// Unicode's own table; the test after the fuzz tests holds what the header promises instead -
// that a reader makes the same string of our bytes and of .NET's.

#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include <cstdint>
#include <fstream>
#include <random>
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

// The same through the explicit-length overload, which is the one a cut name and a cut reply go
// through (`string(text, len)`): the bytes after `len` are not read at all, so a sequence the
// buffer cuts is cut here and not by a NUL.
std::string jsonOf(const char* text, size_t len) {
  char out[1024];
  mm::JsonWriter w(out, sizeof out);
  w.string(text, len);
  EXPECT_FALSE(w.overflowed());
  return std::string(out, w.length());
}

// Is this well-formed UTF-8? **Table 3-7 of the standard**, written out here rather than shared
// with `JsonWriter.h`: the point of the fuzz tests at the end of this file is that two
// implementations of the same table agree, and a checker that called the writer's decoder would
// agree with it by construction.
bool wellFormedUtf8(const std::string& bytes) {
  size_t i = 0;
  while (i < bytes.size()) {
    const unsigned char c = static_cast<unsigned char>(bytes[i]);
    size_t need = 0;
    unsigned char lo = 0x80;
    unsigned char hi = 0xBF;
    if (c < 0x80) {
      i++;
      continue;
    }
    if (c >= 0xC2 && c <= 0xDF) {
      need = 2;
    } else if (c == 0xE0) {
      need = 3;
      lo = 0xA0;
    } else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) {
      need = 3;
    } else if (c == 0xED) {
      need = 3;
      hi = 0x9F;
    } else if (c == 0xF0) {
      need = 4;
      lo = 0x90;
    } else if (c >= 0xF1 && c <= 0xF3) {
      need = 4;
    } else if (c == 0xF4) {
      need = 4;
      hi = 0x8F;
    } else {
      return false;  // 0x80-0xC1 and 0xF5-0xFF begin nothing
    }
    if (i + need > bytes.size()) return false;
    for (size_t k = 1; k < need; k++) {
      const unsigned char cc = static_cast<unsigned char>(bytes[i + k]);
      if (cc < (k == 1 ? lo : 0x80) || cc > (k == 1 ? hi : 0xBF)) return false;
    }
    i += need;
  }
  return true;
}

// One code point as UTF-8, for the generator of well-formed input. Not the writer's code either.
void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// What a failing fuzz case prints, so that it can be typed straight into a test above.
std::string hex(const std::string& bytes) {
  static const char* const kDigits = "0123456789ABCDEF";
  std::string out;
  for (const char c : bytes) {
    const unsigned char b = static_cast<unsigned char>(c);
    out.push_back(kDigits[b >> 4]);
    out.push_back(kDigits[b & 0x0F]);
    out.push_back(' ');
  }
  return out;
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

// The clause task p1.7-25 is about. .NET's own input is a UTF-16 string, so the only thing the
// encoder ever has to replace is a lone surrogate; this writer's input is **bytes**, which is a
// wider door, and until this task an overlong form, a code point above U+10FFFF and the lead
// bytes F5-FF went through it as themselves - so a repeater whose name carried them made a
// result System.Text.Json refuses, and a refused result is retried at the head of the ordered
// queue. One U+FFFD per **maximal subpart** (Unicode 3.9, and what .NET makes of the same
// bytes): the longest prefix that could still have become a character, and then the byte that
// broke it is read as a byte of its own.
TEST(JsonEscaping, Clause8ASurrogateOrAByteThatIsNotUtf8IsTheReplacementCharacter) {
  EXPECT_EQ("\"cut \\uFFFD\"", json("cut \xC3"));         // a lead byte with nothing after it
  EXPECT_EQ("\"\\uFFFD(zz\"", json("\xC3\x28zz"));        // a continuation byte that is not one
  EXPECT_EQ("\"\\uFFFDzz\"", json("\x80zz"));             // a lone continuation byte

  // An overlong form: no sequence that begins C0 or C1 can be well-formed, so the lead byte is
  // its own maximal subpart and the continuation byte after it is another.
  EXPECT_EQ("\"\\uFFFD\\uFFFD\"", json("\xC0\x80"));              // U+0000, overlong
  EXPECT_EQ("\"\\uFFFD\\uFFFD\"", json("\xC0\xAF"));              // `/`, overlong
  EXPECT_EQ("\"\\uFFFD\\uFFFD\\uFFFD\"", json("\xE0\x83\x85"));   // U+00C5, overlong three bytes
  EXPECT_EQ("\"\\uFFFD\\uFFFD\\uFFFD\\uFFFD\"", json("\xF0\x80\x83\x85"));  // and overlong four

  // Above U+10FFFF: F4 may be followed by 80-8F and no more, and F5-FF begin nothing at all.
  EXPECT_EQ("\"\\uFFFD\\uFFFD\\uFFFD\\uFFFD\"", json("\xF4\x90\x80\x80"));  // U+110000
  EXPECT_EQ("\"\\uFFFD\\uFFFD\\uFFFD\\uFFFD\"", json("\xF5\x80\x80\x80"));  // a lead byte of nothing

  // A surrogate encoded as if it were a character: ED may be followed by 80-9F and no more.
  EXPECT_EQ("\"\\uFFFD\\uFFFD\\uFFFD\"", json("\xED\xA0\x80"));  // U+D800
  EXPECT_EQ("\"\\uFFFD\\uFFFD\\uFFFD\"", json("\xED\xBF\xBF"));  // U+DFFF, the same

  // The maximal subpart itself: `E2 82` could still have been the euro sign, so the two bytes
  // are **one** U+FFFD, and the `A` that broke it is an `A`.
  EXPECT_EQ("\"\\uFFFDA\"", json("\xE2\x82\x41"));
  // The same over three bytes of an emoji: `F0 9F 98` could still have become one, so it is one
  // U+FFFD however long the subpart was, and never one per byte.
  EXPECT_EQ("\"\\uFFFDA\"", json("\xF0\x9F\x98\x41"));

  // And a sequence the **buffer** cuts, which is what a name cut to kMaxObservationNameLen or a
  // reply cut to its cap looks like: the bytes that are there are one maximal subpart.
  EXPECT_EQ("\"a\\uFFFD\"", jsonOf("a\xE2\x82\xAC", 3));      // two bytes of a euro sign
  EXPECT_EQ("\"a\\uFFFD\"", jsonOf("a\xE2\x82\xAC", 2));      // one byte of it
  EXPECT_EQ("\"a\xE2\x82\xAC\"", jsonOf("a\xE2\x82\xAC", 4));  // and the whole of it
  EXPECT_EQ("\"\\uFFFD\"", jsonOf("\xF0\x9F\x98\x80", 3));    // three bytes of an emoji
}

// ---- and nothing the writer makes of anything is ill-formed UTF-8 -----------------------------

// The rail clause 8 exists for, held over input nobody typed: **no output of this writer is
// ill-formed UTF-8 and none of it is unparseable**, whatever bytes arrive. The checker is
// `wellFormedUtf8` above - written from Table 3-7 and not from the writer's decoder, so a
// mistake shared between the two cannot hide here - and the parse is ArduinoJson's, which is the
// reader on this end; what the server runs is System.Text.Json, which refuses a document that is
// not UTF-8 and is the reason this matters.
//
// Seeded, so a failure is reproducible and CI and this machine run the same four thousand cases.
TEST(JsonEscaping, NothingThisWriterMakesOfAnyBytesIsIllFormedUtf8) {
  std::mt19937 random(20251026u);
  std::uniform_int_distribution<int> anyByte(1, 255);  // a NUL ends a C string; the writer stops there
  std::uniform_int_distribution<int> continuation(0x80, 0xBF);
  std::uniform_int_distribution<int> pieces(0, 6);
  std::uniform_int_distribution<int> shape(0, 3);
  std::uniform_int_distribution<int> tailLength(1, 3);
  std::uniform_int_distribution<uint32_t> codePoint(1, 0x10FFFF);
  // Uniform noise is almost all lone bytes, and the bug this test exists for needed a **shape**:
  // a lead byte with a plausible tail after it, which an overlong form and a code point past
  // U+10FFFF both are. These are the lead bytes no well-formed sequence can begin with or whose
  // second byte is narrower than 80-BF, so a random tail reaches the overlong forms, the
  // surrogates and the range above U+10FFFF often rather than once in a hundred runs.
  static const int kLeads[] = {0x80, 0xC0, 0xC1, 0xE0, 0xED, 0xF0, 0xF4, 0xF5, 0xFE, 0xFF};
  std::uniform_int_distribution<size_t> lead(0, sizeof kLeads / sizeof kLeads[0] - 1);

  for (int n = 0; n < 4000; n++) {
    std::string input;
    const int count = pieces(random);
    for (int p = 0; p < count; p++) {
      switch (shape(random)) {
        case 0:
          input.push_back(static_cast<char>(anyByte(random)));
          break;
        case 1: {  // a lead byte that begins no well-formed sequence, and some of a tail
          input.push_back(static_cast<char>(kLeads[lead(random)]));
          const int tail = tailLength(random);
          for (int t = 0; t < tail; t++) input.push_back(static_cast<char>(continuation(random)));
          break;
        }
        case 2: {  // and a well-formed character, so that the two kinds are mixed
          uint32_t cp = codePoint(random);
          while (cp >= 0xD800 && cp <= 0xDFFF) cp = codePoint(random);
          appendUtf8(input, cp);
          break;
        }
        default:
          input.push_back(static_cast<char>(continuation(random)));
          break;
      }
    }
    const std::string out = jsonOf(input.data(), input.size());
    ASSERT_EQ(out.front(), '"') << hex(input);
    ASSERT_EQ(out.back(), '"') << hex(input);
    ASSERT_TRUE(wellFormedUtf8(out)) << "ill-formed UTF-8 out of " << hex(input) << ": " << hex(out);
    for (const char c : out) {
      ASSERT_GE(static_cast<unsigned char>(c), 0x20u) << "an unescaped control out of " << hex(input);
    }
    const std::string doc = "{\"text\":" + out + "}";
    JsonDocument parsed;
    ASSERT_FALSE(deserializeJson(parsed, doc.data(), doc.size())) << hex(input) << " -> " << doc;
  }
}

// The other half of the same rail: input that **is** well-formed UTF-8 comes back as itself. A
// writer could satisfy the test above by replacing everything; this one says it replaces nothing
// it does not have to. The code points are drawn from the whole of Unicode but the surrogates,
// so clauses 3 to 7 are all exercised and ArduinoJson unescapes whichever one applied.
TEST(JsonEscaping, EveryWellFormedStringComesBackAsItself) {
  std::mt19937 random(20251027u);
  std::uniform_int_distribution<uint32_t> codePoint(1, 0x10FFFF);
  std::uniform_int_distribution<int> length(1, 8);

  for (int n = 0; n < 1000; n++) {
    std::string input;
    const int len = length(random);
    for (int i = 0; i < len; i++) {
      uint32_t cp = codePoint(random);
      while (cp >= 0xD800 && cp <= 0xDFFF) cp = codePoint(random);
      appendUtf8(input, cp);
    }
    ASSERT_TRUE(wellFormedUtf8(input)) << "the generator made ill-formed input: " << hex(input);
    const std::string doc = "{\"text\":" + jsonOf(input.data(), input.size()) + "}";
    JsonDocument parsed;
    ASSERT_FALSE(deserializeJson(parsed, doc.data(), doc.size())) << hex(input) << " -> " << doc;
    EXPECT_EQ(input, std::string(parsed["text"].as<const char*>())) << hex(input) << " -> " << doc;
  }
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
