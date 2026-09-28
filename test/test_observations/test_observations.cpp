// The observation ring, the batch timer and the rate limiter (tasks/phase-1/02).
//
// A board that is its own exit node has no journal: when the server or WiFi is away the ring
// fills and then drops its **oldest** entries, counting every one, and the count is what the
// next heartbeat's `observationsDropped` tells the server. A batch goes at twenty or at sixty
// seconds, whichever comes first. And nothing this firmware sends may break the server's
// 120-a-minute agent policy.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "agent/Observations.h"
#include "agent/RateLimit.h"
#include "agent/ServerPolicy.h"

namespace {

mm::HeardAdvert advert(uint8_t first, uint32_t heardAtMillis) {
  mm::HeardAdvert a;
  a.publicKey[0] = first;
  a.heardAtMillis = heardAtMillis;
  a.advType = 2;  // ADV_TYPE_REPEATER
  return a;
}

}  // namespace

// ---- the ring ---------------------------------------------------------------------------------

TEST(Ring, ItHoldsWhatItIsGivenInTheOrderItWasHeard) {
  mm::ObservationRing ring;
  EXPECT_TRUE(ring.empty());
  for (uint8_t i = 0; i < 5; i++) ring.push(advert(i, i * 1000u));
  ASSERT_EQ(5u, ring.size());
  EXPECT_EQ(0u, ring.dropped());
  EXPECT_EQ(5u, ring.heard());
  for (uint8_t i = 0; i < 5; i++) EXPECT_EQ(i, ring.at(i).publicKey[0]) << "index " << static_cast<int>(i);
}

TEST(Ring, WhenItIsFullTheOldestGoesAndIsCounted) {
  mm::ObservationRing ring;
  const size_t cap = mm::ObservationRing::kCapacity;
  for (size_t i = 0; i < cap; i++) ring.push(advert(static_cast<uint8_t>(i), static_cast<uint32_t>(i)));
  ASSERT_TRUE(ring.full());
  EXPECT_EQ(0u, ring.dropped());

  // Three more: the three oldest are gone, and the newest are at the end.
  for (size_t i = 0; i < 3; i++) ring.push(advert(static_cast<uint8_t>(200 + i), static_cast<uint32_t>(1000 + i)));
  EXPECT_EQ(cap, ring.size());
  EXPECT_EQ(3u, ring.dropped());
  EXPECT_EQ(cap + 3, ring.heard());
  EXPECT_EQ(3, ring.at(0).publicKey[0]) << "the oldest kept is the fourth that was pushed";
  EXPECT_EQ(202, ring.at(cap - 1).publicKey[0]);
}

TEST(Ring, ABatchTheServerTookIsConsumedFromTheOldestEnd) {
  mm::ObservationRing ring;
  for (uint8_t i = 0; i < 30; i++) ring.push(advert(i, i));
  ring.consume(mm::kObservationBatchSize);
  ASSERT_EQ(10u, ring.size());
  EXPECT_EQ(20, ring.at(0).publicKey[0]);
  EXPECT_EQ(0u, ring.dropped()) << "consuming a posted batch is not dropping it";

  ring.consume(1000);  // more than there is: everything, and no wrap
  EXPECT_TRUE(ring.empty());
}

TEST(Ring, TheRolesAreTheWordsTheServerStores) {
  EXPECT_STREQ("companion", mm::roleName(1));
  EXPECT_STREQ("repeater", mm::roleName(2));
  EXPECT_STREQ("roomserver", mm::roleName(3));
  EXPECT_STREQ("sensor", mm::roleName(4));
  EXPECT_STREQ("unknown", mm::roleName(0));
  EXPECT_STREQ("unknown", mm::roleName(9));
  EXPECT_EQ(nullptr, mm::roleName(mm::kAdvTypeUnknown)) << "no advert decoded: the field is left out";
}

// ---- the batch timer ---------------------------------------------------------------------------

TEST(BatchTimer, ItFiresAtTwentyOrAtSixtySecondsAndNotBefore) {
  EXPECT_FALSE(mm::observationBatchDue(0, 10u * 60000u)) << "nothing pending is never due";
  EXPECT_FALSE(mm::observationBatchDue(19, 59999));
  EXPECT_TRUE(mm::observationBatchDue(20, 0)) << "twenty is a batch, however fresh";
  EXPECT_TRUE(mm::observationBatchDue(21, 0));
  EXPECT_TRUE(mm::observationBatchDue(1, 60000)) << "one that has waited a minute is a batch";
  EXPECT_TRUE(mm::observationBatchDue(1, 600000));
  EXPECT_FALSE(mm::observationBatchDue(1, 59999));
  EXPECT_EQ(20u, mm::kObservationBatchSize);
  EXPECT_EQ(60000u, mm::kObservationFlushMs);
}

// ---- the rate limiter ---------------------------------------------------------------------------

TEST(RateLimiter, TheNormalLoopNeverMeetsIt) {
  mm::RateLimiter limiter;
  // A heartbeat every 30 s and a batch every 60 s for an hour: 180 requests, none held.
  uint32_t now = 1000;
  for (int i = 0; i < 120; i++) {
    EXPECT_TRUE(limiter.take(now)) << "at " << now;
    now += 30000;
  }
  EXPECT_EQ(0u, limiter.held());
}

TEST(RateLimiter, TheHundredAndTwentyFirstInAMinuteIsHeldUntilTheWindowMoves) {
  mm::RateLimiter limiter;
  for (size_t i = 0; i < mm::RateLimiter::kMaxPerMinute; i++) ASSERT_TRUE(limiter.take(1000 + i));
  EXPECT_EQ(mm::RateLimiter::kMaxPerMinute, limiter.inWindow(1200));

  EXPECT_FALSE(limiter.take(1200));
  EXPECT_EQ(1u, limiter.held());
  EXPECT_EQ(59800u, limiter.waitMs(1200)) << "the oldest falls out of the window at 61000";

  // One millisecond after the oldest has aged out, one more may go - and one only.
  EXPECT_EQ(0u, limiter.waitMs(61000));
  EXPECT_TRUE(limiter.take(61000));
  EXPECT_FALSE(limiter.take(61000));
}

TEST(RateLimiter, ItSurvivesTheMillisecondCounterWrapping) {
  mm::RateLimiter limiter;
  const uint32_t before = 0xFFFFFF00u;  // 256 ms before millis() wraps
  for (size_t i = 0; i < mm::RateLimiter::kMaxPerMinute; i++) ASSERT_TRUE(limiter.take(before + i));
  EXPECT_FALSE(limiter.take(before + 1000)) << "still inside the minute, across the wrap";
  EXPECT_TRUE(limiter.take(before + 61000)) << "a minute later, wrapped, and the window is clear";
}

// ---- what the server's answer means -------------------------------------------------------------

TEST(ServerPolicy, AFourOhOneHaltsEverything) {
  // "a 401 stops everything but the console and says token revoked or wrong on the OLED"
  // (tasks/phase-1/02). It is the one answer that no amount of waiting improves.
  EXPECT_EQ(mm::ServerAction::Halt, mm::actionFor(true, 401));
}

TEST(ServerPolicy, AFourTwoNineWaitsExactlyWhatRetryAfterSays) {
  EXPECT_EQ(mm::ServerAction::WaitRetryAfter, mm::actionFor(true, 429));
  EXPECT_EQ(17000u, mm::retryAfterMs(17));
  EXPECT_EQ(1000u, mm::retryAfterMs(1));
  // No header, or one this firmware would not parse: a minute, which is the safe way round.
  EXPECT_EQ(60000u, mm::retryAfterMs(0));
  // And bounded, so a server that says "come back next week" does not stop the board for a week.
  EXPECT_EQ(3600000u, mm::retryAfterMs(86400));
}

TEST(ServerPolicy, AFiveHundredBacksOffFromHalfAMinuteToTen) {
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(true, 500));
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(true, 503));
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(false, 0)) << "no reply at all is the same";
  EXPECT_EQ(mm::ServerAction::BackOff, mm::actionFor(true, 408));

  uint32_t wait = 0;
  const uint32_t expected[] = {30000, 60000, 120000, 240000, 480000, 600000, 600000, 600000};
  for (uint32_t want : expected) {
    wait = mm::nextBackoffMs(wait);
    EXPECT_EQ(want, wait);
  }
}

TEST(ServerPolicy, TheAnswersThatMeanTheRequestItselfIsWrong) {
  for (int status : {400, 403, 404, 409, 410, 413, 422}) {
    EXPECT_EQ(mm::ServerAction::Discard, mm::actionFor(true, status)) << "http " << status;
  }
}

TEST(ServerPolicy, ATwoHundredIsSimplyOk) {
  EXPECT_EQ(mm::ServerAction::Ok, mm::actionFor(true, 200));
  EXPECT_EQ(mm::ServerAction::Ok, mm::actionFor(true, 204));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
