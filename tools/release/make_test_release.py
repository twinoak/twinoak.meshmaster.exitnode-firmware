#!/usr/bin/env python3
"""The release of this product that the firmware's own tests read (task p1.15-58).

    python tools/release/make_test_release.py            write tests/fixtures/release/
    python tools/release/make_test_release.py --check    re-derive it and refuse a difference

A release of this firmware written by **this repository's own tooling** - build_release.py for
the manifest, sign_manifest.py's rule and manifest.py's signature for the `.sig` - over stand-in
images rather than over 1.4 MB of real firmware. It is what `test/test_release_reader` runs the
five checks of FIRMWARE-MANIFEST.md section 5 against, and what every refusal that criterion asks
for is derived from, in the test, by changing a byte of what is here.

It is signed with the specification's **test key**, which is public on purpose, and its version is
`0.0.0-test+<sha>` - not a release's shape, so sign_manifest.py's rule that the test key never
signs a release is the rule that governs it too, and no board outside a test build trusts it.

The images are deterministic: the same filler, the same seeds, the same `built_at`, so `--check`
in CI is what proves nobody edited a byte of the fixture by hand instead of changing the tooling.
Their shape is the real one - a merged image is a bootloader, 0xFF up to the application slot, and
then the application byte for byte, which is what `esptool merge_bin` writes (tools/pio/merge_bin.py).
"""

import argparse
import hashlib
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import build_release  # noqa: E402
import manifest as mf  # noqa: E402
import sign_manifest  # noqa: E402
from manifest import Refused  # noqa: E402

OUT = os.path.join(REPO, "tests", "fixtures", "release")
FIXTURES = os.path.join(REPO, "tests", "fixtures", "firmware")

VERSION = "0.0.0-test+1a2b3c4"
COMMIT = "1a2b3c4" + "0" * 33
BUILT_AT = "2026-11-18T12:00:00Z"
TAG = "v" + VERSION.split("+", 1)[0]

APP_SIZE = 4096
BOOTLOADER_SIZE = 1024
DESCRIPTOR_AT = {"heltec_v3": 0x400, "heltec_v4": 0x200}
SEED = {"heltec_v3": 3, "heltec_v4": 4}


def filler(size, seed):
    """Deterministic stand-in bytes - the reference fixtures' own generator."""
    out = bytearray()
    state = seed
    while len(out) < size:
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        out += struct.pack("<I", state)
    return bytearray(out[:size])


def images_for(board, load):
    descriptor = mf.make_descriptor(build_release.ROLE, VERSION, board, build_release.BAND)
    app = filler(APP_SIZE, SEED[board])
    at = DESCRIPTOR_AT[board]
    app[at:at + mf.DESC_LEN] = descriptor
    merged = filler(BOOTLOADER_SIZE, SEED[board] + 100)
    merged += bytearray(b"\xFF" * (load - BOOTLOADER_SIZE))
    merged += app
    return bytes(app), bytes(merged)


def write(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    parts = build_release.check_partitions()
    names = []
    for board in build_release.BOARDS:
        load = build_release.app_slot_offset(board, parts)
        app, merged = images_for(board, load)
        for use, blob in (("ota", app), ("factory", merged)):
            name = build_release.names_for(board, TAG)[use]
            with open(os.path.join(out_dir, name), "wb") as fh:
                fh.write(blob)
            names.append(name)

    build_release.build(VERSION, COMMIT, TAG, out_dir, BUILT_AT, upstream=build_release.meshcore_pin())

    # SHA256SUMS, as the release workflow writes it: every published .bin, in name order.
    lines = []
    for name in sorted(names):
        with open(os.path.join(out_dir, name), "rb") as fh:
            lines.append("%s  %s\n" % (hashlib.sha256(fh.read()).hexdigest(), name))
    with open(os.path.join(out_dir, "SHA256SUMS"), "w", encoding="ascii", newline="\n") as fh:
        fh.writelines(lines)

    # Signed the way a dry run is signed, and refused the way a release would be: the version is
    # not a release's shape, which is what lets the public test key sign it at all.
    sign_manifest.FIXTURES = FIXTURES
    sign_manifest.sign(os.path.join(out_dir, "manifest.json"), use_test_key=True)
    return sorted(names) + ["manifest.json", "manifest.json.sig", "SHA256SUMS"]


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="re-derive the fixture and refuse a difference")
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args(argv[1:])

    try:
        if not args.check:
            written = write(args.out)
            print("%s: %s" % (os.path.relpath(args.out, REPO), ", ".join(written)))
            return 0
        with tempfile.TemporaryDirectory() as scratch:
            written = write(scratch)
            wrong = []
            for name in written:
                fresh = os.path.join(scratch, name)
                committed = os.path.join(args.out, name)
                if not os.path.isfile(committed):
                    wrong.append("%s is not in %s" % (name, os.path.relpath(args.out, REPO)))
                    continue
                with open(fresh, "rb") as a, open(committed, "rb") as b:
                    if a.read() != b.read():
                        wrong.append("%s is not what the tooling writes" % name)
            extra = sorted(set(os.listdir(args.out)) - set(written) - {"README.md"})
            wrong += ["%s is in the directory and the tooling does not write it" % n for n in extra]
    except Refused as exc:
        print("REFUSED: %s" % exc, file=sys.stderr)
        return 1
    if wrong:
        print("REFUSED: tests/fixtures/release is not what tools/release/ writes:\n  " +
              "\n  ".join(wrong) +
              "\n\nIt is generated, never edited: run tools/release/make_test_release.py.",
              file=sys.stderr)
        return 1
    print("tests/fixtures/release: %d file(s), every byte the tooling's" % len(written))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
