#include "Board.h"

namespace mm {
namespace observer {

namespace {

// The V4 when the board definition says PSRAM (boards/heltec_v4.json), the V3 for any other
// board build (ESP32_PLATFORM is platformio.ini's own flag for the boards), and the V4 on the
// host: the larger board, so that the tests written for two slots in pile 1.7 run as written,
// and a test of the V3's refusals switches to kHeltecV3 by hand.
#if defined(BOARD_HAS_PSRAM)
const BoardProfile& kBuilt = kHeltecV4;
#elif defined(ESP32_PLATFORM)
const BoardProfile& kBuilt = kHeltecV3;
#else
const BoardProfile& kBuilt = kHeltecV4;
#endif

const BoardProfile* g_override = nullptr;

}  // namespace

const BoardProfile& boardProfile() { return g_override != nullptr ? *g_override : kBuilt; }

void useBoardProfile(const BoardProfile* profile) { g_override = profile; }

const char* slotRefusal(int slot, const BoardProfile& board) {
  if (slot < board.slots) return nullptr;
  return "this board has memory for one broker; a V4 carries two";
}

}  // namespace observer
}  // namespace mm
