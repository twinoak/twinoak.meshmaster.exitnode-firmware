#!/usr/bin/env python3
"""The manifest of a release: what each published file is, with its size and its hash.

    python tools/release/build_release.py --version 0.8.0+1a2b3c4 --commit <40 hex> \\
        [--dir out/release] [--built-at 2026-11-18T12:00:00Z]

`--dir` holds the files the release will publish, named as .github/workflows/release.yml names
them - `meshmaster-agent-<board>-<tag>.bin` and `meshmaster-agent-<board>-<tag>-merged.bin` for
each board. It writes `manifest.json` beside them (FIRMWARE-MANIFEST.md section 2, schema 1):

    product  twinoak-exitnode-firmware        role  exit-node        band  868
    one image a board, `exit-node.<board>.868`, with two files:
      ota      the application image, at the board's own first application slot
      factory  the merged image, at 0x00000000

**Nothing here is typed that can be read.** Every size and every SHA-256 comes from the file on
disk; the `ota` load address is the `app0` offset of the partition table the environment names in
platformio.ini, read through tools/check-partitions.py, so moving the slot moves the manifest with
it. The version and the commit are the workflow's, which got them from `VERSION` and from the ref.

Before it writes anything it refuses, each in a sentence on stderr:

  * a file is missing, or the merged image does not carry this very application at its load
    address - the two files of one release are made by one build;
  * a file holds no descriptor, or more than one, or one that disagrees with the manifest entry
    above it (section 4, and check 5 of section 5 applied where the release is made);
  * an image carries the specification's public test key: that is a test build, which trusts a key
    everybody has and is never released (section 3);
  * the manifest does not validate (section 2).

Signing is the next step (sign_manifest.py) and the gate before publishing the one after
(verify_release.py). Nothing here signs and nothing here publishes.
"""

import argparse
import importlib.util
import pathlib
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import manifest as mf  # noqa: E402
from manifest import Refused  # noqa: E402

# What this firmware is, in the manifest's words. These are this repository's own knowledge of its
# product; the descriptor found inside each file is held against them, which is why there are two.
PRODUCT = "twinoak-exitnode-firmware"
ROLE = "exit-node"
BAND = "868"
MCU = "esp32s3"
LINK = {"major": 0, "minor": 0}  # this firmware speaks no box link
# The boards, in release.yml's matrix order. Adding one is a line here and a line in both workflows.
BOARDS = ("heltec_v3", "heltec_v4")
FACTORY_LOAD_ADDRESS = 0x0


def check_partitions():
    """tools/check-partitions.py, whose name is not an identifier, so it is loaded by path. It
    owns which csv an environment uses and how one is parsed; this reads app0's offset from it."""
    if "check_partitions" in sys.modules:
        return sys.modules["check_partitions"]
    path = os.path.join(REPO, "tools", "check-partitions.py")
    spec = importlib.util.spec_from_file_location("check_partitions", path)
    module = importlib.util.module_from_spec(spec)
    # In sys.modules before it runs: @dataclass looks its own module up by name while the class
    # body is being processed, and a module that is not there yet is an AttributeError.
    sys.modules["check_partitions"] = module
    spec.loader.exec_module(module)
    return module


def app_slot_offset(board, parts=None):
    """The first application slot's offset for a board - not a typed constant, so that moving
    `app0` in the csv moves the manifest's load_address with it."""
    module = parts or check_partitions()
    csv = module.csv_for_env(pathlib.Path(REPO) / "platformio.ini", board)
    for part in module.parse_csv(csv):
        if part.name == "app0":
            return part.offset
    raise Refused("%s has no app0 partition for %s" % (csv, board))


def read(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        raise Refused("cannot read %s: %s" % (path, exc))


def names_for(board, tag):
    return {"ota": "meshmaster-agent-%s-%s.bin" % (board, tag),
            "factory": "meshmaster-agent-%s-%s-merged.bin" % (board, tag)}


def refuse_test_key_build(fixtures_dir, images):
    """A build whose trust table holds the specification's public test key takes anybody's
    firmware, and is never released (section 3). The witness is the key's 32 bytes in the image:
    tools/gen-trust-table.py puts them there for [env:native] and for no board environment."""
    key = mf.test_public_key(fixtures_dir)
    for label, blob in images:
        at = blob.find(key)
        if at >= 0:
            raise Refused(
                "%s carries the specification's public test key at 0x%X: a test build, which "
                "trusts a key everybody has and is never released (FIRMWARE-MANIFEST.md section "
                "3). Build without the test key in the trust table." % (label, at))


def the_one_descriptor(blob, label):
    try:
        return mf.find_descriptor(blob)
    except ValueError as exc:
        raise Refused("%s: %s (FIRMWARE-MANIFEST.md section 4 wants exactly one)" % (label, exc))


def hold_against_image(offset, fields, image, version, label):
    """Check 5 of section 5, applied where the release is made rather than only where it is used:
    the descriptor inside the file and the manifest entry above it say the same thing."""
    wanted = {"role": image["role"], "board": image["board"], "band": image["band"],
              "version": version, "slot": mf.SLOT_ANY,
              "link_major": LINK["major"], "link_minor": LINK["minor"]}
    wrong = ["%s is %r in the descriptor and %r in the manifest" % (key, fields.get(key), want)
             for key, want in sorted(wanted.items()) if fields.get(key) != want]
    if wrong:
        raise Refused("%s: the descriptor at 0x%X does not agree with the manifest entry %s:\n  %s"
                      % (label, offset, image["id"], "\n  ".join(wrong)))


def build(version, commit, tag, out_dir, built_at, boards=BOARDS, fixtures_dir=None, upstream=None):
    if not version:
        raise Refused("--version is required")
    if not (len(commit) == 40 and all(c in "0123456789abcdef" for c in commit)):
        raise Refused("--commit must be 40 lowercase hex characters, not %r" % commit)
    fixtures_dir = fixtures_dir or os.path.join(REPO, "tests", "fixtures", "firmware")

    parts = check_partitions()
    images, evidence, descriptors = [], [], []
    for board in boards:
        names = names_for(board, tag)
        app = read(os.path.join(out_dir, names["ota"]))
        merged = read(os.path.join(out_dir, names["factory"]))
        load = app_slot_offset(board, parts)

        refuse_test_key_build(fixtures_dir, [(names["ota"], app), (names["factory"], merged)])

        # The merged image is the same application with a bootloader and a partition table in
        # front of it, byte for byte, at the slot the manifest gives as the application's load
        # address - so the two files of one release cannot come from two builds.
        tail = merged[load:load + len(app)]
        if tail != app:
            at = next((i for i in range(min(len(tail), len(app))) if tail[i] != app[i]), len(tail))
            raise Refused("%s does not carry %s at 0x%X: they differ at byte %d of %d. The two "
                          "files of a release are made by one build."
                          % (names["factory"], names["ota"], load, at, len(app)))
        evidence.append("ok    %s carries %s at 0x%08X, all %d bytes"
                        % (names["factory"], names["ota"], load, len(app)))

        image = {
            "id": "%s.%s.%s" % (ROLE, board, BAND),
            "role": ROLE, "board": board, "band": BAND, "mcu": MCU,
            "files": [mf.file_entry("ota", names["ota"], app, load),
                      mf.file_entry("factory", names["factory"], merged, FACTORY_LOAD_ADDRESS)],
        }
        for use, blob in (("ota", app), ("factory", merged)):
            offset, fields = the_one_descriptor(blob, names[use])
            hold_against_image(offset, fields, image, version, names[use])
            descriptors.append((names[use], offset, fields))
        images.append(image)

    manifest = {
        "schema": mf.SCHEMA,
        "product": PRODUCT,
        "version": version,
        "tag": tag,
        "commit": commit,
        "built_at": built_at,
        "link": dict(LINK),
    }
    if upstream:
        # What it is built on, which for this firmware is MeshCore at platformio.ini's [meshcore]
        # pin. Optional in section 2, and the one field of the manifest a person is likely to read.
        manifest["upstream"] = upstream
    manifest["images"] = images

    try:
        mf.validate(manifest)
    except ValueError as exc:
        raise Refused("the manifest does not validate (FIRMWARE-MANIFEST.md section 2): %s" % exc)

    data = mf.canonical(manifest)
    path = os.path.join(out_dir, "manifest.json")
    with open(path, "wb") as fh:
        fh.write(data)
    return {"manifest": manifest, "path": path, "evidence": evidence, "descriptors": descriptors}


def meshcore_pin():
    """The MeshCore commit and tag the build is pinned at, from platformio.ini's [meshcore]."""
    import configparser
    ini = configparser.ConfigParser()
    ini.read(os.path.join(REPO, "platformio.ini"), encoding="utf-8")
    if not ini.has_section("meshcore"):
        return None
    return {"name": "MeshCore", "tag": ini.get("meshcore", "tag"), "commit": ini.get("meshcore", "pin")}


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True, help="X.Y.Z+<sha7>, the string the firmware reports")
    ap.add_argument("--commit", required=True, help="40 hex characters")
    ap.add_argument("--tag", default=None, help="by default v<version up to the +>")
    ap.add_argument("--dir", default=os.path.join(REPO, "out", "release"),
                    help="where the built files are, and where manifest.json is written")
    ap.add_argument("--built-at", default=None, help="UTC YYYY-MM-DDTHH:MM:SSZ; by default now")
    args = ap.parse_args(argv[1:])

    tag = args.tag or "v" + args.version.split("+", 1)[0]
    built_at = args.built_at or time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    try:
        result = build(args.version, args.commit, tag, args.dir, built_at, upstream=meshcore_pin())
    except Refused as exc:
        print("REFUSED: %s" % exc, file=sys.stderr)
        return 1

    for line in result["evidence"]:
        print(line)
    for name, offset, fields in result["descriptors"]:
        print("ok    %s holds one descriptor at 0x%X: %s on %s, band %s, version %s"
              % (name, offset, fields["role"], fields["board"], fields["band"], fields["version"]))
    print("ok    the manifest validates against FIRMWARE-MANIFEST.md section 2")
    print("%s: %d image(s), %d file(s)" % (result["path"], len(result["manifest"]["images"]),
                                           sum(len(i["files"]) for i in result["manifest"]["images"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
