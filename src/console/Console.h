// The serial console: reads lines from the USB serial port, parses them (Parser.h) and acts.
//
// What it prints, and what it does not: typed characters are echoed so a terminal without local
// echo is usable, except the value of a `set` of a secret key - wifi.pass (and its other name
// wifi.pwd), meshmaster.token, mqtt1.password, mqtt2.password - which echoes as asterisks; a
// reply never contains a secret (`get` says set/unset for one, `set` answers "ok <key>");
// `status` prints the public key, the radio, the firmware, the link, the time, the server's last
// answer, the observer and the heap; `get mqtt.status` the observer's two broker slots. The rest
// of what the port carries is the log (Log.h), which holds the same line.

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
  void (*sessions)(Stream& out);
  void (*errors)(Stream& out);
  void (*reboots)(Stream& out);
  void (*dump)(Stream& out);
  void (*wifi)(Stream& out);
  void (*reboot)();
  void (*settingChanged)(const KeyDef& key);  // after a successful `set`, so it takes effect live
  void (*erased)();                            // after `erase confirm`
  void (*mqttStatus)(Stream& out);             // `get mqtt.status`: one line per broker slot (task p1.7-27)
  // A second line for a key whose value alone does not say what it does: `get flood.scope` prints
  // the region's transport key and the code of an example payload, so that two boards given the
  // same scope can be compared (task p1.8-32). nullptr, or a key with nothing to add, prints one
  // line as every other key does.
  void (*keyNote)(const KeyDef& key, Stream& out);
  // `discover.scopes` (task p1.8-33): the neighbour table as it stands, and a cycle of the scope
  // sweep started now. nullptr prints nothing.
  void (*discoverScopes)(Stream& out);
  // `update` and `update slot` (task p1.15-57): which partition table this board runs on, and
  // adding the second application slot to it. The second one erases and writes one sector of
  // flash and is the only command here that does; nullptr prints nothing.
  void (*update)(Stream& out);
  void (*updateSlot)(Stream& out);
  // `update fetch <id>` (task p1.15-59): fetch release `<id>` from the configured server into the
  // other application slot and judge it. The command starts the fetch and returns; the steps and
  // the ending go to the log, which is this same port - a download of 1.4 MB is done in pieces
  // from the loop and cannot be waited for here. `id` is digits, as the parser checked.
  void (*updateFetch)(const char* id, Stream& out);
  // `update apply` (task p1.15-60): switch to the image task 59 judged and restart, so that it
  // runs on trial for ten minutes. It either does not return - the board restarts - or prints
  // one sentence saying why nothing was switched; nullptr prints nothing.
  void (*updateApply)(Stream& out);
  // `update back` (task p1.16-64): switch to whatever lies in the slot that is not running and
  // restart into it, on trial like any other. Flashing the release before over USB does not bring
  // it back on a board that has been updated - the bytes of the first slot are the same whether
  // they were flashed a minute ago or have lain there since - so this is the way back. Same shape
  // as `update apply`: it either does not return or prints one sentence; nullptr prints nothing.
  void (*updateBack)(Stream& out);
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
  void presets();
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
