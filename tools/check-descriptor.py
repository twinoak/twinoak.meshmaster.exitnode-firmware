#!/usr/bin/env python3
"""The descriptor check: does the image say inside itself what it is, exactly once?

FIRMWARE-MANIFEST.md section 4 gives every image 96 bytes that name its role, its board, its band
and its version, so that a board about to write 1.4 MB into its other slot can tell what it has
actually got - a file can be renamed, and a manifest describes files. **Exactly one** valid
descriptor must be in an image: none means it does not say what it is, two mean it cannot be
believed.

tools/pio/release_headers.py places the bytes in the application image at build time, and the
merged image carries the same application - so this runs over both, for every board, on every CI
build (.github/workflows/ci.yml), and the release gate runs the same check again over the files it
is about to publish (tools/release/verify_release.py).

    python tools/check-descriptor.py --env heltec_v3 \\
        --image .pio/build/heltec_v3/firmware.bin \\
        --merged .pio/build/heltec_v3/firmware-merged.bin [--version 0.8.0+1a2b3c4]
    python tools/check-descriptor.py --self-test

`--version` is the string the build was given (release.yml's `-D MESHMASTER_AGENT_VERSION`); with
none it is `0.0.0-dev`, which is what src/version.cpp reports for a local build. `--self-test`
runs the tool over an image with no descriptor, one with two and one whose only descriptor fails
its crc - the specification's own fixtures - and expects each to be refused.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "release"))

import build_release  # noqa: E402  the product's role, band and boards, in one place
import manifest as mf  # noqa: E402

FIXTURES = os.path.join(ROOT, "tests", "fixtures", "firmware")
FALLBACK_VERSION = "0.0.0-dev"  # src/version.cpp's own default


def read(path):
    with open(path, "rb") as fh:
        return fh.read()


def check_image(path, board, version, report):
    """Returns the descriptor's offset, or None after writing why not."""
    blob = read(path)
    try:
        offset, fields = mf.find_descriptor(blob)
    except ValueError as exc:
        report.append("FAIL  %s: %s" % (os.path.basename(path), exc))
        return None
    wanted = {"role": build_release.ROLE, "board": board, "band": build_release.BAND,
              "version": version, "slot": mf.SLOT_ANY,
              "link_major": build_release.LINK["major"], "link_minor": build_release.LINK["minor"]}
    wrong = ["%s is %r, not %r" % (k, fields.get(k), v) for k, v in sorted(wanted.items())
             if fields.get(k) != v]
    if wrong:
        report.append("FAIL  %s: the descriptor at 0x%X says %s"
                      % (os.path.basename(path), offset, "; ".join(wrong)))
        return None
    report.append("ok    %s: one descriptor at 0x%X - %s on %s, band %s, version %s"
                  % (os.path.basename(path), offset, fields["role"], fields["board"],
                     fields["band"], fields["version"]))
    return offset


def run(env, image, merged, version):
    report = []
    board = env
    app_at = check_image(image, board, version, report)
    if merged:
        merged_at = check_image(merged, board, version, report)
        if app_at is not None and merged_at is not None:
            load = build_release.app_slot_offset(board)
            if merged_at != load + app_at:
                report.append("FAIL  the merged image's descriptor is at 0x%X and the "
                              "application's at 0x%X, which loads at 0x%X: the merged image does "
                              "not carry this application" % (merged_at, app_at, load))
            else:
                report.append("ok    the merged image's descriptor is the application's, at "
                              "0x%X + 0x%X" % (load, app_at))
    print("\n".join(report))
    return 1 if any(line.startswith("FAIL") for line in report) else 0


def self_test():
    """The three shapes an image must be refused for, from the specification's own fixtures."""
    cases = [
        ("an image with no descriptor", b"\xA5" * 4096, "0 valid descriptors"),
        ("image-two-descriptors.bin", read(os.path.join(FIXTURES, "image-two-descriptors.bin")),
         "2 valid descriptors"),
        ("image-broken-descriptor.bin", read(os.path.join(FIXTURES, "image-broken-descriptor.bin")),
         "0 valid descriptors"),
    ]
    failures = []
    for what, blob, expected in cases:
        try:
            mf.find_descriptor(blob)
            failures.append("FAIL  %s was accepted and must be refused" % what)
        except ValueError as exc:
            if expected not in str(exc):
                failures.append("FAIL  %s was refused with %r, expected %r" % (what, str(exc), expected))
            else:
                print("ok    %s: %s" % (what, exc))

    # And one that must be accepted, made the way a build makes it.
    good = bytearray(b"\x00" * 4096)
    blob = mf.make_descriptor(build_release.ROLE, "0.8.0+abcdef0", "heltec_v3", build_release.BAND)
    good[0x100:0x100 + mf.DESC_LEN] = blob
    offset, fields = mf.find_descriptor(bytes(good))
    if offset != 0x100 or fields["role"] != build_release.ROLE or fields["slot"] != mf.SLOT_ANY:
        failures.append("FAIL  a good image did not read back as one: 0x%X %r" % (offset, fields))
    else:
        print("ok    an image with one descriptor: 0x%X, %s on %s, band %s, version %s"
              % (offset, fields["role"], fields["board"], fields["band"], fields["version"]))

    # The specification's own image, read with the same code: another product's, and it reads.
    offset, fields = mf.find_descriptor(read(os.path.join(FIXTURES, "image-walter-ota.bin")))
    if fields["role"] != "manager":
        failures.append("FAIL  image-walter-ota.bin read as %r" % fields)
    else:
        print("ok    image-walter-ota.bin: one descriptor at 0x%X, %s on %s"
              % (offset, fields["role"], fields["board"]))

    print("\n".join(failures) if failures else "self-test: %d case(s), 0 failed" % (len(cases) + 2))
    return 1 if failures else 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--env", help="the board environment, which is also the board's name")
    ap.add_argument("--image", help="the application image")
    ap.add_argument("--merged", help="the merged image, if there is one")
    ap.add_argument("--version", default=FALLBACK_VERSION)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv[1:])
    if args.self_test:
        return self_test()
    if not args.env or not args.image:
        ap.error("--env and --image, or --self-test")
    return run(args.env, args.image, args.merged, args.version)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
