// The name this board announces to the network, and its station address as a person reads it
// (task p1.22-74).
//
// An owner of two boards reported on 22 November 2026 that one of them had a new address after an
// update. Nothing in this firmware *sets* a MAC - the station's address is the chip's own, burnt
// into its eFuse, and the rail in test/test_hostname is what keeps that answer true. It is read
// once, in src/net/WifiLink.cpp, for the two words below. What the firmware did not do was give
// each board a name of its own: a router that keeps its leases by name sees two boards under one
// name as one board, and the name is the only half of this that a firmware can choose.
//
// So a board calls itself `meshmaster-` and the last four hex digits of its station MAC, lower
// case - `meshmaster-1a2b` - and `status` prints the name beside the address, which is what a
// reservation in a router is made by. Nothing is stored and nothing is settable: the name follows
// the chip, so a board that is reflashed, erased or re-provisioned comes back under the same one.
//
// Both functions are pure and host-tested (test/test_hostname): the board passes the six bytes
// the eFuse gave it (src/net/WifiLink.cpp), a test passes its own. Neither is a secret and neither
// is overheard - a MAC is on the outside of every frame the board sends.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mm {

// "meshmaster-" + four hex digits + NUL.
constexpr size_t kHostnameCap = 16;
// "aa:bb:cc:dd:ee:ff" + NUL.
constexpr size_t kMacTextCap = 18;

// The name, from the six bytes of a station MAC: `meshmaster-` and the last two bytes as four
// lower-case hex digits, leading zeros kept (`meshmaster-000f`). `out` is NUL-terminated whenever
// `cap` is not 0, and truncated rather than overrun if `cap` is under kHostnameCap.
void hostnameFromMac(const uint8_t mac[6], char* out, size_t cap);

// The same six bytes as `aa:bb:cc:dd:ee:ff`, lower case - the form a router's lease list and a
// reservation page show. Same promise about `out`.
void macToText(const uint8_t mac[6], char* out, size_t cap);

}  // namespace mm
