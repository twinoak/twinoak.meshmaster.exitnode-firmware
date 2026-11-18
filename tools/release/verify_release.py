#!/usr/bin/env python3
"""The last gate before anything is published: do these files pass the checks a board will make?

    python tools/release/verify_release.py out/release                 # keys/release-keys.txt
    python tools/release/verify_release.py --test-key out/release      # a dry run's

Walks FIRMWARE-MANIFEST.md section 5 in its order, over the files on disk, with the public keys a
firmware build would trust and nothing else:

  1. the signature line names a trusted key and verifies over manifest.json's bytes;
  2. the manifest validates (section 2) and its product is this firmware's;
  3. there is an image for every board this release publishes, `exit-node.<board>.868`;
  4. every file's size and SHA-256 are the manifest's - and SHA256SUMS agrees with them;
  5. every file holds exactly one descriptor and it agrees with its entry;
  6. (section 5's sixth check is about a `swd` file's app_region; this product publishes none.)

A release that cannot pass this is not published - which is the whole reason
.github/workflows/release.yml runs it after signing and before `gh release create`. The same run
is a dry run's last step, so the gate is rehearsed and not met for the first time on a release.

**The trusted keys.** keys/release-keys.txt is the release's trust table and the file
tools/gen-trust-table.py generates the firmware's compiled-in key list from, through `load_keys`
below - so the gate and the board read the same file by the same rules. --test-key trusts the
specification's public test key instead, for a rehearsal, and nothing else.
"""

import argparse
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import manifest as mf  # noqa: E402
from build_release import BAND, BOARDS, LINK, PRODUCT, ROLE  # noqa: E402
from manifest import Refused  # noqa: E402

KEYS_FILE = os.path.join(REPO, "keys", "release-keys.txt")
FIXTURES = os.path.join(REPO, "tests", "fixtures", "firmware")
MAX_KEYS = 3  # section 3: room for three

NO_KEYS = """no release key is trusted: %s names none.

That is the state before the key exists and it is safe - a board with no key compiled in takes no
update at all (FIRMWARE-MANIFEST.md section 3) - but it also means this release cannot be verified
and so must not be published. Put the release key's public half in %s as

    ed25519 <key id> <public hex>   # who made it, when

and its private half in the RELEASE_SIGNING_KEY secret (docs/release.md). A rehearsal needs no
key: --test-key."""


def load_keys(path, test_build=False):
    """The trust table: one key a line, `ed25519 <key id> <public hex>`, # to end of line a
    comment, at most three (section 3). The id is recomputed from the key and held against the
    line, so a typed id that is wrong is a refusal and never a key nobody can use.

    The specification's public test key is refused here **for a release build**: a released
    firmware that trusted it would take anybody's. `test_build` is [env:native]'s, where the key
    is what the tests sign with - there the line is allowed and the key is not listed twice."""
    keys = []
    test_key = mf.test_public_key(FIXTURES)
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as fh:
            for lineno, raw in enumerate(fh, 1):
                line = raw.split("#", 1)[0].strip()
                if not line:
                    continue
                parts = line.split()
                if len(parts) != 3 or parts[0] != "ed25519":
                    raise Refused("%s line %d: expected `ed25519 <key id> <public hex>`, got %r"
                                  % (path, lineno, raw.strip()))
                try:
                    public = bytes.fromhex(parts[2])
                except ValueError:
                    raise Refused("%s line %d: the public key is not hex" % (path, lineno))
                if len(public) != 32:
                    raise Refused("%s line %d: an Ed25519 public key is 32 bytes, this is %d"
                                  % (path, lineno, len(public)))
                if mf.key_id(public) != parts[1]:
                    raise Refused("%s line %d: the key id is %s, not %s (section 3: the first "
                                  "eight bytes of SHA-256 over the public key)"
                                  % (path, lineno, mf.key_id(public), parts[1]))
                if public == test_key and not test_build:
                    raise Refused(
                        "%s line %d names the specification's public test key (%s). It is never "
                        "in a release build's trust table: a firmware that trusted it would take "
                        "anybody's (FIRMWARE-MANIFEST.md section 3). A test build gets that key "
                        "from tools/gen-trust-table.py --test-key, not from this file."
                        % (path, lineno, parts[1]))
                if public not in keys:
                    keys.append(public)
    if len(keys) > MAX_KEYS:
        raise Refused("%s names %d keys; a build has room for %d (section 3)"
                      % (path, len(keys), MAX_KEYS))
    return keys


def read(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        raise Refused("cannot read %s: %s" % (path, exc))


def verify(out_dir, keys, label_of_keys, boards=BOARDS):
    checks = []

    def ok(line):
        checks.append("ok    " + line)

    manifest_path = os.path.join(out_dir, "manifest.json")
    data = read(manifest_path)
    sig = read(manifest_path + ".sig").decode("ascii", "replace")

    # 1. the signature.
    if not keys:
        raise Refused(NO_KEYS % (KEYS_FILE, KEYS_FILE))
    if not mf.verify(keys, data, sig):
        named = sig.strip().split(" ")
        named = named[1] if len(named) == 3 else "(the line is not `ed25519 <key id> <signature>`)"
        raise Refused("check 1: the signature in manifest.json.sig does not verify over "
                      "manifest.json with %s. It names the key %s. Either the manifest changed "
                      "after it was signed, or it was signed by a key this release does not "
                      "trust - the secret RELEASE_SIGNING_KEY holds a key whose public half is "
                      "not in %s (FIRMWARE-MANIFEST.md section 3)."
                      % (label_of_keys, named, os.path.relpath(KEYS_FILE, REPO)))
    ok("check 1: the signature verifies with %s: key %s"
       % (label_of_keys, sig.strip().split(" ")[1]))

    # 2. the manifest.
    if data.startswith(b"\xef\xbb\xbf"):
        raise Refused("check 2: manifest.json begins with a byte-order mark; section 2 says UTF-8 "
                      "without one")
    try:
        manifest = json.loads(data)
    except ValueError as exc:
        raise Refused("check 2: manifest.json is not JSON: %s" % exc)
    try:
        mf.validate(manifest)
    except ValueError as exc:
        raise Refused("check 2: the manifest does not validate (section 2): %s" % exc)
    if manifest["product"] != PRODUCT:
        raise Refused("check 2: the manifest's product is %r, not %r" % (manifest["product"], PRODUCT))
    if mf.canonical(manifest) != data:
        raise Refused("check 2: manifest.json is not in the canonical form this repository writes "
                      "(section 2, 'How ours is written')")
    if manifest["link"] != LINK:
        raise Refused("check 2: the manifest says link %r and this firmware speaks %r"
                      % (manifest["link"], LINK))
    ok("check 2: the manifest validates, is %s %s, and is in canonical form"
       % (PRODUCT, manifest["version"]))

    # 3. one image a board, and no other.
    want = ["%s.%s.%s" % (ROLE, board, BAND) for board in boards]
    got = [image["id"] for image in manifest["images"]]
    if sorted(got) != sorted(want):
        raise Refused("check 3: the images are %r and this release publishes %r" % (got, want))
    ok("check 3: %d image(s): %s" % (len(got), ", ".join(got)))

    # 4. and 5. each file.
    counted = 0
    for image in manifest["images"]:
        for entry in image["files"]:
            blob = read(os.path.join(out_dir, entry["name"]))
            if len(blob) != entry["size"]:
                raise Refused("check 4: %s is %d bytes and the manifest says %d"
                              % (entry["name"], len(blob), entry["size"]))
            digest = hashlib.sha256(blob).hexdigest()
            if digest != entry["sha256"]:
                raise Refused("check 4: %s hashes to %s and the manifest says %s"
                              % (entry["name"], digest, entry["sha256"]))
            try:
                offset, fields = mf.find_descriptor(blob)
            except ValueError as exc:
                raise Refused("check 5: %s: %s (section 4)" % (entry["name"], exc))
            wanted = {"role": image["role"], "board": image["board"], "band": image["band"],
                      "version": manifest["version"], "slot": mf.SLOT_ANY,
                      "link_major": manifest["link"]["major"],
                      "link_minor": manifest["link"]["minor"]}
            wrong = ["%s is %r, not %r" % (k, fields.get(k), v) for k, v in sorted(wanted.items())
                     if fields.get(k) != v]
            if wrong:
                raise Refused("check 5: %s: the descriptor at 0x%X disagrees with the manifest: %s"
                              % (entry["name"], offset, "; ".join(wrong)))
            counted += 1
            ok("checks 4 and 5: %s (%s, %d bytes, loads at %s): the hash is the manifest's and the "
               "one descriptor at 0x%X says %s/%s/%s %s"
               % (entry["name"], entry["use"], entry["size"], entry.get("load_address", "-"),
                  offset, fields["role"], fields["board"], fields["band"], fields["version"]))

    # SHA256SUMS, as a person reading the release would use it.
    sums = {}
    for raw in read(os.path.join(out_dir, "SHA256SUMS")).decode("ascii").splitlines():
        if not raw.strip():
            continue
        digest, name = raw.split(None, 1)
        sums[name.strip().lstrip("*")] = digest
    for image in manifest["images"]:
        for entry in image["files"]:
            if entry["name"] not in sums:
                raise Refused("SHA256SUMS does not list %s, which the manifest publishes"
                              % entry["name"])
            if sums[entry["name"]] != entry["sha256"]:
                raise Refused("SHA256SUMS says %s for %s and the manifest says %s"
                              % (sums[entry["name"]], entry["name"], entry["sha256"]))
    ok("SHA256SUMS lists every one of the %d published file(s), with the manifest's hashes" % counted)
    return checks, manifest


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_dir", help="the directory the release's files are in")
    ap.add_argument("--test-key", action="store_true",
                    help="trust the specification's public test key instead of keys/release-keys.txt")
    ap.add_argument("--keys", default=KEYS_FILE)
    args = ap.parse_args(argv[1:])

    try:
        if args.test_key:
            keys = [mf.test_public_key(FIXTURES)]
            label = "the specification's public test key"
        else:
            keys = load_keys(args.keys)
            where = os.path.relpath(args.keys, REPO)
            label = "%s (%d key%s)" % (where, len(keys), "" if len(keys) == 1 else "s")
        checks, manifest = verify(args.out_dir, keys, label)
    except Refused as exc:
        print("REFUSED: %s" % exc, file=sys.stderr)
        return 1
    print("\n".join(checks))
    print("%s %s (%s) passes every check of FIRMWARE-MANIFEST.md section 5 that applies to it"
          % (manifest["product"], manifest["tag"], manifest["version"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
