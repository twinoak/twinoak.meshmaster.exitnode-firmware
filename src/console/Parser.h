// The serial console's line parser: one line in, one Command or one refusal out.
//
// A pure function, host-portable (no Arduino), so that test/test_console_parser can hold every
// line shape and every refusal without a board. What a command *does* is src/console/Console.cpp,
// which is board-only and is the only place the parser's result is acted on.
//
// The grammar (115 200 baud, one line per command, `>` prompt):
//
//   help                     the commands and the keys
//   set <key> <value>        store a setting (the value is the rest of the line, trimmed)
//   get <key>                print a setting; a secret prints "set" or "unset"
//   get                      print every setting, one per line
//   status                   identity, radio, firmware, WiFi, time, server, heap
//   mesh                     the adverts heard: how many, and the last five with their names
//   server                   the heartbeat and the observation batches: last, next, counters
//   jobs                     the last ten jobs claimed: outcome, transactions, when
//   sessions                 the repeater logins held: class, level, age (never a secret)
//   errors                   every counter the heartbeat carries, and the ones beside it
//   reboots                  why this board last restarted, and the four before that
//   dump                     the last 4 kB of the log, oldest first
//   wifi                     scan, and the link's state
//   reboot
//   erase confirm            forget every setting (the identity stays); "confirm" is the word
//   get mqtt.presets         the broker presets a slot can take (task p1.7-26)
//
// Since task p1.7-26 the lines of the observer firmware's Danish guide are understood as well:
// `set wifi.pwd ...` is `set wifi.pass ...` (Keys.cpp, findKey), and `set timezone ...` and
// `set bridge.source ...` - two settings this firmware has nothing to store for - are answered
// with one line each saying what it does instead (Keys.cpp, explainKey), as a Note rather than
// a refusal, so the guide typed in order ends in a valid configuration with no `error:` in it.

#pragma once

#include <cstddef>

#include "settings/Keys.h"

namespace mm {

enum class Verb : unsigned char {
  Empty,   // a blank line: nothing to do, no error
  Help,
  Set,
  Get,     // one key
  GetAll,  // every key
  Status,
  Mesh,    // the adverts heard
  Server,  // the heartbeat and the observation batches
  Jobs,      // the last ten jobs claimed
  Sessions,  // the repeater logins held
  Errors,    // the counters
  Reboots,   // why this board restarted, the last five
  Dump,      // the last 4 kB of the log ring
  Wifi,
  Reboot,
  Erase,   // only ever produced with the confirmation word present
  Note,    // a line accepted and answered with one line of explanation: `set timezone ...`
  Presets, // `get mqtt.presets`
};

struct Command {
  Verb verb = Verb::Empty;
  const KeyDef* key = nullptr;    // Set and Get
  char value[kMaxValueLen + 1] = {0};  // Set, after normalizeValue (Keys.h)
  const char* note = nullptr;     // Note: the line to print
};

// The longest line the console accepts, without its line ending.
constexpr size_t kMaxLineLen = 200;

struct ParseResult {
  bool ok = false;
  Command command;
  char error[160] = {0};  // one line, when !ok
};

ParseResult parseLine(const char* line);

// The confirmation word `erase` needs, so the help text and the test say the same thing.
constexpr const char* kEraseWord = "confirm";

}  // namespace mm
