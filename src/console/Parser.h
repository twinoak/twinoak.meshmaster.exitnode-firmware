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
//   wifi                     scan, and the link's state
//   reboot
//   erase confirm            forget every setting (the identity stays); "confirm" is the word

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
  Jobs,    // the last ten jobs claimed
  Wifi,
  Reboot,
  Erase,   // only ever produced with the confirmation word present
};

struct Command {
  Verb verb = Verb::Empty;
  const KeyDef* key = nullptr;    // Set and Get
  char value[kMaxValueLen + 1] = {0};  // Set
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
