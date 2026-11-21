// The observer's slots as a fetch sees them (task p1.19-68).
//
// `update/Fetch.h` names an `Observers` seam and this is the one implementation of it: the
// broker connections of `src/observer/Connection.h`. Three forwards and nothing else, and
// **header-only and portable** on purpose - both halves of it are portable files, so
// test/test_fetch drives the adapter the board drives rather than a stand-in written beside it,
// and a fetch that closes the slots is tested with the real state machine of the slots.
//
// Why a fetch closes them at all: on 20 November two boards on 0.7.4 were asked to update to
// 0.7.5. Risskov went through in a minute. Aarhus C - the one board of the fleet with both
// observer slots connected, each a TLS session of its own - refused eight seconds in, with three
// ranges that stopped short and 48 208 bytes delivered of the third, which is three TLS records
// to the byte. Thomas, 21 November: *it would be good for the exit nodes to stop all observers
// before trying to update.*

#pragma once

#include "Fetch.h"
#include "observer/Connection.h"

namespace mm {
namespace update {

class ConnectionObservers final : public Observers {
 public:
  explicit ConnectionObservers(observer::Connections& slots) : slots_(slots) {}

  void standDown() override { slots_.standDownForFetch(); }
  bool allDown() const override { return slots_.allSlotsDown(); }
  void standUp(bool aSwitchFollows) override { slots_.standUpAfterFetch(aSwitchFollows); }

 private:
  observer::Connections& slots_;
};

}  // namespace update
}  // namespace mm
