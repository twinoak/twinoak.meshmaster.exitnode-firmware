// Switched, proved, or back (task p1.15-60).
//
// Task 59 leaves a judged image in the slot that is not running. This boots it, and decides
// whether it stays.
//
// A firmware that boots is not yet a firmware that works. What an exit node is for is reaching
// meshmaster.dk and the mesh, and a new image that cannot do either would leave a board that can
// never be told to update again. So a switched image runs **on trial**: it has ten minutes from
// its boot to show three things, and if it does not, the old image runs again without anybody's
// help.
//
//   the three     1. the mesh started;
//                 2. the WiFi joined and the clock was set from the network;
//                 3. a heartbeat was answered 2xx.
//
//                 All three seen in that one boot and the image is marked valid - then, and at
//                 no moment before. Ten minutes without all three and it is marked invalid and
//                 the board restarts into the old one.
//
//   a reset       A crash, the watchdog or a power cut during the trial has the same end, and
//                 nobody here has to do anything about it: the bootloader marks an image that is
//                 still pending-verify at a reset as aborted, which makes its otadata entry
//                 invalid, and the other entry - the old image's - is then the one it boots
//                 (components/bootloader_support/src/bootloader_utility.c and
//                 bootloader_common_loader.c at ESP-IDF 4.4.7 as pinned: `ota_select_invalid`
//                 is true for ABORTED). CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is the Arduino
//                 core's own default and so this firmware's, which tools/check-sdkconfig.py
//                 holds by not having to list it.
//
//   the core      The Arduino core marks a pending image valid **before `setup` runs** unless it
//                 is told otherwise (`initArduino` in cores/esp32/esp32-hal-misc.c, under
//                 CONFIG_APP_ROLLBACK_ENABLE). It is told otherwise: `verifyRollbackLater()` is
//                 a weak symbol and src/platform/BootChoice.cpp defines it `true`, so the
//                 verdict is this file's and nobody else's.
//
//   a bootloader  A board whose bootloader came from other firmware may have been built without
//   that does     that option, and then an image switched to is never marked pending and nothing
//   not undo      undoes it. The new image sees that at its boot - the running image's state is
//                 `new` or nothing rather than `pending` - says so in the journal, and goes by
//                 the same three conditions and the same ten minutes; when they fail it sets the
//                 boot choice back itself and restarts.
//
// **A flash over USB still wins.** README.md tells an owner to upgrade with the plain `.bin`,
// which the web flasher writes to the **first** slot whatever the board boots from. So at boot,
// when this firmware runs from the second slot and the first holds an image the record does not
// know, that the framework will load, and that carries this product's descriptor for this board,
// the firmware switches to it, once, and that image is on trial like any other. An image the
// record knows - the one running, the one it was switched from, or one it knows as tried and
// failed - is not switched to again.
//
// Everything in this file is portable and host-tested (test/test_trial): the slots and the boot
// choice are behind BootSlots, the three conditions behind TrialConditions, the clock behind
// TrialClock and the restart behind DeliberateRestart; the bytes of the slot that is not running
// are task 59's SlotSink, and the record is UpdateRecord.h. The board's halves are
// src/platform/BootChoice.cpp and src/main.cpp. **Nothing here calls the ESP-IDF**, and the one
// call that changes which slot a board boots is in one file of this firmware and no other
// (test/test_flash_writes).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Fetch.h"
#include "UpdateRecord.h"

namespace mm {
class TextOut;
}

namespace mm {
namespace update {

// How long a switched image has to show the three things, from its boot. **One constant**, and
// the task's own figure: long enough for a cold WiFi join, an SNTP answer and a heartbeat on a
// slow link, short enough that a board at a window with a bad image is back on the old one
// before anybody notices. tasks/phase-1.15/FOLLOWUPS.md carries it as a constant to settle.
static const uint32_t kTrialMs = 10u * 60u * 1000u;

// Two of the five step words this task's record writes into its journal, by name, because task 61
// reads them back: the heartbeat's `step` is the newest entry of the journal and a heartbeat that
// said `proving` and was answered is the third condition of the trial. The other three -
// `committing`, `rolling-back` and `restored` - are written here and read by nobody.
extern const char kStepSwitching[];
extern const char kStepProving[];

// The three, in the order they are reported.
enum class Condition : uint8_t { Mesh = 0, Link = 1, Server = 2 };
static const size_t kConditions = 3;

const char* conditionWord(Condition which);

// -- the seams ---------------------------------------------------------------------------------

// The rollback state of an application slot, as the framework keeps it in otadata
// (esp_ota_img_states_t). Only three of them mean anything here, and the rest are told apart so
// that a journal line can say which was found.
enum class SlotState : uint8_t {
  Unknown = 0,    // the framework would not say: no otadata, or a slot it does not know
  New = 1,        // switched to, and the bootloader has not marked it pending (see the header)
  Pending = 2,    // this boot is the image's one trial, and a reset ends it
  Valid = 3,      // proved
  Invalid = 4,    // marked invalid by an image that rejected itself
  Aborted = 5,    // a reset ended a trial and the bootloader wrote this
  Undefined = 6,  // the first image of a board: no rollback state at all
};

const char* slotStateWord(SlotState state);

// What the framework can say about an image lying in a slot.
struct SlotImage {
  bool verifies = false;  // the framework would load it (esp_image_verify)
  uint32_t length = 0;    // its bytes, as its own header says
};

// The two application slots, the boot choice and the rollback marks. One object, because a board
// has one pair of slots.
class BootSlots {
 public:
  virtual ~BootSlots() {}

  // False when none of this works on this board: one application slot, a table this boot has not
  // read, or a framework that will not name the running partition. `why()` is the one line a
  // person is given, and it is the slot's own (src/update/Fetch.h, SlotSink::why).
  virtual bool available() const = 0;
  virtual const char* why() const = 0;

  // Which application slot is running: 0 for the first, 1 for the second, -1 when unknown.
  virtual int runningSlot() const = 0;

  // The running image's rollback state.
  virtual SlotState runningState() const = 0;

  // What lies in the slot that is **not** running.
  virtual bool describeOther(SlotImage& out) const = 0;

  // Make the slot that is not running the next to boot. The framework verifies the image first
  // and refuses one it would not load. False when it refused, and nothing changed.
  virtual bool bootOther() = 0;

  // The running image has proved itself: cancel the rollback. False when the framework refused.
  virtual bool markValid() = 0;

  // The running image has not proved itself: mark it invalid and start again, which the
  // bootloader answers by booting the other slot. **Does not return** when it worked; false when
  // the framework refused (no image to go back to, or no rollback state to write).
  virtual bool markInvalidAndRestart() = 0;
};

// The trial's three conditions, each a plain reading of something that already exists on the
// board: the mesh, the WiFi and the clock, and the agent loop's heartbeat.
class TrialConditions {
 public:
  virtual ~TrialConditions() {}
  virtual bool meshStarted() const = 0;
  virtual bool linkAndClock() const = 0;   // WiFi joined **and** the clock set from the network
  virtual bool heartbeatAnswered() const = 0;
};

// The firmware's own deliberate restart path (src/Health.h): the reason written where every
// restart's reason is kept, what waits in RAM given its one try, and then the restart.
class DeliberateRestart {
 public:
  virtual ~DeliberateRestart() {}
  // Keep `why` as the reason of the restart that follows, so the next boot prints it rather than
  // guessing one from the reset cause.
  virtual void note(const char* why) = 0;
  // What waits in RAM for the server - the results the buffer holds and the adverts in the ring -
  // posted now, one try. Whatever is left goes with the RAM.
  virtual void postWhatWaits() = 0;
  // **Does not return.**
  virtual void restart() = 0;
};

// Both clocks: the milliseconds the ten minutes are measured in, and the UNIX seconds a journal
// entry is stamped with (0 when the board has no time).
class TrialClock {
 public:
  virtual ~TrialClock() {}
  virtual uint32_t ms() const = 0;
  virtual uint32_t epoch() const = 0;
};

// -- what a boot finds -------------------------------------------------------------------------

enum class BootFinding : uint8_t {
  Nothing = 0,     // no update in hand and nothing in the first slot to take: an ordinary boot
  NoSlots,         // this board has no two application slots; none of this runs
  TrialBegun,      // this boot is the new image's trial, and it has ten minutes
  RolledBack,      // the old image is back: the record was finished *rolled back*
  SwitchNotTaken,  // the record said switching and this is the same image: nothing was tried
  CutShort,        // the trial was interrupted on a board whose bootloader does not undo: the
                   // boot choice was set back here and the restart follows
  SwitchedAtBoot,  // the first slot held an image the record does not know: switched to it, and
                   // the restart follows
};

const char* bootFindingWord(BootFinding finding);

// -- the machine -------------------------------------------------------------------------------

class Updater {
 public:
  // The seams, as a struct rather than ten positional pointers: every one of them is a pointer
  // to an interface and a test that got two of them the wrong way round would still compile.
  struct Seams {
    BootSlots* slots = nullptr;
    SlotSink* slot = nullptr;  // the bytes of the slot that is not running (task 59)
    RecordStore* store = nullptr;
    TrialConditions* conditions = nullptr;
    DeliberateRestart* restart = nullptr;
    TrialClock* clock = nullptr;
  };

  // `runningVersion` is what this image says it is (its own descriptor's version), `board` what
  // this board calls itself (mm::boardName()), and `bootReason` what the board called this
  // reset (src/Health.h, bootReason()). `out` may be nullptr; with one, every step prints a line
  // as it happens. Nothing printed here is a token, a header or a byte of a secret.
  void begin(const Seams& seams, const char* runningVersion, const char* board, const char* bootReason,
             TextOut* out);

  // At boot, once: what is this boot? Reads the record, journals what it finds, and - for a
  // trial that was cut short or a flash over USB in the first slot - sets the boot choice and
  // restarts, which does not return on a board.
  BootFinding atBoot();

  // Every pass of the loop, and inside every wait a job takes. Does nothing at all unless a
  // trial is running.
  void step();

  // `update apply`: switch to the image task 59 judged, and restart. False and one sentence in
  // `why` when nothing was switched; true does not return on a board.
  bool apply(char* why, size_t cap);

  bool onTrial() const { return onTrial_; }
  // Milliseconds of the trial left, 0 when none is running.
  uint32_t trialLeftMs() const;
  bool conditionMet(Condition which) const;
  const UpdateRecord& record() const { return record_; }
  BootFinding finding() const { return finding_; }
  // Whether the bootloader marked this image pending, which is what decides who undoes a trial
  // that fails.
  bool bootloaderWillUndo() const { return pending_; }

  // Whether the slot that is **not** running may be written now, and the sentence a person is
  // given when it may not. It may not while an update is in hand: the other slot then holds the
  // image this board would go back to, and a fetch into it would write over the only firmware
  // that is known to work. nullptr when there is nothing in the way, which is every other
  // moment. Task 59's fetch is what asks (src/main.cpp).
  const char* whyTheOtherSlotMayNotBeWritten() const;

  // The lines `update` prints: the trial while there is one, and the last update's ending.
  void report(TextOut& out) const;
  // The words `status` adds to its first line while a trial runs, or "".
  const char* statusNote() const;

 private:
  void beginTrial(SlotState state);
  void commit();
  void rollBack(const char* why);
  void finish(Ended ended, const char* why);
  void journal(const char* step, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  bool save();
  // The version of an image the record has already met: the one running, the one it was switched
  // from, or the one it was switched to - tried and failed, or committed and now the other slot's.
  bool recordKnows(const char* version) const;
  // Switch to the other slot, having written the record: the boot choice, what waits in RAM, and
  // the restart, in that order. False when the boot choice refused and the record was put back.
  bool switchAndRestart(const char* why, bool postFirst);
  // What the three conditions came to, as a sentence for a journal note.
  void conditionsLine(char* out, size_t cap) const;
  // Which of the three was missing, for a rollback's reason.
  const char* whatIsMissing() const;

  Seams seams_;
  const char* version_ = "";
  const char* board_ = "";
  const char* bootReason_ = "";
  TextOut* out_ = nullptr;

  UpdateRecord record_;
  BootFinding finding_ = BootFinding::Nothing;
  bool begun_ = false;
  bool onTrial_ = false;
  bool pending_ = false;  // the bootloader marked this image pending: it will undo a failed trial
  uint32_t trialStartMs_ = 0;
  bool met_[kConditions] = {false, false, false};
  uint32_t metMs_[kConditions] = {0, 0, 0};
};

}  // namespace update
}  // namespace mm
