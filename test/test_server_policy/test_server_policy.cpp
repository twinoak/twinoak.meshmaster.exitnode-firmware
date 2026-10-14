// What one answer from meshmaster.dk means for the loop (agent/ServerPolicy.h).
//
// The policy is a pure function of the status code and the Retry-After beside it, so every
// answer the server can give is held here against the action it earns and the wait that action
// carries - without a board, and without a server. The cases that were in test_observations
// before task p1.2-10 are the first four; the rest are that task's own, from the first day on
// the air: a release of meshmaster.dk answers 503 for a minute or two, and the board waited it
// out eight times over (bench, 3 October 2026).

#include <gtest/gtest.h>

#include <vector>

#include <agent/Backoff.h>
#include <agent/ServerPolicy.h>

namespace {

// The sequence a run of failures walks, with the ceiling `linkProven` chooses.
std::vector<uint32_t> sequence(bool linkProven, int steps) {
  std::vector<uint32_t> out;
  uint32_t ms = 0;
  for (int i = 0; i < steps; i++) {
    ms = mm::nextBackoffMs(ms, linkProven);
    out.push_back(ms);
  }
  return out;
}

}  // namespace

TEST(ServerPolicy, AFourOhOneHaltsEverything) {
  // "a 401 stops everything but the console and says token revoked or wrong on the OLED"
  // (tasks/phase-1/02). It is the one answer that no amount of waiting improves, with a
  // Retry-After or without one.
  EXPECT_EQ(mm::ServerAction::Halt, mm::actionFor(true, 401, 0));
  EXPECT_EQ(mm::ServerAction::Halt, mm::actionFor(true, 401, 30));
}

TEST(ServerPolicy, AFourTwoNineWaitsExactlyWhatRetryAfterSays) {
  EXPECT_EQ(mm::ServerAction::WaitRetryAfter, mm::actionFor(true, 429, 17));
  EXPECT_EQ(17000u, mm::retryAfterMs(17));
  EXPECT_EQ(1000u, mm::retryAfterMs(1));
  // No header, or one this firmware would not parse: a minute, which is the safe way round -
  // and a 429 is still a 429 without it, because the server is rate-limiting either way.
  EXPECT_EQ(mm::ServerAction::WaitRetryAfter, mm::actionFor(true, 429, 0));
  EXPECT_EQ(60000u, mm::retryAfterMs(0));
  // And bounded, so a server that says "come back next week" does not stop the board for a week.
  EXPECT_EQ(3600000u, mm::retryAfterMs(86400));
  EXPECT_EQ(mm::kMaxRetryAfterMs, mm::retryAfterMs(86400));
}

TEST(ServerPolicy, TheAnswersThatMeanTheRequestItselfIsWrong) {
  for (int status : {400, 403, 404, 409, 410, 413, 422}) {
    EXPECT_EQ(mm::ServerAction::Discard, mm::actionFor(true, status, 0)) << "http " << status;
    // A Retry-After on one of these changes nothing: there is nothing to come back for.
    EXPECT_EQ(mm::ServerAction::Discard, mm::actionFor(true, status, 10)) << "http " << status;
  }
}

TEST(ServerPolicy, ATwoHundredIsSimplyOk) {
  EXPECT_EQ(mm::ServerAction::Ok, mm::actionFor(true, 200, 0));
  EXPECT_EQ(mm::ServerAction::Ok, mm::actionFor(true, 204, 0));
}

// ---- task p1.2-10: the deploy ------------------------------------------------------------------

TEST(ServerPolicy, AFiveHundredBacksOffFromHalfAMinute) {
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(true, 500, 0));
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(true, 503, 0));
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(false, 0, 0)) << "no reply at all is the same";
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(true, 408, 0));
  // A reply that never arrived carries no header, but a stale `retryAfterS` must not be read as
  // one: `reachable` false is decided before any status or header is looked at.
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(false, 503, 90));
  // A 500 is a bug and not a schedule: its Retry-After, if a server ever sent one, is not
  // believed, and the doubling is the answer.
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(true, 500, 90));
  EXPECT_FALSE(mm::saysWhenToComeBack(500));
}

TEST(ServerPolicy, ADeploysFiveOhThreeIsBelievedWhenItSaysHowLongItWillBe) {
  // The deploy of 3 October: five calls answered 503 while the new build started, and the
  // doubling then outlasted the outage by a quarter of an hour. A 503 that says 90 s holds for
  // 90 s and nothing doubles.
  for (int status : {502, 503, 504}) {
    EXPECT_TRUE(mm::saysWhenToComeBack(status)) << "http " << status;
    EXPECT_EQ(mm::ServerAction::WaitRetryAfter, mm::actionFor(true, status, 90)) << "http " << status;
    EXPECT_EQ(90000u, mm::retryAfterMs(90));
    // And without one there is nothing to honour: the doubling is still the answer.
    EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(true, status, 0)) << "http " << status;
  }
  // The same bound as the 429's: a maintenance window of a day does not stop the board for a day.
  EXPECT_EQ(mm::kMaxRetryAfterMs, mm::retryAfterMs(86400));
}

TEST(ServerPolicy, TheCeilingIsTwoMinutesOnceTheBoardsOwnLinkIsProven) {
  // WiFi up and a time off the network: the thing that is not answering is the server, and a
  // server that is back deserves a call within two minutes rather than within ten.
  EXPECT_EQ(mm::kMaxBackoffWhenLinkUpMs, mm::maxBackoffMs(true));
  EXPECT_EQ(120000u, mm::kMaxBackoffWhenLinkUpMs);
  const std::vector<uint32_t> proven = {30000, 60000, 120000, 120000, 120000, 120000};
  EXPECT_EQ(proven, sequence(true, 6));

  // The five failures of the deploy, at the old ceiling and at this one: 480 s against 120 s.
  EXPECT_EQ(480000u, sequence(false, 5).back());
  EXPECT_EQ(120000u, sequence(true, 5).back());
}

TEST(ServerPolicy, TheCeilingIsStillTenMinutesForABoardWhoseLinkIsNotProven) {
  // A WiFi that associates and goes nowhere, a captive portal, a DNS that answers nothing: the
  // board's own link is the suspect and the long wait it always had is right.
  EXPECT_EQ(mm::kMaxBackoffMs, mm::maxBackoffMs(false));
  EXPECT_EQ(600000u, mm::kMaxBackoffMs);
  const std::vector<uint32_t> unproven = {30000, 60000, 120000, 240000, 480000, 600000, 600000, 600000};
  EXPECT_EQ(unproven, sequence(false, 8));
}

TEST(ServerPolicy, TheCeilingFollowsTheLinkMidSequence) {
  // A board deep in the sequence whose link comes good drops to the lower ceiling at its next
  // failure rather than keeping the step it had reached; one that loses the link climbs again.
  uint32_t ms = 0;
  for (int i = 0; i < 6; i++) ms = mm::nextBackoffMs(ms, false);
  EXPECT_EQ(600000u, ms);
  ms = mm::nextBackoffMs(ms, true);
  EXPECT_EQ(120000u, ms) << "SNTP answered: the server is the only suspect left";
  ms = mm::nextBackoffMs(ms, false);
  EXPECT_EQ(240000u, ms) << "and the link went away again";
}

TEST(ServerPolicy, TheBackoffIsTheSharedSequenceAtTheServersNumbers) {
  // The two-argument form the agent loop calls is agent/Backoff.h's three-argument one at the
  // server's own numbers, so the two can never drift apart.
  uint32_t a = 0;
  uint32_t b = 0;
  for (int i = 0; i < 10; i++) {
    a = mm::nextBackoffMs(a, false);
    b = mm::nextBackoffMs(b, mm::kFirstBackoffMs, mm::kMaxBackoffMs);
    EXPECT_EQ(a, b) << "step " << i;
  }
  EXPECT_EQ(mm::kMaxBackoffMs, a);

  uint32_t c = 0;
  uint32_t d = 0;
  for (int i = 0; i < 10; i++) {
    c = mm::nextBackoffMs(c, true);
    d = mm::nextBackoffMs(d, mm::kFirstBackoffMs, mm::kMaxBackoffWhenLinkUpMs);
    EXPECT_EQ(c, d) << "step " << i;
  }
  EXPECT_EQ(mm::kMaxBackoffWhenLinkUpMs, c);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
