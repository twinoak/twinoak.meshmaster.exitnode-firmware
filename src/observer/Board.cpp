#include "Board.h"

namespace mm {
namespace observer {

namespace {

// The V4 when the board definition says PSRAM (boards/heltec_v4.json), the V3 for any other
// board build (ESP32_PLATFORM is platformio.ini's own flag for the boards), and the V4 on the
// host: the board with PSRAM, so that the tests written for both heaps in pile 1.7 run as
// written, and a test of the V3's internal-only gate switches to kHeltecV3 by hand.
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
  // Since task p1.9-37 both profiles have every slot kSlots has, so nothing a board of this
  // repository is given reaches this line; it is what a board with room for one broker would
  // say, and the tests say it through a profile of their own.
  return "this board has memory for one broker only";
}

}  // namespace observer
}  // namespace mm
