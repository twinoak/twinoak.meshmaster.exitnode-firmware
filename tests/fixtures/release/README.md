# A release of this firmware, written by its own tooling

**Generated, never edited.** Everything here is one release of
`twinoak-exitnode-firmware` as [`tools/release/`](../../../tools/release) writes
and signs one — the manifest from `build_release.py`, the signature from
`sign_manifest.py`'s rule and `manifest.py`'s Ed25519 — over **stand-in images**
rather than over 1.4 MB of real firmware:

```
python tools/release/make_test_release.py            write this directory again
python tools/release/make_test_release.py --check    re-derive it and refuse a difference
```

`--check` runs in CI (`tools/release/test_release.py`), which is what proves
nobody edited a byte here by hand instead of changing the tooling. Everything is
deterministic: the same filler, the same seeds, the same `built_at`.

The images are the shape a real release has — a bootloader, `0xFF` up to the
application slot, then the application byte for byte, which is what
`esptool merge_bin` writes — so `build_release.py`'s rule that *the two files of
a release are made by one build* is exercised on them.

It is signed with the specification's **public test key**
([`../firmware/TEST-KEY.txt`](../firmware/TEST-KEY.txt)) and its version is
`0.0.0-test+1a2b3c4`, which is deliberately **not** a release's shape: that is
what lets the test key sign it at all, by the same rule that lets it sign a dry
run and refuses it a release. No board outside a test build trusts that key.

[`test/test_release_reader`](../../../test/test_release_reader) is what reads it:
the five checks of `FIRMWARE-MANIFEST.md` §5 pass for both boards, and every
refusal the task asked for is derived in the test by changing a byte of what is
here.
