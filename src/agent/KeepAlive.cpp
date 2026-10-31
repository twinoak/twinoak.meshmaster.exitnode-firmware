#include "KeepAlive.h"

namespace mm {
namespace uplink {

const char* KeepAlive::whyText(Why w) {
  switch (w) {
    case Why::NotYet:
      return "the first call";
    case Why::ServerClosed:
      return "the server closed it";
    case Why::CallFailed:
      return "the last call failed";
    case Why::RootChanged:
      return "the root changed";
    case Why::Dropped:
      return "dropped on purpose";
    case Why::NotKept:
      return "the server answered and closed";
  }
  return "";
}

Attempt KeepAlive::call(Transport& t, Root root, uint32_t nowEpoch) {
  counters_.calls++;
  lastHandshook_ = false;
  lastReopened_ = false;

  // Reuse only what the client still sees alive, and only against the same root. A kept
  // connection the client no longer sees is the server's idle timeout at work (or the link's),
  // and is not a failure of anything: the call goes on a new one.
  bool reuse = kept_ && t.connected();
  if (kept_ && !reuse) {
    kept_ = false;
    why_ = Why::ServerClosed;
  }
  if (reuse && keptRoot_ != root) {
    kept_ = false;
    reuse = false;
    why_ = Why::RootChanged;
  }
  if (!reuse) {
    t.close();  // nothing kept, or not the right thing: start from a client with no history
    counters_.handshakes++;
    lastHandshook_ = true;
  }

  Attempt a = t.exchange(root);

  if (reuse && a.code <= 0 && a.tlsError == 0) {
    // The kept connection was dead under the request - a router that dropped the idle session,
    // a close that arrived after the probe. Once, inside this call: a new connection, the same
    // request. What comes back from that is the call's answer, failure included.
    counters_.reopened++;
    lastReopened_ = true;
    t.close();
    counters_.handshakes++;
    lastHandshook_ = true;
    a = t.exchange(root);
  } else if (reuse) {
    counters_.reused++;
  }

  if (a.code <= 0) {
    // A failed call keeps nothing: the client has stopped, and the next call starts clean.
    t.close();
    kept_ = false;
    why_ = Why::CallFailed;
    return a;
  }
  const bool stillThere = t.connected();
  if (stillThere) {
    if (!kept_ || lastHandshook_) keptSince_ = nowEpoch;
    kept_ = true;
    keptRoot_ = root;
  } else {
    t.close();
    kept_ = false;
    why_ = Why::NotKept;
  }
  return a;
}

void KeepAlive::drop(Transport& t) {
  t.close();
  if (kept_) why_ = Why::Dropped;
  kept_ = false;
}

}  // namespace uplink
}  // namespace mm
