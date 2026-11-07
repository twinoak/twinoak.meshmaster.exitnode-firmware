// The rotation probe, on a host (task p1.3-13; meshmaster.dk's task p4.7-95 and its
// RotationProbeTests): the two tries of a login with the blank login between them, walked against
// a scripted exchange. The four paths - silent; silent, probe answered, silent (`unauthorized`,
// LoginSilentResult byte for byte); silent, probe silent, silent (`unreachable`); silent, probe
// refused, silent (`unauthorized`, `probeAnsweredAs: refused`) - and that an `old` candidate, no
// candidate, and a blank material never probe, that the probe costs one transaction, and that it
// stores no session.
//
// Beside it, and in the same file because it is the same module: **the guest door** (task
// p1.4-14; meshmaster.dk's tasks p4.7-94 and p4.11-111). `loginMaterialFor` is the material of a
// job's class, and what is not missing material - the blank login of `acl`, `none` and, with no
// guest password in the claim, `guest` - against the one case that is, an admin job with no admin
// password.
//
// And, since task p1.10-41, **the local door**: a job the mesh answers without the air
// (`IMesh::answersLocally`) is answered by the board at the other end of the seam, at admin,
// whatever the login carried - so the material is the empty string at every class, `admin`
// included, and a unit configures its own repeater with no password stored anywhere. Every branch
// of the decision is walked twice below, once through the door and once on the air.
//
// The policy is lib/agent-core/src/LoginSequence.cpp; the board's exchange is JobRunner's
// LoginOnTheAir and
// probeReachable, which a host cannot run, so the last tests hold that file's text to what the
// probe must and must not do - one spend, a blank login, no session - and to where the door's
// material comes from, as test_config_apply holds the writes.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <agent/JobEnvelopes.h>
#include <agent/JobResults.h>
#include <agent/LoginSequence.h>
#include <agent/ReplyCodec.h>

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

// ------------------------------------------------------------------ the guest door (task p1.4-14)

mm::ClaimedJob jobAt(const char* type, mm::CredentialClass c) {
  mm::ClaimedJob job;
  std::snprintf(job.type, sizeof job.type, "%s", type);
  job.credentialClass = c;
  return job;
}

struct Material {
  const char* material = nullptr;
  std::string note;
};

Material materialOf(const mm::ClaimedJob& job, bool answeredLocally = false) {
  char note[mm::kLoginNoteCap] = {0};
  const char* m = mm::loginMaterialFor(job, answeredLocally, note, sizeof note);
  return Material{m, std::string(note)};
}

// A guest-class job the claim carried no password for is the guest door's empty case: the blank
// login, which a repeater whose owner never set a guest password answers *at guest*. It is the
// class that decides this and not the type, so the door's `discover` and the probes that follow
// it read the same.
TEST(GuestDoor, AGuestClaimWithNoPasswordIsTheBlankLoginAndSaysSo) {
  for (const char* type : {"discover", "status", "telemetry", "neighbours"}) {
    const mm::ClaimedJob job = jobAt(type, mm::CredentialClass::Guest);
    const Material m = materialOf(job);
    ASSERT_NE(m.material, nullptr) << type << ": no guest password is not missing material";
    EXPECT_STREQ(m.material, "") << type;
    EXPECT_EQ(m.note,
              "no guest password in the claim: the blank login, which a repeater with no guest password answers at "
              "guest")
        << type;
    EXPECT_FALSE(mm::probesBetweenTries(job.credentialCandidate, m.material))
        << type << ": a blank material is the probe itself, so the rotation probe is untouched by the door";
  }
}

TEST(GuestDoor, AGuestClaimWithAPasswordStillSendsIt) {
  mm::ClaimedJob job = jobAt("discover", mm::CredentialClass::Guest);
  job.credential.setGuestPassword("gu3st");
  const Material m = materialOf(job);
  EXPECT_STREQ(m.material, "gu3st");
  EXPECT_EQ(m.note, "") << "nothing to say about a password the claim carried whole";

  // Longer than the companion sends: cut, with a line that names neither the value nor its length.
  mm::ClaimedJob long_ = jobAt("status", mm::CredentialClass::Guest);
  long_.credential.setGuestPassword("0123456789abcdefghij");
  const Material cut = materialOf(long_);
  ASSERT_NE(cut.material, nullptr);
  EXPECT_EQ(std::strlen(cut.material), mm::kMaxPasswordLen);
  EXPECT_NE(cut.note.find("the guest password is longer than 15 bytes"), std::string::npos) << cut.note;
  EXPECT_EQ(cut.note.find("0123456789"), std::string::npos) << "no log line takes the value";
}

// The one case that really is missing material, and it is unchanged: there is no admin login
// without an admin password.
TEST(GuestDoor, AnAdminClaimWithNoAdminPasswordStillHasNothingToLogInWith) {
  const Material m = materialOf(jobAt("config.read", mm::CredentialClass::Admin));
  EXPECT_EQ(m.material, nullptr);
  EXPECT_EQ(m.note, "the claim carried no admin material; nothing to log in with");

  mm::ClaimedJob job = jobAt("config.read", mm::CredentialClass::Admin);
  job.credential.setAdminPassword("s3cret");
  EXPECT_STREQ(materialOf(job).material, "s3cret");
}

TEST(GuestDoor, AclAndNoneAreTheAclLoginAsTheyAlwaysWere) {
  for (mm::CredentialClass c : {mm::CredentialClass::Acl, mm::CredentialClass::None}) {
    const Material m = materialOf(jobAt("status", c));
    ASSERT_NE(m.material, nullptr) << mm::credentialClassName(c);
    EXPECT_STREQ(m.material, "") << mm::credentialClassName(c);
    EXPECT_EQ(m.note, "") << mm::credentialClassName(c);
  }
}

// The blank login through the sequence: answered at guest the job runs on the session it opened,
// and two silences are `unreachable` - which is what a repeater that *does* have a guest password
// answers it with, and what the server expects there (EXIT-NODES §4.1 step 2).
TEST(GuestDoor, ABlankGuestLoginAnsweredAtGuestRunsAndASilentOneIsUnreachableAtTwo) {
  ScriptedRepeater answered;
  answered.logins = {mm::LoginTry::Ok};
  const mm::LoginVerdict ok = mm::loginSequence(answered, false);
  EXPECT_EQ(ok.outcome, mm::Outcome::Ok);
  EXPECT_EQ(answered.loginCalls, 1) << "one blank login, and the job runs on the session it opened";
  EXPECT_EQ(answered.probeCalls, 0);
  EXPECT_EQ(answered.transactions, 1);
  EXPECT_EQ(answered.sessionsStored, 1);

  ScriptedRepeater silent;
  silent.logins = {mm::LoginTry::NoReply, mm::LoginTry::NoReply};
  const mm::LoginVerdict none = mm::loginSequence(silent, false);
  EXPECT_EQ(none.outcome, mm::Outcome::Unreachable);
  EXPECT_FALSE(none.silentAfterReachable) << "nothing is known, and nothing is claimed";
  EXPECT_EQ(silent.loginCalls, 2);
  EXPECT_EQ(silent.probeCalls, 0);
  EXPECT_EQ(silent.transactions, 2);
  EXPECT_TRUE(silent.logged.empty());
}

// What the door's `discover` reports: the path, and the tier the reply granted, which is what the
// server's association step reads (`DiscoverResult.LoggedInAs`).
TEST(GuestDoor, TheDiscoverAtGuestReportsThePathAndLoggedInAsGuest) {
  const char* hops[] = {"a1b2c3"};
  char out[200];
  const size_t len = mm::writeDiscoverResult(true, hops, 1, mm::kPathSourceLogin, 0,
                                             mm::permissionName(mm::kPermGuest), out, sizeof out);
  ASSERT_GT(len, 0u);
  EXPECT_EQ(std::string(out, len), "{\"path\":{\"hops\":[\"a1b2c3\"],\"source\":\"login\"},\"loggedInAs\":\"guest\"}");
}

// ------------------------------------------------------------------ the local door (task p1.10-41)

// A job the mesh answers **without the air** (IMesh::answersLocally, task p1.10-40) is answered
// by the board at the other end of the seam - the repeater-v2 unit's Walter, whose local door
// answers a login at admin whatever it carried. So the material is the empty string at **every**
// class, never `nullptr`, with one line in the job's log; and the four classes are walked here
// twice, once with material in the claim and once without.
TEST(LocalLogin, ALocalJobLogsInWithNoMaterialAtEveryClass) {
  const struct {
    mm::CredentialClass c;
    const char* type;
    const char* name;
  } classes[] = {
      {mm::CredentialClass::Admin, "config.read", "admin"},
      {mm::CredentialClass::Guest, "status", "guest"},
      {mm::CredentialClass::Acl, "status", "acl"},
      {mm::CredentialClass::None, "trace", "none"},
  };
  const std::string expected = "answered by the board, no password needed: the empty material at class ";
  for (const auto& k : classes) {
    EXPECT_STREQ(mm::credentialClassName(k.c), k.name) << "the class's own word, which the line names";

    // No material in the claim: what meshmaster.dk hands a unit for its own repeater from the
    // Walter that has this (its task p4.27-175) - nothing, at any class.
    const mm::ClaimedJob bare = jobAt(k.type, k.c);
    const Material none = materialOf(bare, /*answeredLocally=*/true);
    ASSERT_NE(none.material, nullptr) << k.name << ": a local login is never *nothing to log in with*";
    EXPECT_STREQ(none.material, "") << k.name;
    EXPECT_EQ(none.note, expected + k.name) << k.name;
    EXPECT_FALSE(mm::probesBetweenTries(bare.credentialCandidate, none.material))
        << k.name << ": an empty material is the blank login itself, so a local job never probes";

    // And with material: an older server, or a password typed for this one job under prompt
    // custody. The door does not look at it, so it is not sent - and it is not in the line.
    mm::ClaimedJob carried = jobAt(k.type, k.c);
    carried.credential.setAdminPassword("adm1n-pass");
    carried.credential.setGuestPassword("gu3st-pass");
    const Material with = materialOf(carried, /*answeredLocally=*/true);
    ASSERT_NE(with.material, nullptr) << k.name;
    EXPECT_STREQ(with.material, "") << k.name << ": a password the door would ignore is not sent";
    EXPECT_EQ(with.note, none.note) << k.name << ": the same line, whatever the claim carried";
    EXPECT_EQ(with.note.find("adm1n"), std::string::npos) << "no log line takes the value";
    EXPECT_EQ(with.note.find("gu3st"), std::string::npos) << "nor the guest one";
  }
}

// The job meshmaster.dk refused on 7 November 2026, both ways round: job 22016 was a
// `config.read` of a unit's **own** repeater at `admin` with no password stored anywhere.
TEST(LocalLogin, TheSameAdminClaimRunsThroughTheDoorAndStillRefusesOnTheAir) {
  const mm::ClaimedJob job = jobAt("config.read", mm::CredentialClass::Admin);

  const Material local = materialOf(job, /*answeredLocally=*/true);
  ASSERT_NE(local.material, nullptr);
  EXPECT_STREQ(local.material, "");

  // Not local, and nothing changes: there is no admin login without an admin password.
  const Material air = materialOf(job, /*answeredLocally=*/false);
  EXPECT_EQ(air.material, nullptr) << "a repeater on the air still needs the password it keeps";
  EXPECT_EQ(air.note, "the claim carried no admin material; nothing to log in with");
}

// A password longer than the firmware keeps is cut, and said so, for a login that goes out. A
// local job sends none, so there is nothing to cut and nothing to say about it.
TEST(LocalLogin, ALocalJobGetsNoCutLineBecauseNothingOfThePasswordIsSent) {
  mm::ClaimedJob job = jobAt("cli", mm::CredentialClass::Admin);
  job.credential.setAdminPassword("0123456789abcdefghij");
  ASSERT_TRUE(job.credential.adminPasswordCut) << "the claim did carry more than the companion keeps";

  const Material local = materialOf(job, /*answeredLocally=*/true);
  EXPECT_STREQ(local.material, "");
  EXPECT_EQ(local.note.find("longer than"), std::string::npos) << local.note;
  EXPECT_EQ(local.note.find("0123456789"), std::string::npos) << "no log line takes the value";

  // On the air it is the cut password and the line that says so, as before.
  const Material air = materialOf(job, /*answeredLocally=*/false);
  ASSERT_NE(air.material, nullptr);
  EXPECT_EQ(std::strlen(air.material), mm::kMaxPasswordLen);
  EXPECT_NE(air.note.find("the admin password is longer than 15 bytes"), std::string::npos) << air.note;
}

// ------------------------------------------------------------------ the board's half, on its text

// A host cannot run JobRunner, so what the probe must and must not do on the air is held on the
// file's text: one transaction through the gate, the blank login, a reply read as a login or as
// the refusal, and no session stored or dropped by it.
TEST(RotationProbeOnTheBoard, TheProbeSpendsOneTransactionSendsABlankLoginAndStoresNoSession) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "lib/agent-core/src/JobRunner.cpp");
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
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "lib/agent-core/src/JobRunner.cpp");
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

// The runner's half of the door, on the file's text: the material is the policy's decision and
// not a branch of its own, nothing goes on the air for a class that has none, and the discover's
// session line names the tier the reply granted rather than the word "admin".
TEST(GuestDoorOnTheBoard, TheRunnerTakesItsMaterialFromThePolicyAndSendsNothingWhenThereIsNone) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "lib/agent-core/src/JobRunner.cpp");
  const size_t from = runner.find("const char* JobRunner::loginMaterial(");
  const size_t to = runner.find("JobRunner::LoginResult JobRunner::loginOnce(", from);
  ASSERT_NE(from, std::string::npos);
  ASSERT_NE(to, std::string::npos);
  const std::string body = runner.substr(from, to - from);
  EXPECT_NE(body.find("loginMaterialFor(job, mesh_->answersLocally(), note, sizeof note)"), std::string::npos)
      << "one decision, on a host; the runner only asks the mesh whether the door answers and puts the line in the "
         "job's log";
  EXPECT_EQ(body.find("hasGuestPassword"), std::string::npos) << "the branch lives in LoginSequence.cpp";
  EXPECT_EQ(body.find("hasAdminPassword"), std::string::npos);
  EXPECT_EQ(body.find("if (mesh_->answersLocally())"), std::string::npos)
      << "the door is an argument of the decision and not a branch of the runner's own (task p1.10-41)";

  const size_t ensure = runner.find("Outcome JobRunner::ensureSession(");
  const size_t probe = runner.find("bool JobRunner::probeReachable(", ensure);
  ASSERT_NE(ensure, std::string::npos);
  ASSERT_NE(probe, std::string::npos);
  const std::string session = runner.substr(ensure, probe - ensure);
  const size_t nothing = session.find("if (material == nullptr) return Outcome::Unauthorized;");
  const size_t sequence = session.find("loginSequence(exchange, probes)");
  ASSERT_NE(nothing, std::string::npos);
  ASSERT_NE(sequence, std::string::npos);
  EXPECT_LT(nothing, sequence) << "an admin job with no admin password is unauthorized with nothing sent";
}

TEST(GuestDoorOnTheBoard, TheDiscoversSessionLineNamesTheTierTheReplyGranted) {
  const std::string runner = readFile(fs::path(PROJECT_DIR) / "lib/agent-core/src/JobRunner.cpp");
  const size_t from = runner.find("Outcome JobRunner::learned(");
  const size_t to = runner.find("Outcome JobRunner::runDiscover(", from);
  ASSERT_NE(from, std::string::npos);
  ASSERT_NE(to, std::string::npos);
  const std::string body = runner.substr(from, to - from);
  EXPECT_NE(body.find("the %s session is open for %lu s"), std::string::npos);
  EXPECT_NE(body.find("permissionName(ctx.sessionRole)"), std::string::npos)
      << "a discover at the guest door opens a guest session, not an admin one";
  EXPECT_EQ(body.find("the admin session is open"), std::string::npos);

  // And runDiscover logs in with the class's material, whatever the class is; the paragraph that
  // says which login names the guest door's two.
  const std::string discover = runner.substr(to, 1600);
  EXPECT_NE(discover.find("const char* material = loginMaterial(job);"), std::string::npos);
  EXPECT_NE(discover.find("At class `guest`"), std::string::npos) << "the \"which login\" paragraph says so";
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
