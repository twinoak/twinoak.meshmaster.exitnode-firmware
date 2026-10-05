// The rotation probe, on a host (task p1.3-13; meshmaster.dk's task p4.7-95 and its
// RotationProbeTests): the two tries of a login with the blank login between them, walked against
// a scripted exchange. The four paths - silent; silent, probe answered, silent (`unauthorized`,
// LoginSilentResult byte for byte); silent, probe silent, silent (`unreachable`); silent, probe
// refused, silent (`unauthorized`, `probeAnsweredAs: refused`) - and that an `old` candidate, no
// candidate, and a blank material never probe, that the probe costs one transaction, and that it
// stores no session.
//
// The policy is src/agent/LoginSequence.cpp; the board's exchange is JobRunner's LoginOnTheAir and
// probeReachable, which a host cannot run, so the last tests hold that file's text to what the
// probe must and must not do - one spend, a blank login, no session - as test_config_apply holds
// the writes.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "agent/JobEnvelopes.h"
#include "agent/JobResults.h"
#include "agent/LoginSequence.h"
#include "mesh/ReplyCodec.h"

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.good()) << "cannot read " << path;
  std::ostringstream all;
  all << in.rdbuf();
  return all.str();
}

// A repeater on a script: what it answers each login with the candidate, and what it answers
// the blank login. Every login and every probe is a transaction, as on the board; only a login
// that is `Ok` stores a session, as the runner's loginOnce does and probeReachable never does.
class ScriptedRepeater : public mm::LoginExchange {
 public:
  std::vector<mm::LoginTry> logins;
  mm::ProbeAnswer probeAnswer = mm::ProbeAnswer::NoReply;
  uint8_t probeRole = mm::kPermGuest;

  int loginCalls = 0;
  int probeCalls = 0;
  int transactions = 0;
  int sessionsStored = 0;
  std::vector<int> attempts;
  std::vector<std::string> logged;

  mm::LoginTry login(int attempt) override {
    attempts.push_back(attempt);
    transactions++;
    const size_t i = static_cast<size_t>(loginCalls++);
    const mm::LoginTry answer = i < logins.size() ? logins[i] : mm::LoginTry::NoReply;
    if (answer == mm::LoginTry::Ok) sessionsStored++;
    return answer;
  }

  mm::ProbeAnswer probe(uint8_t& role) override {
    probeCalls++;
    transactions++;
    role = probeRole;
    return probeAnswer;
  }

  void log(const char* line) override { logged.emplace_back(line); }
};

std::string data(const mm::LoginVerdict& v) {
  char out[128];
  const size_t len = mm::writeLoginSilentResult(mm::probeAnsweredAs(v), out, sizeof out);
  return std::string(out, len);
}

// ------------------------------------------------------------------ who probes

TEST(RotationProbe, OnlyTheNewCandidateWithAPasswordProbes) {
  EXPECT_TRUE(mm::probesBetweenTries("new", "s3cret"));
  EXPECT_FALSE(mm::probesBetweenTries("old", "s3cret")) << "the old candidate is the server's fallback, not a probe";
  EXPECT_FALSE(mm::probesBetweenTries("", "s3cret")) << "no candidate: a plain job";
  EXPECT_FALSE(mm::probesBetweenTries(nullptr, "s3cret"));
  EXPECT_FALSE(mm::probesBetweenTries("new", "")) << "a blank material is the probe itself";
  EXPECT_FALSE(mm::probesBetweenTries("new", nullptr));
  EXPECT_FALSE(mm::probesBetweenTries("New", "s3cret")) << "the word is the server's, compared as the .NET agent compares it";
}

// ------------------------------------------------------------------ the four paths

TEST(RotationProbe, SilentTwiceWithNoProbeIsUnreachableAtTwo) {
  ScriptedRepeater r;
  r.logins = {mm::LoginTry::NoReply, mm::LoginTry::NoReply};
  const mm::LoginVerdict v = mm::loginSequence(r, false);
  EXPECT_EQ(v.outcome, mm::Outcome::Unreachable);
  EXPECT_FALSE(v.silentAfterReachable);
  EXPECT_EQ(r.loginCalls, 2);
  EXPECT_EQ(r.probeCalls, 0) << "a job that is not the new candidate never probes";
  EXPECT_EQ(r.transactions, 2);
  EXPECT_EQ(r.attempts, (std::vector<int>{1, 2}));
  EXPECT_TRUE(r.logged.empty());
}

TEST(RotationProbe, SilentThenAnsweredProbeThenSilentIsUnauthorizedWithTheProbesTier) {
  ScriptedRepeater r;
  r.logins = {mm::LoginTry::NoReply, mm::LoginTry::NoReply};
  r.probeAnswer = mm::ProbeAnswer::Answered;
  r.probeRole = mm::kPermReadOnly;
  const mm::LoginVerdict v = mm::loginSequence(r, true);
  EXPECT_EQ(v.outcome, mm::Outcome::Unauthorized);
  EXPECT_TRUE(v.silentAfterReachable);
  EXPECT_FALSE(v.probeRefused);
  EXPECT_EQ(v.probeAnsweredRole, mm::kPermReadOnly);
  EXPECT_STREQ(mm::probeAnsweredAs(v), "read-only");
  EXPECT_EQ(data(v), "{\"reason\":\"silentAfterReachable\",\"probeAnsweredAs\":\"read-only\"}");
  EXPECT_EQ(r.loginCalls, 2);
  EXPECT_EQ(r.probeCalls, 1) << "exactly one blank login, between the two tries";
  EXPECT_EQ(r.transactions, 3) << "the probe costs one transaction: three logins on the air instead of two";
  EXPECT_EQ(r.sessionsStored, 0) << "nothing stored a session: the probe never does, and no login was answered";
  ASSERT_EQ(r.logged.size(), 1u);
  EXPECT_EQ(r.logged[0],
            "the repeater answered a blank login at read-only and then said nothing to the new candidate: on stock "
            "firmware that silence is a refusal of the password (task p4.7-95); the server will try the old "
            "candidate next");
}

TEST(RotationProbe, TheTierIsTheWordTheServerUsesForEveryRole) {
  const struct {
    uint8_t role;
    const char* tier;
  } tiers[] = {{mm::kPermGuest, "guest"}, {mm::kPermReadOnly, "read-only"}, {mm::kPermReadWrite, "read-write"},
               {mm::kPermAdmin, "admin"}};
  for (const auto& t : tiers) {
    ScriptedRepeater r;
    r.logins = {mm::LoginTry::NoReply, mm::LoginTry::NoReply};
    r.probeAnswer = mm::ProbeAnswer::Answered;
    r.probeRole = t.role;
    const mm::LoginVerdict v = mm::loginSequence(r, true);
    EXPECT_EQ(v.outcome, mm::Outcome::Unauthorized);
    EXPECT_STREQ(mm::probeAnsweredAs(v), t.tier);
    EXPECT_EQ(data(v), std::string("{\"reason\":\"silentAfterReachable\",\"probeAnsweredAs\":\"") + t.tier + "\"}");
  }
  // A reply's permission byte carries more than the role; only the role is the tier.
  ScriptedRepeater r;
  r.logins = {mm::LoginTry::NoReply, mm::LoginTry::NoReply};
  r.probeAnswer = mm::ProbeAnswer::Answered;
  r.probeRole = static_cast<uint8_t>(0xF0 | mm::kPermReadWrite);
  const mm::LoginVerdict v = mm::loginSequence(r, true);
  EXPECT_EQ(v.probeAnsweredRole, mm::kPermReadWrite);
  EXPECT_STREQ(mm::probeAnsweredAs(v), "read-write");
}

TEST(RotationProbe, SilentThenSilentProbeThenSilentIsUnreachableAtThreeWithNoVerdict) {
  ScriptedRepeater r;
  r.logins = {mm::LoginTry::NoReply, mm::LoginTry::NoReply};
  r.probeAnswer = mm::ProbeAnswer::NoReply;
  const mm::LoginVerdict v = mm::loginSequence(r, true);
  EXPECT_EQ(v.outcome, mm::Outcome::Unreachable);
  EXPECT_FALSE(v.silentAfterReachable) << "a probe that was silent too proves nothing";
  EXPECT_FALSE(v.probeRefused);
  EXPECT_EQ(r.loginCalls, 2);
  EXPECT_EQ(r.probeCalls, 1);
  EXPECT_EQ(r.transactions, 3);
  EXPECT_EQ(r.sessionsStored, 0);
  EXPECT_TRUE(r.logged.empty()) << "nothing is noted: the server sees `unreachable` with no data, as before";
}

TEST(RotationProbe, SilentThenRefusedProbeThenSilentIsUnauthorizedAnsweredAsRefused) {
  ScriptedRepeater r;
  r.logins = {mm::LoginTry::NoReply, mm::LoginTry::NoReply};
  r.probeAnswer = mm::ProbeAnswer::Refused;
  const mm::LoginVerdict v = mm::loginSequence(r, true);
  EXPECT_EQ(v.outcome, mm::Outcome::Unauthorized);
  EXPECT_TRUE(v.silentAfterReachable);
  EXPECT_TRUE(v.probeRefused) << "the legacy LOGIN_FAIL frame is an answer: the repeater is on the air";
  EXPECT_STREQ(mm::probeAnsweredAs(v), "refused");
  EXPECT_EQ(data(v), "{\"reason\":\"silentAfterReachable\",\"probeAnsweredAs\":\"refused\"}");
  EXPECT_EQ(r.probeCalls, 1);
  EXPECT_EQ(r.transactions, 3);
  ASSERT_EQ(r.logged.size(), 1u);
  EXPECT_NE(r.logged[0].find("answered a blank login at refused"), std::string::npos);
}

// ------------------------------------------------------------------ the second try is the one judged

TEST(RotationProbe, ACandidateAcceptedOnTheFirstTryNeverProbes) {
  ScriptedRepeater r;
  r.logins = {mm::LoginTry::Ok};
  r.probeAnswer = mm::ProbeAnswer::Answered;
  const mm::LoginVerdict v = mm::loginSequence(r, true);
  EXPECT_EQ(v.outcome, mm::Outcome::Ok);
  EXPECT_FALSE(v.silentAfterReachable);
  EXPECT_EQ(r.loginCalls, 1);
  EXPECT_EQ(r.probeCalls, 0);
  EXPECT_EQ(r.transactions, 1);
  EXPECT_EQ(r.sessionsStored, 1);
}

TEST(RotationProbe, AFirstReplyTheMeshAteAndASecondTryAnsweredIsAPlainLoginWithNoVerdict) {
  ScriptedRepeater r;
  r.logins = {mm::LoginTry::NoReply, mm::LoginTry::Ok};
  r.probeAnswer = mm::ProbeAnswer::Answered;
  r.probeRole = mm::kPermReadOnly;
  const mm::LoginVerdict v = mm::loginSequence(r, true);
  EXPECT_EQ(v.outcome, mm::Outcome::Ok) << "the row is settled by the login the repeater answered";
  EXPECT_FALSE(v.silentAfterReachable);
  EXPECT_EQ(r.loginCalls, 2);
  EXPECT_EQ(r.probeCalls, 1) << "the probe went out after the first silence";
  EXPECT_EQ(r.transactions, 3);
  EXPECT_EQ(r.sessionsStored, 1) << "the second try's session, and nothing from the probe";
  EXPECT_TRUE(r.logged.empty());
}

TEST(RotationProbe, ASecondTryRefusedAfterAnAnsweredProbeIsTheRefusalItselfAndNotTheProbes) {
  ScriptedRepeater r;
  r.logins = {mm::LoginTry::NoReply, mm::LoginTry::Refused};
  r.probeAnswer = mm::ProbeAnswer::Answered;
  r.probeRole = mm::kPermGuest;
  const mm::LoginVerdict v = mm::loginSequence(r, true);
  EXPECT_EQ(v.outcome, mm::Outcome::Unauthorized);
  EXPECT_FALSE(v.silentAfterReachable) << "a refusal the repeater spoke carries no LoginSilentResult";
  EXPECT_EQ(r.probeCalls, 1);
  EXPECT_TRUE(r.logged.empty());
}

TEST(RotationProbe, TheOtherOutcomesEndTheSequenceWhereTheyAre) {
  for (const auto& pair : {std::pair<mm::LoginTry, mm::Outcome>{mm::LoginTry::Refused, mm::Outcome::Unauthorized},
                          std::pair<mm::LoginTry, mm::Outcome>{mm::LoginTry::Failed, mm::Outcome::Failed},
                          std::pair<mm::LoginTry, mm::Outcome>{mm::LoginTry::Timeout, mm::Outcome::Timeout}}) {
    ScriptedRepeater r;
    r.logins = {pair.first};
    r.probeAnswer = mm::ProbeAnswer::Answered;
    const mm::LoginVerdict v = mm::loginSequence(r, true);
    EXPECT_EQ(v.outcome, pair.second);
    EXPECT_EQ(r.loginCalls, 1);
    EXPECT_EQ(r.probeCalls, 0) << "only a silence is followed by the probe";
  }
  // The lease running out on the second try, after an answered probe, is `timeout` and not a verdict.
  ScriptedRepeater r;
  r.logins = {mm::LoginTry::NoReply, mm::LoginTry::Timeout};
  r.probeAnswer = mm::ProbeAnswer::Answered;
  const mm::LoginVerdict v = mm::loginSequence(r, true);
  EXPECT_EQ(v.outcome, mm::Outcome::Timeout);
  EXPECT_FALSE(v.silentAfterReachable);
}

TEST(RotationProbe, TheOldCandidateAndNoCandidateWalkTheTwoTriesAsBefore) {
  for (const char* candidate : {"old", ""}) {
    ScriptedRepeater r;
    r.logins = {mm::LoginTry::NoReply, mm::LoginTry::NoReply};
    r.probeAnswer = mm::ProbeAnswer::Answered;
    const mm::LoginVerdict v = mm::loginSequence(r, mm::probesBetweenTries(candidate, "s3cret"));
    EXPECT_EQ(v.outcome, mm::Outcome::Unreachable) << candidate;
    EXPECT_EQ(r.probeCalls, 0) << candidate;
    EXPECT_EQ(r.transactions, 2) << candidate;
  }
}

// ------------------------------------------------------------------ the board's half, on its text

// A host cannot run JobRunner, so what the probe must and must not do on the air is held on the
// file's text: one transaction through the gate, the blank login, a reply read as a login or as
// the refusal, and no session stored or dropped by it.
TEST(RotationProbeOnTheBoard, TheProbeSpendsOneTransactionSendsABlankLoginAndStoresNoSession) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "src/agent/JobRunner.cpp");
  const size_t from = runner.find("bool JobRunner::probeReachable(");
  const size_t to = runner.find("// ------------------------------------------------------------------ one exchange", from);
  ASSERT_NE(from, std::string::npos);
  ASSERT_NE(to, std::string::npos);
  const std::string body = runner.substr(from, to - from);

  size_t at = 0;
  int spends = 0;
  while ((at = body.find("spend(job, ctx)", at)) != std::string::npos) {
    spends++;
    at++;
  }
  EXPECT_EQ(spends, 1) << "one transaction, through the gate like every send";
  const size_t spend = body.find("if (!spend(job, ctx)) {");
  const size_t send = body.find("mesh_->sendLogin(\"\", tag)");
  const size_t wait = body.find("mesh_->awaitReply(tag, loginWaitMs(job), pump_)");
  const size_t parse = body.find("parseLoginReply(reply.data, reply.len, login)");
  ASSERT_NE(spend, std::string::npos) << "the gate's refusal is no probe";
  ASSERT_NE(send, std::string::npos) << "the blank login: our own key and no password";
  ASSERT_NE(wait, std::string::npos) << "waited for as a login is";
  ASSERT_NE(parse, std::string::npos) << "a reply that parses is a login; one that does not is the refusal";
  EXPECT_LT(spend, send);
  EXPECT_LT(send, wait);
  EXPECT_LT(wait, parse);
  EXPECT_EQ(body.find("sessions_."), std::string::npos) << "no session is stored, and none dropped, by the probe";
  EXPECT_NE(body.find("ctx.probeRefused = true;"), std::string::npos);
  EXPECT_NE(body.find("ctx.probeAnsweredRole ="), std::string::npos);
  EXPECT_EQ(body.find("sendLogin(material"), std::string::npos) << "never the candidate";
}

TEST(RotationProbeOnTheBoard, TheSequenceIsTheRunnersAndItsVerdictIsTheResultsData) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "src/agent/JobRunner.cpp");
  const size_t from = runner.find("Outcome JobRunner::ensureSession(");
  const size_t to = runner.find("bool JobRunner::probeReachable(", from);
  ASSERT_NE(from, std::string::npos);
  ASSERT_NE(to, std::string::npos);
  const std::string body = runner.substr(from, to - from);
  EXPECT_NE(body.find("probesBetweenTries(job.credentialCandidate, material)"), std::string::npos)
      << "who probes is the policy's decision, from the claim's candidate word and the material";
  EXPECT_NE(body.find("loginSequence(exchange, probes)"), std::string::npos);
  EXPECT_NE(body.find("ctx.silentAfterReachable = true;"), std::string::npos);
  EXPECT_EQ(body.find("for (int attempt"), std::string::npos) << "the loop lives in LoginSequence.cpp, on a host";

  const size_t exchange = runner.find("class JobRunner::LoginOnTheAir : public LoginExchange");
  ASSERT_NE(exchange, std::string::npos);
  const std::string adapter = runner.substr(exchange, from - exchange);
  EXPECT_NE(adapter.find("runner_.loginOnce(job_, ctx_, material_, what)"), std::string::npos);
  EXPECT_NE(adapter.find("runner_.probeReachable(job_, ctx_)"), std::string::npos);

  // The result's data: LoginSilentResult on exactly the `unauthorized` that carries the verdict,
  // after the below-class branch and never over a handler's own data.
  const size_t below = runner.find("dataLen == 0 && outcome == Outcome::Unauthorized && ctx.grantedBelowClass");
  const size_t silent = runner.find("dataLen == 0 && outcome == Outcome::Unauthorized && ctx.silentAfterReachable");
  ASSERT_NE(below, std::string::npos);
  ASSERT_NE(silent, std::string::npos);
  EXPECT_LT(below, silent);
  const std::string branch = runner.substr(silent, 600);
  EXPECT_NE(branch.find("writeLoginSilentResult("), std::string::npos);
  EXPECT_NE(branch.find("ctx.probeRefused ? kProbeAnsweredRefused : permissionName(ctx.probeAnsweredRole)"),
            std::string::npos)
      << "the tier is the probe's role in the server's words, or `refused`";
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
