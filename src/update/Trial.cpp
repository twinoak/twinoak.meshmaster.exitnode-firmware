#include "Trial.h"

#include <SHA256.h>  // rweather/Crypto, as update/Release.cpp hashes a release's file with

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <agent/Iso8601.h>
#include <agent/TextOut.h>

#include "Descriptor.h"
#include "Release.h"

namespace mm {
namespace update {

const char kStepSwitching[] = "switching";
const char kStepProving[] = "proving";

namespace {

const char kStepCommitting[] = "committing";
const char kStepRollingBack[] = "rolling-back";
const char kStepRestored[] = "restored";

// The three reasons a trial can end badly, each beginning with the word a person reads on the
// console (`update` says *rolled back: no wifi…*) and task 61 posts in the journal's note.
const char kMissingMesh[] = "no mesh: the radio did not start in ten minutes";
const char kMissingLink[] = "no wifi: the link or a clock off the network did not come up in ten minutes";
const char kMissingServer[] = "no server: no heartbeat was answered in ten minutes";

void hexOf(const uint8_t* bytes, size_t len, char* out) {
  static const char* kDigits = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[2 * i] = kDigits[bytes[i] >> 4];
    out[2 * i + 1] = kDigits[bytes[i] & 0x0F];
  }
  out[2 * len] = '\0';
}

// The SHA-256 of the first `len` bytes of the slot that is not running, as 64 lowercase hex
// characters. This is what deliverable 1 means by *its hash has been read from the slot again*:
// the bytes that would be booted, not the bytes that went past on the way in.
bool hashSlot(const SlotSink& slot, uint32_t len, char* outHex) {
  const SlotBytes bytes(slot, len);
  SHA256 sha;
  sha.reset();
  uint8_t chunk[512];
  for (uint32_t at = 0; at < len;) {
    const uint32_t take = (len - at < sizeof(chunk)) ? (len - at) : sizeof(chunk);
    if (!bytes.read(at, chunk, take)) return false;
    sha.update(chunk, take);
    at += take;
  }
  uint8_t digest[32];
  sha.finalize(digest, sizeof(digest));
  hexOf(digest, sizeof(digest), outHex);
  return true;
}

}  // namespace

const char* conditionWord(Condition which) {
  switch (which) {
    case Condition::Mesh:
      return "mesh";
    case Condition::Link:
      return "wifi and the clock";
    case Condition::Server:
      return "a heartbeat answered";
  }
  return "";
}

const char* slotStateWord(SlotState state) {
  switch (state) {
    case SlotState::New:
      return "new";
    case SlotState::Pending:
      return "pending";
    case SlotState::Valid:
      return "valid";
    case SlotState::Invalid:
      return "invalid";
    case SlotState::Aborted:
      return "aborted";
    case SlotState::Undefined:
      return "undefined";
    case SlotState::Unknown:
      break;
  }
  return "unknown";
}

const char* bootFindingWord(BootFinding finding) {
  switch (finding) {
    case BootFinding::NoSlots:
      return "no slots";
    case BootFinding::TrialBegun:
      return "on trial";
    case BootFinding::RolledBack:
      return "rolled back";
    case BootFinding::SwitchNotTaken:
      return "the switch was not taken";
    case BootFinding::CutShort:
      return "a trial was cut short";
    case BootFinding::SwitchedAtBoot:
      return "switched to the first slot";
    case BootFinding::Stuck:
      return "stuck in a firmware that could not be sent back";
    case BootFinding::Nothing:
      break;
  }
  return "nothing";
}

// -- the machine -------------------------------------------------------------------------------

void Updater::begin(const Seams& seams, const char* runningVersion, const char* board, const char* bootReason,
                    TextOut* out) {
  seams_ = seams;
  version_ = runningVersion != nullptr ? runningVersion : "";
  board_ = board != nullptr ? board : "";
  bootReason_ = bootReason != nullptr ? bootReason : "";
  out_ = out;
}

void Updater::say(const char* fmt, ...) {
  if (out_ == nullptr) return;
  char line[256];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if (n <= 0) return;
  out_->printf("update: %s", line);
  out_->println();
}

void Updater::journal(const char* step, const char* fmt, ...) {
  char note[kMaxNoteLen + 1];
  va_list args;
  va_start(args, fmt);
  vsnprintf(note, sizeof(note), fmt, args);
  va_end(args);
  record_.journal.add(seams_.clock != nullptr ? seams_.clock->epoch() : 0, step, note);
  say("%s: %s", step, note);
}

bool Updater::save() {
  if (seams_.store == nullptr) return false;
  const bool ok = saveUpdate(*seams_.store, record_);
  if (!ok) say("the record of this update could not be written");
  return ok;
}

bool Updater::recordKnows(const char* version) const {
  if (version == nullptr || version[0] == '\0') return true;  // nothing this firmware can judge
  if (strcmp(version, version_) == 0) return true;
  if (record_.from[0] != '\0' && strcmp(version, record_.from) == 0) return true;
  if (record_.to[0] != '\0' && strcmp(version, record_.to) == 0) return true;
  return false;
}

OtherSlot Updater::readTheOtherSlot(Descriptor& found, size_t& descriptors) const {
  found = Descriptor();
  descriptors = 0;
  if (!seams_.slots->available()) return OtherSlot::Nothing;
  SlotImage other;
  if (!seams_.slots->describeOther(other)) return OtherSlot::Nothing;
  if (!other.verifies || other.length == 0) return OtherSlot::WillNotLoad;

  const SlotBytes bytes(*seams_.slot, other.length);
  uint32_t at = 0;
  if (!findOneDescriptor(bytes, found, at, descriptors)) return OtherSlot::NoDescriptor;
  if (found.role != kRoleExitNode || strcmp(found.board, board_) != 0 || strcmp(found.band, kBand) != 0) {
    return OtherSlot::AnotherProduct;
  }
  // The one thing the bytes do not say: whether this board has met that version. The record does
  // (task p1.16-64).
  return recordKnows(found.version) ? OtherSlot::Known : OtherSlot::Unknown;
}

bool Updater::switchAndRestart(const char* why, bool postWhatWaits) {
  // The record is already written. The boot choice next, and the restart last: that order is
  // what makes a power cut between any two of them leave a board that is still running the image
  // it was running, with a record that the next boot can finish.
  if (!seams_.slots->bootOther()) {
    record_ = UpdateRecord();
    saveUpdate(*seams_.store, record_);
    say("the framework refused the boot choice, so nothing was switched: %s", seams_.slots->why());
    return false;
  }
  if (postWhatWaits) seams_.restart->postWhatWaits();
  seams_.restart->note(why);
  seams_.restart->restart();
  return true;
}

BootFinding Updater::atBoot() {
  begun_ = true;
  record_ = loadUpdate(*seams_.store);

  if (!seams_.slots->available()) {
    finding_ = BootFinding::NoSlots;
    say("no update can be made on this board: %s", seams_.slots->why());
    return finding_;
  }

  const int running = seams_.slots->runningSlot();
  const SlotState state = seams_.slots->runningState();

  if (record_.inHand()) {
    const bool runningIsTheNewOne = running >= 0 && static_cast<uint8_t>(running) == record_.slot;

    if (record_.phase == Phase::Switching) {
      if (runningIsTheNewOne) {
        beginTrial(state);
        finding_ = BootFinding::TrialBegun;
        return finding_;
      }
      // The boot choice was set and the board came back in the image it was running: the restart
      // beat the switch, or the framework did not keep it. Nothing was tried.
      journal(kStepRollingBack, "the switch was never taken: %s is running again", version_);
      finish(Ended::RolledBack, "the switch was never taken");
      finding_ = BootFinding::SwitchNotTaken;
      return finding_;
    }

    // On trial, and this boot is not the image that was on trial: the bootloader undid it, which
    // is what a crash, the watchdog or a power cut during a trial comes to.
    if (!runningIsTheNewOne) {
      char reason[kMaxNoteLen + 1];
      if (record_.why[0] != '\0') {
        // The image on trial got as far as writing the reason, and journalled `rolling-back`
        // with it before it restarted: this boot only has to finish the record.
        snprintf(reason, sizeof(reason), "%s", record_.why);
      } else {
        // It did not: the reset came out of nowhere, so this boot is the one that writes why.
        snprintf(reason, sizeof(reason), "restarted during the trial (%s)", bootReason_);
        journal(kStepRollingBack, "%s", reason);
      }
      finish(Ended::RolledBack, reason);
      finding_ = BootFinding::RolledBack;
      return finding_;
    }

    // On trial, and the image on trial is running again: this board's bootloader did not undo the
    // reset that ended the trial (deliverable 4). The same end, by this firmware's own hand.
    char reason[kMaxNoteLen + 1];
    snprintf(reason, sizeof(reason), "restarted during the trial (%s), and this bootloader does not undo one",
             bootReason_);
    journal(kStepRollingBack, "%s", reason);
    snprintf(record_.why, sizeof(record_.why), "%s", reason);
    onTrial_ = false;
    save();  // the phase stays `trial`: the boot after this restart is what finishes the record
    if (!seams_.slots->bootOther()) {
      // Nothing can send this board back: it is not pending, so the bootloader will not, and the
      // framework refused the boot choice. **That is not `rolledBack`** - the image that failed
      // is what runs (task p1.16-64, deliverable 5).
      char line[kMaxNoteLen + 1];
      snprintf(line, sizeof(line), "%.48s, and the boot choice could not be set back", reason);
      finish(Ended::Stuck, line);
      finding_ = BootFinding::Stuck;
      return finding_;
    }
    seams_.restart->note("a firmware on trial was restarted and is being undone");
    seams_.restart->restart();
    finding_ = BootFinding::CutShort;
    return finding_;
  }

  // Nothing in hand. **A flash over USB still wins** (deliverable 6): the web flasher writes the
  // plain `.bin` to the first slot whatever the board boots from, so a board running from the
  // second slot looks at the first one at every boot.
  finding_ = BootFinding::Nothing;
  if (running != 1) return finding_;  // running from the first slot: nothing is switched at boot

  // One reading, and **one line a boot** about what it found: before task p1.16-64 a first slot
  // holding a release this board had already run, or an image with no descriptor at all, was
  // passed over without a word, and an owner who had flashed it saw the flasher succeed and the
  // board come up on the other firmware (deliverable 3).
  Descriptor found;
  size_t descriptors = 0;
  switch (readTheOtherSlot(found, descriptors)) {
    case OtherSlot::Nothing:
    case OtherSlot::WillNotLoad:
      // There is no image there that the framework would load, so there is nothing to say.
      return finding_;
    case OtherSlot::NoDescriptor:
      if (descriptors > 1) {
        say("the first slot holds %u descriptors, so it is not switched to", static_cast<unsigned>(descriptors));
      } else {
        say("the first slot holds an image with no descriptor, so it is not switched to: a release older than the "
            "second slot carries none, and only a merged image over USB reaches one");
      }
      return finding_;
    case OtherSlot::AnotherProduct: {
      char role[16];
      say("the first slot holds %s %s band %s, which is not this product for this board", roleName(found.role, role, sizeof(role)),
          found.board, found.band);
      return finding_;
    }
    case OtherSlot::Known:
      // A version this board has run - the one it was switched from, or one it tried and undid.
      // The bytes of a slot are the same whether somebody flashed them a minute ago or they have
      // lain there since the update, so nothing is switched and nothing is guessed: the way back
      // is asked for.
      say("the first slot holds %s, which this board has run, so it is not switched to: `update back` goes back to it",
          found.version);
      return finding_;
    case OtherSlot::Unknown:
      break;  // the one switch a boot makes, and it is task p1.15-60's
  }

  record_ = UpdateRecord();
  record_.phase = Phase::Switching;
  snprintf(record_.from, sizeof(record_.from), "%s", version_);
  snprintf(record_.to, sizeof(record_.to), "%s", found.version);
  record_.at = seams_.clock->epoch();
  record_.slot = 0;
  journal(kStepSwitching, "the first slot holds %s, which this board has not run: flashed over USB", found.version);
  if (!save()) return finding_;
  if (!switchAndRestart("a firmware flashed over USB into the first slot", false)) return finding_;
  finding_ = BootFinding::SwitchedAtBoot;
  return finding_;
}

void Updater::beginTrial(SlotState state) {
  pending_ = state == SlotState::Pending;
  onTrial_ = true;
  trialStartMs_ = seams_.clock->ms();
  record_.phase = Phase::OnTrial;
  for (size_t i = 0; i < kConditions; i++) {
    met_[i] = false;
    metMs_[i] = 0;
  }
  // Task 59's record no longer describes the other slot: what it called judged is what is
  // running now, and the other slot holds the image this one was switched from. It is cleared
  // here, at the one boot that knows the switch happened, so that `update`'s `other` line and
  // the next `update apply` both tell the truth.
  const SlotRecord nothingJudged;
  saveRecord(*seams_.store, nothingJudged);
  if (pending_) {
    journal(kStepProving, "on trial for %lu min; a reset ends it and the bootloader undoes it",
            static_cast<unsigned long>(kTrialMs / 60000u));
  } else {
    journal(kStepProving, "on trial for %lu min; this bootloader leaves it %s, not pending: this firmware undoes it",
            static_cast<unsigned long>(kTrialMs / 60000u), slotStateWord(state));
  }
  save();
}

void Updater::step() {
  if (!onTrial_) return;

  static const Condition kOrder[kConditions] = {Condition::Mesh, Condition::Link, Condition::Server};
  for (size_t i = 0; i < kConditions; i++) {
    if (met_[i]) continue;
    bool now = false;
    switch (kOrder[i]) {
      case Condition::Mesh:
        now = seams_.conditions->meshStarted();
        break;
      case Condition::Link:
        now = seams_.conditions->linkAndClock();
        break;
      case Condition::Server:
        now = seams_.conditions->heartbeatAnswered();
        break;
    }
    if (!now) continue;
    met_[i] = true;
    metMs_[i] = seams_.clock->ms();
    say("on trial: %s, %lu s in", conditionWord(kOrder[i]),
        static_cast<unsigned long>((metMs_[i] - trialStartMs_) / 1000u));
  }

  if (met_[0] && met_[1] && met_[2]) {
    commit();
    return;
  }
  if (seams_.clock->ms() - trialStartMs_ >= kTrialMs) rollBack(whatIsMissing());
}

void Updater::commit() {
  char line[kMaxNoteLen + 1];
  conditionsLine(line, sizeof(line));
  // **Then, and at no moment before**: all three conditions have been seen in this boot.
  const bool marked = seams_.slots->markValid();
  if (!marked && pending_) {
    // The framework would not record the verdict, and this image is still pending-verify: the
    // bootloader would undo it at the next reset, whenever that came and whoever was watching.
    // Going back now with a record that says why beats being undone later by nobody.
    rollBack("the image proved itself and the framework would not mark it valid");
    return;
  }
  onTrial_ = false;
  if (marked) {
    journal(kStepCommitting, "%s", line);
  } else {
    journal(kStepCommitting, "%s; the framework would not mark it valid, and nothing undoes it here", line);
  }
  finish(Ended::Committed, line);
}

void Updater::rollBack(const char* why) {
  onTrial_ = false;
  journal(kStepRollingBack, "%s", why);
  snprintf(record_.why, sizeof(record_.why), "%s", why);
  // The phase stays `trial`: the boot after this restart finishes the record, whichever image it
  // is (deliverable 5), and it reads this reason out of the record rather than guessing one.
  save();
  seams_.restart->note(why);
  if (pending_) {
    if (seams_.slots->markInvalidAndRestart()) return;  // does not return on a board
    // The framework refused (ESP_ERR_OTA_ROLLBACK_FAILED), and this image is **still pending**.
    // A plain restart is what undoes a pending image - the bootloader marks it aborted and the
    // other slot's entry is then the one it chooses (the header of this file) - so this restarts
    // rather than finishing anything here: nothing is called rolled back before it is (task
    // p1.16-64, deliverable 4). The phase stays `trial`, and the boot after the restart finishes
    // the record from whichever image comes up.
    journal(kStepRollingBack, "the framework would not mark it invalid; restarting, which undoes a pending image");
    save();
    seams_.restart->restart();  // does not return on a board
    return;
  }
  if (seams_.slots->bootOther()) {
    seams_.restart->restart();  // does not return on a board
    return;
  }
  // Not pending and the boot choice refused: nothing at all can send this board back, and the
  // image that failed is what runs. **Not `rolledBack`** (deliverable 5).
  char line[kMaxNoteLen + 1];
  snprintf(line, sizeof(line), "%.48s, and the board could not be sent back", why);
  journal(kStepRollingBack, "%s", line);
  finish(Ended::Stuck, line);
}

void Updater::finish(Ended ended, const char* why) {
  record_.phase = Phase::Ended;
  record_.ended = ended;
  snprintf(record_.why, sizeof(record_.why), "%s", why != nullptr ? why : "");
  if (ended == Ended::RolledBack) journal(kStepRestored, "%s running", version_);
  onTrial_ = false;
  save();
  say("%s: %s", endedWord(ended), record_.why);
}

bool Updater::apply(char* why, size_t cap) {
  if (!begun_) record_ = loadUpdate(*seams_.store);

  if (onTrial_ || record_.inHand()) {
    snprintf(why, cap, "an update is already in hand (%s %s); nothing is switched on top of one",
             phaseWord(record_.phase), record_.to);
    return false;
  }
  if (!seams_.slots->available()) {
    snprintf(why, cap, "%s", seams_.slots->why());
    return false;
  }
  const SlotRecord judged = loadRecord(*seams_.store);
  if (!judged.judged) {
    snprintf(why, cap,
             "the other slot holds nothing this firmware will boot; `update fetch <release id>` first");
    return false;
  }
  char now[2 * 32 + 1] = {0};
  if (!hashSlot(*seams_.slot, judged.size, now)) {
    snprintf(why, cap, "the other slot's %lu bytes could not be read back, so nothing is switched",
             static_cast<unsigned long>(judged.size));
    return false;
  }
  if (strcmp(now, judged.sha256hex) != 0) {
    snprintf(why, cap, "the other slot hashes to %.16s... and the record says %.16s...: nothing is switched", now,
             judged.sha256hex);
    return false;
  }

  const int running = seams_.slots->runningSlot();
  record_ = UpdateRecord();
  record_.phase = Phase::Switching;
  snprintf(record_.from, sizeof(record_.from), "%s", version_);
  snprintf(record_.to, sizeof(record_.to), "%s", judged.version);
  snprintf(record_.sha256hex, sizeof(record_.sha256hex), "%s", judged.sha256hex);
  record_.at = seams_.clock->epoch();
  record_.slot = running == 0 ? 1 : 0;
  journal(kStepSwitching, "%s to %s, %lu bytes, in app%u", version_, judged.version,
          static_cast<unsigned long>(judged.size), static_cast<unsigned>(record_.slot));
  if (!save()) {
    record_ = UpdateRecord();
    saveUpdate(*seams_.store, record_);
    snprintf(why, cap, "the record of the update could not be written, so nothing is switched");
    return false;
  }
  if (!switchAndRestart("an update was applied at the console", true)) {
    snprintf(why, cap, "the framework refused the boot choice, so nothing is switched: %s", seams_.slots->why());
    return false;
  }
  return true;
}

bool Updater::goBack(const char* elsewhere, char* why, size_t cap) {
  if (!begun_) record_ = loadUpdate(*seams_.store);

  // The console's own two first, because they are what somebody at the window has just started
  // and this machine knows neither: meshmaster.dk's ask in hand, and a fetch running.
  if (elsewhere != nullptr && elsewhere[0] != '\0') {
    snprintf(why, cap, "%s", elsewhere);
    return false;
  }
  if (onTrial_ || record_.inHand()) {
    snprintf(why, cap, "an update is already in hand (%s %s); nothing is switched on top of one",
             phaseWord(record_.phase), record_.to);
    return false;
  }
  if (!seams_.slots->available()) {
    snprintf(why, cap, "%s", seams_.slots->why());
    return false;
  }

  Descriptor found;
  size_t descriptors = 0;
  switch (readTheOtherSlot(found, descriptors)) {
    case OtherSlot::Nothing:
      snprintf(why, cap, "%s", seams_.slots->why());
      return false;
    case OtherSlot::WillNotLoad:
      snprintf(why, cap, "the other slot holds nothing this firmware would boot, so there is nothing to go back to");
      return false;
    case OtherSlot::NoDescriptor:
      snprintf(why, cap,
               "the other slot holds an image with no descriptor%s, and nothing switches to one: a release older than "
               "the second slot carries none, and only a merged image over USB reaches it",
               descriptors > 1 ? " this firmware can read" : "");
      return false;
    case OtherSlot::AnotherProduct: {
      char role[16];
      snprintf(why, cap, "the other slot holds %s %s band %s, which is not this product for this board",
               roleName(found.role, role, sizeof(role)), found.board, found.band);
      return false;
    }
    case OtherSlot::Known:
    case OtherSlot::Unknown:
      break;  // taken, whether or not the record has met it: that is what this command is for
  }

  const int running = seams_.slots->runningSlot();
  record_ = UpdateRecord();
  record_.phase = Phase::Switching;
  snprintf(record_.from, sizeof(record_.from), "%s", version_);
  snprintf(record_.to, sizeof(record_.to), "%s", found.version);
  // No hash: no release named these bytes, as for an image found after a flash over USB. What
  // stands for the judgement here is the framework's own - `bootOther()` refuses an image it
  // would not load - and the descriptor just read out of the slot.
  record_.at = seams_.clock->epoch();
  record_.slot = running == 0 ? 1 : 0;
  journal(kStepSwitching, "back to %s, which lies in app%u", found.version, static_cast<unsigned>(record_.slot));
  if (!save()) {
    record_ = UpdateRecord();
    saveUpdate(*seams_.store, record_);
    snprintf(why, cap, "the record of the update could not be written, so nothing is switched");
    return false;
  }
  if (!switchAndRestart("`update back` was typed at the console", true)) {
    snprintf(why, cap, "the framework refused the boot choice, so nothing is switched: %s", seams_.slots->why());
    return false;
  }
  return true;
}

uint32_t Updater::trialLeftMs() const {
  if (!onTrial_) return 0;
  const uint32_t gone = seams_.clock->ms() - trialStartMs_;
  return gone >= kTrialMs ? 0 : kTrialMs - gone;
}

bool Updater::conditionMet(Condition which) const {
  const size_t i = static_cast<size_t>(which);
  return i < kConditions && met_[i];
}

void Updater::conditionsLine(char* out, size_t cap) const {
  snprintf(out, cap, "mesh, wifi and the clock, and a heartbeat answered %lu s into the trial",
           static_cast<unsigned long>((metMs_[2] - trialStartMs_) / 1000u));
}

const char* Updater::whatIsMissing() const {
  if (!met_[0]) return kMissingMesh;
  if (!met_[1]) return kMissingLink;
  if (!met_[2]) return kMissingServer;
  return "";
}

const char* Updater::statusNote() const { return onTrial_ ? "on trial" : ""; }

const char* Updater::whyTheOtherSlotMayNotBeWritten() const {
  if (onTrial_) {
    return "a firmware is on trial and the other slot holds the one this board would go back to; wait for `update` "
           "to say committed or rolled back";
  }
  if (record_.inHand()) {
    return "an update is in hand and the board has not restarted into it yet; type `reboot` first";
  }
  return nullptr;
}

void Updater::reportOther(TextOut& out) const {
  Descriptor found;
  size_t descriptors = 0;
  const OtherSlot what = readTheOtherSlot(found, descriptors);
  char slot[8];
  snprintf(slot, sizeof(slot), "app%d", seams_.slots->runningSlot() == 0 ? 1 : 0);
  switch (what) {
    case OtherSlot::Nothing:
      return;  // this board has no second slot, and `update`'s table lines have said so already
    case OtherSlot::WillNotLoad:
      out.printf("back      %s holds nothing this firmware would boot\r\n", slot);
      return;
    case OtherSlot::NoDescriptor:
      out.printf("back      %s holds an image with no descriptor%s, which nothing switches to\r\n", slot,
                 descriptors > 1 ? " this firmware can read" : "");
      return;
    case OtherSlot::AnotherProduct: {
      char role[16];
      out.printf("back      %s holds %s %s band %s, which is not this product for this board\r\n", slot,
                 roleName(found.role, role, sizeof(role)), found.board, found.band);
      return;
    }
    case OtherSlot::Known:
    case OtherSlot::Unknown:
      break;
  }
  out.printf("back      %s holds %s\r\n", slot, found.version);
  // And the command, where that is what it would do. While an update is in hand it is refused,
  // and `trial` or `last` below says what is in the way.
  if (!onTrial_ && !record_.inHand()) {
    out.printf("          `update back` switches to it and restarts; it then has ten minutes to prove itself\r\n");
  }
}

void Updater::report(TextOut& out) const {
  if (onTrial_) {
    const uint32_t left = trialLeftMs();
    out.printf("trial     on trial, %lu min %lu s left: mesh %s, wifi and clock %s, heartbeat %s\r\n",
               static_cast<unsigned long>(left / 60000u), static_cast<unsigned long>((left / 1000u) % 60u),
               met_[0] ? "yes" : "not yet", met_[1] ? "yes" : "not yet", met_[2] ? "yes" : "not yet");
    char when[24];
    formatUtc(record_.at, when, sizeof(when));
    out.printf("          %s, switched to from %s at %s; %s\r\n", record_.to, record_.from,
               record_.at != 0 ? when : "no clock",
               pending_ ? "the bootloader undoes it if this boot does not end"
                        : "this bootloader does not mark an image pending, so this firmware undoes it");
    return;
  }
  if (record_.phase == Phase::Ended) {
    char when[24];
    formatUtc(record_.at, when, sizeof(when));
    if (record_.ended == Ended::Committed) {
      out.printf("last      committed: %s, from %s, switched at %s\r\n", record_.to, record_.from,
                 record_.at != 0 ? when : "no clock");
    } else if (record_.ended == Ended::Stuck) {
      // Not back, and not kept: the trial failed and nothing could send the board back, so what
      // runs is the image that failed (task p1.16-64, deliverable 5).
      out.printf("last      stuck: %s; %s is still running and %s is in the other slot, switched at %s\r\n",
                 record_.why, record_.to, record_.from, record_.at != 0 ? when : "no clock");
    } else {
      out.printf("last      rolled back: %s; %s is running, %s was switched to at %s\r\n", record_.why, record_.from,
                 record_.to, record_.at != 0 ? when : "no clock");
    }
    return;
  }
  if (record_.inHand()) {
    out.printf("last      %s to %s; the restart has not happened yet\r\n", phaseWord(record_.phase), record_.to);
  }
}

}  // namespace update
}  // namespace mm
