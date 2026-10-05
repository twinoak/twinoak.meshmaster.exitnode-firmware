// The two tries of a login, and the blank login between them that tells a wrong password from a
// repeater off the air (EXIT-NODES §7, CONFIGURATION §7 step 4; meshmaster.dk's task p4.7-95,
// whose finding for this repository is task p1.3-13).
//
// `JobRunner::ensureSession` is the .NET agent's `EnsureSessionAsync`: the material of the job's
// class once, and once more when the first try got no reply. Refused is `unauthorized`, silence
// twice is `unreachable` - and on stock firmware a password the repeater does not know is silence
// (`handleLoginReq`, examples/simple_repeater/MyMesh.cpp:89-141 at d9296435, is `return 0` to a
// non-blank password that is neither of its two), so a wrong password and a dead repeater look the
// same from here. That matters in exactly one place: a job handed the **new** candidate of an
// `unverified` credential rotation, where the server's fallback to the old candidate is keyed to
// a refusal and a silence would leave the row unverified for ever.
//
// The .NET agent's answer (task p4.7-95) is a **blank** login between the two tries: this exit
// node's own key and no password, the `acl` login every status read makes. `handleLoginReq`'s
// first statement answers it at the ACL entry's level for any key the ACL holds, and its second
// answers *guest* (and writes a guest entry) on a repeater with no guest password set. Answered at
// any level - a refusal is an answer too - the repeater is on the air, and a second silence to
// the candidate right after it is the password's silence: `unauthorized`, with `LoginSilentResult`
// as the data so the server can tell it from a refusal the repeater spoke and from a silence that
// taught nothing. Silent too, nothing is known: `unreachable`, as before. The **second** try is
// the one judged, not the first - a reply the mesh ate is how the row became unverified in the
// first place, and a second try the repeater answers is a plain login that settles the row.
//
// One transaction more, on that path only. The probe stores no session: the next job is a
// candidate job and logs in afresh anyway.
//
// The policy is here, on a host, so that every path can be walked against a scripted exchange
// (test/test_rotation_probe); the board supplies the logins (JobRunner.cpp's LoginOnTheAir),
// the way ConfigApply.h's engine and ApplyOnTheAir divide an apply.

#pragma once

#include <cstdint>

#include "JobEnvelopes.h"

namespace mm {

// What one login came to - the runner's own reading of a reply. `Refused` is a reply that was
// not a login-ok, and a login answered below the class it was for (task p3.5-41); `NoReply` is
// what stock firmware answers a key and password it does not accept; `Failed` is a packet that
// could not be built; `Timeout` is the lease running out before the send.
enum class LoginTry : uint8_t { Ok, Refused, NoReply, Failed, Timeout };

// What the blank login came to. `Refused` is the legacy PUSH_CODE_LOGIN_FAIL frame, which a stock
// repeater never sends and which is an answer all the same; `NoReply` is also what the gate's
// refusal (the lease ran out) comes to, because either way nothing was learned.
enum class ProbeAnswer : uint8_t { NoReply, Answered, Refused };

// What the board supplies: a login with the job's material, a blank login, and the job's log.
class LoginExchange {
 public:
  virtual ~LoginExchange() = default;
  // One login with the job's material, `attempt` 1 or 2: counted, logged, and the session
  // bookkeeping a reply implies.
  virtual LoginTry login(int attempt) = 0;
  // One blank login: counted, logged, and **no session stored** whatever came back. `role` is
  // the role byte the reply carried when it was `Answered` (kPermGuest .. kPermAdmin).
  virtual ProbeAnswer probe(uint8_t& role) = 0;
  virtual void log(const char* line) = 0;
};

// What the sequence came to.
struct LoginVerdict {
  Outcome outcome = Outcome::Unreachable;
  // The candidate's second try was silent right after a blank login the repeater answered: the
  // outcome is `unauthorized` and the result's data is LoginSilentResult.
  bool silentAfterReachable = false;
  // How the probe was answered, when it was: the legacy refusal, or a login at `probeAnsweredRole`.
  bool probeRefused = false;
  uint8_t probeAnsweredRole = 0;
};

// Whether a job probes between its tries: the claim's `credential.candidate` is `new` and the
// material is a password - the .NET agent's `Candidate == "new" && material.Length > 0`. An `old`
// candidate, no candidate, or a blank material (an acl-class job, whose login *is* the probe)
// never probes.
bool probesBetweenTries(const char* credentialCandidate, const char* material);

// The sequence: `login(1)`; on silence and `probes`, `probe()`; on silence `login(2)`; and the
// verdict - `ok`, `unauthorized`, `failed` or `timeout` the moment a try comes to one of them,
// else `unauthorized` with `silentAfterReachable` after an answered probe and `unreachable`
// after a silent one or none.
LoginVerdict loginSequence(LoginExchange& exchange, bool probes);

// The tier the probe was answered at, in the server's words: `guest`, `read-only`, `read-write`,
// `admin` (permissionName of the role), or `refused` for the legacy frame.
const char* probeAnsweredAs(const LoginVerdict& verdict);
extern const char kProbeAnsweredRefused[];

}  // namespace mm
