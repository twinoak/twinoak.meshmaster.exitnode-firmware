// The loop task's stack, as a person reads it (task p1.17-65).
//
// 0.7.3 did not start: three copies of an update's record did not fit the 8 192 bytes the Arduino
// core's loop task had. The task has 16 384 now, and the board says at boot and in `status` how
// much of them was never used - because nobody had ever measured it, and because the number
// falling is the warning that the next deep call is the one that does not fit.
//
// All three lines are one function's words (src/platform/LoopStack.h), over a seam that is the
// configuration and FreeRTOS on a board and this test's own figures here - the boot's line, the
// `stack` line of `status`, and the first of the three `stack` lines of `get mqtt.status`. Task 65
// wrote the second of those into `get mqtt.status` alone while its words said `status`, and
// Thomas typed `status` on 20 November and found nothing (task p1.18-67), so the tests at the end
// of this file walk src/main.cpp command by command: which command prints which line, and the two
// numbers formatted nowhere but that one function. Two lines that could drift apart are two lines
// a person cannot compare, and a line the words promise and no command prints is worse than
// either.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/LoopStack.h"

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

// What the scripted seam answers with. A function pointer is the seam, so the figures live here.
mm::platform::LoopStack scripted;

mm::platform::LoopStack readScripted() { return scripted; }

std::string lineNow() {
  char out[96] = {0};
  mm::platform::loopStackLine(out, sizeof out);
  return std::string(out);
}

std::string fileText(const std::string& relative) {
  std::ifstream in(std::filesystem::path(PROJECT_DIR) / relative, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

size_t occurrences(const std::string& haystack, const std::string& needle) {
  size_t n = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) n++;
  return n;
}

// One function of src/main.cpp, from its signature to the closing brace in the first column, as
// test_receiver reads a function of the pinned MeshCore. Empty when there is no such function,
// which every caller asserts on: a renamed command must not quietly pass these tests.
std::string functionBody(const std::string& text, const std::string& signature) {
  const size_t at = text.find(signature);
  if (at == std::string::npos) return std::string();
  const size_t end = text.find("\n}\n", at);
  return end == std::string::npos ? text.substr(at) : text.substr(at, end - at);
}

}  // namespace

TEST(TheLoopStackLine, SaysTheSizeAndTheLeastEverFree) {
  scripted = mm::platform::LoopStack();
  scripted.sizeBytes = 16384;
  scripted.leastEverFree = 11240;
  mm::platform::readLoopStackWith(&readScripted);
  EXPECT_EQ("loop task: 16384 bytes, the least ever free 11240", lineNow());
}

TEST(TheLoopStackLine, SaysTheSizeAloneUntilSomethingHasBeenMeasured) {
  scripted = mm::platform::LoopStack();
  scripted.sizeBytes = 16384;
  mm::platform::readLoopStackWith(&readScripted);
  EXPECT_EQ("loop task: 16384 bytes; nothing measured yet", lineNow());
}

TEST(TheLoopStackLine, SaysNothingMeasuredWithNoSeamAtAll) {
  mm::platform::readLoopStackWith(nullptr);
  EXPECT_EQ("loop task: nothing measured yet", lineNow());
}

// A board whose stack is nearly gone is the one a person has to be able to read, so the small
// numbers are the ones held here as well.
TEST(TheLoopStackLine, ReadsTheSameWhateverTheFiguresAre) {
  scripted = mm::platform::LoopStack();
  scripted.sizeBytes = 16384;
  scripted.leastEverFree = 96;
  mm::platform::readLoopStackWith(&readScripted);
  EXPECT_EQ("loop task: 16384 bytes, the least ever free 96", lineNow());

  scripted.sizeBytes = 8192;
  scripted.leastEverFree = 4294967295u;  // the whole range of the figure FreeRTOS gives
  EXPECT_EQ("loop task: 8192 bytes, the least ever free 4294967295", lineNow());
}

// A short buffer is cut and still NUL-terminated: the line is printed from a caller's own array.
TEST(TheLoopStackLine, FitsWhateverBufferItIsGiven) {
  scripted = mm::platform::LoopStack();
  scripted.sizeBytes = 16384;
  scripted.leastEverFree = 11240;
  mm::platform::readLoopStackWith(&readScripted);
  char out[12] = {0};
  mm::platform::loopStackLine(out, sizeof out);
  EXPECT_EQ(std::string("loop task: "), std::string(out)) << "eleven characters and a NUL";
  char one[1] = {'x'};
  mm::platform::loopStackLine(one, sizeof one);
  EXPECT_STREQ("", one);
}

// The boot's line, `status`' line and `get mqtt.status`' first line are the same words, because
// there is one function and three callers of it. A fourth place formatting either number is what
// this refuses.
TEST(TheLoopStackLine, TheBootAndStatusPrintItFromTheOneFunction) {
  const std::string main = fileText("src/main.cpp");
  ASSERT_FALSE(main.empty()) << "src/main.cpp could not be read";
  EXPECT_EQ(3u, occurrences(main, "loopStackLine("))
      << "src/main.cpp must call mm::platform::loopStackLine exactly three times: the boot's line "
         "when setup() ends, the `stack` line of `status`, and the loop task's line of "
         "`get mqtt.status`";
  EXPECT_EQ(1u, occurrences(main, "getArduinoLoopTaskStackSize()"))
      << "the loop task's size is read in one place, the seam of src/platform/LoopStack.h";
  EXPECT_EQ(1u, occurrences(main, "uxTaskGetStackHighWaterMark(loopTaskHandle)"))
      << "and so is the least that was ever free";
}

// **`status` prints it.** This is the one task 65 got wrong: the line went beside the observer's
// own `stack` lines, which are in `printMqttStatus` and not in `printStatus`, and `status` printed
// no such line at all while two documents said it did.
TEST(TheLoopStackLine, StatusPrintsTheLoopTasksLineAndNoOther) {
  const std::string main = fileText("src/main.cpp");
  ASSERT_FALSE(main.empty()) << "src/main.cpp could not be read";
  const std::string status = functionBody(main, "void printStatus(Stream& out) {");
  ASSERT_FALSE(status.empty()) << "src/main.cpp has no printStatus(Stream&)";
  EXPECT_EQ(1u, occurrences(status, "loopStackLine("))
      << "`status` prints the loop task's line, from the one function that forms it";
  EXPECT_EQ(1u, occurrences(status, "\"stack     %s\\r\\n\""))
      << "under the same `stack` prefix every other line of `status` is aligned to";
  EXPECT_EQ(std::string::npos, status.find("slot %d task"))
      << "and the observer's two tasks' lines stay in `get mqtt.status`, which is where the rest "
         "of the observer's memory is read";
}

// **`get mqtt.status` keeps all three of its lines**: the loop task's, for scale, and the
// observer's two tasks'. Nothing a person reads there today is taken away by this task.
TEST(TheLoopStackLine, GetMqttStatusKeepsItsThreeLines) {
  const std::string main = fileText("src/main.cpp");
  ASSERT_FALSE(main.empty()) << "src/main.cpp could not be read";
  const std::string mqtt = functionBody(main, "void printMqttStatus(Stream& out) {");
  ASSERT_FALSE(mqtt.empty()) << "src/main.cpp has no printMqttStatus(Stream&)";
  EXPECT_EQ(1u, occurrences(mqtt, "loopStackLine(")) << "the loop task's line, as before";
  EXPECT_EQ(2u, occurrences(mqtt, "\"stack     slot %d task:"))
      << "and a slot's line in both of its shapes: measured, and nothing measured yet";
  EXPECT_EQ(2u, occurrences(mqtt, "mm::MqttClient::kTaskStackBytes"))
      << "a slot task's size is the client's own constant, 6144 bytes, and not a number typed here";
}

// **The boot prints it when `setup()` ends**, which is the line 0.7.3 never reached.
TEST(TheLoopStackLine, TheBootPrintsItWhenSetupEnds) {
  const std::string main = fileText("src/main.cpp");
  ASSERT_FALSE(main.empty()) << "src/main.cpp could not be read";
  const std::string setup = functionBody(main, "\nvoid setup() {");
  ASSERT_FALSE(setup.empty()) << "src/main.cpp has no setup()";
  EXPECT_EQ(1u, occurrences(setup, "loopStackLine(")) << "one line, at the end of setup()";
  EXPECT_EQ(1u, occurrences(setup, "LOG_I(\"stack: %s\", stack)"))
      << "logged as `stack: loop task: ...`, which is what docs/field.md tells Thomas to look for";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
