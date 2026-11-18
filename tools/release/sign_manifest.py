#!/usr/bin/env python3
"""Sign a release manifest: Ed25519 over its bytes (FIRMWARE-MANIFEST.md section 3).

    RELEASE_SIGNING_KEY=<private hex>  python tools/release/sign_manifest.py out/release/manifest.json
    python tools/release/sign_manifest.py --test-key out/release/manifest.json

The first is a release. The private key comes from an environment variable and from nowhere else -
the `RELEASE_SIGNING_KEY` secret of .github/workflows/release.yml. There is no `--key` argument
and no key file, because an argument is in the process table and a file is one `git add` from
being committed. **A release with no such secret is refused**, with what to do about it.

The second is a rehearsal: it signs with `tests/fixtures/firmware/TEST-KEY.txt`, the key pair the
specification publishes on purpose. The test key may only sign a version that is not a release -
`0.0.0-dryrun+<sha7>` or `0.0.0-dev`, not `X.Y.Z+<sha7>` - so a dry run's manifest can never be
mistaken for a signed release.

Nothing here prints a private key, in either mode. What it prints is the line it wrote: the
algorithm, the key id (section 3: eight bytes of SHA-256 over the public key, not a secret) and
the signature. tools/release/manifest.py does the signing; this file decides what may be signed.
"""

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import manifest as mf  # noqa: E402
from manifest import Refused  # noqa: E402

ENV_VAR = "RELEASE_SIGNING_KEY"
FIXTURES = os.path.join(REPO, "tests", "fixtures", "firmware")

# A released version: X.Y.Z and the commit it was built from, nothing else. A dry run's
# 0.0.0-dryrun+<sha7> and a local 0.0.0-dev are deliberately not of this shape.
RELEASE_VERSION = re.compile(r"^\d+\.\d+\.\d+\+[0-9a-f]{7,40}$")

NO_KEY = """no signing key: %s is not set in the environment.

A release of this firmware is signed where it is built, with the one private key that also signs
twinoak.meshcore.repeater-firmware's and twinoak.meshcore.walter-firmware's releases - the key
whose public half is the line in keys/release-keys.txt. Give this repository that half as the
secret %s (Settings > Secrets and variables > Actions).

If it cannot be had, make a key for this firmware alone:

    python tools/release/manifest.py keygen

Put the printed `private` half in the secret %s and nowhere else, replace the line in
keys/release-keys.txt with its `public` half, and add the public half on meshmaster.dk under
Admin > Firmware release keys. Until one of the two is done a release is refused here, which is
the safe state: no board takes an update signed by anything else.

A rehearsal that publishes nothing needs no secret: --test-key.""" % (ENV_VAR, ENV_VAR, ENV_VAR)


def release_shaped(manifest):
    """Whether this manifest claims to be a release: X.Y.Z+<sha> as the version, or vX.Y.Z as the
    tag. A dry run is neither, which is what makes the test key safe for one."""
    return bool(RELEASE_VERSION.fullmatch(str(manifest.get("version", "")))) or \
        bool(re.fullmatch(r"v\d+\.\d+\.\d+", str(manifest.get("tag", ""))))


def private_key(use_test_key):
    """The 32 private bytes and a word for where they came from. Never returned to a caller that
    prints things, and never put in an argument or in a file."""
    if use_test_key:
        return mf.test_key_pair(FIXTURES)[0], "the specification's public test key"
    raw = os.environ.get(ENV_VAR, "").strip()
    if not raw:
        raise Refused(NO_KEY)
    try:
        key = bytes.fromhex(raw)
    except ValueError:
        raise Refused("%s is not hex. It is the `private` line of `manifest.py keygen`, 64 hex "
                      "characters and nothing else." % ENV_VAR)
    if len(key) != 32:
        raise Refused("%s holds %d bytes; an Ed25519 private key is 32 (64 hex characters)."
                      % (ENV_VAR, len(key)))
    if key == mf.test_key_pair(FIXTURES)[0]:
        raise Refused("%s holds the specification's test key, which is public and signs nothing "
                      "that is released (FIRMWARE-MANIFEST.md section 3). Use --test-key for a "
                      "rehearsal, or set the secret to the release key." % ENV_VAR)
    return key, "the key in %s" % ENV_VAR


def sign(manifest_path, use_test_key):
    with open(manifest_path, "rb") as fh:
        data = fh.read()
    try:
        manifest = json.loads(data)
    except ValueError as exc:
        raise Refused("%s is not JSON: %s" % (manifest_path, exc))

    key, whose = private_key(use_test_key)
    if use_test_key and release_shaped(manifest):
        raise Refused("refusing to sign %s %s with the specification's test key: that key is "
                      "public, so the signature would prove nothing and a board that trusted it "
                      "would take anybody's firmware (FIRMWARE-MANIFEST.md section 3). A release "
                      "is signed with the key in %s."
                      % (manifest.get("tag"), manifest.get("version"), ENV_VAR))

    line = mf.sign(key, data)
    with open(manifest_path + ".sig", "w", encoding="ascii", newline="\n") as fh:
        fh.write(line)
    return line.strip(), whose, manifest


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("manifest", help="out/release/manifest.json; the signature goes beside it")
    ap.add_argument("--test-key", action="store_true",
                    help="sign with the specification's public test key. Refused for a release "
                         "version or a vX.Y.Z tag")
    args = ap.parse_args(argv[1:])
    try:
        line, whose, manifest = sign(args.manifest, args.test_key)
    except Refused as exc:
        print("REFUSED: %s" % exc, file=sys.stderr)
        return 1
    print("signed %s %s (tag %s) with %s" % (manifest.get("product"), manifest.get("version"),
                                             manifest.get("tag"), whose))
    print("%s.sig: %s" % (args.manifest, line))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
