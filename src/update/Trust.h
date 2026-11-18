// The public keys this build trusts a release manifest to be signed with (task p1.15-58).
//
// FIRMWARE-MANIFEST.md section 3: a build trusts a small fixed list of keys, room for three, so
// that a key can be replaced by shipping a release that trusts both the old and the new one and
// then one that trusts only the new. The list is keys/release-keys.txt, turned into
// src/update/generated/trust_keys.h by tools/gen-trust-table.py before every build; the release
// gate reads the same file by the same rules (tools/release/verify_release.py).
//
// **A build with no key takes no update at all.** That is the state before the key exists and it
// is the safe one: nothing can be signed in a way this firmware believes, so nothing is taken.
// `update` on the console says so in those words, and task 61's heartbeat member is not written
// by such a build.
//
// Nothing here is a secret. A public key and its id are published with every release.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mm {
namespace update {

// Section 3's room for three.
static const size_t kMaxTrustedKeys = 3;

struct TrustedKey {
  // The first eight bytes of SHA-256 over `key`, which the signature line names so that a
  // verifier holding more than one key can pick the right one. It proves nothing by itself.
  uint8_t id[8];
  uint8_t key[32];
};

// The table this build was compiled with, and how many of its rows are real.
const TrustedKey* trustedKeys();
size_t trustedKeyCount();

// Whether this build trusts the specification's public test key: true for [env:native] and for no
// board environment. A release build that said true would take anybody's firmware.
bool trustsTestKey();

// The line `update` prints about the keys, into `out`. Separated from the table so that a test can
// render the build that has no key at all without being one.
void renderKeysLine(const TrustedKey* keys, size_t count, bool testKey, char* out, size_t len);

}  // namespace update
}  // namespace mm
