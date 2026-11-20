// Reading a release, and deciding whether to believe it (task p1.15-58, deliverable 7).
//
// FIRMWARE-MANIFEST.md section 5, in its order, as this firmware makes it:
//
//   1  the signature line names a key this build trusts and verifies over manifest.json's bytes
//   2  the manifest validates (section 2) and its product is this firmware's
//   3  there is an image for this board: role `exit-node`, band `868`
//   4  the file's size and SHA-256 are the manifest's
//   5  the file holds exactly one descriptor and it agrees with its manifest entry
//
// (Section 5's sixth check is about a `swd` file's app_region, which this product publishes none
// of.) **Every refusal carries the number of the check that refused it**, 1 to 5, so that what
// meshmaster.dk is told and what a person reads on the console are the same five words.
//
// Nothing here calls the ESP-IDF, touches flash or opens a socket: it is bytes in, a verdict out,
// and the native tests build all of it (test/test_release_reader). Task 59 fetches the bytes into
// the other slot and runs checks 4 and 5 over what was written; this file is what it runs.
//
// The signature is Ed25519 as in RFC 8032 over the manifest's bytes - MeshCore's own lib/ed25519,
// which this firmware already carries for its identity, proven on the RFC's first test vector
// before it is trusted with a release.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Descriptor.h"
#include "Trust.h"

namespace mm {
namespace update {

// The checks, by their number in section 5.
static const uint8_t kCheckSignature = 1;
static const uint8_t kCheckManifest = 2;
static const uint8_t kCheckImage = 3;
static const uint8_t kCheckHash = 4;
static const uint8_t kCheckDescriptor = 5;

// What this product is, in the manifest's words. tools/release/build_release.py writes the same
// three strings; neither reads them from the other, which is the point of having both.
extern const char kProduct[];
extern const char kRole[];
extern const char kBand[];

struct Outcome {
  bool ok;
  uint8_t check;  // 0 when ok, else 1 to 5
  char why[192];
};

// The bytes of one file of a release, wherever they are: in RAM here, in the other application
// slot in task 59. Reading is by offset, so nothing has to hold 1.4 MB at once.
class ReleaseBytes {
 public:
  virtual ~ReleaseBytes() {}
  virtual uint32_t size() const = 0;
  // False for a read that runs past the end or that the medium refused.
  virtual bool read(uint32_t offset, uint8_t* out, uint32_t len) const = 0;
};

class MemoryBytes : public ReleaseBytes {
 public:
  MemoryBytes(const uint8_t* data, uint32_t len) : data_(data), len_(len) {}
  uint32_t size() const override { return len_; }
  bool read(uint32_t offset, uint8_t* out, uint32_t len) const override;

 private:
  const uint8_t* data_;
  uint32_t len_;
};

static const size_t kMaxFilesPerImage = 4;

struct ReleaseFile {
  char use[10];
  char name[96];
  uint32_t size;
  uint8_t sha256[32];
  bool hasLoadAddress;
  uint32_t loadAddress;
};

struct ReleaseInfo {
  char product[40];
  char version[33];
  char tag[24];
  char commit[41];
  uint8_t linkMajor;
  uint8_t linkMinor;
  // The one image of the manifest that is for this board, and its files.
  char imageId[56];
  char role[16];
  char board[24];
  char band[10];
  char mcu[12];
  ReleaseFile files[kMaxFilesPerImage];
  size_t fileCount;
};

// Checks 1, 2 and 3. `manifest` is the bytes of manifest.json exactly as they were signed and
// `signatureLine` the one line of manifest.json.sig; `board` is what this board calls itself
// (mm::boardName()). On ok, `out` holds the image for that board.
//
// **`out` is emptied first and filled in place**, so a refusal leaves nothing of a release half
// read in it whatever it held before (task p1.17-65). It is 872 bytes: a second one of them on the
// stack beside it was most of a frame this reader is not allowed (tools/check-frames.py).
Outcome readRelease(const uint8_t* manifest, size_t manifestLen, const char* signatureLine,
                    const TrustedKey* keys, size_t keyCount, const char* board, ReleaseInfo& out);

// Checks 4 and 5, over the bytes of one file of that release.
Outcome checkFile(const ReleaseInfo& info, const ReleaseFile& file, const ReleaseBytes& bytes);

// Section 4 over a whole file: exactly one valid descriptor on a 4-byte boundary. `count` is how
// many were found, so that none and two can be told apart in the sentence.
bool findOneDescriptor(const ReleaseBytes& bytes, Descriptor& out, uint32_t& offset, size_t& count);

// The file of a release with this `use` ("ota", "factory"), or nullptr.
const ReleaseFile* fileWithUse(const ReleaseInfo& info, const char* use);

}  // namespace update
}  // namespace mm
