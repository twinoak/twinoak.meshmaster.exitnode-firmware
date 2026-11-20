// The radio's receiver: the timer that puts it back, and the floor that follows one
// (task p1.18-66, src/mesh/Receiver.h).
//
// Three things are held here. **The number**: `agc.reset.interval` in whole seconds becomes the
// milliseconds MeshCore's dispatcher arms its timer with, 0 staying 0 because 0 is upstream's own
// word for *never*. **The words**: what `status`' `radio` line says of the interval and what its
// `frames` line says of a floor that is being sampled again - both from one function each, so the
// line a person reads at the window is the line a test holds. **The board**, read off the source,
// because `AgentMesh` needs a radio to run: the dispatcher's hook is handed this file's answer and
// nothing of ours resets a radio itself, sets an interference threshold or turns on the hardware
// channel-activity check.
//
// And the citations, against the pinned MeshCore on disk, as test_flood_scope holds its own: a
// bump that moved the timer, the default of 0 or the floor's reset is a red test here and not a
// silent change of what this board's receiver does.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mesh/Receiver.h"

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace fs = std::filesystem;

namespace {

std::string readWhole(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

size_t occurrences(const std::string& haystack, const std::string& needle) {
  size_t n = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) n++;
  return n;
}

// Every source file of ours: src/ and the portable library, headers included.
std::vector<fs::path> ourSources() {
  std::vector<fs::path> files;
  for (const char* root : {"src", "lib/agent-core"}) {
    const fs::path dir = fs::path(PROJECT_DIR) / root;
    if (!fs::is_directory(dir)) continue;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(dir)) {
      if (!e.is_regular_file()) continue;
      const std::string ext = e.path().extension().string();
      if (ext == ".h" || ext == ".cpp") files.push_back(e.path());
    }
  }
  return files;
}

std::string intervalPhrase() {
  char out[48] = {0};
  mm::agcResetPhrase(out, sizeof out);
  return std::string(out);
}

std::string floorPhrase(int dbm) {
  char out[48] = {0};
  mm::noiseFloorPhrase(dbm, out, sizeof out);
  return std::string(out);
}

}  // namespace

// ---- the number -------------------------------------------------------------------------------

// First in the file, because it is the one test that reads the interval nothing has pushed: four
// seconds, which is what the standard profile gives the repeaters and what the key's fallback
// says (test/test_console_parser holds the two numbers together).
TEST(TheReceiversTimer, IsFourSecondsBeforeAnythingIsPushed) {
  EXPECT_EQ(4, mm::kAgcResetDefaultSeconds);
  EXPECT_EQ(mm::kAgcResetDefaultSeconds, mm::agcResetSeconds());
  EXPECT_EQ(4000, mm::agcResetIntervalMs());
  EXPECT_EQ("receiver reset every 4 s", intervalPhrase());
}

// What `Dispatcher::getAGCResetInterval()` is for: milliseconds, because `futureMillis(int)` is
// what the dispatcher arms its timer with (src/Dispatcher.cpp at the pin) and upstream's own
// companion multiplies its four-second units by 4000 for the same hook.
TEST(TheReceiversTimer, IsTheSettingInMilliseconds) {
  mm::setAgcResetSeconds(4);
  EXPECT_EQ(4000, mm::agcResetIntervalMs());
  mm::setAgcResetSeconds(1);
  EXPECT_EQ(1000, mm::agcResetIntervalMs());
  mm::setAgcResetSeconds(30);
  EXPECT_EQ(30000, mm::agcResetIntervalMs());
  mm::setAgcResetSeconds(3600);
  EXPECT_EQ(3600000, mm::agcResetIntervalMs());
  EXPECT_EQ(3600, mm::agcResetSeconds());
}

// 0 is never, and it stays 0: the dispatcher's own test is `getAGCResetInterval() > 0`, so a
// 0 there is the timer off and no reset is ever made.
TEST(TheReceiversTimer, ZeroIsNeverAndStaysZero) {
  mm::setAgcResetSeconds(0);
  EXPECT_EQ(0, mm::agcResetSeconds());
  EXPECT_EQ(0, mm::agcResetIntervalMs());
  EXPECT_EQ("receiver never reset", intervalPhrase());
}

// A number outside the range the console takes - one that reached NVS before this range existed,
// or a key written by hand - is clamped into it rather than turning the timer off by accident.
TEST(TheReceiversTimer, ClampsWhatIsOutsideTheRange) {
  mm::setAgcResetSeconds(-1);
  EXPECT_EQ(mm::kAgcResetMinSeconds, mm::agcResetSeconds());
  EXPECT_EQ(0, mm::agcResetIntervalMs());
  mm::setAgcResetSeconds(100000);
  EXPECT_EQ(mm::kAgcResetMaxSeconds, mm::agcResetSeconds());
  EXPECT_EQ(3600 * 1000, mm::agcResetIntervalMs());
  EXPECT_EQ(0, mm::kAgcResetMinSeconds);
  EXPECT_EQ(3600, mm::kAgcResetMaxSeconds);
}

// A setting changed at the console is what the next call answers: main.cpp pushes it from
// `onSettingChanged` and the dispatcher reads this on its next loop, so nothing waits for a
// restart. The sequence a bench walk makes.
TEST(TheReceiversTimer, ANewSettingIsWhatTheNextCallAnswersWithNoRestart) {
  mm::setAgcResetSeconds(4);
  ASSERT_EQ(4000, mm::agcResetIntervalMs());
  mm::setAgcResetSeconds(10);
  EXPECT_EQ(10000, mm::agcResetIntervalMs()) << "the next call, not the next boot";
  EXPECT_EQ("receiver reset every 10 s", intervalPhrase());
  mm::setAgcResetSeconds(0);
  EXPECT_EQ(0, mm::agcResetIntervalMs());
  EXPECT_EQ("receiver never reset", intervalPhrase());
  mm::setAgcResetSeconds(4);
  EXPECT_EQ(4000, mm::agcResetIntervalMs());
  EXPECT_EQ("receiver reset every 4 s", intervalPhrase());
}

// ---- the words of `status` --------------------------------------------------------------------

// The `radio` line names the seconds, and *never* at 0. Whatever the number is.
TEST(TheRadioLine, NamesTheIntervalOrNever) {
  mm::setAgcResetSeconds(mm::kAgcResetDefaultSeconds);
  EXPECT_EQ("receiver reset every 4 s", intervalPhrase());
  mm::setAgcResetSeconds(1);
  EXPECT_EQ("receiver reset every 1 s", intervalPhrase());
  mm::setAgcResetSeconds(3600);
  EXPECT_EQ("receiver reset every 3600 s", intervalPhrase());
  mm::setAgcResetSeconds(0);
  EXPECT_EQ("receiver never reset", intervalPhrase());
  EXPECT_EQ(std::string::npos, intervalPhrase().find("0 s")) << "never is a word, not a number";
  mm::setAgcResetSeconds(mm::kAgcResetDefaultSeconds);
}

// A short buffer is cut and still NUL-terminated: both lines are printed from a caller's array.
TEST(TheRadioLine, FitsWhateverBufferItIsGiven) {
  mm::setAgcResetSeconds(4);
  char out[10] = {0};
  mm::agcResetPhrase(out, sizeof out);
  EXPECT_EQ(std::string("receiver "), std::string(out));
  char one[1] = {'x'};
  mm::agcResetPhrase(one, sizeof one);
  EXPECT_STREQ("", one);
  mm::noiseFloorPhrase(-109, one, sizeof one);
  EXPECT_STREQ("", one);
}

// **A floor that is being measured again is no reading.** MeshCore sets `_noise_floor = 0` in
// `resetAGC()` so the sampling reconverges from scratch, and answers 0 until it has; a floor is
// negative dBm, so 0 is never printed as a level. -999 is this firmware's own *not measured*.
TEST(TheFloor, IsAReadingOnlyWhenItIsALevel) {
  EXPECT_TRUE(mm::noiseFloorIsReading(-109));
  EXPECT_TRUE(mm::noiseFloorIsReading(-120));
  EXPECT_TRUE(mm::noiseFloorIsReading(-1));
  EXPECT_FALSE(mm::noiseFloorIsReading(0)) << "the floor is being sampled again after a reset";
  EXPECT_FALSE(mm::noiseFloorIsReading(-999)) << "nothing was measured at all";
  EXPECT_FALSE(mm::noiseFloorIsReading(-1000));
}

TEST(TheFloor, SaysItIsBeingMeasuredRatherThanZeroDbm) {
  EXPECT_EQ("noise floor -112 dBm", floorPhrase(-112));
  EXPECT_EQ("noise floor -109 dBm", floorPhrase(-109));
  EXPECT_EQ("noise floor being measured", floorPhrase(0));
  EXPECT_EQ("noise floor being measured", floorPhrase(-999));
  EXPECT_EQ(std::string::npos, floorPhrase(0).find("0 dBm")) << "no 0 is printed as a level";
  EXPECT_EQ(std::string::npos, floorPhrase(0).find("dBm"));
}

// ---- what the board does with it, read off the source -----------------------------------------

// `AgentMesh::getAGCResetInterval()` is the whole of this firmware's part in the reset: the
// dispatcher's hook, answered with this file's number. A grep, because `AgentMesh` needs a radio.
TEST(Board, TheDispatchersHookIsHandedThisFilesAnswer) {
  const std::string mesh = readWhole(fs::path(PROJECT_DIR) / "src" / "mesh" / "AgentMesh.h");
  ASSERT_FALSE(mesh.empty());
  EXPECT_NE(mesh.find("int getAGCResetInterval() const override { return agcResetIntervalMs(); }"), std::string::npos)
      << "src/mesh/AgentMesh.h must answer the dispatcher's hook with mm::agcResetIntervalMs()";

  // One override of the hook in the whole of our tree, and one place the number is read: that
  // override. Nothing else of ours has anything to do with the dispatcher's timer.
  size_t overrides = 0;
  size_t readers = 0;
  for (const fs::path& f : ourSources()) {
    const std::string text = readWhole(f);
    overrides += occurrences(text, "int getAGCResetInterval() const override");
    readers += occurrences(text, "return agcResetIntervalMs();");
  }
  EXPECT_EQ(1u, overrides) << "one override of the hook, and it is src/mesh/AgentMesh.h's";
  EXPECT_EQ(1u, readers) << "the interval is read in one place: that override";
}

// **No second timer of ours, and no reset of ours.** The dispatcher calls the radio's `resetAGC()`
// itself; a line of ours that did it too would reset a receiver mid-receive and on its own clock.
TEST(Board, NoLineOfOursResetsARadio) {
  for (const fs::path& f : ourSources()) {
    const std::string text = readWhole(f);
    for (const char* call : {"->resetAGC(", ".resetAGC(", "doResetAGC("}) {
      EXPECT_EQ(std::string::npos, text.find(call)) << f.string() << " calls " << call
                                                    << " - the dispatcher's timer is the only thing that resets the radio";
    }
  }
}

// **Nothing else of the radio changes** (the task's deliverable 5): the interference threshold and
// the hardware channel-activity check are MeshCore's defaults - off - and no reply waits a
// different length because of this task.
TEST(Board, NeitherTheInterferenceThresholdNorTheCadCheckIsTurnedOn) {
  for (const fs::path& f : ourSources()) {
    const std::string text = readWhole(f);
    for (const char* hook : {"getInterferenceThreshold", "getCADEnabled", "setCADEnabled", "triggerNoiseFloorCalibrate"}) {
      EXPECT_EQ(std::string::npos, text.find(hook)) << f.string() << " names " << hook;
    }
  }
}

// `status` reads the radio's floor and the interval through the two functions above and formats
// neither itself, so the `frames` line cannot print a `0 dBm` by another route.
TEST(Board, StatusPrintsBothLinesFromTheseFunctions) {
  const std::string main = readWhole(fs::path(PROJECT_DIR) / "src" / "main.cpp");
  ASSERT_FALSE(main.empty());
  EXPECT_EQ(2u, occurrences(main, "mm::agcResetPhrase("))
      << "the interval is said in two places and in the same words: the line `applyAgcSettings` logs "
         "when it is read, and the `radio` line of `status`";
  EXPECT_EQ(1u, occurrences(main, "mm::noiseFloorPhrase(")) << "one `frames` line, one phrase";
  EXPECT_EQ(std::string::npos, main.find("noise floor %d")) << "src/main.cpp must not format the floor itself";
  EXPECT_EQ(2u, occurrences(main, "radio_driver.getNoiseFloor()"))
      << "the floor is read twice: the `frames` line and the observer's stats";
}

// The observer's message leaves `noise_floor` out for a floor that is being measured, and says so
// from the same rule (test/test_observer walks the bytes).
TEST(Board, TheObserversDialectAsksTheSameQuestion) {
  const std::string dialect = readWhole(fs::path(PROJECT_DIR) / "src" / "observer" / "Dialect.cpp");
  ASSERT_FALSE(dialect.empty());
  EXPECT_EQ(2u, occurrences(dialect, "noiseFloorIsReading(f.noiseFloor)"))
      << "the `stats` object's existence and the member itself both ask it";
}

// ---- the citations, against the pinned source on disk -----------------------------------------

// Skipped when MeshCore is not unpacked, as test_flood_scope's own citation test is: CI's native
// job installs the host dependencies alone (docs/upstream.md, tasks/sync/TEMPLATE.md).
TEST(Upstream, TheTimerAndTheFloorAreStillWhatThePinnedSourceDoes) {
  const fs::path meshcore = fs::path(PROJECT_DIR) / ".pio" / "libdeps" / "native" / "MeshCore";
  if (!fs::is_directory(meshcore)) GTEST_SKIP() << meshcore << " is not unpacked";

  // The hook: off unless an application overrides it, which is why this task exists at all.
  const std::string header = readWhole(meshcore / "src" / "Dispatcher.h");
  ASSERT_FALSE(header.empty());
  EXPECT_NE(header.find("virtual int getAGCResetInterval() const { return 0; }"), std::string::npos)
      << "Dispatcher::getAGCResetInterval is no longer disabled by default";
  EXPECT_NE(header.find("unsigned long futureMillis(int millis_from_now) const;"), std::string::npos)
      << "the interval is milliseconds because this is what arms the timer";
  EXPECT_NE(header.find("virtual void resetAGC() { }"), std::string::npos) << "the radio's own reset";

  // The timer: armed after every send and when it has run out, and the reset made by the
  // dispatcher itself.
  const std::string loop = readWhole(meshcore / "src" / "Dispatcher.cpp");
  ASSERT_FALSE(loop.empty());
  EXPECT_NE(loop.find("if (getAGCResetInterval() > 0 && millisHasNowPassed(next_agc_reset_time)) {"), std::string::npos)
      << "0 is still never";
  EXPECT_EQ(2u, occurrences(loop, "next_agc_reset_time = futureMillis(getAGCResetInterval());"))
      << "armed going back into receive mode, and again after a reset";
  EXPECT_NE(loop.find("_radio->resetAGC();"), std::string::npos) << "the dispatcher resets the radio, not we";

  // The floor: 0 until the sampling has converged again, which is what `being measured` means.
  const std::string wrapper = readWhole(meshcore / "src" / "helpers" / "radiolib" / "RadioLibWrappers.cpp");
  ASSERT_FALSE(wrapper.empty());
  const size_t reset = wrapper.find("void RadioLibWrapper::resetAGC() {");
  ASSERT_NE(reset, std::string::npos);
  const std::string body = wrapper.substr(reset, wrapper.find("\n}\n", reset) - reset);
  EXPECT_NE(body.find("_noise_floor = 0;"), std::string::npos)
      << "a reset no longer starts the floor's sampling from scratch";
  EXPECT_NE(body.find("if ((state & STATE_INT_READY) != 0 || isReceivingPacket()) return;"), std::string::npos)
      << "a reset is still never made mid-receive";
  const std::string wrapperHeader = readWhole(meshcore / "src" / "helpers" / "radiolib" / "RadioLibWrappers.h");
  EXPECT_NE(wrapperHeader.find("int getNoiseFloor() const override { return _noise_floor; }"), std::string::npos)
      << "and getNoiseFloor still hands that 0 straight out";

  // Upstream's own companion turns the timer on from a setting, in milliseconds.
  const std::string companion = readWhole(meshcore / "examples" / "companion_radio" / "MyMesh.h");
  ASSERT_FALSE(companion.empty());
  EXPECT_NE(companion.find("return ((int)_prefs.agc_reset_interval) * 4000;"), std::string::npos)
      << "the companion's own units are four seconds, and its hook is milliseconds";
}

// PlatformIO's googletest integration links no gtest_main, so every test file brings its own.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
