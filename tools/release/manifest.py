#!/usr/bin/env python3
"""FIRMWARE-MANIFEST.md as code, for this product.

The specification is `tests/fixtures/firmware/FIRMWARE-MANIFEST.md`, synced from meshmaster.dk
(`tools/sync-fixtures.ps1 -Set firmware`), and the fixtures beside it are what it generates. The
reference implementation of that document, `spec/tools/manifestref.py`, lives in the hardware
repository and in the Walter firmware's checkout of it - **not here, and it does not know this
product**: `twinoak-exitnode-firmware`, the role `exit-node` and its descriptor byte 32 were
chosen on 18 November 2026 and are not in the document yet (tasks/phase-1.15/FOLLOWUPS.md).

So this file is the document transcribed again, with those three values added, and what holds it
to the reference is not a promise but the fixtures: `tools/release/test_release.py` re-derives
every signature, descriptor and canonical form in `tests/fixtures/firmware/index.json` with the
code below and refuses a difference. The firmware's own reader (`src/update/`) is a third
implementation, in C++, held against the same fixtures by `test/test_release_reader`.

    python tools/release/manifest.py descriptor <image.bin>   find and print the descriptor
    python tools/release/manifest.py keygen                   print a new key pair

Standard library only; the signing is tools/release/ed25519ref.py.
"""

import base64
import hashlib
import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ed25519ref  # noqa: E402

# ----------------------------------------------------------------------------- the descriptor

DESC_MAGIC = b"TWOKFWD1"
DESC_LEN = 96
# Section 4's table, with 32 for this firmware's role. The numbers below 32 are the document's and
# are kept so that a file of another product reads as what it is rather than as "role-1".
ROLES = {"unknown": 0, "repeater": 1, "bridge": 2, "scanner": 3, "manager": 16, "exit-node": 32}
ROLE_NAMES = {v: k for k, v in ROLES.items()}
SLOT_ANY = 0xFF


class Refused(Exception):
    """A sentence on stderr and exit 1. Every refusal of the release path is one of these."""


def _fixed(text, size):
    raw = text.encode("ascii")
    if len(raw) >= size:
        raise ValueError("%r does not fit %d bytes with its terminator" % (text, size))
    return raw + b"\x00" * (size - len(raw))


def make_descriptor(role, version, board, band, link_major=0, link_minor=0, slot=SLOT_ANY):
    """The 96 bytes an image carries about itself (FIRMWARE-MANIFEST.md section 4)."""
    body = (DESC_MAGIC + struct.pack("<HBBBBH", DESC_LEN, ROLES[role], slot, link_major, link_minor, 0)
            + _fixed(version, 32) + _fixed(board, 24) + _fixed(band, 8) + b"\x00" * 12)
    assert len(body) == DESC_LEN - 4
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def parse_descriptor(blob):
    if len(blob) != DESC_LEN or blob[:8] != DESC_MAGIC:
        raise ValueError("not a descriptor")
    length, role, slot, major, minor, flags = struct.unpack("<HBBBBH", blob[8:16])
    if length != DESC_LEN:
        raise ValueError("descriptor length %d" % length)
    if struct.unpack("<I", blob[92:96])[0] != (zlib.crc32(blob[:92]) & 0xFFFFFFFF):
        raise ValueError("descriptor crc")

    def text(raw):
        return raw.split(b"\x00", 1)[0].decode("ascii")

    return {"role": ROLE_NAMES.get(role, "role-%d" % role), "slot": slot, "link_major": major,
            "link_minor": minor, "flags": flags, "version": text(blob[16:48]),
            "board": text(blob[48:72]), "band": text(blob[72:80])}


def find_descriptor(image):
    """(offset, fields). Exactly one valid descriptor, on a 4-byte boundary: none means the file
    does not say what it is, two mean it cannot be believed (section 4)."""
    found = []
    at = image.find(DESC_MAGIC)
    while at >= 0:
        if at % 4 == 0 and at + DESC_LEN <= len(image):
            try:
                found.append((at, parse_descriptor(image[at:at + DESC_LEN])))
            except ValueError:
                pass
        at = image.find(DESC_MAGIC, at + 1)
    if len(found) != 1:
        raise ValueError("%d valid descriptors in the image, expected exactly one" % len(found))
    return found[0]


# ----------------------------------------------------------------------------- the manifest

SCHEMA = 1
# The two products of the document, and this one. An unknown product is refused by `validate`; a
# product that is not ours is refused a step later, by the release gate and by the firmware
# (check 2), which is why all three are listed here and only one of them is PRODUCT.
PRODUCTS = ("twinoak-repeater-firmware", "twinoak-walter-firmware", "twinoak-exitnode-firmware")
USES = ("swd", "usb-dfu", "usb-uf2", "ota", "factory", "debug", "provision")


def canonical(manifest):
    """How this repository writes manifest.json: UTF-8 without a BOM, two-space indent, LF, one
    trailing newline, keys in the order they are put in. What is signed is the file's bytes."""
    return (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def validate(manifest):
    """The checks a consumer makes after the signature has passed (section 2). Raises ValueError."""
    def need(cond, what):
        if not cond:
            raise ValueError(what)

    need(manifest.get("schema") == SCHEMA, "schema is not %d" % SCHEMA)
    need(manifest.get("product") in PRODUCTS, "unknown product")
    for key in ("version", "tag", "commit", "built_at"):
        need(isinstance(manifest.get(key), str) and manifest[key], key + " missing")
    need(len(manifest["commit"]) == 40, "commit is not 40 hex characters")
    link = manifest.get("link", {})
    need(isinstance(link.get("major"), int) and isinstance(link.get("minor"), int), "link version missing")
    need(isinstance(manifest.get("images"), list) and manifest["images"], "no images")
    ids = set()
    for image in manifest["images"]:
        for key in ("id", "role", "board", "band", "mcu"):
            need(isinstance(image.get(key), str) and image[key], "image." + key + " missing")
        need(image["id"] not in ids, "duplicate image id " + image["id"])
        ids.add(image["id"])
        need(isinstance(image.get("files"), list) and image["files"], "image has no files")
        for f in image["files"]:
            need(f.get("use") in USES, "unknown use %r" % f.get("use"))
            need(isinstance(f.get("name"), str) and "/" not in f["name"] and "\\" not in f["name"],
                 "bad file name")
            need(isinstance(f.get("size"), int) and f["size"] > 0, "bad size")
            need(isinstance(f.get("sha256"), str) and len(f["sha256"]) == 64, "bad sha256")
            if f["use"] in ("swd", "ota", "factory"):
                need(isinstance(f.get("load_address"), str) and f["load_address"].startswith("0x"),
                     "load_address missing")
            if f["use"] == "swd":
                region = image.get("app_region", {})
                need("start" in region and "end" in region, "an swd image needs app_region")
                start, end = int(region["start"], 16), int(region["end"], 16)
                load = int(f["load_address"], 16)
                need(start <= load and load + f["size"] <= end, "the image does not fit its app_region")
            need(f.get("format") != "hex" or f["use"] == "provision",
                 "format hex is for a provision file only")
            if f["use"] == "provision":
                need(f.get("format") == "hex", "a provision file is Intel HEX (format hex)")
                need("load_address" not in f, "a provision file has no load_address: its records say where")
                need(image["mcu"] == "nrf52840", "a provision file is for an nRF52840 board")
                region = image.get("app_region", {})
                need("start" in region and "end" in region,
                     "a provision file needs its image's app_region")
        need(sum(1 for f in image["files"] if f.get("use") == "provision") <= 1,
             "more than one provision file in image " + image["id"])


def file_entry(use, name, data, load_address=None):
    """One file of one image: its size and hash read from its bytes and never typed."""
    entry = {"use": use, "name": name, "format": name.rsplit(".", 1)[-1], "size": len(data),
             "sha256": hashlib.sha256(data).hexdigest()}
    if load_address is not None:
        entry["load_address"] = "0x%08X" % load_address
    return entry


# ----------------------------------------------------------------------------- the signature


def key_id(public):
    """Sixteen hex characters: the first eight bytes of SHA-256 over the raw 32-byte public key."""
    return hashlib.sha256(public).hexdigest()[:16]


def sign(private, message):
    """The one line of manifest.json.sig (section 3), with its newline."""
    public = ed25519ref.public_key(private)
    signature = ed25519ref.sign(private, message)
    return "ed25519 %s %s\n" % (key_id(public), base64.b64encode(signature).decode("ascii"))


def parse_signature_line(line):
    """(key id, 64 signature bytes), read as strictly as the firmware reads it
    (src/update/Release.cpp): `ed25519 <16 lowercase hex> <88 characters of standard base64>`, one
    space between the three, one optional trailing newline and nothing else. Raises ValueError."""
    if line.endswith("\r\n"):
        line = line[:-2]
    elif line.endswith("\n"):
        line = line[:-1]
    parts = line.split(" ")
    if len(parts) != 3:
        raise ValueError("the signature line is not three words separated by one space each")
    if parts[0] != "ed25519":
        raise ValueError("the algorithm is %r, not ed25519" % parts[0])
    if len(parts[1]) != 16 or any(c not in "0123456789abcdef" for c in parts[1]):
        raise ValueError("the key id is not 16 lowercase hex characters")
    if len(parts[2]) != 88 or not parts[2].endswith("="):
        raise ValueError("the signature is not 88 characters of base64")
    try:
        raw = base64.b64decode(parts[2], validate=True)
    except Exception:
        raise ValueError("the signature is not strict base64")
    if len(raw) != 64:
        raise ValueError("an Ed25519 signature is 64 bytes")
    if base64.b64encode(raw).decode("ascii") != parts[2]:
        # Python's b64decode checks the alphabet and not the bits: the last character of 88 carries
        # four that a 64-byte value does not use, and a line that sets one of them is a second
        # spelling of the same signature. The firmware refuses it (src/update/Release.cpp), so this
        # does too.
        raise ValueError("the signature is not the one spelling of those 64 bytes")
    return parts[1], raw


def verify(publics, message, sig_line):
    """publics: the raw public keys the verifier trusts. The line names the one it used."""
    try:
        named, signature = parse_signature_line(sig_line)
    except ValueError:
        return False
    for public in publics:
        if key_id(public) == named:
            return ed25519ref.verify(public, message, signature)
    return False


# ----------------------------------------------------------------------------- the test key


def test_key_pair(fixtures_dir):
    """The specification's key pair, read from the fixtures that publish it on purpose
    (tests/fixtures/firmware/TEST-KEY.txt and index.json) and never written down here. Returns
    (private, public); the private half is a secret of nothing, which is the point of it."""
    text_path = os.path.join(fixtures_dir, "TEST-KEY.txt")
    with open(text_path, encoding="ascii") as fh:
        words = dict(line.split(None, 1) for line in fh if line.split()[:1] in (["private"], ["public"]))
    private = bytes.fromhex(words["private"].strip())
    public = bytes.fromhex(words["public"].strip())
    if ed25519ref.public_key(private) != public:
        raise Refused("%s: the private and public halves do not belong together" % text_path)
    index_path = os.path.join(fixtures_dir, "index.json")
    with open(index_path, encoding="utf-8") as fh:
        index = json.load(fh)
    if public.hex() != index["test_public_key"] or key_id(public) != index["test_key_id"]:
        raise Refused("%s and %s do not agree about the test key" % (text_path, index_path))
    return private, public


def test_public_key(fixtures_dir):
    return test_key_pair(fixtures_dir)[1]


# ----------------------------------------------------------------------------- the command line


def main(argv):
    if len(argv) >= 3 and argv[1] == "descriptor":
        with open(argv[2], "rb") as fh:
            image = fh.read()
        try:
            offset, fields = find_descriptor(image)
        except ValueError as exc:
            print("REFUSED: %s: %s" % (argv[2], exc), file=sys.stderr)
            return 1
        print("0x%X  %s" % (offset, json.dumps(fields, sort_keys=True)))
        return 0
    if len(argv) >= 2 and argv[1] == "keygen":
        # A key for a person to make once, by hand, and to put the private half of into the
        # RELEASE_SIGNING_KEY secret and nowhere else (docs/release.md).
        private = os.urandom(32)
        public = ed25519ref.public_key(private)
        print("private %s" % private.hex())
        print("public  %s" % public.hex())
        print("key id  %s" % key_id(public))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
