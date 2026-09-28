#include "Console.h"

#include <cstdio>
#include <cstring>

#include "Log.h"
#include "version.h"

namespace mm {

void Console::begin(Stream& io, SettingsStore& settings, const ConsoleHost& host) {
  io_ = &io;
  settings_ = &settings;
  host_ = host;
  len_ = 0;
  io_->printf("%s\r\n", firmwareString());
  io_->println("type 'help' for the commands");
  prompt();
}

void Console::prompt() { io_->print("> "); }

void Console::loop() {
  while (io_->available() > 0) {
    const int c = io_->read();
    if (c < 0) break;
    takeChar(static_cast<char>(c));
  }
}

bool Console::secretValueIsBeingTyped() const {
  static const char* const prefixes[] = {"set wifi.pass ", "set meshmaster.token "};
  for (const char* p : prefixes) {
    const size_t n = std::strlen(p);
    if (len_ >= n && std::strncmp(line_, p, n) == 0) return true;
  }
  return false;
}

void Console::takeChar(char c) {
  if (c == '\n' && lastWasCr_) {  // the LF of a CR LF
    lastWasCr_ = false;
    return;
  }
  lastWasCr_ = (c == '\r');

  if (c == '\r' || c == '\n') {
    io_->print("\r\n");
    line_[len_] = '\0';
    runLine();
    len_ = 0;
    prompt();
    return;
  }
  if (c == '\b' || c == 0x7f) {
    if (len_ > 0) {
      len_--;
      io_->print("\b \b");
    }
    return;
  }
  if (len_ >= kMaxLineLen + 2) {
    // Keep taking characters so the line ends where the terminal's does, but drop them; the
    // parser refuses the line as too long.
    len_ = kMaxLineLen + 3;
    return;
  }
  line_[len_++] = c;
  io_->print(secretValueIsBeingTyped() ? '*' : c);
}

void Console::help() {
  io_->println("commands:");
  io_->println("  set <key> <value>   store a setting; it takes effect at once");
  io_->println("  get <key>           print a setting (a secret prints set or unset)");
  io_->println("  get                 print every setting");
  io_->println("  status              identity, radio, firmware, wifi, time, server, heap");
  io_->println("  mesh                the adverts heard, and the last five with their names");
  io_->println("  server              the heartbeat and the observation batches, and the counters");
  io_->println("  jobs                the last ten jobs claimed: outcome, transactions, when");
  io_->println("  wifi                scan for networks and show the link");
  io_->println("  reboot");
  io_->printf("  erase %s       forget every setting; the identity stays\r\n", kEraseWord);
  io_->println("keys:");
  for (const KeyDef& k : kKeys) {
    io_->printf("  %-18s %s\r\n", k.name, k.help);
  }
}

void Console::getAll() {
  char stored[kMaxValueLen + 1];
  char out[kMaxValueLen + 48];
  for (const KeyDef& k : kKeys) {
    settings_->get(k, stored, sizeof stored);
    describeValue(k, stored, out, sizeof out);
    io_->printf("%s\r\n", out);
  }
}

void Console::runLine() {
  if (len_ > kMaxLineLen + 2) {
    io_->printf("error: line too long (%u characters at most)\r\n", static_cast<unsigned>(kMaxLineLen));
    return;
  }
  const ParseResult r = parseLine(line_);
  if (!r.ok) {
    io_->printf("error: %s\r\n", r.error);
    return;
  }
  char out[kMaxValueLen + 48];
  switch (r.command.verb) {
    case Verb::Empty:
      return;
    case Verb::Help:
      help();
      return;
    case Verb::Set:
      if (!settings_->put(*r.command.key, r.command.value)) {
        io_->printf("error: %s could not be stored in NVS\r\n", r.command.key->name);
        return;
      }
      replyForSet(*r.command.key, out, sizeof out);
      io_->printf("%s\r\n", out);
      if (host_.settingChanged) host_.settingChanged(*r.command.key);
      return;
    case Verb::Get: {
      char stored[kMaxValueLen + 1];
      settings_->get(*r.command.key, stored, sizeof stored);
      describeValue(*r.command.key, stored, out, sizeof out);
      io_->printf("%s\r\n", out);
      return;
    }
    case Verb::GetAll:
      getAll();
      return;
    case Verb::Status:
      if (host_.status) host_.status(*io_);
      return;
    case Verb::Mesh:
      if (host_.mesh) host_.mesh(*io_);
      return;
    case Verb::Server:
      if (host_.server) host_.server(*io_);
      return;
    case Verb::Jobs:
      if (host_.jobs) host_.jobs(*io_);
      return;
    case Verb::Wifi:
      if (host_.wifi) host_.wifi(*io_);
      return;
    case Verb::Reboot:
      io_->println("rebooting");
      io_->flush();
      if (host_.reboot) host_.reboot();
      return;
    case Verb::Erase:
      if (!settings_->eraseAll()) {
        io_->println("error: NVS could not be cleared");
        return;
      }
      io_->println("ok every setting forgotten; the identity stays");
      if (host_.erased) host_.erased();
      return;
  }
}

}  // namespace mm
