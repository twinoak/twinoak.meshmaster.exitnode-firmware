// What this board has room for (tasks p1.8-31 and p1.9-37): how many broker slots its heap gate
// will open, whether its TLS buffers go to PSRAM, and the figures the gate is built from - each
// one either measured on a bench with its date, or conservative with the reason written beside it.
//
// The figures (tasks/phase-1.9/README.md, *Measured*, Heltec V3 FD9B, 2 November 2026, after task
// p1.9-36 built the ESP-IDF with mbedtls's content buffers allocated per record):
//
//   no slot, ten minutes after boot    120 392 free,  93 988 min ever, 102 388 largest block
//   slot 1 (MeshView) up, an hour      100 924 free,  69 736 min ever,  86 004 largest block
//
// So one slot costs 19 468 bytes of internal heap (120 392 - 100 924), where before 36 it cost
// 63 564: a connection between records no longer holds the two 16 KB content buffers, and the
// certificates are freed after the handshake. The uplink's calls over the kept connection of
// task p1.8-30 dipped 6 404 with no slot (120 392 - 113 988) and 6 212 with one
// (100 924 - 94 712); the one handshake of the walk, at boot, dipped 26 404 (120 392 - 93 988) -
// sixteen calls on that connection and fifty-five on the next hour's, so a handshake is the rare
// call and the expensive one. It is the figure the gate assumes until the board has measured a
// call of its own (Connection.h, onUplinkCall).
//
// Why two slots on both boards now (task p1.9-37, from the rows above): one slot and a handshake
// together take a V3 to 120 392 - 19 468 - 26 404 = 74 520 free, and two slots and a handshake to
// 55 052 - over three times kHeapFloorBytes - where before 36 the same sum was
// 136 580 - 63 564 - 81 116 and a V3's second slot never got memory. The hour's deepest trough
// with one slot (69 736, which is 31 188 under its own at-rest figure and has the slot's own
// handshake in it) leaves 50 268 with two. A V4 keeps its two slots and its PSRAM: the buffers a
// record borrows are large enough to be routed there (src/platform/TlsHeap.cpp), so its internal
// heap can only do better than the V3's measured figures, which is what it is given. A board
// that turns out not to fit its second slot is not hurt by this - the gate refuses to open it
// (`waiting-for-heap`), and a call that comes near the floor still closes the highest slot that
// is open.
//
// Host-portable: a struct of numbers and the choice of one by a build define. The native tests
// pick either profile by hand (useBoardProfile), and a profile of their own with one slot is how
// the refusal below is still tested.

#pragma once

#include <cstdint>

namespace mm {
namespace observer {

struct BoardProfile {
  const char* name;             // as MESHMASTER_BOARD spells it
  int slots;                    // broker slots the gate will open: the rest are refused with `slotRefusal`
  bool psram;                   // the TLS buffers go to PSRAM (src/platform/TlsHeap.cpp)
  uint32_t slotInternalBytes;   // what one open slot costs the internal heap
  uint32_t slotPsramBytes;      // what one open slot costs PSRAM (0 on a board without)
  uint32_t uplinkDipBytes;      // what the uplink's call dips the internal heap, before a call has been measured on this boot
  const char* slotSource;       // where slotInternalBytes comes from, one phrase
  const char* uplinkSource;     // where uplinkDipBytes comes from, one phrase
};

// Heltec V3: both figures measured on 2 November 2026 (above). The uplink's dip is the figure of
// the walk's one handshake, which is four times what a call over the kept connection dips; it is
// the conservative one until the board has measured a call of its own.
constexpr BoardProfile kHeltecV3 = {
    "heltec_v3", 2, false, 120392u - 100924u, 0, 120392u - 93988u,
    "measured 2 Nov 2026, after 36 (one MeshView slot: 120 392 free -> 100 924)",
    "measured 2 Nov 2026, after 36 (the walk's one handshake: 120 392 free -> 93 988)",
};

// Heltec V4: not measured on a V4 (Thomas had none on 2 November). Its internal figures are the
// V3's, measured, and not reduced: since task p1.9-36 a connection holds no mbedtls allocation
// over kPsramThresholdBytes between records, so there is nothing of the steady cost left to move
// to PSRAM, and the V4's internal heap can only do better than the V3's. Its PSRAM share is the
// peak a record borrows there - the 16 KB in-buffer and the 4 KB out-buffer
// (CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN) - with room for the handshake's own large allocations,
// which is pile 1.8's 40 960 for the same reason it was conservative then: 2 MB can spare it.
constexpr BoardProfile kHeltecV4 = {
    "heltec_v4", 2, true, 120392u - 100924u, 16u * 1024u + 4u * 1024u + 20u * 1024u, 120392u - 93988u,
    "the V3's measured figure, unreduced: after 36 a connection holds nothing over the PSRAM threshold (not measured on a V4)",
    "the V3's measured figure, unreduced, for the same reason (not measured on a V4)",
};

// The profile this image is built for: the V4 when the board definition says it has PSRAM
// (boards/heltec_v4.json, BOARD_HAS_PSRAM), the V3 for any other board; on the host it is the
// V4's, the one with PSRAM, and a test of the V3's rule switches (Board.cpp says why).
const BoardProfile& boardProfile();

// Tests only: use this profile until called with nullptr, which restores the built one.
void useBoardProfile(const BoardProfile* profile);

// Why slot `slot` (0-based) cannot be used on `board`, or nullptr when it can. One phrase, the
// same one the console's refusal, the slot's `off (...)` line and the log print. Since task
// p1.9-37 both profiles open every slot there is (kSlots), so no board this repository builds is
// refused one; the seam stands for a board that cannot take two, and a profile of the tests' own
// is what still exercises it.
const char* slotRefusal(int slot, const BoardProfile& board);

}  // namespace observer
}  // namespace mm
