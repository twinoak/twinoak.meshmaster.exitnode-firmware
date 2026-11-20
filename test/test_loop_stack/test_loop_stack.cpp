// The loop task's stack, as a person reads it (task p1.17-65).
//
// 0.7.3 did not start: three copies of an update's record did not fit the 8 192 bytes the Arduino
// core's loop task had. The task has 16 384 now, and the board says at boot and in `status` how
// much of them was never used - because nobody had ever measured it, and because the number
// falling is the warning that the next deep call is the one that does not fit.
//
// Both lines are one function's words (src/platform/LoopStack.h), over a seam that is the
// configuration and FreeRTOS on a board and this test's own figures here. The last test walks
// src/main.cpp and fails if the two numbers are ever formatted anywhere but that one function:
// two lines that could drift apart are two lines a person cannot compare.

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

// The boot's line and `status`' line are the same words, because there is one function and two
// callers of it. A third place formatting either number is what this refuses.
TEST(TheLoopStackLine, TheBootAndStatusPrintItFromTheOneFunction) {
  const std::string main = fileText("src/main.cpp");
  ASSERT_FALSE(main.empty()) << "src/main.cpp could not be read";
  EXPECT_EQ(2u, occurrences(main, "loopStackLine("))
      << "src/main.cpp must call mm::platform::loopStackLine exactly twice: the boot's line when "
         "setup() ends, and the `stack` line of `status`";
  EXPECT_EQ(1u, occurrences(main, "getArduinoLoopTaskStackSize()"))
      << "the loop task's size is read in one place, the seam of src/platform/LoopStack.h";
  EXPECT_EQ(1u, occurrences(main, "uxTaskGetStackHighWaterMark(loopTaskHandle)"))
      << "and so is the least that was ever free";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
