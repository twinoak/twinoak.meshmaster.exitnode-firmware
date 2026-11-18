// What this image says about itself, for the board that is running it (task p1.15-58).
//
// Declared apart from Descriptor.h because the bytes are a board build's: [env:native] builds the
// reader of section 4 and not an image, so it has no descriptor of its own to read.

#pragma once

#include <stdint.h>

#include "Descriptor.h"

namespace mm {
namespace update {

// The 96 bytes in this image, as the build placed them.
const uint8_t* thisImageDescriptorBytes();

// Them, read. False would mean the build put something that is not a descriptor in the image,
// which tools/check-descriptor.py would have failed CI for first.
bool thisImageDescriptor(Descriptor& out);

}  // namespace update
}  // namespace mm
