# 58 — A release says what it is, and is signed

**Depends on:** —
**Spec:** meshmaster.dk's `tests/fixtures/firmware/FIRMWARE-MANIFEST.md` (§2 the manifest, §3 the signature and the keys, §4 the descriptor, §5 the checks) and the specification's fixtures beside it, which are on its master today (`index.json`, `TEST-KEY.txt`, the two products' manifests and signatures, the images with one, two and a broken descriptor); `.github/workflows/release.yml` (~L41–46: the files of a release; ~L177–235: the build; ~L240–330: the publishing) and `docs/release.md`; `tools/release.ps1`, `tools/pio/merge_bin.py`; `tools/sync-fixtures.ps1` (~L63: the sets it copies) and `tests/fixtures/`; `src/version.h`, `src/version.cpp`; `README.md` (*Flashing it*, and *Where CI runs*). In the Walter firmware's repository, read only, as the model: `.github/workflows/release.yml` (~L1–60 and ~L326–390: signing, the dry run, the last gate), `tools/release/build_release.py`, `sign_manifest.py`, `verify_release.py`, `keys/release-keys.txt`, `tools/gen_trust_table.py`, `tools/vendor_tweetnacl_verify.py`, `spec/tools/manifestref.py` (the reference: `canonical`, `validate`, `sign`, `verify`, the descriptor), `components/core/include/core/image/descriptor.h` and `trust.h`.
**Size:** one session, a long one.
**Model:** claude-opus-5

## Goal

A release of this firmware is, for each board, an application image and a merged image, and
`SHA256SUMS`. Nothing in it is signed, and an image does not say inside itself what it is. A
person reads the file's name and flashes it.

A board that fetches its own firmware has no person reading names. It needs what the
repeater's and the Walter's releases have: a manifest that names every file with its hash,
a signature over the manifest made where the image is built, and a descriptor inside each
image. MeshMaster's catalogue needs the same three to import a release at all.

This task makes a release carry them, and gives the firmware the keys and the code to check
them. Nothing here fetches anything.

## The release, as the catalogue and the board read it

`FIRMWARE-MANIFEST.md`, schema 1, with these values for this product. The product, the role
and the role's byte are new, chosen on 18 November, and not yet in the specification
(`FOLLOWUPS.md`).

| | |
|---|---|
| `product` | `twinoak-exitnode-firmware` |
| `version` | what the firmware reports as its version: `0.8.0` |
| `link` | `{"major":0,"minor":0}`: this firmware speaks no box link |
| image `id` | `exit-node.<board>.868` |
| image `role` | `exit-node`; the descriptor's role byte is **32** |
| image `board` | `heltec_v3`, `heltec_v4`: the heartbeat's `board` |
| image `band` | `868` |
| image `mcu` | `esp32s3` |
| image `files` | `ota`: the application image, `load_address` `0x00010000`. `factory`: the merged image, `load_address` `0x00000000` |

The descriptor of §4 is in the application image once, and so once in the merged image:
`slot` `0xFF`, `link_major` and `link_minor` 0, `version`, `board` and `band` as above.

The signature is §3's: `manifest.json.sig`, one line, `ed25519 <key id> <base64>`, over the
bytes of `manifest.json`.

A release's page on GitHub then holds, beside the images and `SHA256SUMS` it has today,
`manifest.json` and `manifest.json.sig`.

## Deliverables

1. **The descriptor is in the image.** The 96 bytes of §4, placed by the build, with the
   values above. Exactly one in an application image and exactly one in a merged image, and
   CI says so for both boards on every build.
2. **A release has its manifest.** The release tooling writes `manifest.json` for the two
   boards as the reference writes one (`canonical`), from the files it is about to publish:
   their sizes and hashes are read from the files and never typed.
3. **And its signature.** The workflow signs the manifest with the `RELEASE_SIGNING_KEY`
   secret and publishes `manifest.json.sig` beside it. The key is never echoed, written to
   a file that is kept, or put in an artifact. A dry run signs with the specification's test
   key and publishes nothing.
4. **The last gate before anything is published.** Every check of §5 that applies to this
   product, run over the files to be published with the keys the build trusts: 1, 2, 4 and
   5, and 3 for each board. A release whose manifest does not verify against a key in
   `keys/release-keys.txt` is refused there, in a sentence that says what is missing: the
   secret, or the key's public half in the file.
5. **The keys the firmware trusts.** `keys/release-keys.txt`, public halves, in the Walter's
   format. A build trusts the keys in it, room for three. The specification's test key is
   trusted by a test build and by no release build, and the release tooling refuses to sign
   a release with it.
   The file starts with the line the Walter's has: one release key for the three firmwares,
   which MeshMaster's catalogue already trusts.
6. **A build with no key takes no update**, and says so: nothing of task 61's heartbeat
   member is written by it, and `update` on the console says *no release key in this build*.
7. **The firmware can check a release.** Code the native tests build, with no ESP32 call in
   it: the signature's line read strictly, Ed25519 as in RFC 8032 over the manifest's bytes
   against the trusted keys, the manifest read and validated as the reference validates one,
   the image for a board found, a file's hash, and the one descriptor in a file held against
   its manifest entry. Each failure is a check's number, 1 to 5. The Ed25519 code is proved
   on RFC 8032's first test vector before it is trusted with a release.
8. **The specification's fixtures come from meshmaster.dk.** `tools/sync-fixtures.ps1` gains
   a third set, `firmware`, that copies `tests/fixtures/firmware/` of meshmaster.dk with its
   hashes in `MANIFEST.txt`, as the two sets it has. They are on its master today, so that
   sync is run in this task. The tests of deliverable 7 read that copy, and a release of
   this product that this task's own tooling writes and signs with the test key.
9. **The words.** `docs/release.md`: the two new files, the secret, the keys file, the last
   gate, and what a dry run does. `README.md`, where it lists a release's files. One line
   in `release-notes/unreleased/`.

## Acceptance criteria

- [ ] An application image and a merged image of each board hold exactly one descriptor, with the role byte 32, the board's name, `868` and the build's version (CI, and a test of the tool that checks it against an image with none and one with two).
- [ ] The manifest the tooling writes for a rehearsed release is read by deliverable 7's own reader with no refusal, has the reference's fields in the reference's order (`canonical`), names four files, and each file's size and SHA-256 are the file's (test).
- [ ] RFC 8032's first test vector verifies, and fails with one bit of the message, of the signature and of the key changed (tests).
- [ ] The specification's `manifest-walter.json` and `manifest-repeater.json` verify with their signatures against the test key and against no other, and each is then refused at check 2 as another product's. `manifest-repeater-tampered.json` and `manifest-repeater-unknown-key.json.sig` fail check 1. `image-two-descriptors.bin` and `image-broken-descriptor.bin` are refused, and `image-walter-ota.bin` yields its one descriptor (tests).
- [ ] A release of this product, written and signed with the test key by this task's tooling: checks 1 to 5 pass for `heltec_v3` and for `heltec_v4` (test).
- [ ] That release with one byte of the manifest changed: check 1. With the manifest of another product, signed: check 2. Asked for a board it has no image for: check 3. With one byte of the `ota` file changed: check 4. With the other board's `ota` file under this board's name and hash: check 5 (tests).
- [ ] A signature line with a second space, with base64 that is not strict, or with a key id of 15 characters is refused at check 1 (tests).
- [ ] A release build with the test key in its list does not build; a test build does (shown in the report).
- [ ] A dry run of the release workflow ends green with a manifest and a signature by the test key among its artifacts and nothing published; the last gate refuses a manifest signed by a key that is not in the file (shown in the report, with the run).
- [ ] `tools\sync-fixtures.ps1 -Verify` passes with the third set.
- [ ] `pio test -e native` green, both boards build, CI green.

## Out of scope

Fetching a release (59). The secret itself and which key it holds: Thomas adds it, and the
first release after this task is refused until he has. Key rotation beyond the room for
three. The specification: this product is not in `FIRMWARE-MANIFEST.md` yet. Signing
anything but the manifest. Secure boot.
