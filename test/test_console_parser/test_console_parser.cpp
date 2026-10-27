// The serial console's parser, one test per line shape and per refusal (tasks/phase-1/01):
// every key, both secret keys' redaction, a malformed line, an unknown key, a value out of range
// (radio.tx above 22) - and the README's provisioning session replayed line for line, so the
// documented session is the one the console accepts.

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "console/Parser.h"
#include "settings/Keys.h"

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

using mm::kKeyCount;
using mm::kKeys;
using mm::ParseResult;
using mm::parseLine;
using mm::Verb;

namespace {

std::string describe(const mm::KeyDef& key, const char* stored) {
  char out[256];
  mm::describeValue(key, stored, out, sizeof out);
  return out;
}

std::string setReply(const mm::KeyDef& key) {
  char out[64];
  mm::replyForSet(key, out, sizeof out);
  return out;
}

}  // namespace

// ---- the keys -------------------------------------------------------------------------------

TEST(Keys, TheTableHasTheTwentyEightKeysTheTasksAskFor) {
  // The twelve of phase 1, and the sixteen of the observer (task p1.7-26), in `get`'s order.
  const char* expected[] = {"wifi.ssid",      "wifi.pass",      "meshmaster.url", "meshmaster.token", "radio.freq",
                            "radio.bw",       "radio.sf",       "radio.cr",       "radio.tx",         "log.level",
                            "log.frames",     "display.timeout", "mqtt.origin",   "mqtt.iata",        "mqtt.owner",
                            "mqtt.email",     "mqtt1.preset",   "mqtt1.server",   "mqtt1.audience",   "mqtt1.topic",
                            "mqtt1.username", "mqtt1.password", "mqtt2.preset",   "mqtt2.server",     "mqtt2.audience",
                            "mqtt2.topic",    "mqtt2.username", "mqtt2.password"};
  ASSERT_EQ(kKeyCount, static_cast<int>(sizeof expected / sizeof expected[0]));
  for (int i = 0; i < kKeyCount; i++) {
    EXPECT_STREQ(expected[i], kKeys[i].name);
    EXPECT_EQ(&kKeys[i], mm::findKey(expected[i]));
  }
}

TEST(Keys, NvsNamesFitTheFifteenCharacterLimitAndAreUnique) {
  for (int i = 0; i < kKeyCount; i++) {
    EXPECT_LE(std::strlen(kKeys[i].nvs), 15u) << kKeys[i].name;
    for (int j = i + 1; j < kKeyCount; j++) EXPECT_STRNE(kKeys[i].nvs, kKeys[j].nvs);
  }
}

TEST(Keys, ExactlyTheFourSecretsAreSecret) {
  int secrets = 0;
  for (const mm::KeyDef& k : kKeys) {
    if (mm::isSecret(k)) secrets++;
  }
  EXPECT_EQ(4, secrets);
  EXPECT_TRUE(mm::isSecret(*mm::findKey("wifi.pass")));
  EXPECT_TRUE(mm::isSecret(*mm::findKey("meshmaster.token")));
  EXPECT_TRUE(mm::isSecret(*mm::findKey("mqtt1.password")));
  EXPECT_TRUE(mm::isSecret(*mm::findKey("mqtt2.password")));
}

TEST(Keys, WifiPwdIsAnotherNameForWifiPass) {
  // The observer firmware's name for the passphrase (task p1.7-26): the same key, so the same
  // redaction - and `get wifi.pwd` says what `get wifi.pass` says.
  EXPECT_EQ(mm::findKey("wifi.pass"), mm::findKey("wifi.pwd"));
  const ParseResult r = parseLine("set wifi.pwd correct horse battery");
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_STREQ("wifi.pass", r.command.key->name);
  EXPECT_STREQ("correct horse battery", r.command.value);
  EXPECT_EQ("ok wifi.pass", setReply(*r.command.key));
  EXPECT_EQ("wifi.pass set", describe(*parseLine("get wifi.pwd").command.key, "x"));
  EXPECT_EQ(nullptr, mm::findKey("wifi.password")) << "only the observer firmware's spelling";
}

TEST(Keys, TimezoneAndBridgeSourceAreAnsweredNotStored) {
  for (const char* line : {"set timezone CEST", "set bridge.source rx", "get timezone", "set timezone", "get bridge.source"}) {
    const ParseResult r = parseLine(line);
    EXPECT_TRUE(r.ok) << line << ": " << r.error;
    EXPECT_EQ(Verb::Note, r.command.verb) << line;
    ASSERT_NE(nullptr, r.command.note) << line;
    EXPECT_EQ(nullptr, r.command.key) << line;
  }
  EXPECT_STREQ("timezone: nothing to set; this firmware keeps UTC from the server or SNTP, and what it publishes is stamped in UTC",
               parseLine("set timezone CEST").command.note);
  EXPECT_STREQ("bridge.source: nothing to set; an exit node publishes what it receives (rx) and forwards nothing",
               parseLine("set bridge.source rx").command.note);
  EXPECT_EQ(Verb::Presets, parseLine("get mqtt.presets").command.verb);
  EXPECT_FALSE(parseLine("set mqtt.presets x").ok) << "the list is read, never set";
}

TEST(Keys, EveryKeyAcceptsAValidValueAndSetIsAcceptedForIt) {
  struct Sample {
    const char* key;
    const char* value;
  };
  const Sample samples[] = {
      {"wifi.ssid", "Twinoak"},
      {"wifi.pass", "correct horse battery"},
      {"meshmaster.url", "https://meshmaster.dk"},
      {"meshmaster.token", "mma_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"},
      {"radio.freq", "869.618"},
      {"radio.bw", "62.5"},
      {"radio.sf", "8"},
      {"radio.cr", "8"},
      {"radio.tx", "22"},
      {"log.level", "debug"},
      {"log.frames", "on"},
      {"display.timeout", "300"},
      {"mqtt.origin", "Window at Grenaa"},
      {"mqtt.iata", "AAR"},
      {"mqtt.owner", "0123456789ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef"},
      {"mqtt.email", "thomas@example.dk"},
      {"mqtt1.preset", "meshview"},
      {"mqtt1.server", "wss://mqtt.meshview.dk:443/mqtt"},
      {"mqtt1.audience", "mqtt.meshview.dk"},
      {"mqtt1.topic", "meshcore/{iata}/{device}/{type}"},
      {"mqtt1.username", "observer"},
      {"mqtt1.password", "correct horse battery"},
      {"mqtt2.preset", "meshmapper"},
      {"mqtt2.server", "mqtts://broker.example.org:8883"},
      {"mqtt2.audience", "broker.example.org"},
      {"mqtt2.topic", "mine/{device}/{type}"},
      {"mqtt2.username", "observer"},
      {"mqtt2.password", "correct horse battery"},
  };
  ASSERT_EQ(kKeyCount, static_cast<int>(sizeof samples / sizeof samples[0]));
  for (const Sample& s : samples) {
    const std::string line = std::string("set ") + s.key + " " + s.value;
    const ParseResult r = parseLine(line.c_str());
    EXPECT_TRUE(r.ok) << line << ": " << r.error;
    EXPECT_EQ(Verb::Set, r.command.verb) << line;
    ASSERT_NE(nullptr, r.command.key) << line;
    EXPECT_STREQ(s.key, r.command.key->name);
    EXPECT_STREQ(s.value, r.command.value);

    const std::string get = std::string("get ") + s.key;
    const ParseResult g = parseLine(get.c_str());
    EXPECT_TRUE(g.ok) << get << ": " << g.error;
    EXPECT_EQ(Verb::Get, g.command.verb);
    EXPECT_STREQ(s.key, g.command.key->name);
  }
}

// ---- redaction ------------------------------------------------------------------------------

TEST(Redaction, TheWifiPassphraseIsNeverInWhatGetPrints) {
  const mm::KeyDef& key = *mm::findKey("wifi.pass");
  EXPECT_EQ("wifi.pass set", describe(key, "correct horse battery"));
  EXPECT_EQ("wifi.pass unset", describe(key, nullptr));
  EXPECT_EQ("wifi.pass unset", describe(key, ""));
  EXPECT_EQ("ok wifi.pass", setReply(key));
}

TEST(Redaction, TheAgentTokenIsNeverInWhatGetPrints) {
  const mm::KeyDef& key = *mm::findKey("meshmaster.token");
  const char* token = "mma_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  const std::string shown = describe(key, token);
  EXPECT_EQ("meshmaster.token set", shown);
  EXPECT_EQ(std::string::npos, shown.find("mma_"));
  EXPECT_EQ("meshmaster.token unset", describe(key, nullptr));
  EXPECT_EQ("ok meshmaster.token", setReply(key));
}

TEST(Redaction, ANonSecretPrintsItsValueOrItsFallback) {
  EXPECT_EQ("wifi.ssid Twinoak", describe(*mm::findKey("wifi.ssid"), "Twinoak"));
  EXPECT_EQ("wifi.ssid unset", describe(*mm::findKey("wifi.ssid"), nullptr));
  EXPECT_EQ("radio.freq unset (869.618)", describe(*mm::findKey("radio.freq"), nullptr));
  EXPECT_EQ("radio.freq 868.0", describe(*mm::findKey("radio.freq"), "868.0"));
  EXPECT_EQ("log.frames unset (off)", describe(*mm::findKey("log.frames"), ""));
}

// ---- refusals -------------------------------------------------------------------------------

TEST(Refusals, AMalformedLineIsRefusedWithAReason) {
  ParseResult r = parseLine("set");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("set needs a key and a value: set <key> <value>", r.error);

  r = parseLine("set wifi.ssid");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("set wifi.ssid needs a value: set wifi.ssid <value>", r.error);

  r = parseLine("get wifi.ssid extra");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("get takes one key: get <key>", r.error);

  r = parseLine("status now");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("status takes no argument", r.error);

  r = parseLine("frobnicate");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("unknown command: frobnicate (try 'help')", r.error);

  r = parseLine("set wifi.ssid \x01name");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("control characters are not accepted", r.error);

  const std::string tooLong = "set wifi.ssid " + std::string(300, 'a');
  r = parseLine(tooLong.c_str());
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("line too long (200 characters at most)", r.error);
}

TEST(Refusals, AnUnknownKeyIsRefusedOnSetAndOnGet) {
  ParseResult r = parseLine("set wifi.password hunter22");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("unknown key: wifi.password (try 'help')", r.error);

  r = parseLine("get radio.power");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("unknown key: radio.power (try 'help')", r.error);
}

TEST(Refusals, RadioTxAboveTwentyTwoIsOutOfRange) {
  ParseResult r = parseLine("set radio.tx 23");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("radio.tx: out of range, -9..22", r.error);
  EXPECT_EQ(nullptr, r.command.key);
  EXPECT_EQ(Verb::Empty, r.command.verb);

  EXPECT_TRUE(parseLine("set radio.tx 22").ok);
  EXPECT_TRUE(parseLine("set radio.tx -9").ok);
  EXPECT_FALSE(parseLine("set radio.tx -10").ok);
  EXPECT_FALSE(parseLine("set radio.tx 22.5").ok);
  EXPECT_STREQ("radio.tx: not a whole number", parseLine("set radio.tx high").error);
}

TEST(Refusals, TheOtherNumericKeysHoldMeshCoresRanges) {
  EXPECT_STREQ("radio.freq: out of range, 150..2500", parseLine("set radio.freq 100").error);
  EXPECT_STREQ("radio.freq: not a number", parseLine("set radio.freq 869,618").error);
  EXPECT_STREQ("radio.bw: out of range, 7.8..500", parseLine("set radio.bw 1000").error);
  EXPECT_STREQ("radio.sf: out of range, 5..12", parseLine("set radio.sf 13").error);
  EXPECT_STREQ("radio.cr: out of range, 5..8", parseLine("set radio.cr 4").error);
  EXPECT_TRUE(parseLine("set radio.bw 7.8").ok);
  EXPECT_TRUE(parseLine("set radio.sf 12").ok);
}

TEST(Refusals, TheUrlNeedsTheHttpsScheme) {
  EXPECT_STREQ("meshmaster.url: the scheme must be https://", parseLine("set meshmaster.url http://meshmaster.dk").error);
  EXPECT_STREQ("meshmaster.url: the scheme must be https://", parseLine("set meshmaster.url meshmaster.dk").error);
  EXPECT_STREQ("meshmaster.url: https://host[:port][/path], at most 128 characters",
               parseLine("set meshmaster.url https://").error);
  EXPECT_STREQ("meshmaster.url: '?' is not allowed in a URL here", parseLine("set meshmaster.url https://a.dk/?x").error);
  EXPECT_TRUE(parseLine("set meshmaster.url https://meshmaster.dk").ok);
  EXPECT_TRUE(parseLine("set meshmaster.url https://localhost:5001").ok);
  EXPECT_TRUE(parseLine("set meshmaster.url https://meshmaster.dk/").ok);
}

TEST(Refusals, TheTokenHasTheServersShape) {
  EXPECT_STREQ("meshmaster.token: a meshmaster.dk agent token starts with mma_",
               parseLine("set meshmaster.token mmp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx").error);
  EXPECT_STREQ("meshmaster.token: 12..128 characters", parseLine("set meshmaster.token mma_abc").error);
  EXPECT_STREQ("meshmaster.token: letters, digits, - and _ after mma_",
               parseLine("set meshmaster.token mma_xxxxxxxxxx+xxxxxxxxxx").error);
}

TEST(Refusals, TheWifiValuesAreBounded) {
  EXPECT_STREQ("wifi.pass: 8..63 characters", parseLine("set wifi.pass short").error);
  EXPECT_STREQ("wifi.ssid: 1..32 characters", parseLine("set wifi.ssid 123456789012345678901234567890123").error);
  EXPECT_TRUE(parseLine("set wifi.ssid a b c").ok);
  EXPECT_STREQ("a b c", parseLine("set wifi.ssid a b c").command.value);
}

TEST(Refusals, LogKeysTakeTheirWordsOnly) {
  EXPECT_STREQ("log.level: error | warn | info | debug", parseLine("set log.level verbose").error);
  EXPECT_STREQ("log.frames: on | off", parseLine("set log.frames yes").error);
}

TEST(Refusals, TheObserverKeysHoldTheObserverFirmwaresShapes) {
  // mqtt.iata: three letters, any case typed, uppercase stored (task p1.7-26).
  EXPECT_STREQ("mqtt.iata: three letters, e.g. AAR", parseLine("set mqtt.iata AARH").error);
  EXPECT_STREQ("mqtt.iata: three letters, e.g. AAR", parseLine("set mqtt.iata A1R").error);
  EXPECT_STREQ("AAR", parseLine("set mqtt.iata aar").command.value);
  EXPECT_STREQ("CPH", parseLine("set mqtt.iata Cph").command.value);
  // mqtt.owner: 64 hex digits.
  EXPECT_STREQ("mqtt.owner: a public key is 64 hex digits", parseLine("set mqtt.owner 0123").error);
  EXPECT_STREQ("mqtt.owner: a public key is 64 hex digits",
               parseLine("set mqtt.owner 0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEG").error);
  // mqtt.email: one @, no spaces.
  EXPECT_STREQ("mqtt.email: name@host, at most 63 characters", parseLine("set mqtt.email thomas").error);
  EXPECT_STREQ("mqtt.email: name@host, at most 63 characters", parseLine("set mqtt.email thomas@ example.dk").error);
  EXPECT_TRUE(parseLine("set mqtt.email thomas@example.dk").ok);
  // mqttN.preset: the table's names and the two words, case-sensitive.
  EXPECT_STREQ("mqtt1.preset: none | meshview | meshmapper | custom (get mqtt.presets)", parseLine("set mqtt1.preset analyzer-us").error);
  EXPECT_STREQ("mqtt2.preset: none | meshview | meshmapper | custom (get mqtt.presets)", parseLine("set mqtt2.preset MeshView").error);
  EXPECT_TRUE(parseLine("set mqtt1.preset none").ok);
  EXPECT_TRUE(parseLine("set mqtt2.preset custom").ok);
  // mqttN.server: TLS only.
  EXPECT_STREQ("mqtt1.server: the scheme must be wss:// or mqtts:// (TLS is verified; ws:// and mqtt:// are not taken)",
               parseLine("set mqtt1.server ws://192.168.1.50:9001/mqtt").error);
  EXPECT_STREQ("mqtt1.server: the scheme must be wss:// or mqtts:// (TLS is verified; ws:// and mqtt:// are not taken)",
               parseLine("set mqtt1.server mqtt://mqtt.tennmesh.com:1883").error);
  EXPECT_STREQ("mqtt1.server: wss://host[:port][/path] or mqtts://host[:port], at most 63 characters",
               parseLine("set mqtt1.server wss://").error);
  EXPECT_STREQ("mqtt1.server: '?' is not allowed in a URL here", parseLine("set mqtt1.server wss://a.dk/?x").error);
  EXPECT_TRUE(parseLine("set mqtt1.server wss://mqtt.meshview.dk:443/mqtt").ok);
  EXPECT_TRUE(parseLine("set mqtt1.server mqtts://meshrank.net:8883").ok);
  // mqttN.audience: a host name.
  EXPECT_STREQ("mqtt1.audience: a host name, letters, digits, . and -, at most 63 characters",
               parseLine("set mqtt1.audience wss://mqtt.meshview.dk").error);
  EXPECT_STREQ("mqtt1.audience: a host name, letters, digits, . and -, at most 63 characters", parseLine("set mqtt1.audience .dk").error);
  // mqttN.topic: no wildcard, no space.
  EXPECT_STREQ("mqtt1.topic: a topic template of printable characters, no space, + or #, at most 95 characters",
               parseLine("set mqtt1.topic meshcore/+/{device}/#").error);
  EXPECT_STREQ("mqtt1.topic: a topic template of printable characters, no space, + or #, at most 95 characters",
               parseLine("set mqtt1.topic meshcore/{iata} x").error);
  EXPECT_TRUE(parseLine("set mqtt1.topic meshcore/{iata}/{device}/{type}").ok);
  // The sizes are the observer firmware's buffers less the NUL.
  EXPECT_STREQ("mqtt.origin: 1..31 characters", parseLine(("set mqtt.origin " + std::string(32, 'a')).c_str()).error);
  EXPECT_STREQ("mqtt1.username: 1..31 characters", parseLine(("set mqtt1.username " + std::string(32, 'a')).c_str()).error);
  EXPECT_STREQ("mqtt1.password: 1..63 characters", parseLine(("set mqtt1.password " + std::string(64, 'a')).c_str()).error);
}

TEST(Redaction, ASlotPasswordIsNeverInWhatGetPrints) {
  const mm::KeyDef& key = *mm::findKey("mqtt2.password");
  EXPECT_EQ("mqtt2.password set", describe(key, "correct horse battery"));
  EXPECT_EQ("mqtt2.password unset", describe(key, nullptr));
  EXPECT_EQ("ok mqtt2.password", setReply(key));
  EXPECT_EQ("mqtt1.preset unset (none)", describe(*mm::findKey("mqtt1.preset"), ""));
  EXPECT_EQ("mqtt.iata AAR", describe(*mm::findKey("mqtt.iata"), "AAR"));
}

// ---- the other line shapes ------------------------------------------------------------------

TEST(Lines, BlankAndWhitespaceLinesAreNothing) {
  EXPECT_EQ(Verb::Empty, parseLine("").command.verb);
  EXPECT_TRUE(parseLine("").ok);
  EXPECT_EQ(Verb::Empty, parseLine("   \r\n").command.verb);
  EXPECT_EQ(Verb::Empty, parseLine(nullptr).command.verb);
}

TEST(Lines, TheArgumentlessVerbsParseWithATerminalsLineEnding) {
  EXPECT_EQ(Verb::Help, parseLine("help\r\n").command.verb);
  EXPECT_EQ(Verb::Status, parseLine("status\r").command.verb);
  EXPECT_EQ(Verb::Wifi, parseLine("  wifi  ").command.verb);
  EXPECT_EQ(Verb::Reboot, parseLine("reboot\n").command.verb);
  EXPECT_EQ(Verb::GetAll, parseLine("get").command.verb);
  EXPECT_EQ(Verb::Mesh, parseLine("mesh").command.verb);
  EXPECT_EQ(Verb::Server, parseLine("server\r\n").command.verb);
  EXPECT_EQ(Verb::Jobs, parseLine("  jobs ").command.verb);
  EXPECT_EQ(Verb::Sessions, parseLine("sessions\r\n").command.verb);
  // Task 06's three: the counters, why the board restarted, and the log ring.
  EXPECT_EQ(Verb::Errors, parseLine("errors\r\n").command.verb);
  EXPECT_EQ(Verb::Reboots, parseLine("  reboots  ").command.verb);
  EXPECT_EQ(Verb::Dump, parseLine("dump\n").command.verb);
  // `reboots` is not `reboot`, and typing one must never do the other.
  EXPECT_EQ(Verb::Reboot, parseLine("reboot").command.verb);
  EXPECT_EQ(Verb::Reboots, parseLine("reboots").command.verb);

  // Each of them takes no argument, and says so by name rather than "unknown command".
  const char* const withAnArgument[] = {"help x",    "status x",  "mesh x",  "server x",  "jobs x",
                                        "sessions x", "errors x", "reboots x", "dump x",  "wifi x",
                                        "reboot x"};
  for (const char* line : withAnArgument) {
    const ParseResult r = parseLine(line);
    EXPECT_FALSE(r.ok) << line;
    EXPECT_NE(std::string(r.error).find("takes no argument"), std::string::npos) << line << " said: " << r.error;
  }
}

TEST(Lines, EraseNeedsItsConfirmationWord) {
  ParseResult r = parseLine("erase");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("erase forgets every setting; to do it, type: erase confirm", r.error);

  r = parseLine("erase yes");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ("erase: the word is 'confirm'", r.error);

  r = parseLine("erase confirm");
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(Verb::Erase, r.command.verb);
}

TEST(Lines, VerbsAreCaseSensitiveLikeTheKeys) {
  EXPECT_FALSE(parseLine("SET wifi.ssid x").ok);
  EXPECT_FALSE(parseLine("set Wifi.ssid x").ok);
}

// ---- the README's session -------------------------------------------------------------------

// Every ```console block of README.md, replayed: a "> set" line must be accepted and answered
// with the reply the board gives, a "> get" line answered with what the board prints from the
// values the session has set so far, and every other prompt line must parse. That is what
// "the README's provisioning session is what the console actually accepts, line for line" means.
TEST(Readme, TheProvisioningSessionIsAcceptedLineForLine) {
  std::ifstream in(std::string(PROJECT_DIR) + "/README.md");
  ASSERT_TRUE(in.is_open()) << PROJECT_DIR << "/README.md";

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }

  std::map<std::string, std::string> store;
  int promptLines = 0;
  int blocks = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    if (lines[i] != "```console") continue;
    blocks++;
    size_t j = i + 1;
    for (; j < lines.size() && lines[j] != "```"; j++) {
      const std::string& l = lines[j];
      if (l.rfind(">", 0) != 0) continue;  // a reply line: checked by the prompt line before it
      promptLines++;
      const std::string typed = l.size() > 1 ? l.substr(2) : "";
      const ParseResult r = parseLine(typed.c_str());
      const std::string next = j + 1 < lines.size() ? lines[j + 1] : "";
      if (next.rfind("error: ", 0) == 0) {
        // The README shows a refusal: the parser must refuse, with that exact line.
        EXPECT_FALSE(r.ok) << "README.md line " << (j + 1) << ": '" << l << "' was accepted";
        EXPECT_EQ(next, std::string("error: ") + r.error) << "README.md line " << (j + 2);
        continue;
      }
      EXPECT_TRUE(r.ok) << "README.md line " << (j + 1) << ": '" << l << "' -> " << r.error;
      if (!r.ok) continue;

      if (r.command.verb == Verb::Set) {
        store[r.command.key->name] = r.command.value;
        ASSERT_LT(j + 1, lines.size());
        EXPECT_EQ(setReply(*r.command.key), lines[j + 1]) << "README.md line " << (j + 2);
      } else if (r.command.verb == Verb::Get) {
        const auto it = store.find(r.command.key->name);
        ASSERT_LT(j + 1, lines.size());
        EXPECT_EQ(describe(*r.command.key, it == store.end() ? nullptr : it->second.c_str()), lines[j + 1])
            << "README.md line " << (j + 2);
      } else if (r.command.verb == Verb::GetAll) {
        for (int k = 0; k < kKeyCount; k++) {
          ASSERT_LT(j + 1 + k, lines.size());
          const auto it = store.find(kKeys[k].name);
          EXPECT_EQ(describe(kKeys[k], it == store.end() ? nullptr : it->second.c_str()), lines[j + 1 + k])
              << "README.md line " << (j + 2 + k);
        }
      }
    }
    i = j;
  }
  EXPECT_GE(blocks, 1) << "README.md has no ```console block";
  EXPECT_GE(promptLines, 6) << "the README's session is shorter than a provisioning";
  EXPECT_NE(store.end(), store.find("wifi.ssid")) << "the session never sets wifi.ssid";
  EXPECT_NE(store.end(), store.find("meshmaster.token")) << "the session never sets the token";
}

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
