// The pace and the price.
//
// The **price** is `TransactionCosts.For` of src/MeshMaster.Core/Transport/PlannedLoad.cs - the
// table of EXIT-NODES §7 as code, and what the server's planned load charges a schedule with
// before the operator is allowed to save it. If this board spent a different number of
// transactions on the same job, the page's promise would be a guess.
//
// The **pace** is `Throttle` of src/MeshMaster.Agent/Budget.cs: one transaction at a time, at
// least `minGapSeconds` between two starts, at most `maxTransactionsPerMinute` in any sixty
// seconds, and a burst of claimed work executed at that pace rather than refused.
//
// The session cache beside them is what makes a claimed status+telemetry pair cost one login.

#include <gtest/gtest.h>

#include <agent/Airtime.h>
#include <agent/Sessions.h>

namespace {

// ------------------------------------------------------------------ the price

TEST(TransactionCosts, TheFourTypesCostWhatThePlannedLoadPricesThem) {
  // status: one request, and two at class acl - the firmware version comes from a second,
  // owner-info request, and the association's verify step records the version it was proved on.
  EXPECT_EQ(mm::transactionsFor("status", mm::CredentialClass::Guest), 1);
  EXPECT_EQ(mm::transactionsFor("status", mm::CredentialClass::Admin), 1);
  EXPECT_EQ(mm::transactionsFor("status", mm::CredentialClass::Acl), 2);

  EXPECT_EQ(mm::transactionsFor("telemetry", mm::CredentialClass::Acl), 1);
  EXPECT_EQ(mm::transactionsFor("trace", mm::CredentialClass::None), 1);

  // neighbours: the firmware answers a page at a time, so a well-connected repeater costs more
  // than a lonely one. Unknown is the one page every reply has.
  EXPECT_EQ(mm::transactionsFor("neighbours", mm::CredentialClass::Acl), 1);
  EXPECT_EQ(mm::transactionsFor("neighbours", mm::CredentialClass::Acl, 0), 1);
  EXPECT_EQ(mm::transactionsFor("neighbours", mm::CredentialClass::Acl, 9), 1);
  EXPECT_EQ(mm::transactionsFor("neighbours", mm::CredentialClass::Acl, 10), 2);
  EXPECT_EQ(mm::transactionsFor("neighbours", mm::CredentialClass::Acl, 18), 2);
  EXPECT_EQ(mm::transactionsFor("neighbours", mm::CredentialClass::Acl, 19), 3);
}

// MONITORING §2's own arithmetic, in the guidance printed beside the schedule fields: a
// status+telemetry pair is one login and two requests - three transactions through a guest
// fallback pair, four through an acl association, where the status read fetches the firmware
// version as well.
// The writes (tasks p4-53, 54, 55): an apply is n + m + 1 with m = n, two commands being five
// transactions and a login; every other write is one command, one reply, sent once.
TEST(TransactionCosts, TheWritesCostWhatThePlannedLoadPricesThem) {
  EXPECT_EQ(mm::transactionsFor("config.apply", mm::CredentialClass::Admin, 2), 5);
  EXPECT_EQ(mm::transactionsFor("config.apply", mm::CredentialClass::Admin, 3), 7);
  EXPECT_EQ(mm::transactionsFor("config.apply", mm::CredentialClass::Admin, 1), 3);
  EXPECT_EQ(mm::transactionsFor("config.apply", mm::CredentialClass::Admin), 3) << "an unknown plan is priced as one command";
  for (const char* type : {"cli", "advert.request", "clock.sync", "password.set", "reboot"}) {
    EXPECT_EQ(mm::transactionsFor(type, mm::CredentialClass::Admin), 1) << type;
  }
}

TEST(TransactionCosts, TheStatusAndTelemetryPairCostsThreeOrFour) {
  const int guestPair = mm::kTransactionsForLogin + mm::transactionsFor("status", mm::CredentialClass::Guest) +
                        mm::transactionsFor("telemetry", mm::CredentialClass::Guest);
  const int aclPair = mm::kTransactionsForLogin + mm::transactionsFor("status", mm::CredentialClass::Acl) +
                      mm::transactionsFor("telemetry", mm::CredentialClass::Acl);
  EXPECT_EQ(guestPair, 3);
  EXPECT_EQ(aclPair, 4);
}

TEST(TransactionCosts, ATraceNeedsNoLoginAtAll) {
  // "trace | none | 1" in EXIT-NODES §7's table: the firmware answers a trace to anybody.
  EXPECT_EQ(mm::roleNeededFor(mm::CredentialClass::None), mm::kPermGuest);
  EXPECT_EQ(mm::transactionsFor("trace", mm::CredentialClass::None), 1);
}

// ------------------------------------------------------------------ the pace

mm::AirtimeGate gateWith(int perMinute, int gapSeconds) {
  mm::AirtimeGate gate;
  mm::AgentBudget budget;
  budget.maxTransactionsPerMinute = perMinute;
  budget.minGapSeconds = gapSeconds;
  gate.setBudget(budget);
  return gate;
}

TEST(AirtimeGate, TheFirstTransactionGoesAtOnce) {
  mm::AirtimeGate gate = gateWith(2, 20);
  EXPECT_EQ(gate.waitMs(0), 0u);
  EXPECT_EQ(gate.waitMs(1234567), 0u);
}

TEST(AirtimeGate, TheGapIsHeldBetweenTwoStarts) {
  mm::AirtimeGate gate = gateWith(120, 20);  // a ceiling far away, so only the gap can bite
  gate.took(1000);
  EXPECT_EQ(gate.waitMs(1000), 20000u);
  EXPECT_EQ(gate.waitMs(15000), 6000u);
  EXPECT_EQ(gate.waitMs(21000), 0u);
  EXPECT_EQ(gate.waitMs(999999), 0u);
}

TEST(AirtimeGate, TheCeilingHoldsTheThirdOfTwoAMinuteUntilTheWindowMoves) {
  mm::AirtimeGate gate = gateWith(2, 0);  // no gap, so only the ceiling can bite
  gate.took(0);
  gate.took(30000);
  // Two in the window: the next may go when the oldest is a minute old.
  EXPECT_EQ(gate.waitMs(30000), 30000u);
  EXPECT_EQ(gate.waitMs(59999), 1u);
  EXPECT_EQ(gate.waitMs(60000), 0u);
  EXPECT_EQ(gate.inWindow(60000), 1u) << "the first has left the minute";
}

// Five queued reads at a budget of one a minute take five minutes and never a burst - the .NET
// agent's ThrottleTests, arithmetic for arithmetic.
TEST(AirtimeGate, FiveQueuedReadsAreRunAtTheBudgetsPaceAndNeverInABurst) {
  mm::AirtimeGate gate = gateWith(1, 20);
  uint32_t now = 0;
  uint32_t waited = 0;
  for (int i = 0; i < 5; i++) {
    const uint32_t wait = gate.waitMs(now);
    now += wait;
    waited += wait;
    gate.took(now);
  }
  EXPECT_EQ(gate.transmissions(), 5u);
  EXPECT_EQ(waited, 4u * 60000u) << "four minutes of waiting for the last four of five";
  EXPECT_EQ(now, 240000u);
}

TEST(AirtimeGate, TheLongerOfTheGapAndTheWindowIsWhatIsWaited) {
  mm::AirtimeGate gate = gateWith(2, 45);
  gate.took(0);
  EXPECT_EQ(gate.waitMs(0), 45000u) << "only the gap has anything to say about the second";
  gate.took(45000);
  // Two in the minute now. The window would admit the third at 60 000 (a minute after the
  // first) and the gap not until 90 000 (45 s after the second): the longer of the two is what
  // is waited, which is the gap.
  EXPECT_EQ(gate.waitMs(45000), 45000u);
  EXPECT_EQ(gate.waitMs(80000), 10000u);
  EXPECT_EQ(gate.waitMs(90000), 0u);
}

TEST(AirtimeGate, ItSurvivesTheMillisecondCounterWrapping) {
  mm::AirtimeGate gate = gateWith(2, 20);
  const uint32_t nearTheEnd = 0xFFFFF000u;
  gate.took(nearTheEnd);
  EXPECT_EQ(gate.waitMs(nearTheEnd + 1000), 19000u);
  EXPECT_EQ(gate.waitMs(nearTheEnd + 20000), 0u) << "the sum wrapped and the arithmetic still holds";
}

TEST(AirtimeGate, ABudgetOfZeroIsReadAsOneRatherThanAsNoTrafficAtAll) {
  mm::AirtimeGate gate = gateWith(0, 0);
  gate.took(0);
  EXPECT_EQ(gate.waitMs(0), 60000u);
  EXPECT_EQ(gate.waitMs(60000), 0u);
}

TEST(AirtimeGate, TheDefaultBudgetIsTheConservativeOneOfSectionEight) {
  mm::AirtimeGate gate;
  EXPECT_EQ(gate.budget().maxTransactionsPerMinute, 2);
  EXPECT_EQ(gate.budget().minGapSeconds, 20);
}

// ------------------------------------------------------------------ the sessions

TEST(Sessions, AReusedSessionIsWhatMakesAClaimedPairCostOneLogin) {
  mm::SessionStore sessions;
  const char* key = "a1b2";
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::Acl, 0), nullptr);

  ASSERT_NE(sessions.put(key, mm::CredentialClass::Acl, mm::kPermReadOnly, 1000), nullptr);
  const mm::RepeaterSession* reused = sessions.get(key, mm::CredentialClass::Acl, 2000);
  ASSERT_NE(reused, nullptr);
  EXPECT_STREQ(reused->tier(), "read-only");
  EXPECT_EQ(sessions.live(2000), 1u);
}

TEST(Sessions, ASessionIsNeverHeldAboveItsLevel) {
  mm::SessionStore sessions;
  const char* key = "a1b2";
  // A blank login answered at guest for a job that asked for acl: stored at guest, which is what
  // it can carry, and invisible to the class that asked.
  const mm::RepeaterSession* stored = sessions.put(key, mm::CredentialClass::Acl, mm::kPermGuest, 0);
  ASSERT_NE(stored, nullptr);
  EXPECT_EQ(stored->credentialClass, mm::CredentialClass::Guest);
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::Acl, 0), nullptr);
  EXPECT_NE(sessions.get(key, mm::CredentialClass::Guest, 0), nullptr);
  // A session serves its own class and no other, unless it is admin: the .NET store's
  // `session.Class == wanted || session.IsAdmin`. A `trace` runs at class none and asks for no
  // session at all, so nothing is lost by the guest session being invisible to it.
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::None, 0), nullptr);
}

TEST(Sessions, AnAdminSessionServesEveryClassAndAnAclOneServesItsOwn) {
  mm::SessionStore sessions;
  sessions.put("admin", mm::CredentialClass::Admin, mm::kPermAdmin, 0);
  EXPECT_NE(sessions.get("admin", mm::CredentialClass::Admin, 0), nullptr);
  EXPECT_NE(sessions.get("admin", mm::CredentialClass::Acl, 0), nullptr);
  EXPECT_NE(sessions.get("admin", mm::CredentialClass::Guest, 0), nullptr);

  sessions.put("acl", mm::CredentialClass::Acl, mm::kPermReadWrite, 0);
  EXPECT_NE(sessions.get("acl", mm::CredentialClass::Acl, 0), nullptr);
  EXPECT_EQ(sessions.get("acl", mm::CredentialClass::Admin, 0), nullptr);
}

TEST(Sessions, ASessionExpiresAndIsGoneRatherThanStale) {
  mm::SessionStore sessions;
  sessions.put("a1b2", mm::CredentialClass::Acl, mm::kPermReadOnly, 0);
  EXPECT_NE(sessions.get("a1b2", mm::CredentialClass::Acl, mm::SessionStore::kLifetimeMs - 1), nullptr);
  EXPECT_EQ(sessions.get("a1b2", mm::CredentialClass::Acl, mm::SessionStore::kLifetimeMs), nullptr);
  EXPECT_EQ(sessions.live(mm::SessionStore::kLifetimeMs), 0u);
}

TEST(Sessions, ADroppedSessionIsGoneAtOnce) {
  mm::SessionStore sessions;
  sessions.put("a1b2", mm::CredentialClass::Acl, mm::kPermReadOnly, 0);
  sessions.invalidate("a1b2");
  EXPECT_EQ(sessions.get("a1b2", mm::CredentialClass::Acl, 0), nullptr);
}

// The old shape the field printed (`session reused (Admin, read-only, 92 s old)`, task
// p3.5-41): an admin-password login answered read-only, cached under the class that asked.
// Seeded that way, the store refuses it to an admin job and to an acl job, and offers it to a
// guest one - so a below-class session can never be handed to the class that asked for it.
TEST(Sessions, AReadOnlySessionPutUnderAdminIsRefusedToAnAdminJobAndOfferedToAGuestOne) {
  mm::SessionStore sessions;
  const char* key = "b2b2";
  const mm::RepeaterSession* stored = sessions.put(key, mm::CredentialClass::Admin, mm::kPermReadOnly, 0);
  ASSERT_NE(stored, nullptr);
  EXPECT_EQ(stored->credentialClass, mm::CredentialClass::Guest);
  EXPECT_STREQ(stored->tier(), "read-only");
  EXPECT_FALSE(stored->isAdmin());
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::Admin, 0), nullptr);
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::Acl, 0), nullptr);
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::Guest, 0), stored);
  EXPECT_EQ(sessions.live(0), 1u);
}

// An acl.set on our own key is the session's level (task p4.1-60): after a grant the admin
// session is a read-only one, reusable by the next acl-class job and invisible to an admin-class
// one; after a revoke it is gone. The restore's put is the same call.
TEST(Sessions, AnAclSetOfOurOwnKeyDropsTheAdminSessionToTheLevelSetAndARevokeDropsIt) {
  mm::SessionStore sessions;
  const char* key = "ef15";
  sessions.put(key, mm::CredentialClass::Admin, mm::kPermAdmin, 0);
  ASSERT_NE(sessions.get(key, mm::CredentialClass::Admin, 0), nullptr);

  // The grant: `setperm <own key> 1` confirmed - or the restore at the end of an admin session.
  const mm::RepeaterSession* after = sessions.put(key, mm::CredentialClass::Acl, mm::kPermReadOnly, 1000);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(after->credentialClass, mm::CredentialClass::Acl);
  EXPECT_STREQ(after->tier(), "read-only");
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::Admin, 1000), nullptr) << "no admin session survives its own setperm";
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::Acl, 1000), after);
  EXPECT_EQ(sessions.live(1000), 1u) << "one session per repeater: the put replaced, not added";

  // The revoke: `setperm <own key> 0` confirmed.
  sessions.invalidate(key);
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::Acl, 1000), nullptr);
  EXPECT_EQ(sessions.get(key, mm::CredentialClass::Guest, 1000), nullptr);
  EXPECT_EQ(sessions.live(1000), 0u);
}

TEST(Sessions, TheSlotsAreReadableForTheConsoleAndHoldNoSecret) {
  mm::SessionStore sessions;
  sessions.put("c3d4", mm::CredentialClass::Acl, mm::kPermReadOnly, 5000);
  int live = 0;
  for (size_t i = 0; i < mm::SessionStore::kMaxSessions; i++) {
    const mm::RepeaterSession* s = sessions.slot(i, 6000);
    if (s == nullptr) continue;
    live++;
    EXPECT_STREQ(s->key, "c3d4");
    EXPECT_EQ(s->loggedInAtMs, 5000u);
    EXPECT_EQ(s->credentialClass, mm::CredentialClass::Acl);
    EXPECT_STREQ(s->tier(), "read-only");
    // What a session is: a key, a class, a role byte and a time, and nothing else - the struct
    // is no bigger than those four fields and their padding.
    EXPECT_LE(sizeof(mm::RepeaterSession), sizeof(s->key) + sizeof(s->credentialClass) + sizeof(s->permissions) +
                                               sizeof(s->loggedInAtMs) + 3 /* alignment */);
  }
  EXPECT_EQ(live, 1);
  EXPECT_EQ(sessions.slot(0, 5000 + mm::SessionStore::kLifetimeMs), nullptr) << "an expired slot is not shown";
  EXPECT_EQ(sessions.slot(mm::SessionStore::kMaxSessions, 6000), nullptr);
}

TEST(Sessions, FourRepeatersFitAndTheFifthTakesAnExpiredSlot) {
  mm::SessionStore sessions;
  const char* keys[] = {"one", "two", "three", "four"};
  for (const char* k : keys) sessions.put(k, mm::CredentialClass::Acl, mm::kPermReadOnly, 0);
  EXPECT_EQ(sessions.live(0), mm::SessionStore::kMaxSessions);

  const uint32_t later = mm::SessionStore::kLifetimeMs + 1;
  sessions.put("five", mm::CredentialClass::Acl, mm::kPermReadOnly, later);
  EXPECT_NE(sessions.get("five", mm::CredentialClass::Acl, later), nullptr);
  EXPECT_EQ(sessions.live(later), 1u) << "the four before it had expired";
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
