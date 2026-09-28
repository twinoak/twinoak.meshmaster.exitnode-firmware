#include "ServerPolicy.h"

#include "Backoff.h"

namespace mm {

ServerAction actionFor(bool reachable, int httpStatus) {
  if (!reachable) return ServerAction::BackOff;
  if (httpStatus >= 200 && httpStatus < 300) return ServerAction::Ok;
  if (httpStatus == 401) return ServerAction::Halt;
  if (httpStatus == 429) return ServerAction::WaitRetryAfter;
  // The 4xx the .NET agent calls Definitive (ServerAnswer.Definitive in Uplink.cs): the request
  // itself is wrong, or the resource is gone, and sending it again would only be wrong again.
  // 403 is here too - an exit node the owner paused is handed nothing, and saying so twice a
  // second helps nobody - and 413, which a batch too large for the server earns.
  if (httpStatus == 400 || httpStatus == 403 || httpStatus == 404 || httpStatus == 409 || httpStatus == 410 ||
      httpStatus == 413 || httpStatus == 422) {
    return ServerAction::Discard;
  }
  // Everything else - a 5xx, a 408, a status no version of this server has sent yet - is worth
  // trying again later.
  return ServerAction::BackOff;
}

uint32_t retryAfterMs(uint32_t retryAfterS) {
  if (retryAfterS == 0) return kRetryAfterWhenUnstatedMs;
  const uint32_t ms = retryAfterS > kMaxRetryAfterMs / 1000u ? kMaxRetryAfterMs : retryAfterS * 1000u;
  return ms;
}

uint32_t nextBackoffMs(uint32_t previousMs) {
  // The sequence is the one both backoffs of this firmware share (src/Backoff.h); what is
  // waited is this number through withJitter(), so two boards that failed together drift apart.
  return nextBackoffMs(previousMs, kFirstBackoffMs, kMaxBackoffMs);
}

}  // namespace mm
