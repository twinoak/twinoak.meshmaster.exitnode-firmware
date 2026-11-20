#include "LoopStack.h"

#include <stdio.h>

namespace mm {
namespace platform {

namespace {

LoopStackReader theReader = nullptr;

}  // namespace

void readLoopStackWith(LoopStackReader reader) { theReader = reader; }

void loopStackLine(char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  const LoopStack s = theReader != nullptr ? theReader() : LoopStack();
  if (s.sizeBytes == 0) {
    snprintf(out, cap, "loop task: nothing measured yet");
    return;
  }
  if (s.leastEverFree == 0) {
    snprintf(out, cap, "loop task: %lu bytes; nothing measured yet", static_cast<unsigned long>(s.sizeBytes));
    return;
  }
  snprintf(out, cap, "loop task: %lu bytes, the least ever free %lu", static_cast<unsigned long>(s.sizeBytes),
           static_cast<unsigned long>(s.leastEverFree));
}

}  // namespace platform
}  // namespace mm
