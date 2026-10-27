// The contract is the .NET agent's (tasks/phase-1/02): what this firmware POSTs to
// /agent/v1/heartbeat and /agent/v1/observations is held against
// tests/fixtures/contracts/AgentHeartbeat.json, AgentHeartbeatNoWake.json and
// ObservationBatch.json **byte for byte**, for the inputs MeshMaster.Contracts' own round-trip
// tests wrote those fixtures from. A difference is a bug in the firmware, not a second dialect.
//
// Three heartbeat fixtures, because the record has three shapes an exit node really sends: the
// one that proposes a check-in interval and a wake cap, the one that proposes neither - which is
// what this board sends - and, since meshmaster.dk's task p4.17-140 and this repository's task
// p1.7-28, the one that also carries the `observer` object, which is what this board sends once
// a slot of the community's MQTT maps is configured on its console (D-46). The second is a file
// of meshmaster.dk's since its task p4.15-131; before it, this suite held that shape by cutting
// the first file's bytes (task p1.6-24).
//
// ObservationBatch had no fixture until meshmaster.dk's task p4.3-71 wrote one, and this test
// held the batch to the record in src/MeshMaster.Contracts/Envelopes.cs instead. The file is
// here now and the inline expectation is gone: what is left inline is the cases the sample does
// not carry - a direct neighbour, an empty batch, a name from a stranger and a position with six
// decimals west of Greenwich.

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <agent/AgentLoop.h>
#include <agent/Envelopes.h>
#include <agent/JobEnvelopes.h>
#include "version.h"

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

// The thirteen types of API §3, in the order MeshMaster.Contracts' JobTypes.All gives them -
// which is the order the fixture carries. Since task 05 that is the firmware's own list,
// kJobTypes: the heartbeat advertises every type, and this test holds the bytes it sends to the
// fixture. When meshmaster.dk adds a job type the fixture moves, and so must the list.
const char* const* const kAllJobTypes = mm::kJobTypes;

std::string heartbeatJson(const mm::Heartbeat& hb) {
  char out[2048];
  const size_t n = mm::writeHeartbeat(hb, out, sizeof out);
  EXPECT_GT(n, 0u);
  EXPECT_EQ(n, std::strlen(out));
  return std::string(out, n);
}

}  // namespace

// ---- the heartbeat, against the .NET bytes ---------------------------------------------------

// The sample MeshMaster.Contracts' ContractsTests builds the fixture from, field for field. Since
// meshmaster.dk's task p4.14-120 that sample is an agent that **proposes** its own check-in
// interval and a cap for its wake (EXIT-NODES §6), so the two optional fields are set here; the
// test below holds the other half, which is this firmware's: an agent that proposes neither
// writes the fixture's bytes without them.
mm::Heartbeat theFixturesSample(const mm::ErrorCounter* errors, int errorCount) {
  mm::Heartbeat hb;
  hb.agentVersion = "0.3.1";
  hb.transport = "onboard";
  hb.at = 1790172000u;  // 2026-09-23T14:00:00Z
  hb.companionPublicKey = "a1b2";
  hb.companionFirmware = "v1.17.1";
  hb.hasRadio = true;
  hb.radio = {869.618, 62.5, 8, 8};
  hb.hasJobTypes = true;
  hb.jobTypes = kAllJobTypes;
  hb.jobTypeCount = mm::kJobTypeCount;
  hb.hasBudget = true;
  hb.budget.maxTransactionsPerMinute = 2;  // AgentBudget.Default
  hb.budget.minGapSeconds = 20;
  hb.companionConnected = true;
  hb.queueDepth = 0;
  hb.errors = errors;
  hb.errorCount = errorCount;
  hb.board = "heltec_v3";
  return hb;
}

TEST(Heartbeat, TheSameInputMakesTheDotNetFixtureByteForByte) {
  const std::string fixture = readFixture("AgentHeartbeat.json");
  ASSERT_FALSE(fixture.empty()) << "tests/fixtures/contracts/AgentHeartbeat.json is missing - run tools/sync-fixtures.ps1";

  const mm::ErrorCounter errors[] = {{"uplink", 0}};
  mm::Heartbeat hb = theFixturesSample(errors, 1);
  hb.hasNextCheckInS = true;
  hb.nextCheckInS = 3600;
  hb.hasMaxTransactionsThisWake = true;
  hb.maxTransactionsThisWake = 12;

  EXPECT_EQ(fixture, heartbeatJson(hb));
}

// The Heltec proposes nothing: it runs `AgentLoop::loop()` with WiFi up and is back in thirty
// seconds, so neither field is written and its heartbeat is byte for byte what it was before
// meshmaster.dk's task p4.14-120 - which is that task's own promise ("an agent that sends
// neither is byte-for-byte as before") held from this end. Until task p1.6-24 the expectation
// was `AgentHeartbeat.json` **cut** at `,\"nextCheckInS\":`, which was anchored to the record's
// field order and would have said nothing if that order moved; meshmaster.dk's task p4.15-131
// wrote the absent shape as a file of its own, so the expectation is now a whole fixture and
// no test here cuts one.
TEST(Heartbeat, AnAgentThatProposesNothingWritesTheOtherHeartbeatFixture) {
  const std::string noWake = readFixture("AgentHeartbeatNoWake.json");
  ASSERT_FALSE(noWake.empty())
      << "tests/fixtures/contracts/AgentHeartbeatNoWake.json is missing - run tools/sync-fixtures.ps1";

  const mm::ErrorCounter errors[] = {{"uplink", 0}};
  const mm::Heartbeat hb = theFixturesSample(errors, 1);
  EXPECT_FALSE(hb.hasNextCheckInS) << "the default is to propose nothing";
  EXPECT_FALSE(hb.hasMaxTransactionsThisWake);
  EXPECT_EQ(noWake, heartbeatJson(hb));

  // And the two files are the one sample: the same heartbeat, with and without the two wake
  // fields. Held here so that a change to either in meshmaster.dk fails this test rather than
  // quietly leaving the two tests measuring two different agents.
  const std::string withWake = readFixture("AgentHeartbeat.json");
  ASSERT_FALSE(withWake.empty());
  ASSERT_FALSE(noWake.empty());
  EXPECT_EQ(noWake.substr(0, noWake.size() - 1), withWake.substr(0, noWake.size() - 1))
      << "the two heartbeat fixtures are no longer the same sample";
  EXPECT_EQ(withWake.substr(noWake.size() - 1), ",\"nextCheckInS\":3600,\"maxTransactionsThisWake\":12}")
      << "the two wake fields are not what AgentHeartbeat.json adds to it";
}

// One without the other: both are optional members on their own, and a proposal with no cap is
// what a unit on a full battery sends.
TEST(Heartbeat, EitherOfTheTwoWakeFieldsGoesOnItsOwn) {
  const mm::ErrorCounter errors[] = {{"uplink", 0}};
  mm::Heartbeat hb = theFixturesSample(errors, 1);
  hb.hasNextCheckInS = true;
  hb.nextCheckInS = 900;
  const std::string interval = heartbeatJson(hb);
  EXPECT_NE(interval.find("\"board\":\"heltec_v3\",\"nextCheckInS\":900}"), std::string::npos) << interval;
  EXPECT_EQ(interval.find("maxTransactionsThisWake"), std::string::npos) << interval;

  hb.hasNextCheckInS = false;
  hb.hasMaxTransactionsThisWake = true;
  hb.maxTransactionsThisWake = 6;
  const std::string cap = heartbeatJson(hb);
  EXPECT_NE(cap.find("\"board\":\"heltec_v3\",\"maxTransactionsThisWake\":6}"), std::string::npos) << cap;
  EXPECT_EQ(cap.find("nextCheckInS"), std::string::npos) << cap;
}

// ---- the observer (task p1.7-28; meshmaster.dk's D-46 and its task p4.17-140) -----------------

// The two slots of the fixture's own sample, as the provider of an exit node that is also an
// observer fills them: slot 1 connected, slot 2 backing off with the broker's reason.
const char kSlot2LastError[] = "the broker refused the sign-in";

std::array<mm::HeartbeatObserverSlot, 2> theFixturesSlots() {
  std::array<mm::HeartbeatObserverSlot, 2> slots{};
  slots[0].slot = 1;
  slots[0].preset = "meshview";
  slots[0].host = "wss://mqtt.meshview.dk:443/mqtt";
  slots[0].state = "connected";
  slots[0].since = 1790171882u;  // 2026-09-23T13:58:02Z
  slots[0].published = 1420;
  slots[0].dropped = 3;
  slots[1].slot = 2;
  slots[1].preset = "meshmapper";
  slots[1].host = "wss://mqtt.meshmapper.net:443/mqtt";
  slots[1].state = "backing-off";
  slots[1].since = 1790171981u;  // 2026-09-23T13:59:41Z
  slots[1].published = 980;
  slots[1].dropped = 17;
  slots[1].lastError = kSlot2LastError;
  return slots;
}

TEST(Observer, TheSameInputMakesTheDotNetFixtureByteForByte) {
  const std::string fixture = readFixture("AgentHeartbeatObserver.json");
  ASSERT_FALSE(fixture.empty())
      << "tests/fixtures/contracts/AgentHeartbeatObserver.json is missing - run tools/sync-fixtures.ps1";

  const std::array<mm::HeartbeatObserverSlot, 2> slots = theFixturesSlots();
  mm::HeartbeatObserver observer;
  observer.iata = "AAR";
  observer.hasSlots = true;
  observer.slots = slots.data();
  observer.slotCount = static_cast<int>(slots.size());

  const mm::ErrorCounter errors[] = {{"uplink", 0}};
  mm::Heartbeat hb = theFixturesSample(errors, 1);
  hb.hasNextCheckInS = true;
  hb.nextCheckInS = 3600;
  hb.hasMaxTransactionsThisWake = true;
  hb.maxTransactionsThisWake = 12;
  hb.observer = &observer;

  EXPECT_EQ(fixture, heartbeatJson(hb));

  // And the fixtures are one sample: the observer file is AgentHeartbeat.json with the object
  // appended, which is what the fixtures' own README says the nested pairs are. Held here so
  // that a change to either in meshmaster.dk fails this test rather than quietly leaving the
  // two tests measuring two different agents.
  const std::string withWake = readFixture("AgentHeartbeat.json");
  ASSERT_FALSE(withWake.empty());
  EXPECT_EQ(withWake.substr(0, withWake.size() - 1), fixture.substr(0, withWake.size() - 1))
      << "AgentHeartbeatObserver.json is no longer AgentHeartbeat.json plus the observer";

  // The object on its own and the second of its slots are files of their own, and the heartbeat
  // holds both verbatim: the slot is inside the observer and the observer is inside the
  // heartbeat (tests/fixtures/contracts/README.md).
  const std::string object = readFixture("AgentObserver.json");
  const std::string slot = readFixture("AgentObserverSlot.json");
  ASSERT_FALSE(object.empty());
  ASSERT_FALSE(slot.empty());
  EXPECT_NE(std::string::npos, fixture.find(object)) << "the heartbeat does not hold AgentObserver.json";
  EXPECT_NE(std::string::npos, object.find(slot)) << "AgentObserver.json does not hold AgentObserverSlot.json";
}

// The one that matters for every exit node that is not an observer, which is all of them until
// an owner types a preset: **no provider, no member, and the body does not move by a byte**.
TEST(Observer, AnAgentWithNoObserverWritesTheHeartbeatItWroteBefore) {
  const std::string noWake = readFixture("AgentHeartbeatNoWake.json");
  ASSERT_FALSE(noWake.empty());

  const mm::ErrorCounter errors[] = {{"uplink", 0}};
  const mm::Heartbeat hb = theFixturesSample(errors, 1);
  EXPECT_EQ(nullptr, hb.observer) << "the default is to carry no observer";
  const std::string json = heartbeatJson(hb);
  EXPECT_EQ(noWake, json);
  EXPECT_EQ(std::string::npos, json.find("observer"));
}

// A slot that is off names neither preset nor broker and has no clock behind it, so four of the
// eight members are absent - and `published` and `dropped` are not, because the record's are a
// non-nullable `long` and System.Text.Json writes a zero.
TEST(Observer, ASlotThatIsOffCarriesItsStateAndItsTwoCountsAndNothingElse) {
  mm::HeartbeatObserverSlot slots[1];
  slots[0].slot = 2;
  slots[0].state = "off";
  mm::HeartbeatObserver observer;
  observer.hasSlots = true;
  observer.slots = slots;
  observer.slotCount = 1;

  mm::Heartbeat hb;
  hb.agentVersion = "v";
  hb.at = 0;
  hb.hasJobTypes = false;
  hb.hasBudget = false;
  hb.observer = &observer;
  const std::string json = heartbeatJson(hb);
  EXPECT_NE(std::string::npos,
            json.find("\"observer\":{\"slots\":[{\"slot\":2,\"state\":\"off\",\"published\":0,\"dropped\":0}]}}"))
      << json;
  EXPECT_EQ(std::string::npos, json.find("preset"));
  EXPECT_EQ(std::string::npos, json.find("host"));
  EXPECT_EQ(std::string::npos, json.find("since"));
  EXPECT_EQ(std::string::npos, json.find("lastError"));
  EXPECT_EQ(std::string::npos, json.find("iata"));
}

// `slots` absent is not `slots: []`, the distinction every optional list in this file keeps: a
// region and no slot array is a legal object, and so is an empty array.
TEST(Observer, AnEmptySlotListIsNotTheSameAsNoSlotList) {
  mm::HeartbeatObserver observer;
  observer.iata = "CPH";
  mm::Heartbeat hb;
  hb.agentVersion = "v";
  hb.at = 0;
  hb.hasJobTypes = false;
  hb.hasBudget = false;
  hb.observer = &observer;
  EXPECT_NE(std::string::npos, heartbeatJson(hb).find("\"observer\":{\"iata\":\"CPH\"}}"));

  observer.hasSlots = true;
  observer.slotCount = 0;
  EXPECT_NE(std::string::npos, heartbeatJson(hb).find("\"observer\":{\"iata\":\"CPH\",\"slots\":[]}}"));
}

// A broker's refusal is text from somebody else's server, and it goes through the same escaper
// every other string of this writer does - the rail of task p1.7-25, held on the newest field.
TEST(Observer, ABrokersReasonIsEscapedAndNeverBreaksTheBody) {
  mm::HeartbeatObserverSlot slots[1];
  slots[0].slot = 1;
  slots[0].state = "backing-off";
  slots[0].lastError = "it said \"no\"\t\xff";
  mm::HeartbeatObserver observer;
  observer.hasSlots = true;
  observer.slots = slots;
  observer.slotCount = 1;
  mm::Heartbeat hb;
  hb.agentVersion = "v";
  hb.at = 0;
  hb.hasJobTypes = false;
  hb.hasBudget = false;
  hb.observer = &observer;
  EXPECT_NE(std::string::npos, heartbeatJson(hb).find("\"lastError\":\"it said \\\"no\\\"\\t\\uFFFD\""));
}

// The observer's own slot states, as this firmware spells them and as the server's
// `AgentObserverStates` holds them. The contract is the fixture; this is the other half of it -
// the two state words that file carries, so that a state renamed on this board without a word
// to meshmaster.dk fails here rather than on a page.
TEST(Observer, TheFixturesStateWordsAreTheOnesThisFirmwareWrites) {
  const std::string fixture = readFixture("AgentHeartbeatObserver.json");
  ASSERT_FALSE(fixture.empty());
  for (const char* state : {"connected", "backing-off"}) {
    EXPECT_NE(std::string::npos, fixture.find(std::string("\"state\":\"") + state + "\""))
        << "the fixture no longer carries the state word " << state;
  }
  const mm::HeartbeatObserverSlot slot;
  EXPECT_STREQ("off", slot.state) << "the default is the word a slot that is not configured carries";
}

TEST(Heartbeat, WhatThisFirmwareActuallySendsHasNoJobTypesAndFourCounters) {
  const mm::ErrorCounter errors[] = {
      {"overheard", 41}, {"wifiReconnects", 2}, {"httpFailures", 0}, {"observationsDropped", 7}};
  mm::Heartbeat hb;
  hb.agentVersion = "0.0.0-dev";
  hb.at = 1790172000u;
  hb.companionPublicKey = "7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c";
  hb.companionFirmware = "companion-v1.17.1";
  hb.hasRadio = true;
  hb.radio = {869.618, 62.5, 8, 8};
  hb.hasJobTypes = true;
  hb.jobTypeCount = 0;  // nothing can be claimed until task 03
  hb.budget.maxTransactionsPerMinute = 2;
  hb.budget.minGapSeconds = 20;
  hb.companionConnected = true;
  hb.errors = errors;
  hb.errorCount = 4;
  hb.board = "heltec_v3";

  EXPECT_EQ(
      "{\"agentVersion\":\"0.0.0-dev\",\"transport\":\"onboard\",\"at\":\"2026-09-23T14:00:00Z\","
      "\"companionPublicKey\":\"7f3a9c02e1b44d9a0f6c2b7d8e9a1c3f5d7b9e0a2c4e6f8a0b1c2d3e4f5a6b7c\","
      "\"companionFirmware\":\"companion-v1.17.1\",\"radio\":{\"freqMHz\":869.618,\"bwKHz\":62.5,\"sf\":8,\"cr\":8},"
      "\"jobTypes\":[],\"budget\":{\"maxTransactionsPerMinute\":2,\"minGapSeconds\":20},"
      "\"companionConnected\":true,\"queueDepth\":0,"
      "\"errors\":{\"overheard\":41,\"wifiReconnects\":2,\"httpFailures\":0,\"observationsDropped\":7},"
      "\"board\":\"heltec_v3\"}",
      heartbeatJson(hb));
}

TEST(Heartbeat, ANullFieldIsLeftOutAndAnEmptyArrayIsNot) {
  mm::Heartbeat hb;
  hb.agentVersion = "0.0.0-dev";
  hb.at = 1790172000u;
  hb.budget.maxTransactionsPerMinute = 2;
  hb.budget.minGapSeconds = 20;
  // No identity yet: no companion key, no firmware, no radio block, no board, no counters.
  hb.board = nullptr;
  const std::string json = heartbeatJson(hb);
  EXPECT_EQ(std::string::npos, json.find("companionPublicKey"));
  EXPECT_EQ(std::string::npos, json.find("companionFirmware"));
  EXPECT_EQ(std::string::npos, json.find("\"radio\""));
  EXPECT_EQ(std::string::npos, json.find("\"errors\""));
  EXPECT_EQ(std::string::npos, json.find("\"board\""));
  EXPECT_NE(std::string::npos, json.find("\"jobTypes\":[]"));
  EXPECT_NE(std::string::npos, json.find("\"companionConnected\":false"));
}

TEST(Heartbeat, TheFirmwareStringIsTheTwoFieldsTheHeartbeatSends) {
  // `status` prints "meshmaster-agent <agentVersion> on MeshCore <companionFirmware>", and the
  // heartbeat sends the two halves: the bench reads one and the server's page shows the other.
  const std::string composed =
      std::string("meshmaster-agent ") + meshmaster_agent_version() + " on MeshCore " + mm::meshcoreTag();
  EXPECT_EQ(composed, std::string(mm::firmwareString()));
}

// A consumer says what it is (task p1.6-23). The unit of D-45 is transport `native`, and the
// fixture with that one word changed is what it sends; nothing else of the body moves with the
// word. Anchored on the fixture, as the two tests above are.
TEST(Heartbeat, AConsumerThatSaysNativeWritesTheFixtureWithThatOneWordChanged) {
  const std::string fixture = readFixture("AgentHeartbeat.json");
  ASSERT_FALSE(fixture.empty());
  const std::string theirs = "\"transport\":\"onboard\"";
  const size_t at = fixture.find(theirs);
  ASSERT_NE(at, std::string::npos) << "the fixture no longer says onboard: " << fixture;
  const std::string asNative = fixture.substr(0, at) + "\"transport\":\"native\"" + fixture.substr(at + theirs.size());

  const mm::ErrorCounter errors[] = {{"uplink", 0}};
  mm::Heartbeat hb = theFixturesSample(errors, 1);
  hb.transport = "native";
  hb.hasNextCheckInS = true;
  hb.nextCheckInS = 3600;
  hb.hasMaxTransactionsThisWake = true;
  hb.maxTransactionsThisWake = 12;
  EXPECT_EQ(asNative, heartbeatJson(hb));
}

// The word a consumer that says nothing sends is the one the contract fixtures carry - read out
// of every fixture that has a `transport` member, so that the default is held to meshmaster.dk's
// own bytes and not to a word typed here. Two words are in those files since the sync of task
// p1.7-28: `onboard` (AgentHeartbeat.json, AgentConfig.json) and `native`
// (AgentHeartbeatNoKey.json, the Walter's unit-with-no-radio-identity sample). The whole of the
// server's vocabulary is a file of its own now - AgentTransportList.json, `transports`, which
// this test does not read because it is a list and not a message - and the library still holds
// no copy of it and validates nothing: a word the server does not know is its to refuse.
TEST(Heartbeat, TheDefaultTransportIsTheWordTheContractFixturesCarry) {
  std::set<std::string> words;
  for (const auto& entry : std::filesystem::directory_iterator(FIXTURES_DIR)) {
    if (entry.path().extension() != ".json") continue;
    const std::string text = readFixture(entry.path().filename().string().c_str());
    const std::string key = "\"transport\":\"";
    for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + 1)) {
      const size_t from = at + key.size();
      const size_t to = text.find('"', from);
      if (to != std::string::npos) words.insert(text.substr(from, to - from));
    }
  }
  ASSERT_FALSE(words.empty()) << "no contract fixture carries a transport member any more";

  const mm::HeartbeatIdentity identity;  // as a consumer that fills nothing of it leaves it
  EXPECT_EQ(1u, words.count(identity.transport)) << "the identity's default is not a fixture's word: " << identity.transport;
  const mm::Heartbeat hb;
  EXPECT_EQ(1u, words.count(hb.transport)) << "the envelope's default is not a fixture's word: " << hb.transport;
  EXPECT_STREQ(identity.transport, hb.transport) << "the two defaults are one word";
}

TEST(Heartbeat, ABodyThatDoesNotFitIsRefusedRatherThanCut) {
  mm::Heartbeat hb;
  hb.agentVersion = "0.0.0-dev";
  hb.at = 1790172000u;
  char tiny[32];
  EXPECT_EQ(0u, mm::writeHeartbeat(hb, tiny, sizeof tiny));
}

// ---- the numbers ------------------------------------------------------------------------------

TEST(Numbers, ADoubleIsTheShortestDecimalThatReadsBackAsItself) {
  mm::Heartbeat hb;
  hb.agentVersion = "v";
  hb.at = 0;
  hb.hasBudget = false;
  hb.hasJobTypes = false;
  hb.hasRadio = true;
  hb.radio = {869.618, 62.5, 12, 5};
  EXPECT_NE(std::string::npos, heartbeatJson(hb).find("\"freqMHz\":869.618,\"bwKHz\":62.5,\"sf\":12,\"cr\":5"));

  hb.radio = {433.0, 250.0, 7, 8};
  EXPECT_NE(std::string::npos, heartbeatJson(hb).find("\"freqMHz\":433,\"bwKHz\":250"));
}

// ---- the observation batch ---------------------------------------------------------------------

TEST(Observations, TheSameInputMakesTheDotNetFixtureByteForByte) {
  const std::string fixture = readFixture("ObservationBatch.json");
  ASSERT_FALSE(fixture.empty())
      << "tests/fixtures/contracts/ObservationBatch.json is missing - run tools/sync-fixtures.ps1";

  // The sample MeshMaster.Contracts' ContractsTests builds the fixture from, field for field.
  const char* hops[] = {"a1", "7f"};
  mm::ObservationItem item;
  item.at = 1789725492u;  // 2026-09-18T09:58:12Z
  item.kind = "advert";
  item.publicKey = "a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90";
  item.hasPath = true;
  item.path = hops;
  item.pathCount = 2;
  item.hasHops = true;
  item.hops = 2;
  item.hasSnr = true;
  item.snr = 6.25;
  item.hasRssi = true;
  item.rssi = -97;
  item.hasFields = true;
  item.fields.name = "Grenaa Chimney #1";
  item.fields.hasPosition = true;
  item.fields.latE6 = 56410000;
  item.fields.lonE6 = 10880000;
  item.fields.role = "repeater";
  item.fields.hasFlags = true;
  item.fields.flags = 3;

  char out[1024];
  const size_t n = mm::writeObservationBatch("e7c2", &item, 1, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_EQ(fixture, std::string(out, n));

  // The batch holds exactly the advert of Observation.json - the fixtures' own README says the
  // nested pair is one object - so the item written above is that file's bytes too.
  const std::string one = readFixture("Observation.json");
  ASSERT_FALSE(one.empty());
  EXPECT_NE(std::string::npos, fixture.find(one)) << "ObservationBatch.json does not hold Observation.json";
}

TEST(Observations, APositionIsThePlainDecimalOfTheAdvertsMicrodegrees) {
  // The fixture's own position is two decimals and north-east of Greenwich; an advert can carry
  // six and a western longitude, and neither goes through a double on the way out.
  mm::ObservationItem item;
  item.at = 1790172000u;
  item.publicKey = "a1b2c3d4";
  item.hasFields = true;
  item.fields.hasPosition = true;
  item.fields.latE6 = 56416666;
  item.fields.lonE6 = -10876500;
  char out[256];
  const size_t n = mm::writeObservationBatch("obs-1", &item, 1, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_NE(std::string::npos, std::string(out, n).find("\"lat\":56.416666,\"lon\":-10.8765"));
}

TEST(Observations, ADirectNeighbourHasAnEmptyPathAndZeroHops) {
  mm::ObservationItem item;
  item.at = 1790172000u;
  item.publicKey = "a1";
  item.hasPath = true;
  item.pathCount = 0;
  item.hasHops = true;
  item.hops = 0;
  char out[256];
  const size_t n = mm::writeObservationBatch("obs-1", &item, 1, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_NE(std::string::npos, std::string(out, n).find("\"path\":[],\"hops\":0"));
  EXPECT_EQ(std::string::npos, std::string(out, n).find("\"fields\""));
  EXPECT_EQ(std::string::npos, std::string(out, n).find("\"snr\""));
}

TEST(Observations, AnEmptyBatchIsStillAWellFormedBody) {
  char out[64];
  const size_t n = mm::writeObservationBatch("obs-1", nullptr, 0, out, sizeof out);
  EXPECT_EQ("{\"clientId\":\"obs-1\",\"items\":[]}", std::string(out, n));
}

TEST(Observations, ANameFromAStrangerIsEscapedAndNeverBreaksTheBody) {
  mm::ObservationItem item;
  item.at = 0;
  item.publicKey = "a1";
  item.hasFields = true;
  item.fields.name = "he said \"hi\" \\ back";
  char out[256];
  const size_t n = mm::writeObservationBatch("obs-1", &item, 1, out, sizeof out);
  ASSERT_GT(n, 0u);
  EXPECT_NE(std::string::npos,
            std::string(out, n).find("\"name\":\"he said \\\"hi\\\" \\\\ back\""));
}

// ---- what a name is allowed to be ---------------------------------------------------------------

TEST(Names, TheNameIsCutAtTheLastByteThatIsStillUtf8) {
  // Plain ASCII, whole and then bounded.
  EXPECT_EQ(5u, mm::validUtf8PrefixLength("hello", 32));
  EXPECT_EQ(3u, mm::validUtf8PrefixLength("hello", 3));

  // "Grenå" is six bytes: the å is two, and a bound of five must not cut it in half.
  const char* danish = "Gren\xc3\xa5";
  EXPECT_EQ(6u, mm::validUtf8PrefixLength(danish, 32));
  EXPECT_EQ(4u, mm::validUtf8PrefixLength(danish, 5));

  // A lone continuation byte, an overlong encoding and a surrogate are not UTF-8.
  EXPECT_EQ(0u, mm::validUtf8PrefixLength("\x80zz", 32));
  EXPECT_EQ(0u, mm::validUtf8PrefixLength("\xc0\xaf", 32));
  EXPECT_EQ(0u, mm::validUtf8PrefixLength("\xed\xa0\x80", 32));

  // A control character ends it: a name is one line or it is nothing.
  EXPECT_EQ(2u, mm::validUtf8PrefixLength("ab\ncd", 32));
  EXPECT_EQ(0u, mm::validUtf8PrefixLength(nullptr, 32));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
