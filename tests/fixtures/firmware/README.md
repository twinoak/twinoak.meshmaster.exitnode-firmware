# The firmware-manifest fixtures

A copy of a copy. `FIRMWARE-MANIFEST.md` and everything beside it are the
hardware repository's `software/spec/` — the specification of what a release of
any TwinOak firmware says about itself, and the fixtures its reference
implementation generates — copied into **twinoak/meshmaster.dk** and copied from
there into here, so that the two ends of a release check the same bytes.

**Never edit a file in this directory.** They are written by
[`tools/sync-fixtures.ps1`](../../../tools/sync-fixtures.ps1) and by nothing
else (this `README.md` and `MANIFEST.txt` are this repository's own and are not
copied):

```powershell
.\tools\sync-fixtures.ps1 -Set firmware -Commit <sha>   # fetch from that meshmaster.dk commit
.\tools\sync-fixtures.ps1 -Verify                       # do the files still match MANIFEST.txt?
```

The bytes come across **exactly**, through the contents API's own base64 rather
than as text: a signature is over a file's bytes, so one line ending changed on
the way in would make every signature here fail. `.gitattributes` keeps git out
of them for the same reason, and `MANIFEST.txt`'s hashes are the ones
meshmaster.dk's own manifest records for the same files.

## What is here, and what reads it

`index.json` is the fixtures' own index, and the tests read their expectations
from it rather than from constants of their own:

| | |
|---|---|
| `FIRMWARE-MANIFEST.md` | the specification: §2 the manifest, §3 the signature and the keys, §4 the descriptor in an image, §5 the five checks a consumer makes |
| `manifest-repeater.json`, `manifest-walter.json`, `manifest-repeater-base.json` and their `.sig` | two other products' releases, valid, signed with the test key |
| `manifest-repeater-tampered.json` | one character changed after signing: it must fail check 1 |
| `manifest-repeater-unknown-key.json.sig` | a real signature by a key no verifier here holds: check 1 |
| `image-repeater.bin`, `image-walter-ota.bin` | stand-in images, each with one descriptor at the offset `index.json` gives |
| `image-two-descriptors.bin`, `image-broken-descriptor.bin` | must be refused (§4: exactly one) |
| `base-*.hex` | the nRF52840 `provision` files of §4a. This firmware is an ESP32-S3 and writes none; they are here because the directory is copied whole |
| `TEST-KEY.txt` | the key pair the specification publishes **on purpose**. A build that trusts it is a test build — `[env:native]` and no board |
| `rfc8032_test_1` in `index.json` | RFC 8032 §7.1's first test vector, so an Ed25519 implementation is proven before it is trusted with a release |

Three things read them:

- [`test/test_release_reader`](../../../test/test_release_reader) — the
  firmware's own reader of a release (`src/update/`), on the same fixtures;
- [`tools/release/test_release.py`](../../../tools/release/test_release.py) —
  this repository's transcription of the specification
  (`tools/release/manifest.py`), re-deriving every signature, descriptor and
  canonical form the index records;
- the build itself: `tools/gen-trust-table.py` reads the test key out of
  `index.json` for `[env:native]`, so it is written down in no source file.

[`docs/release.md`](../../../docs/release.md) §5 is the page.
