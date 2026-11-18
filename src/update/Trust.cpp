// The trust table this build was compiled with (task p1.15-58).
//
// The one translation unit that includes the generated header; everything else asks through the
// three functions of Trust.h, so a test can hand a reader any list of keys it likes.

#include "Trust.h"

#include <stdio.h>

// Written by tools/gen-trust-table.py before every build, from keys/release-keys.txt, into a
// directory that is git-ignored. A tree that has never been built has no such file, which is why
// tools/pio/release_headers.py is a `pre:` script of every environment and not of some of them.
#include "generated/trust_keys.h"

namespace mm {
namespace update {
namespace {

// Where the next write goes, given what snprintf said it would have written: never past the
// buffer's last byte, so a line too long for `out` is cut short rather than written over the end.
size_t advance(size_t at, int wrote, size_t len) {
  if (wrote <= 0) return at;
  const size_t room = len - at - 1;
  const size_t want = static_cast<size_t>(wrote);
  return at + (want < room ? want : room);
}

}  // namespace

const TrustedKey* trustedKeys() { return generated::kTrustedKeys; }

size_t trustedKeyCount() { return generated::kTrustedKeyCount; }

bool trustsTestKey() { return generated::kTrustsTestKey; }

void renderKeysLine(const TrustedKey* keys, size_t count, bool testKey, char* out, size_t len) {
  if (out == nullptr || len == 0) return;
  if (count == 0) {
    snprintf(out, len, "no release key in this build: no update will be taken");
    return;
  }
  // The ids, which are public and are what a signature line names. `at` never passes `len - 1`:
  // snprintf returns what it WOULD have written, so a truncated line must not move it along.
  size_t at = advance(0, snprintf(out, len, "%u release key%s trusted:", static_cast<unsigned>(count),
                                  count == 1 ? "" : "s"),
                      len);
  for (size_t i = 0; i < count; i++) {
    if (at + 1 >= len) break;
    at = advance(at,
                 snprintf(out + at, len - at, " %02x%02x%02x%02x%02x%02x%02x%02x", keys[i].id[0],
                          keys[i].id[1], keys[i].id[2], keys[i].id[3], keys[i].id[4], keys[i].id[5],
                          keys[i].id[6], keys[i].id[7]),
                 len);
  }
  if (testKey && at + 1 < len) {
    // A build that says this is a test build and is never a release: the key everybody has is in
    // the list above.
    snprintf(out + at, len - at, " (the specification's test key among them: a test build)");
  }
}

}  // namespace update
}  // namespace mm
