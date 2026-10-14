// What a board does while the server is away, and how it comes back (tasks/phase-1/06).
//
// Four things, none of which needs a board:
//
//   the result buffer   Job results the server did not take are kept in RAM, bounded in bytes
//                       and in count, the **oldest** dropped and counted, and posted later in
//                       the order they were made with the client ids they were written with -
//                       which is what makes a late result safe (EXIT-NODES §5, API §3).
//   the lease           A job whose lease ran out before it started is not put on the air: the
//                       server has given it back to the queue, and a transaction for a lease
//                       somebody else holds is worse than no transaction.
//   the backoff         The sequence both retries of this firmware share, and the jitter on the
//                       wait, so that a hundred boards that lost the same server do not come
//                       back at the same second.
//   the log ring        The four kilobytes `dump` prints: bounded, oldest overwritten, and
//                       never a half line at the front.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <agent/Backoff.h>
#include <agent/LogRing.h>
#include <agent/JobEnvelopes.h>
#include <agent/ResultBuffer.h>
#include <agent/ServerPolicy.h>

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

// A result body of `len` bytes, filled with one recognisable character, the way a real one is
// recognisable by its client id.
std::string body(char fill, size_t len) { return std::string(len, fill); }

std::string oldestOf(const mm::ResultBuffer& b, long long& jobId) {
  size_t len = 0;
  const char* text = b.oldest(jobId, len);
  return text == nullptr ? std::string() : std::string(text, len);
}

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// ---- the result buffer ------------------------------------------------------------------------

TEST(ResultBuffer, ItKeepsWhatTheServerWouldNotTakeInTheOrderItWasMade) {
  mm::ResultBuffer b;
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(0u, b.count());

  ASSERT_TRUE(b.push(41, "{\"clientId\":\"41-a\"}", 19));
  ASSERT_TRUE(b.push(42, "{\"clientId\":\"42-b\"}", 19));
  EXPECT_EQ(2u, b.count());
  EXPECT_EQ(38u, b.bytes());

  long long id = 0;
  EXPECT_EQ("{\"clientId\":\"41-a\"}", oldestOf(b, id));
  EXPECT_EQ(41, id);
  b.popOldest();
  EXPECT_EQ("{\"clientId\":\"42-b\"}", oldestOf(b, id));
  EXPECT_EQ(42, id);
  b.popOldest();
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(0u, b.dropped()) << "nothing was dropped: both went through";
  EXPECT_EQ(2u, b.held());
}

// The late result: what was written while the server was away is what is posted when it comes
// back - the same bytes, with the same client id, which is what the server de-duplicates on.
TEST(ResultBuffer, ALateResultIsPostedWithTheBytesItWasWrittenWith) {
  mm::ResultBuffer b;
  const std::string written = "{\"clientId\":\"12345-deadbeef\",\"outcome\":\"ok\",\"startedAt\":\"2026-09-29T09:00:00Z\"}";
  ASSERT_TRUE(b.push(12345, written.c_str(), written.size()));

  // ... an hour of the server being away, and three heartbeats that did not answer. The buffer
  // is not touched by any of that: it is a queue, not a cache.
  long long id = 0;
  EXPECT_EQ(written, oldestOf(b, id)) << "the bytes are the ones the job wrote, not a re-run's";
  EXPECT_EQ(12345, id);
  b.popOldest();
  EXPECT_TRUE(b.empty());
}

TEST(ResultBuffer, WhenItIsFullTheOldestGoesAndIsCounted) {
  mm::ResultBuffer b;
  for (size_t i = 0; i < mm::ResultBuffer::kMaxResults; i++) {
    ASSERT_TRUE(b.push(static_cast<long long>(i), body(static_cast<char>('a' + i), 64).c_str(), 64));
  }
  EXPECT_EQ(mm::ResultBuffer::kMaxResults, b.count());
  EXPECT_EQ(0u, b.dropped());

  // One more than it holds: the oldest goes, the count stays at the bound, and the drop is
  // counted - which is what the heartbeat's `resultsDropped` reports.
  ASSERT_TRUE(b.push(99, body('z', 64).c_str(), 64));
  EXPECT_EQ(mm::ResultBuffer::kMaxResults, b.count());
  EXPECT_EQ(1u, b.dropped());

  long long id = 0;
  EXPECT_EQ(body('b', 64), oldestOf(b, id)) << "the oldest is gone and the second is now first";
  EXPECT_EQ(1, id);
}

TEST(ResultBuffer, TheByteBoundIsTheOtherOneAndItDropsFromTheOldestEndToo) {
  mm::ResultBuffer b;
  // Two results of a third of the arena each fit; a third one of the same size does not, and
  // makes room by dropping the oldest - even though the count is nowhere near its bound.
  const size_t big = mm::ResultBuffer::kCapacityBytes / 3 + 1;
  ASSERT_TRUE(b.push(1, body('1', big).c_str(), big));
  ASSERT_TRUE(b.push(2, body('2', big).c_str(), big));
  EXPECT_EQ(2u, b.count());
  EXPECT_EQ(0u, b.dropped());

  ASSERT_TRUE(b.push(3, body('3', big).c_str(), big));
  EXPECT_EQ(2u, b.count()) << "the count is under its bound; it was the bytes that ran out";
  EXPECT_EQ(1u, b.dropped());
  long long id = 0;
  EXPECT_EQ(body('2', big), oldestOf(b, id));
  EXPECT_EQ(2, id);
  EXPECT_LE(b.bytes(), mm::ResultBuffer::kCapacityBytes);
}

TEST(ResultBuffer, AResultLargerThanTheWholeBufferIsRefusedAndKeepsTheRest) {
  mm::ResultBuffer b;
  ASSERT_TRUE(b.push(7, "{\"clientId\":\"7-x\"}", 18));
  const std::string huge = body('!', mm::ResultBuffer::kCapacityBytes + 1);
  EXPECT_FALSE(b.push(8, huge.c_str(), huge.size()));
  EXPECT_EQ(1u, b.dropped()) << "the refusal is a drop and is counted like one";
  long long id = 0;
  EXPECT_EQ("{\"clientId\":\"7-x\"}", oldestOf(b, id)) << "dropping everything for one result would be worse";
  EXPECT_EQ(7, id);
}

TEST(ResultBuffer, ItSurvivesBeingDrainedAndFilledAgain) {
  mm::ResultBuffer b;
  for (int round = 0; round < 4; round++) {
    for (int i = 0; i < 5; i++) {
      const std::string text = body(static_cast<char>('a' + i), 1000);
      ASSERT_TRUE(b.push(round * 10 + i, text.c_str(), text.size())) << "round " << round << " item " << i;
    }
    while (!b.empty()) b.popOldest();
    EXPECT_EQ(0u, b.bytes());
  }
  EXPECT_EQ(0u, b.dropped());
  EXPECT_EQ(20u, b.held());
}

// ---- the lease --------------------------------------------------------------------------------

TEST(Lease, AJobWhoseLeaseRanOutIsNotRun) {
  const uint32_t now = 1790000000u;
  EXPECT_FALSE(mm::leaseLapsed(now + 60, true, now)) << "a minute of lease left";
  EXPECT_TRUE(mm::leaseLapsed(now - 1, true, now)) << "a second past it";
  EXPECT_TRUE(mm::leaseLapsed(now, true, now)) << "the lease is until, not through";

  // A claim of two jobs whose second one is reached forty minutes later, because the first was
  // a config.read at two transactions a minute: the second is past its lease and is not sent.
  const uint32_t leasedUntil = now + 40 * 60;
  EXPECT_FALSE(mm::leaseLapsed(leasedUntil, true, now));
  EXPECT_TRUE(mm::leaseLapsed(leasedUntil, true, now + 41 * 60));
}

TEST(Lease, NoClockAndNoLeaseAreBothNotLapsed) {
  const uint32_t now = 1790000000u;
  EXPECT_FALSE(mm::leaseLapsed(0, true, now)) << "the claim named no lease";
  EXPECT_FALSE(mm::leaseLapsed(now - 10000, false, now))
      << "a board with no clock cannot tell, and refusing every job would be worse";
  EXPECT_FALSE(mm::leaseLapsed(0, false, 0));
}

// A host cannot run JobRunner, so - as tasks 04 and 05 held the wipe and the single send - this
// holds the one path every job takes: the lease is read before anything else runJob does, and
// what it returns is a timeout at no transaction.
TEST(Lease, TheRunnerReadsTheLeaseBeforeItRunsAJob) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "lib/agent-core/src/JobRunner.cpp");
  ASSERT_FALSE(runner.empty());

  const size_t entry = runner.find("Outcome JobRunner::runJob(const ClaimedJob& job, Context& ctx, size_t& dataLen) {");
  ASSERT_NE(entry, std::string::npos);
  const size_t lease = runner.find("leaseLapsed(job.leaseUntil, clock_->hasTime(), clock_->now())", entry);
  const size_t canRun = runner.find("canRun(job, why, sizeof why)", entry);
  const size_t beginJob = runner.find("mesh_->beginJob(", entry);
  ASSERT_NE(lease, std::string::npos) << "runJob does not look at the lease at all";
  ASSERT_NE(canRun, std::string::npos);
  ASSERT_NE(beginJob, std::string::npos);
  EXPECT_LT(lease, canRun) << "the lease is read first";
  EXPECT_LT(lease, beginJob) << "and before the mesh is told there is a job";

  const size_t timeout = runner.find("return Outcome::Timeout;", lease);
  ASSERT_NE(timeout, std::string::npos);
  EXPECT_LT(timeout, canRun) << "a lapsed lease returns timeout without running anything";

  // And the claim still posts that result: the loop below it is the one every job goes through.
  EXPECT_NE(runner.find("Outcome outcome = runJob(job, ctx, dataLen);"), std::string::npos);
  EXPECT_NE(runner.find("posted = post(job.id, g_body, len);"), std::string::npos);
}

// A claim is run to the end before the loop gets its turn back, so a job's own waits are where
// the heartbeat has to go (the note tasks 03, 04 and 05 each left for this one). The pump that
// keeps the console alive through a job is what sends it, and it sends **only** that: no claim
// and no observation batch happen inside a job.
TEST(StayingUp, TheHeartbeatGoesFromInsideALongJob) {
  const std::string main = readFile(fs::path(PROJECT_DIR) / "src/main.cpp");
  ASSERT_FALSE(main.empty());
  const size_t pump = main.find("void pumpDuringAJob() {");
  ASSERT_NE(pump, std::string::npos);
  const size_t end = main.find("\n}", pump);
  ASSERT_NE(end, std::string::npos);
  const std::string body = main.substr(pump, end - pump);
  EXPECT_NE(body.find("agent.heartbeatIfOverdue();"), std::string::npos)
      << "the job pump does not send the heartbeat that fell due";
  EXPECT_NE(body.find("mm::healthTick();"), std::string::npos) << "the job pump does not feed the watchdog";
  EXPECT_EQ(body.find("agent.loop()"), std::string::npos) << "the whole loop must not run inside a job";

  const std::string loop = readFile(fs::path(PROJECT_DIR) / "lib/agent-core/src/AgentLoop.cpp");
  const size_t overdue = loop.find("void AgentLoop::heartbeatIfOverdue() {");
  ASSERT_NE(overdue, std::string::npos);
  const size_t overdueEnd = loop.find("\n}", overdue);
  ASSERT_NE(overdueEnd, std::string::npos);
  const std::string fn = loop.substr(overdue, overdueEnd - overdue);
  EXPECT_NE(fn.find("doHeartbeat();"), std::string::npos);
  EXPECT_EQ(fn.find("doJobs("), std::string::npos) << "a claim inside a claim";
  EXPECT_EQ(fn.find("doObservations("), std::string::npos) << "a batch inside a job";
  EXPECT_NE(fn.find("inHeartbeat_"), std::string::npos) << "nothing stops a second heartbeat on the wire";
  EXPECT_NE(fn.find("halted_"), std::string::npos) << "a 401 must stop this one too";
}

// ---- the backoff ------------------------------------------------------------------------------

TEST(Backoff, TheSequenceDoublesToItsCeilingAndStaysThere) {
  // The server loop's own numbers, which ServerPolicy.h names and this function is behind. That
  // the policy's own form is this one at those numbers is test_server_policy's (task p1.2-10).
  uint32_t ms = 0;
  const uint32_t expected[] = {30000, 60000, 120000, 240000, 480000, 600000, 600000, 600000};
  for (uint32_t want : expected) {
    ms = mm::nextBackoffMs(ms, mm::kFirstBackoffMs, mm::kMaxBackoffMs);
    EXPECT_EQ(want, ms);
  }
  // And the WiFi link's, which are the same shape at five seconds and five minutes.
  uint32_t wifi = 0;
  const uint32_t wifiExpected[] = {5000, 10000, 20000, 40000, 80000, 160000, 300000, 300000};
  for (uint32_t want : wifiExpected) {
    wifi = mm::nextBackoffMs(wifi, 5000, 5 * 60000);
    EXPECT_EQ(want, wifi);
  }
}

TEST(Backoff, TheJitterStaysWithinAQuarterAndMovesTheWait) {
  const uint32_t steps[] = {5000, 30000, 120000, 600000};
  for (uint32_t base : steps) {
    const uint32_t swing = base * mm::kJitterPercent / 100u;
    uint32_t lowest = base;
    uint32_t highest = base;
    for (uint32_t r = 0; r < 4096; r++) {
      const uint32_t got = mm::withJitter(base, r * 2654435761u);
      EXPECT_GE(got, base - swing) << "base " << base << " random " << r;
      EXPECT_LE(got, base + swing) << "base " << base << " random " << r;
      if (got < lowest) lowest = got;
      if (got > highest) highest = got;
    }
    EXPECT_LT(lowest, base) << "the jitter never moved the wait down for base " << base;
    EXPECT_GT(highest, base) << "the jitter never moved the wait up for base " << base;
  }
}

TEST(Backoff, TwoBoardsThatFailedTogetherComeBackApart) {
  // The point of the jitter: the same failure at the same second on two boards is two different
  // waits, because the randomness is each board's own.
  const uint32_t base = mm::kFirstBackoffMs;
  int different = 0;
  for (uint32_t r = 0; r < 100; r++) {
    if (mm::withJitter(base, r * 7919u) != mm::withJitter(base, r * 7919u + 104729u)) different++;
  }
  EXPECT_GT(different, 90) << "the wait hardly depends on the randomness";
}

TEST(Backoff, NoWaitIsNotSomethingToJitter) {
  EXPECT_EQ(0u, mm::withJitter(0, 12345));
  // And a wait too short for a quarter of it to be a millisecond is left alone rather than
  // rounded to nothing.
  EXPECT_EQ(3u, mm::withJitter(3, 12345));
}

// ---- the log ring -----------------------------------------------------------------------------

TEST(LogRing, ItHoldsWhatItIsGivenUntilItIsFull) {
  mm::LogRing ring;
  EXPECT_TRUE(ring.empty());
  ring.add("[12:03:14] I wifi: connected\r\n");
  ring.add("[12:03:16] I server: config ok\r\n");
  EXPECT_EQ(2u, ring.lines());
  EXPECT_FALSE(ring.wrapped());

  char out[256] = {0};
  const size_t n = ring.copyTo(out, sizeof out);
  EXPECT_EQ(n, ring.size());
  EXPECT_EQ(std::string("[12:03:14] I wifi: connected\r\n[12:03:16] I server: config ok\r\n"), std::string(out));
}

TEST(LogRing, WhenItIsFullTheOldestGoesAndNoHalfLineIsPrinted) {
  mm::LogRing ring;
  // Lines of a hundred bytes each, more of them than four kilobytes holds.
  for (int i = 0; i < 80; i++) {
    char line[101];
    std::snprintf(line, sizeof line, "[12:03:%02d] I line %02d %s\n", i % 60, i, std::string(76, 'x').c_str());
    ring.add(line);
  }
  EXPECT_TRUE(ring.wrapped());
  EXPECT_EQ(mm::LogRing::kCapacity, ring.size());
  EXPECT_EQ(80u, ring.lines());
  EXPECT_GT(ring.dropped(), 0u) << "lines were overwritten and not counted";

  static char out[mm::LogRing::kCapacity + 1];
  const size_t n = ring.copyTo(out, sizeof out);
  const std::string text(out, n);
  EXPECT_LT(n, mm::LogRing::kCapacity) << "the partial line at the front was dropped, so this is shorter";
  EXPECT_EQ(text.rfind("[12:03:", 0), 0u) << "dump starts at a line boundary, not mid-word: " << text.substr(0, 20);
  EXPECT_NE(text.find("line 79"), std::string::npos) << "the newest line is there";
  EXPECT_EQ(text.find("line 00"), std::string::npos) << "the oldest is gone";
  EXPECT_EQ('\n', text[text.size() - 1]);
}

TEST(LogRing, ALineLongerThanTheRingKeepsItsEnd) {
  mm::LogRing ring;
  std::string monster(mm::LogRing::kCapacity + 500, 'a');
  monster += "THE-END\n";
  ring.add(monster.c_str());
  EXPECT_EQ(mm::LogRing::kCapacity, ring.size());
  static char out[mm::LogRing::kCapacity + 1];
  const size_t n = ring.copyTo(out, sizeof out);
  EXPECT_LE(n, mm::LogRing::kCapacity);
}

TEST(LogRing, ACallersBufferThatIsTooSmallIsFilledAndTerminated) {
  mm::LogRing ring;
  ring.add("0123456789\n");
  char out[6] = {0};
  const size_t n = ring.copyTo(out, sizeof out);
  EXPECT_EQ(5u, n);
  EXPECT_EQ(std::string("01234"), std::string(out));
}

// There is one ring and the log writes to it: `dump` prints what the port printed, which is the
// whole of its redaction (README.md's rails, and agent/Log.h says so in as many words).
TEST(LogRing, TheLogWritesEveryLineIntoTheRingAndNothingElseDoes) {
  const std::string log = readFile(fs::path(PROJECT_DIR) / "src/Log.cpp");
  ASSERT_FALSE(log.empty());
  const size_t printed = log.find("Serial.print(full);");
  const size_t kept = log.find("logRing().add(full, len);");
  ASSERT_NE(printed, std::string::npos) << "the log no longer prints one formatted line";
  ASSERT_NE(kept, std::string::npos) << "the log no longer keeps what it printed";
  EXPECT_LT(printed, kept) << "the ring gets the bytes the port got, after the port got them";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
