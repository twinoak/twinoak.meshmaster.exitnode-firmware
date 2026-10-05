#include "LoginSequence.h"

#include <cstdio>
#include <cstring>

#include "mesh/ReplyCodec.h"

namespace mm {

const char kProbeAnsweredRefused[] = "refused";

bool probesBetweenTries(const char* credentialCandidate, const char* material) {
  return credentialCandidate != nullptr && std::strcmp(credentialCandidate, "new") == 0 && material != nullptr &&
         material[0] != '\0';
}

LoginVerdict loginSequence(LoginExchange& exchange, bool probes) {
  LoginVerdict verdict;
  bool reachable = false;
  for (int attempt = 1; attempt <= 2; attempt++) {
    switch (exchange.login(attempt)) {
      case LoginTry::Ok:
        verdict.outcome = Outcome::Ok;
        return verdict;
      case LoginTry::Refused:
        verdict.outcome = Outcome::Unauthorized;
        return verdict;
      case LoginTry::Failed:
        verdict.outcome = Outcome::Failed;
        return verdict;
      case LoginTry::Timeout:
        verdict.outcome = Outcome::Timeout;
        return verdict;
      case LoginTry::NoReply:
        break;  // once more
    }
    if (probes && attempt == 1) {
      // The blank login, between the first silence and the second try: answered at any level,
      // the repeater is listening, and the second try is judged in that light.
      uint8_t role = 0;
      switch (exchange.probe(role)) {
        case ProbeAnswer::Answered:
          reachable = true;
          verdict.probeAnsweredRole = static_cast<uint8_t>(role & kPermRoleMask);
          break;
        case ProbeAnswer::Refused:
          reachable = true;
          verdict.probeRefused = true;
          break;
        case ProbeAnswer::NoReply:
          break;
      }
    }
  }
  if (reachable) {
    verdict.silentAfterReachable = true;
    verdict.outcome = Outcome::Unauthorized;
    char line[240];
    std::snprintf(line, sizeof line,
                  "the repeater answered a blank login at %s and then said nothing to the new candidate: on stock "
                  "firmware that silence is a refusal of the password (task p4.7-95); the server will try the old "
                  "candidate next",
                  probeAnsweredAs(verdict));
    exchange.log(line);
    return verdict;
  }
  // Two tries and then `unreachable`, which is what the .NET agent reports for the same: on
  // stock firmware a refused key is silence, and silence is all a board can see.
  verdict.outcome = Outcome::Unreachable;
  return verdict;
}

const char* probeAnsweredAs(const LoginVerdict& verdict) {
  return verdict.probeRefused ? kProbeAnsweredRefused : permissionName(verdict.probeAnsweredRole);
}

}  // namespace mm
