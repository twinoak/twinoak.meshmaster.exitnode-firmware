// What this board has room for (task p1.8-31): how many broker slots its heap gate will open,
// whether its TLS buffers go to PSRAM, and the figures the gate is built from - each one either
// measured on a bench with its date, or conservative with the reason written beside it.
//
// The figures (tasks/phase-1.8/README.md, *Measured*, Heltec V3, 28 October 2026, v0.4.0):
//
//   no slot, after a MeshMaster call     136 580 free, 55 464 min ever, 73 716 largest block
//   slot 1 (MeshView) up, after a job     73 016 free,  3 088 min ever, 59 380 largest block
//
// So one slot cost 63 564 bytes of internal heap (136 580 - 73 016), and the uplink's call -
// a TLS handshake per call then, before task p1.8-30 kept the connection - dipped 81 116
// (136 580 - 55 464). The README's row *after 30, no slot* had not been walked when this task
// ran, so these are the figures used, and the gate reads what the uplink's calls cost live
// (Connection.h, onUplinkCall) once a call has been measured on this boot.
//
// Why one slot on a V3 and two on a V4 (Thomas, 31 October 2026): a V3 has no PSRAM, and
// mbedtls's two 16 KB content buffers per connection cannot shrink without a custom ESP-IDF
// build, so a second TLS connection beside the MeshMaster uplink does not fit. A V4 has 2 MB of
// QSPI PSRAM (boards/heltec_v4.json, BOARD_HAS_PSRAM), and src/platform/TlsHeap.cpp puts every
// large mbedtls allocation there, so a slot costs the V4's internal heap only the client, its
// task and the handshake's small allocations.
//
// Host-portable: a struct of numbers and the choice of one by a build define. The native tests
// pick either profile by hand (useBoardProfile), so that a V3's refusal and a V4's second slot
// are both tested on the host.

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

// Heltec V3: both figures measured on 28 October 2026 (above). The uplink's dip is the figure of
// a handshake per call, which is more than a call over a kept connection dips; it is the
// conservative one until the board has measured a call of its own.
constexpr BoardProfile kHeltecV3 = {
    "heltec_v3", 1, false, 136580u - 73016u, 0, 136580u - 55464u,
    "measured 28 Oct 2026 (v0.4.0, one MeshView slot)",
    "measured 28 Oct 2026 (v0.4.0, a handshake per call)",
};

// Heltec V4: not measured on a V4. The slot's internal share is the V3's figure less the two
// content buffers that go to PSRAM (CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN 16384 each) - conservative,
// because the handshake's other allocations over the PSRAM threshold go there too and are not
// subtracted. Its PSRAM share is those two buffers and room for the handshake's large
// allocations. The uplink's dip is the V3's less the same two buffers, for the same reason.
constexpr BoardProfile kHeltecV4 = {
    "heltec_v4", 2, true, (136580u - 73016u) - 2u * 16384u, 2u * 16384u + 8u * 1024u, (136580u - 55464u) - 2u * 16384u,
    "the V3's figure less the two 16 KB content buffers in PSRAM (not measured on a V4)",
    "the V3's figure less the two 16 KB content buffers in PSRAM (not measured on a V4)",
};

// The profile this image is built for: the V4 when the board definition says it has PSRAM
// (boards/heltec_v4.json, BOARD_HAS_PSRAM), the V3 for any other board; on the host it is the
// V4's, the larger, and a test of the V3's rule switches (Board.cpp says why).
const BoardProfile& boardProfile();

// Tests only: use this profile until called with nullptr, which restores the built one.
void useBoardProfile(const BoardProfile* profile);

// Why slot `slot` (0-based) cannot be used on `board`, or nullptr when it can. One phrase, the
// same one the console's refusal, the slot's `off (...)` line and the log print.
const char* slotRefusal(int slot, const BoardProfile& board);

}  // namespace observer
}  // namespace mm
