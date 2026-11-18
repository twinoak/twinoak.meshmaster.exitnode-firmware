// The descriptor this image carries about itself (task p1.15-58, deliverable 1).
//
// The 96 bytes of FIRMWARE-MANIFEST.md section 4, placed in the application image by the build:
// tools/pio/release_headers.py writes them into src/update/generated/image_descriptor.h before
// every board build, from tools/release/manifest.py's `make_descriptor` - the same function
// tools/release/build_release.py writes the manifest with, so the bytes in the image and the
// entry above them cannot come from two different ideas of section 4.
//
// **Exactly one must be in the image**, and the merged image carries the same application, so
// exactly one is in that too. tools/check-descriptor.py holds both, for both boards, on every CI
// build, and the release gate holds them again before anything is published.
//
// `__attribute__((used))` keeps the compiler from dropping the array; `thisImageDescriptor()` is
// what keeps the linker from collecting it, because `update` on the console prints what it says.
// A descriptor nothing reads is a descriptor --gc-sections takes out of the image.
//
// Not built for [env:native]: a host build is no image and has no version or board to describe.

#include <stdint.h>

#include "Descriptor.h"
#include "generated/image_descriptor.h"

namespace mm {
namespace update {

namespace {
// Four-byte aligned, because a reader finds a descriptor only on a 4-byte boundary (section 4).
__attribute__((used, aligned(4))) const uint8_t kImageDescriptor[kDescriptorLength] = {
    MESHMASTER_IMAGE_DESCRIPTOR_BYTES};
}  // namespace

const uint8_t* thisImageDescriptorBytes() { return kImageDescriptor; }

bool thisImageDescriptor(Descriptor& out) { return parseDescriptor(kImageDescriptor, out); }

}  // namespace update
}  // namespace mm
