// The serial console: reads lines from the USB serial port, parses them (Parser.h) and acts.
//
// What it prints, and what it does not: typed characters are echoed so a terminal without local
// echo is usable, except the value of `set wifi.pass ...` and `set meshmaster.token ...`, which
// echoes as asterisks; a reply never contains a secret (`get` says set/unset for one, `set`
// answers "ok <key>"); `status` prints the public key, the radio, the firmware, the link, the
// time, the server's last answer and the heap. The rest of what the port carries is the log
// (Log.h), which holds the same line.

#pragma once

#include <Stream.h>

#include <cstddef>

#include "Parser.h"
#include "settings/Store.h"

namespace mm {

// What the console needs from the rest of the firmware, as plain function pointers.
struct ConsoleHost {
  void (*status)(Stream& out);
  void (*mesh)(Stream& out);
  void (*server)(Stream& out);
  void (*jobs)(Stream& out);
  void (*wifi)(Stream& out);
  void (*reboot)();
  void (*settingChanged)(const KeyDef& key);  // after a successful `set`, so it takes effect live
  void (*erased)();                            // after `erase confirm`
};

class Console {
 public:
  void begin(Stream& io, SettingsStore& settings, const ConsoleHost& host);
  void loop();

 private:
  void takeChar(char c);
  void runLine();
  void prompt();
  void help();
  void getAll();
  bool secretValueIsBeingTyped() const;

  Stream* io_ = nullptr;
  SettingsStore* settings_ = nullptr;
  ConsoleHost host_ = {};
  char line_[kMaxLineLen + 8] = {0};
  size_t len_ = 0;
  bool lastWasCr_ = false;
};

}  // namespace mm
