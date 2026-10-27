// The observer dialect (tasks/phase-1.7/26): the packet and status formatters held to fixtures
// written by hand from the observer firmware's source, byte for byte; the topics to the template;
// the sign-in token to a signature MeshCore's own ed25519 verifies, and its header and claims
// decoded back to the fixture's JSON; the Danish guide's lines fed to the console parser and
// resolved into the two slots they mean; and a grep that nothing in src/observer/ opens a socket.
//
// The fixtures are test/test_observer/fixtures/*.fixture: '#' lines of citations, then key=value
// lines, the last of them `expected=` with the message as the observer firmware would publish
// it. Nothing here needs a board, a radio or a network.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <ed_25519.h>  // MeshCore's lib/ed25519 at the pin, compiled into this build by tools/pio/meshcore_private_libs.py

#include "console/Parser.h"
#include "observer/Config.h"
#include "observer/Dialect.h"
#include "observer/Frame.h"
#include "observer/Presets.h"
#include "observer/Token.h"
#include "observer/Topics.h"
#include "settings/Keys.h"

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

using namespace mm::observer;

namespace {

namespace fs = std::filesystem;

const std::string kFixtures = std::string(PROJECT_DIR) + "/test/test_observer/fixtures/";

std::map<std::string, std::string> readFixture(const std::string& name) {
  std::ifstream in(kFixtures + name + ".fixture");
  EXPECT_TRUE(in.is_open()) << kFixtures << name;
  std::map<std::string, std::string> kv;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    kv[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return kv;
}

std::vector<uint8_t> fromHex(const std::string& hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return out;
}

// A fixed identity for the token tests: MeshCore's keypair from a seed, as LocalIdentity(RNG*)
// makes one (src/Identity.cpp:61-65 at the pin) - the seed here is a constant, so the keys are
// the same in every run. Nothing of the private key reaches a token (held below).
struct FixedIdentity : public Signer {
  uint8_t pub[32];
  uint8_t prv[64];
  FixedIdentity() {
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = static_cast<uint8_t>(0x5A ^ (i * 7));
    ed25519_create_keypair(pub, prv, seed);
  }
  bool sign(const uint8_t* msg, size_t len, uint8_t sig[kSignatureSize]) override {
    ed25519_sign(sig, msg, len, pub, prv);
    return true;
  }
  std::string pubHex() const {
    char hex[65];
    hexUpper(pub, 32, hex, sizeof hex);
    return hex;
  }
  std::string prvHex() const {
    char hex[129];
    hexUpper(prv, 64, hex, sizeof hex);
    return hex;
  }
};

std::vector<std::string> split(const std::string& s, char c) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (;;) {
    const size_t at = s.find(c, start);
    parts.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
    if (at == std::string::npos) break;
    start = at + 1;
  }
  return parts;
}

std::string decode(const std::string& b64) {
  std::vector<uint8_t> out(b64.size());
  const size_t n = base64UrlDecode(b64.c_str(), b64.size(), out.data(), out.size());
  return std::string(reinterpret_cast<const char*>(out.data()), n);
}

// The Danish guide of tasks/phase-1.7/README.md, line for line, with the two placeholders filled
// (an IATA code and a 64-hex owner key) as a person would fill them.
const char* const kDanishGuide[] = {
    "set timezone CEST",
    "set bridge.source rx",
    "reboot",
    "set wifi.ssid navn_ssid",
    "set wifi.pwd password",
    "reboot",
    "set mqtt.iata AAR",
    "set mqtt.owner 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "set mqtt1.preset custom",
    "set mqtt1.server wss://mqtt.meshview.dk:443/mqtt",
    "set mqtt1.audience mqtt.meshview.dk",
    "set mqtt1.topic meshcore/{iata}/{device}/{type}",
    "set mqtt2.preset meshmapper",
    "reboot",
};

}  // namespace

// ---- the packet message ---------------------------------------------------------------------

class PacketFixture : public ::testing::TestWithParam<const char*> {};

TEST_P(PacketFixture, TheFormatterWritesTheObserverFirmwaresBytes) {
  const auto kv = readFixture(GetParam());
  ASSERT_FALSE(kv.empty());
  const std::vector<uint8_t> raw = fromHex(kv.at("raw"));
  const std::vector<uint8_t> hash = fromHex(kv.at("hash"));
  ASSERT_EQ(kHashSize, hash.size());

  const int sf = std::stoi(kv.at("sf"));
  const float snr = std::stof(kv.at("snr"));
  const float score = packetScore(snr, sf, static_cast<int>(raw.size()));
  EXPECT_EQ(std::stoi(kv.at("score1000")), static_cast<int>(score * 1000)) << "the score formula";

  PacketFields f;
  f.origin = kv.at("origin").c_str();
  f.originId = kv.at("originId").c_str();
  f.atEpoch = static_cast<uint32_t>(std::stoul(kv.at("epoch")));
  f.atMicros = static_cast<uint32_t>(std::stoul(kv.at("micros")));
  f.raw = raw.data();
  f.rawLen = raw.size();
  f.hash = hash.data();
  f.snr = snr;
  f.rssi = std::stof(kv.at("rssi"));
  f.score = score;

  char out[kPacketMessageCap];
  const size_t n = formatPacket(f, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(kv.at("expected"), std::string(out, n));
  EXPECT_EQ(n, std::strlen(out));
}

INSTANTIATE_TEST_SUITE_P(Fixtures, PacketFixture,
                         ::testing::Values("packet-flood-advert", "packet-direct-txt", "packet-transport-direct-trace",
                                           "packet-transport-flood-req"));

TEST(Packet, TheThreeRadioNumbersAreLeftOutWhenUnknownAndNothingElseMoves) {
  // MQTTPayloadBuilder.cpp:311-326: NaN SNR, the RSSI sentinel and NaN score each drop their field.
  const auto kv = readFixture("packet-flood-advert");
  const std::vector<uint8_t> raw = fromHex(kv.at("raw"));
  const std::vector<uint8_t> hash = fromHex(kv.at("hash"));
  PacketFields f;
  f.origin = kv.at("origin").c_str();
  f.originId = kv.at("originId").c_str();
  f.atEpoch = static_cast<uint32_t>(std::stoul(kv.at("epoch")));
  f.atMicros = static_cast<uint32_t>(std::stoul(kv.at("micros")));
  f.raw = raw.data();
  f.rawLen = raw.size();
  f.hash = hash.data();
  f.snr = NAN;
  f.rssi = static_cast<float>(kRssiUnknown);
  f.score = NAN;
  char out[kPacketMessageCap];
  ASSERT_GT(formatPacket(f, out, sizeof out), 0u);
  const std::string expected = kv.at("expected");
  const std::string tail = ",\"SNR\":\"9.5\",\"RSSI\":\"-84\",\"score\":\"1000\"}";
  ASSERT_EQ(expected.size() - tail.size(), expected.rfind(tail));
  EXPECT_EQ(expected.substr(0, expected.size() - tail.size()) + "}", std::string(out));
}

TEST(Packet, ABufferTooSmallGivesNothingNotHalfAMessage) {
  const auto kv = readFixture("packet-direct-txt");
  const std::vector<uint8_t> raw = fromHex(kv.at("raw"));
  PacketFields f;
  f.raw = raw.data();
  f.rawLen = raw.size();
  char out[200];
  EXPECT_EQ(0u, formatPacket(f, out, sizeof out));
  EXPECT_STREQ("", out);
}

TEST(Packet, AFrameMeshCoreWouldRefuseIsNoMessage) {
  PacketFields f;
  char out[kPacketMessageCap];
  const uint8_t pathMode3[] = {0x0A, 0xC1, 0x01, 0x02, 0x03, 0x04, 0x05};  // hash size 4: reserved
  f.raw = pathMode3;
  f.rawLen = sizeof pathMode3;
  EXPECT_EQ(0u, formatPacket(f, out, sizeof out));
  const uint8_t tooShort[] = {0x0A, 0x43, 0xAA};  // claims six path bytes, has one
  f.raw = tooShort;
  f.rawLen = sizeof tooShort;
  EXPECT_EQ(0u, formatPacket(f, out, sizeof out));
  const uint8_t noPayload[] = {0x11, 0x00};
  f.raw = noPayload;
  f.rawLen = sizeof noPayload;
  EXPECT_EQ(0u, formatPacket(f, out, sizeof out));
  std::vector<uint8_t> tooLong(2 + 185, 0x00);
  tooLong[0] = 0x11;
  f.raw = tooLong.data();
  f.rawLen = tooLong.size();
  EXPECT_EQ(0u, formatPacket(f, out, sizeof out));
  f.rawLen = 2 + 184;  // MAX_PACKET_PAYLOAD exactly is fine
  EXPECT_GT(formatPacket(f, out, sizeof out), 0u);
}

TEST(Frame, TheLayoutIsReadAsPacketReadFromReadsIt) {
  Frame fr;
  const uint8_t transportDirect[] = {0x27, 0x12, 0x34, 0x56, 0x78, 0x02, 0x5A, 0x5B, 0x01, 0x02, 0x03};
  ASSERT_TRUE(parseFrame(transportDirect, sizeof transportDirect, fr));
  EXPECT_EQ(9, fr.payloadType());
  EXPECT_TRUE(fr.routeDirect());
  EXPECT_TRUE(fr.hasTransportCodes());
  EXPECT_EQ(1, fr.hashSize());
  EXPECT_EQ(2, fr.hopCount());
  EXPECT_EQ(transportDirect + 6, fr.path);
  EXPECT_EQ(3, fr.payloadLen);
  EXPECT_EQ(transportDirect + 8, fr.payload);

  const uint8_t version2[] = {0x91, 0x00, 0x01};  // PAYLOAD_VER above 1: Dispatcher.cpp:152
  EXPECT_FALSE(parseFrame(version2, sizeof version2, fr));
}

TEST(Score, IsMeshCoresPacketScoreIntAtThePin) {
  // RadioLibWrappers.cpp:221-239: SF 8's threshold is -10 dB; (snr + 10) / 10 * (1 - len / 256), clamped.
  EXPECT_FLOAT_EQ(0.0f, packetScore(-10.5f, 8, 40));
  EXPECT_FLOAT_EQ(0.0f, packetScore(0.0f, 6, 40));  // below SF 7: 0 (:231)
  EXPECT_FLOAT_EQ(1.0f, packetScore(9.5f, 8, 32));
  EXPECT_NEAR(0.578906f, packetScore(-3.5f, 8, 28), 1e-6f);
  EXPECT_NEAR((5.0 + 7.5) / 10.0 * (1 - 100 / 256.0), packetScore(5.0f, 7, 100), 1e-6);
  EXPECT_NEAR(0.1 * (1 - 10 / 256.0), packetScore(-19.0f, 12, 10), 1e-6) << "SF 12 is the last row, threshold -20";
  EXPECT_FLOAT_EQ(0.0f, packetScore(-20.5f, 12, 10));
  EXPECT_FLOAT_EQ(0.0f, packetScore(0.0f, 13, 10)) << "past the table";
}

// ---- the status message ---------------------------------------------------------------------

class StatusFixture : public ::testing::TestWithParam<const char*> {};

TEST_P(StatusFixture, TheFormatterWritesTheObserverFirmwaresBytes) {
  const auto kv = readFixture(GetParam());
  ASSERT_FALSE(kv.empty());
  char radio[48];
  char client[128];
  formatRadio(std::stof(kv.at("freq")), std::stof(kv.at("bw")), std::stoi(kv.at("sf")), std::stoi(kv.at("cr")), radio, sizeof radio);
  formatClientVersion(kv.at("firmwareVersion").c_str(), client, sizeof client);
  StatusFields f;
  f.origin = kv.at("origin").c_str();
  f.originId = kv.at("originId").c_str();
  f.model = kv.at("model").c_str();
  f.firmwareVersion = kv.at("firmwareVersion").c_str();
  f.radio = radio;
  f.clientVersion = client;
  f.atEpoch = static_cast<uint32_t>(std::stoul(kv.at("epoch")));
  f.atMicros = static_cast<uint32_t>(std::stoul(kv.at("micros")));
  auto stat = [&](const char* name, int& field) {
    const auto it = kv.find(name);
    if (it != kv.end()) field = std::stoi(it->second);
  };
  stat("batteryMv", f.batteryMv);
  stat("uptimeSecs", f.uptimeSecs);
  stat("packetsSent", f.packetsSent);
  stat("packetsReceived", f.packetsReceived);
  stat("errors", f.errors);
  stat("queueLen", f.queueLen);
  stat("noiseFloor", f.noiseFloor);
  stat("txAirSecs", f.txAirSecs);
  stat("rxAirSecs", f.rxAirSecs);
  stat("recvErrors", f.recvErrors);
  stat("internalHeap", f.internalHeap);
  stat("heapLargest", f.heapLargest);
  stat("mqttSlotsUp", f.mqttSlotsUp);
  stat("mqttSlotsTotal", f.mqttSlotsTotal);
  stat("mqttConnectFailures", f.mqttConnectFailures);
  stat("mqttSlotsBreaker", f.mqttSlotsBreaker);
  char out[kStatusMessageCap];
  const size_t n = formatStatus(f, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(kv.at("expected"), std::string(out, n));
}

INSTANTIATE_TEST_SUITE_P(Fixtures, StatusFixture, ::testing::Values("status-online-stats", "status-online-bare"));

TEST(Status, TheTimestampIsUtcWithSixDigitsAndAnExplicitOffset) {
  char out[kTimestampCap];
  formatTimestamp(1793016000, 123456, out, sizeof out);
  EXPECT_STREQ("2026-10-26T12:00:00.123456+00:00", out);
  formatTimestamp(1793016000, 5, out, sizeof out);
  EXPECT_STREQ("2026-10-26T12:00:00.000005+00:00", out);
  formatTimestamp(1793016000, 7000000, out, sizeof out);  // clamped: MQTTMessageBuilder.cpp:296-297
  EXPECT_STREQ("2026-10-26T12:00:00.999999+00:00", out);
  formatTimestamp(0, 0, out, sizeof out);
  EXPECT_STREQ("1970-01-01T00:00:00.000000+00:00", out);
}

TEST(Status, RepeatLeftOutWhenNullAndOfflineIsAString) {
  StatusFields f;
  f.status = "offline";
  f.repeat = nullptr;
  char out[kStatusMessageCap];
  ASSERT_GT(formatStatus(f, out, sizeof out), 0u);
  const std::string s(out);
  EXPECT_EQ(0u, s.find("{\"status\":\"offline\",\"timestamp\":\"1970-01-01T00:00:00.000000+00:00\",\"origin\":\"\",\"origin_id\":\"\",\"model\":\"\",\"firmware_version\":\"\",\"radio\":\"\",\"client_version\":\"\"}"));
  EXPECT_EQ(std::string::npos, s.find("repeat"));
  EXPECT_EQ(std::string::npos, s.find("stats"));
}

// ---- the topics -----------------------------------------------------------------------------

TEST(Topics, TheDefaultLayoutNeedsAUsableCodeAndADevice) {
  char out[kTopicCap];
  const char* device = "7F3A9C02E1B44D9A0F6C2B7D8E9A1C3F5D7B9E0A2C4E6F8A0B1C2D3E4F5A6B7C";
  ASSERT_TRUE(buildTopic(nullptr, "AAR", device, MessageType::Packets, out, sizeof out));
  EXPECT_STREQ("meshcore/AAR/7F3A9C02E1B44D9A0F6C2B7D8E9A1C3F5D7B9E0A2C4E6F8A0B1C2D3E4F5A6B7C/packets", out);
  ASSERT_TRUE(buildTopic("", "CPH", device, MessageType::Status, out, sizeof out));
  EXPECT_STREQ("meshcore/CPH/7F3A9C02E1B44D9A0F6C2B7D8E9A1C3F5D7B9E0A2C4E6F8A0B1C2D3E4F5A6B7C/status", out);
  EXPECT_FALSE(buildTopic(nullptr, "", device, MessageType::Status, out, sizeof out));
  EXPECT_FALSE(buildTopic(nullptr, "XXX", device, MessageType::Status, out, sizeof out)) << "the guide's placeholder";
  EXPECT_FALSE(buildTopic(nullptr, "AARH", device, MessageType::Status, out, sizeof out));
  EXPECT_FALSE(buildTopic(nullptr, "AAR", "", MessageType::Status, out, sizeof out));
  EXPECT_STREQ("", out);
}

TEST(Topics, ACustomTemplateSubstitutesTheThreePlaceholdersAndCopiesTheRest) {
  char out[kTopicCap];
  ASSERT_TRUE(buildTopic("meshcore/{iata}/{device}/{type}", "ODE", "ABCD", MessageType::Packets, out, sizeof out));
  EXPECT_STREQ("meshcore/ODE/ABCD/packets", out);
  ASSERT_TRUE(buildTopic("mine/{type}/{device}", "", "ABCD", MessageType::Status, out, sizeof out));
  EXPECT_STREQ("mine/status/ABCD", out) << "a template that needs no code";
  ASSERT_TRUE(buildTopic("x/{token}/{type}", "AAR", "ABCD", MessageType::Status, out, sizeof out));
  EXPECT_STREQ("x/{token}/status", out) << "an unknown placeholder is copied through (MQTTTopicTemplate.h:13)";
  char tiny[12];
  EXPECT_FALSE(buildTopic("meshcore/{iata}/{device}/{type}", "AAR", "ABCD", MessageType::Status, tiny, sizeof tiny));
  EXPECT_FALSE(buildTopic("{iata}", "", "ABCD", MessageType::Status, out, sizeof out)) << "an empty result is no topic";
}

// ---- the token ------------------------------------------------------------------------------

TEST(Token, TheHeaderAndTheClaimsAreTheFixturesBytesAndTheSignatureVerifies) {
  const auto kv = readFixture("token");
  ASSERT_FALSE(kv.empty());
  FixedIdentity id;
  TokenClaims c;
  c.publicKeyHex = kv.at("publicKeyHex").c_str();
  c.audience = "mqtt.meshview.dk";
  c.issuedAt = static_cast<uint32_t>(std::stoul(kv.at("iat")));
  c.expiresIn = tokenLifetime(0, 0);
  c.owner = kv.at("owner").c_str();
  c.client = kv.at("client").c_str();
  c.email = kv.at("email").c_str();
  char token[kTokenCap];
  ASSERT_TRUE(makeToken(c, id, token, sizeof token));

  const std::vector<std::string> parts = split(token, '.');
  ASSERT_EQ(3u, parts.size());
  EXPECT_EQ(kv.at("headerB64"), parts[0]);
  EXPECT_EQ(kv.at("claims1B64"), parts[1]);
  EXPECT_EQ(kv.at("header"), decode(parts[0]));
  EXPECT_EQ(kv.at("claims1"), decode(parts[1]));

  // JWTHelper.cpp:91-96: the signature is 64 bytes as 128 uppercase hex, over "<header>.<claims>".
  ASSERT_EQ(128u, parts[2].size());
  for (char ch : parts[2]) EXPECT_TRUE((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F')) << parts[2];
  const std::vector<uint8_t> sig = fromHex(parts[2]);
  const std::string signed_ = parts[0] + "." + parts[1];
  EXPECT_EQ(1, ed25519_verify(sig.data(), reinterpret_cast<const unsigned char*>(signed_.data()), signed_.size(), id.pub));
  const std::string tampered = parts[0] + "." + kv.at("claims2B64");
  EXPECT_EQ(0, ed25519_verify(sig.data(), reinterpret_cast<const unsigned char*>(tampered.data()), tampered.size(), id.pub));
  uint8_t otherPub[32];
  uint8_t otherPrv[64];
  uint8_t seed[32] = {1};
  ed25519_create_keypair(otherPub, otherPrv, seed);
  EXPECT_EQ(0, ed25519_verify(sig.data(), reinterpret_cast<const unsigned char*>(signed_.data()), signed_.size(), otherPub));

  // The private key is in no part of the token, in any case.
  const std::string whole(token);
  std::string lower(whole);
  for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  std::string prv = id.prvHex();
  for (char& ch : prv) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  EXPECT_EQ(std::string::npos, lower.find(prv.substr(0, 16)));
  EXPECT_EQ(std::string::npos, lower.find(prv.substr(prv.size() - 16)));
}

TEST(Token, TheOptionalClaimsAreLeftOutWhenUnsetAndTheSecondSlotExpiresEarlier) {
  const auto kv = readFixture("token");
  FixedIdentity id;
  TokenClaims c;
  c.publicKeyHex = "7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c";  // lowercase in, uppercase claim
  c.audience = "mqtt.meshmapper.net";
  c.issuedAt = static_cast<uint32_t>(std::stoul(kv.at("iat")));
  c.expiresIn = tokenLifetime(0, 1);
  c.owner = "";
  char token[kTokenCap];
  ASSERT_TRUE(makeToken(c, id, token, sizeof token));
  const std::vector<std::string> parts = split(token, '.');
  ASSERT_EQ(3u, parts.size());
  EXPECT_EQ(kv.at("claims2B64"), parts[1]);
  EXPECT_EQ(kv.at("claims2"), decode(parts[1]));
}

TEST(Token, TheUsernameLifetimeAndRenewalAreTheObserverFirmwares) {
  char user[kUsernameCap];
  usernameFor("7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c", user, sizeof user);
  EXPECT_STREQ("v1_7F3A9C02E1B44D9A0F6C2B7D8E9A1C3F5D7B9E0A2C4E6F8A0B1C2D3E4F5A6B7C", user);

  EXPECT_EQ(86400u, tokenLifetime(0, 0));
  EXPECT_EQ(86100u, tokenLifetime(0, 1));
  EXPECT_EQ(3300u, tokenLifetime(3300, 0));
  EXPECT_EQ(3300u - 165u, tokenLifetime(3300, 1));
  EXPECT_EQ(300u, renewalBuffer(86400));
  EXPECT_EQ(60u, renewalBuffer(300));
  EXPECT_EQ(100u, renewalBuffer(1000));

  const uint32_t exp = 1793016000u + 86400u;
  EXPECT_FALSE(tokenNeedsRenewal(true, 1793016000u, exp, 300));
  EXPECT_FALSE(tokenNeedsRenewal(true, exp - 301, exp, 300));
  EXPECT_TRUE(tokenNeedsRenewal(true, exp - 300, exp, 300));
  EXPECT_TRUE(tokenNeedsRenewal(true, exp, exp, 300));
  EXPECT_TRUE(tokenNeedsRenewal(true, 1793016000u, 12345, 300)) << "an expiry from before the clock was set";
  EXPECT_TRUE(tokenNeedsRenewal(false, 100, 0, 300)) << "no clock, no token yet";
  EXPECT_FALSE(tokenNeedsRenewal(false, 100, exp, 300)) << "no clock, but a token: keep it until there is one";
}

TEST(Token, NoAudienceNoToken) {
  FixedIdentity id;
  TokenClaims c;
  c.publicKeyHex = "AB";
  c.audience = "";
  char token[kTokenCap];
  EXPECT_FALSE(makeToken(c, id, token, sizeof token));
  EXPECT_STREQ("", token);
  c.audience = "x";
  char small[80];
  EXPECT_FALSE(makeToken(c, id, small, sizeof small));
  EXPECT_STREQ("", small);
}

TEST(Token, Base64UrlRoundTripsEveryLength) {
  for (size_t len = 0; len < 64; len++) {
    std::vector<uint8_t> in(len);
    for (size_t i = 0; i < len; i++) in[i] = static_cast<uint8_t>(i * 37 + len);
    char enc[128];
    const size_t n = base64Url(in.data(), len, enc, sizeof enc);
    EXPECT_EQ((len / 3) * 4 + (len % 3 == 0 ? 0 : len % 3 + 1), n);
    EXPECT_EQ(std::string::npos, std::string(enc).find('='));
    EXPECT_EQ(std::string::npos, std::string(enc).find('+'));
    EXPECT_EQ(std::string::npos, std::string(enc).find('/'));
    std::vector<uint8_t> back(64);
    EXPECT_EQ(len, base64UrlDecode(enc, n, back.data(), back.size()));
    EXPECT_TRUE(std::equal(in.begin(), in.end(), back.begin()));
  }
  const uint8_t bytes[] = {0xFB, 0xFF, 0xBF};  // the two characters that differ from plain base64
  char enc[8];
  base64Url(bytes, 3, enc, sizeof enc);
  EXPECT_STREQ("-_-_", enc);
}

// ---- the presets and the settings -----------------------------------------------------------

TEST(Presets, TheTwoRowsAndTheTwoWords) {
  const Preset* mm = findPreset("meshmapper");
  ASSERT_NE(nullptr, mm);
  EXPECT_STREQ("wss://mqtt.meshmapper.net:443/mqtt", mm->serverUrl);
  EXPECT_STREQ("mqtt.meshmapper.net", mm->audience);
  EXPECT_EQ(Auth::Jwt, mm->auth);
  EXPECT_EQ(0u, mm->tokenLifetime);
  EXPECT_TRUE(mm->allowRetain);
  EXPECT_EQ(55, mm->keepalive);

  const Preset* mv = findPreset("meshview");
  ASSERT_NE(nullptr, mv);
  EXPECT_STREQ("wss://mqtt.meshview.dk:443/mqtt", mv->serverUrl);
  EXPECT_STREQ("mqtt.meshview.dk", mv->audience);
  EXPECT_EQ(Auth::Jwt, mv->auth);
  EXPECT_FALSE(mv->allowRetain);
  EXPECT_EQ(0, mv->keepalive);

  EXPECT_EQ(nullptr, findPreset("custom"));
  EXPECT_EQ(nullptr, findPreset("none"));
  EXPECT_EQ(nullptr, findPreset("analyzer-us"));
  EXPECT_TRUE(isPresetName("custom"));
  EXPECT_TRUE(isPresetName("none"));
  EXPECT_TRUE(isPresetName("meshview"));
  EXPECT_FALSE(isPresetName("Meshview"));
  EXPECT_FALSE(isPresetName(""));

  // No row carries a password: the table has no field for one.
  for (const Preset& p : kPresets) EXPECT_NE(Auth::UserPass, p.auth) << p.name;
}

TEST(Presets, TheListForTheConsoleNamesEveryRowAndTheTwoWords) {
  std::vector<std::string> lines;
  describePresets([](void* ctx, const char* text) { static_cast<std::vector<std::string>*>(ctx)->push_back(text); }, &lines);
  ASSERT_GE(lines.size(), 4u);
  EXPECT_NE(std::string::npos, lines[0].find("meshview"));
  EXPECT_NE(std::string::npos, lines[0].find("wss://mqtt.meshview.dk:443/mqtt"));
  EXPECT_NE(std::string::npos, lines[1].find("meshmapper"));
  EXPECT_NE(std::string::npos, lines[1].find("keepalive 55 s"));
  EXPECT_NE(std::string::npos, lines[2].find("custom"));
  EXPECT_NE(std::string::npos, lines.back().find("none"));
}

TEST(Settings, TheDanishGuideTypedInOrderEndsInTwoValidSlots) {
  std::map<std::string, std::string> store;
  int notes = 0;
  int reboots = 0;
  for (const char* line : kDanishGuide) {
    const mm::ParseResult r = mm::parseLine(line);
    EXPECT_TRUE(r.ok) << line << ": " << r.error;
    if (!r.ok) continue;
    switch (r.command.verb) {
      case mm::Verb::Set:
        store[r.command.key->name] = r.command.value;
        break;
      case mm::Verb::Note:
        notes++;
        EXPECT_NE(nullptr, r.command.note) << line;
        break;
      case mm::Verb::Reboot:
        reboots++;
        break;
      default:
        ADD_FAILURE() << line << " parsed as something else";
    }
  }
  EXPECT_EQ(2, notes) << "timezone and bridge.source are answered, not stored";
  EXPECT_EQ(3, reboots);
  EXPECT_EQ("navn_ssid", store["wifi.ssid"]);
  EXPECT_EQ("password", store["wifi.pass"]) << "wifi.pwd is wifi.pass";
  EXPECT_EQ(store.end(), store.find("wifi.pwd"));
  EXPECT_EQ("AAR", store["mqtt.iata"]);
  EXPECT_EQ("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", store["mqtt.owner"]);

  ObserverSettings s;
  s.iata = store["mqtt.iata"].c_str();
  s.owner = store["mqtt.owner"].c_str();
  s.slot[0].preset = store["mqtt1.preset"].c_str();
  s.slot[0].server = store["mqtt1.server"].c_str();
  s.slot[0].audience = store["mqtt1.audience"].c_str();
  s.slot[0].topic = store["mqtt1.topic"].c_str();
  s.slot[1].preset = store["mqtt2.preset"].c_str();

  const ResolvedSlot one = resolveSlot(s.slot[0]);
  EXPECT_TRUE(one.active) << one.why;
  EXPECT_STREQ("custom", one.name);
  EXPECT_STREQ("wss://mqtt.meshview.dk:443/mqtt", one.serverUrl);
  EXPECT_EQ(Auth::Jwt, one.auth);
  EXPECT_STREQ("mqtt.meshview.dk", one.audience);
  EXPECT_STREQ("meshcore/{iata}/{device}/{type}", one.topic);

  const ResolvedSlot two = resolveSlot(s.slot[1]);
  EXPECT_TRUE(two.active) << two.why;
  EXPECT_STREQ("meshmapper", two.name);
  EXPECT_STREQ("wss://mqtt.meshmapper.net:443/mqtt", two.serverUrl);
  EXPECT_EQ(Auth::Jwt, two.auth);
  EXPECT_EQ(nullptr, two.topic);
  EXPECT_TRUE(two.allowRetain);

  char why[64];
  EXPECT_TRUE(observerEnabled(s, why, sizeof why)) << why;

  // And both slots publish to the topics the guide means.
  char topic[kTopicCap];
  const char* device = "7F3A9C02E1B44D9A0F6C2B7D8E9A1C3F5D7B9E0A2C4E6F8A0B1C2D3E4F5A6B7C";
  ASSERT_TRUE(buildTopic(one.topic, s.iata, device, MessageType::Packets, topic, sizeof topic));
  EXPECT_STREQ("meshcore/AAR/7F3A9C02E1B44D9A0F6C2B7D8E9A1C3F5D7B9E0A2C4E6F8A0B1C2D3E4F5A6B7C/packets", topic);
  ASSERT_TRUE(buildTopic(two.topic, s.iata, device, MessageType::Status, topic, sizeof topic));
  EXPECT_STREQ("meshcore/AAR/7F3A9C02E1B44D9A0F6C2B7D8E9A1C3F5D7B9E0A2C4E6F8A0B1C2D3E4F5A6B7C/status", topic);
}

TEST(Settings, TheMeshviewPresetIsTheGuidesFourCustomLines) {
  SlotSettings custom;
  custom.preset = "custom";
  custom.server = "wss://mqtt.meshview.dk:443/mqtt";
  custom.audience = "mqtt.meshview.dk";
  custom.topic = "meshcore/{iata}/{device}/{type}";
  SlotSettings preset;
  preset.preset = "meshview";
  const ResolvedSlot a = resolveSlot(custom);
  const ResolvedSlot b = resolveSlot(preset);
  ASSERT_TRUE(a.active);
  ASSERT_TRUE(b.active);
  EXPECT_STREQ(a.serverUrl, b.serverUrl);
  EXPECT_STREQ(a.audience, b.audience);
  EXPECT_EQ(a.auth, b.auth);
  EXPECT_EQ(a.tokenLifetime, b.tokenLifetime);
  EXPECT_EQ(a.allowRetain, b.allowRetain);
  EXPECT_EQ(a.keepalive, b.keepalive);
  char topicA[kTopicCap];
  char topicB[kTopicCap];
  ASSERT_TRUE(buildTopic(a.topic, "AAR", "ABCD", MessageType::Packets, topicA, sizeof topicA));
  ASSERT_TRUE(buildTopic(b.topic, "AAR", "ABCD", MessageType::Packets, topicB, sizeof topicB));
  EXPECT_STREQ(topicA, topicB);
}

TEST(Settings, ASlotThatCannotConnectSaysWhatIsMissing) {
  SlotSettings s;
  EXPECT_FALSE(resolveSlot(s).active);
  EXPECT_STREQ("preset is none", resolveSlot(s).why);
  s.preset = "custom";
  EXPECT_STREQ("custom needs mqttN.server", resolveSlot(s).why);
  s.server = "wss://broker.example:443/mqtt";
  EXPECT_STREQ("custom needs mqttN.audience, or mqttN.username and mqttN.password", resolveSlot(s).why);
  s.username = "user";
  EXPECT_FALSE(resolveSlot(s).active);
  s.password = "pass";
  const ResolvedSlot r = resolveSlot(s);
  EXPECT_TRUE(r.active);
  EXPECT_EQ(Auth::UserPass, r.auth);
  EXPECT_STREQ("user", r.username);
  EXPECT_STREQ("pass", r.password);
  EXPECT_FALSE(r.allowRetain);
  s.audience = "broker.example";
  EXPECT_EQ(Auth::Jwt, resolveSlot(s).auth) << "an audience wins over a username (MQTTBridge.cpp:2948-2954)";

  ObserverSettings all;
  char why[64];
  EXPECT_FALSE(observerEnabled(all, why, sizeof why));
  EXPECT_STREQ("no slot configured (set mqtt1.preset ...)", why);
  all.slot[1].preset = "meshmapper";
  EXPECT_FALSE(observerEnabled(all, why, sizeof why));
  EXPECT_STREQ("mqtt.iata is unset", why);
  all.iata = "XXX";
  EXPECT_FALSE(observerEnabled(all, why, sizeof why));
  EXPECT_STREQ("mqtt.iata is not a usable code", why);
  all.iata = "RNN";
  EXPECT_TRUE(observerEnabled(all, why, sizeof why));
  // A custom slot whose own template has no {iata} needs no code.
  ObserverSettings own;
  own.slot[0].preset = "custom";
  own.slot[0].server = "wss://b.example:443/";
  own.slot[0].audience = "b.example";
  own.slot[0].topic = "mine/{device}/{type}";
  EXPECT_TRUE(observerEnabled(own, why, sizeof why)) << why;
}

// ---- the rails ------------------------------------------------------------------------------

TEST(Rails, NothingInTheObserverOpensASocketOrKnowsTheNetwork) {
  // The connection is task 27's. Until then no file of src/observer/ may name a client, a
  // socket or the WiFi - as a grep, so that a line added by mistake fails here before it builds.
  const char* const forbidden[] = {"WiFiClient", "WiFi.h", "mqtt_client", "esp_mqtt", "socket(", "lwip/", "esp_tls", "HTTPClient",
                                   "PubSubClient", "Psychic", "connect("};
  const fs::path dir = fs::path(PROJECT_DIR) / "src" / "observer";
  int files = 0;
  for (const auto& item : fs::directory_iterator(dir)) {
    if (!item.is_regular_file()) continue;
    files++;
    std::ifstream in(item.path());
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
      n++;
      for (const char* word : forbidden) {
        EXPECT_EQ(std::string::npos, line.find(word)) << item.path().filename().string() << ":" << n << " names " << word;
      }
    }
  }
  EXPECT_GE(files, 12);
}

TEST(Rails, NothingInTheObserverWritesAPayloadToTheLog) {
  // The raw bytes and the payload are formatted into a message buffer and nowhere else: no
  // LOG_ call and no Serial in src/observer/ at all, so there is no line that could carry them.
  const fs::path dir = fs::path(PROJECT_DIR) / "src" / "observer";
  for (const auto& item : fs::directory_iterator(dir)) {
    if (!item.is_regular_file()) continue;
    std::ifstream in(item.path());
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
      n++;
      EXPECT_EQ(std::string::npos, line.find("LOG_")) << item.path().filename().string() << ":" << n;
      EXPECT_EQ(std::string::npos, line.find("Serial.")) << item.path().filename().string() << ":" << n;
      // A printf that is not an snprintf into a buffer: nothing here prints.
      for (size_t at = line.find("printf("); at != std::string::npos; at = line.find("printf(", at + 1)) {
        EXPECT_TRUE(at > 0 && line[at - 1] == 'n') << item.path().filename().string() << ":" << n << " prints";
      }
    }
  }
}

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
