#!/usr/bin/env python3
"""The release tooling's own tests: `python tools/release/test_release.py`.

Three things are held here, and CI runs all of them before anything is built
(.github/workflows/ci.yml):

  the specification     every signature, descriptor and canonical form that
                        tests/fixtures/firmware/index.json records is re-derived with
                        tools/release/manifest.py and must come out the same. That is what holds
                        this repository's transcription of FIRMWARE-MANIFEST.md to the reference
                        implementation in the hardware repository, which made those fixtures.
  RFC 8032              section 7.1's first test vector, and the three ways it must fail.
  the refusals          every sentence the release path says no with, each with its own case:
                        the trust table's, sign_manifest.py's, build_release.py's and the gate's.

No framework and no dependency: a plain script, so it runs anywhere the release does.
"""

import io
import json
import os
import shutil
import sys
import tempfile
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "tools"))

import build_release  # noqa: E402
import ed25519ref  # noqa: E402
import make_test_release  # noqa: E402
import manifest as mf  # noqa: E402
import sign_manifest  # noqa: E402
import verify_release  # noqa: E402
from manifest import Refused  # noqa: E402

FIXTURES = os.path.join(REPO, "tests", "fixtures", "firmware")
RELEASE = os.path.join(REPO, "tests", "fixtures", "release")
KEYS = os.path.join(REPO, "keys", "release-keys.txt")

CASES = []
FAILURES = []


def case(fn):
    CASES.append(fn)
    return fn


def ok(condition, what):
    if not condition:
        raise AssertionError(what)


def refused(what, fn, *args, **kwargs):
    """`fn` must raise Refused, and the sentence must mention `what`."""
    try:
        fn(*args, **kwargs)
    except Refused as exc:
        ok(what in str(exc), "refused with %r, which does not mention %r" % (str(exc), what))
        return str(exc)
    raise AssertionError("%r was accepted and must be refused" % what)


def read(name, where=FIXTURES):
    with open(os.path.join(where, name), "rb") as fh:
        return fh.read()


def index():
    return json.loads(read("index.json").decode("utf-8"))


# ------------------------------------------------------------------ the specification


@case
def the_test_key_is_the_one_the_fixtures_publish():
    private, public = mf.test_key_pair(FIXTURES)
    idx = index()
    ok(public.hex() == idx["test_public_key"], "the public half is not index.json's")
    ok(mf.key_id(public) == idx["test_key_id"], "the key id is not index.json's")
    ok(ed25519ref.public_key(private) == public, "the halves do not belong together")


@case
def rfc8032_test_1_verifies_and_fails_three_ways():
    vector = index()["rfc8032_test_1"]
    public = bytes.fromhex(vector["public"])
    message = bytes.fromhex(vector["message"])
    signature = bytes.fromhex(vector["signature"])
    ok(ed25519ref.verify(public, message, signature), "the RFC's first test vector does not verify")
    ok(not ed25519ref.verify(public, message + b"\x00", signature), "a changed message verified")
    ok(not ed25519ref.verify(public, message, signature[:5] + bytes([signature[5] ^ 1]) + signature[6:]),
       "a changed signature verified")
    ok(not ed25519ref.verify(bytes([public[0] ^ 1]) + public[1:], message, signature),
       "a changed key verified")


@case
def every_signature_in_the_index_comes_out_as_the_index_says():
    public = mf.test_public_key(FIXTURES)
    for entry in index()["signatures"]:
        got = mf.verify([public], read(entry["manifest"]), read(entry["sig"]).decode("ascii"))
        ok(got == entry["valid"], "%s with %s: %s, index.json says %s"
           % (entry["manifest"], entry["sig"], got, entry["valid"]))


@case
def the_reference_manifests_validate_and_are_canonical():
    for name in ("manifest-repeater.json", "manifest-walter.json", "manifest-repeater-base.json"):
        data = read(name)
        parsed = json.loads(data)
        mf.validate(parsed)
        ok(mf.canonical(parsed) == data, "%s is not in canonical form" % name)


@case
def every_descriptor_in_the_index_reads_as_the_index_says():
    for entry in index()["descriptors"]:
        blob = read(entry["image"])
        try:
            offset, fields = mf.find_descriptor(blob)
            good = True
        except ValueError:
            good = False
        ok(good == entry["valid"], "%s: %s, index.json says %s" % (entry["image"], good, entry["valid"]))
        if good:
            ok(offset == entry["offset"], "%s: 0x%X, index.json says 0x%X"
               % (entry["image"], offset, entry["offset"]))
            ok(fields == entry["fields"], "%s: %r, index.json says %r"
               % (entry["image"], fields, entry["fields"]))
            ok(blob[offset:offset + mf.DESC_LEN].hex() == entry["hex"],
               "%s: the bytes are not index.json's" % entry["image"])


@case
def this_products_descriptor_round_trips():
    blob = mf.make_descriptor("exit-node", "0.8.0+1a2b3c4", "heltec_v3", "868")
    ok(len(blob) == mf.DESC_LEN, "a descriptor is %d bytes" % mf.DESC_LEN)
    ok(blob[10] == 32, "the exit node's role byte is 32, not %d" % blob[10])
    fields = mf.parse_descriptor(blob)
    ok(fields == {"role": "exit-node", "slot": 0xFF, "link_major": 0, "link_minor": 0, "flags": 0,
                  "version": "0.8.0+1a2b3c4", "board": "heltec_v3", "band": "868"},
       "it does not read back as it was written: %r" % fields)


# ------------------------------------------------------------------ the signature line


@case
def a_signature_line_is_read_strictly():
    good = read("manifest-repeater.json.sig").decode("ascii")
    mf.parse_signature_line(good)  # must not raise
    algorithm, key, signature = good.strip().split(" ")
    alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    # The 88 characters are 86 of data and two of padding, and the last data character carries
    # four bits that a 64-byte value does not use: setting one of them is a second spelling of the
    # same signature, which strict base64 refuses.
    dangling = signature[:85] + alphabet[alphabet.index(signature[85]) | 1] + signature[86:]

    for text, why in (
        ("%s  %s %s\n" % (algorithm, key, signature), "a second space"),
        ("%s %s %s\n" % (algorithm, key[:-1], signature), "a key id of 15 characters"),
        ("%s %s %s\n" % (algorithm, key.upper(), signature), "an upper-case key id"),
        ("ed25518 %s %s\n" % (key, signature), "another algorithm"),
        ("%s %s %s\n" % (algorithm, key, signature[:10] + "-" + signature[11:]),
         "a character the base64 alphabet does not have"),
        ("%s %s %s\n" % (algorithm, key, dangling), "base64 with bits that decode to nothing"),
        ("%s %s %s\n" % (algorithm, key, signature[:-1]), "a signature of 87 characters"),
        ("%s %s\n" % (algorithm, key), "a line of two words"),
    ):
        try:
            mf.parse_signature_line(text)
        except ValueError:
            continue
        raise AssertionError("%s was accepted" % why)


# ------------------------------------------------------------------ the trust table


@case
def the_trust_table_is_the_key_file():
    keys = verify_release.load_keys(KEYS)
    ok(len(keys) == 1, "keys/release-keys.txt names %d keys, expected 1" % len(keys))
    ok(mf.key_id(keys[0]) == "73efdd383a957596", "the release key is not the one the Walter has")


@case
def a_trust_table_refuses_what_it_must():
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "gen_trust_table", os.path.join(REPO, "tools", "gen-trust-table.py"))
    gen = importlib.util.module_from_spec(spec)
    sys.modules["gen_trust_table"] = gen
    spec.loader.exec_module(gen)

    private, public = mf.test_key_pair(FIXTURES)
    real = open(KEYS, encoding="utf-8").read()
    with tempfile.TemporaryDirectory() as scratch:
        def write(text):
            path = os.path.join(scratch, "keys.txt")
            with open(path, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)
            return path

        refused("the key id is", verify_release.load_keys,
                write("ed25519 0000000000000000 %s\n" % public.hex()))
        refused("expected `ed25519", verify_release.load_keys, write("rsa one two three\n"))
        refused("an Ed25519 public key is 32 bytes", verify_release.load_keys,
                write("ed25519 %s %s\n" % (mf.key_id(b"x" * 31), ("aa" * 31))))
        refused("room for 3", verify_release.load_keys,
                write("".join("ed25519 %s %s\n" % (mf.key_id(bytes([i]) * 32), (bytes([i]) * 32).hex())
                              for i in range(1, 5))))
        # The test key: refused for a release build, taken for a test build.
        with_test = real + "ed25519 %s %s\n" % (mf.key_id(public), public.hex())
        refused("never in a release build's trust table", verify_release.load_keys, write(with_test))
        keys = verify_release.load_keys(write(with_test), test_build=True)
        ok(len(keys) == 2, "a test build takes the test key: %d keys" % len(keys))

        # And the header it renders, for a release build and for a test build.
        header = gen.generate(KEYS, test_build=False)
        ok("kTrustedKeyCount = 1" in header, "the release build's table is not one key")
        ok("kTrustsTestKey = false" in header, "the release build's table says it is a test build")
        ok(public.hex()[:8] not in header.replace(", ", "").replace("0x", ""),
           "the release build's table carries the test key")
        header = gen.generate(KEYS, test_build=True)
        ok("kTrustedKeyCount = 2" in header, "the test build's table is not two keys")
        ok("kTrustsTestKey = true" in header, "the test build's table does not say so")
    ok(private is not None, "the test key pair was not read")


# ------------------------------------------------------------------ signing


@case
def signing_refuses_what_it_must():
    private, _ = mf.test_key_pair(FIXTURES)
    with tempfile.TemporaryDirectory() as scratch:
        path = os.path.join(scratch, "manifest.json")
        release = {"schema": 1, "product": build_release.PRODUCT, "version": "0.8.0+1a2b3c4",
                   "tag": "v0.8.0", "commit": "0" * 40, "built_at": "2026-11-18T12:00:00Z",
                   "link": {"major": 0, "minor": 0}, "images": []}
        with open(path, "wb") as fh:
            fh.write(mf.canonical(release))

        was = os.environ.pop(sign_manifest.ENV_VAR, None)
        try:
            refused("is not set in the environment", sign_manifest.sign, path, False)
            os.environ[sign_manifest.ENV_VAR] = "not hex at all"
            refused("is not hex", sign_manifest.sign, path, False)
            os.environ[sign_manifest.ENV_VAR] = "aa" * 31
            refused("an Ed25519 private key is 32", sign_manifest.sign, path, False)
            os.environ[sign_manifest.ENV_VAR] = private.hex()
            refused("holds the specification's test key", sign_manifest.sign, path, False)
        finally:
            os.environ.pop(sign_manifest.ENV_VAR, None)
            if was is not None:
                os.environ[sign_manifest.ENV_VAR] = was

        # The test key may not sign a release, by its version or by its tag.
        refused("refusing to sign", sign_manifest.sign, path, True)
        rehearsal = dict(release, version="0.0.0-dryrun+1a2b3c4", tag="v0.0.0-dryrun")
        with open(path, "wb") as fh:
            fh.write(mf.canonical(rehearsal))
        line, whose, _ = sign_manifest.sign(path, True)
        ok("test key" in whose, "a dry run was not signed with the test key")
        ok(mf.verify([mf.test_public_key(FIXTURES)], mf.canonical(rehearsal), line),
           "the dry run's signature does not verify")


# ------------------------------------------------------------------ building the manifest


def a_release(scratch, **changes):
    """The fixture release, copied where it can be spoiled."""
    for name in os.listdir(RELEASE):
        if name != "README.md":
            shutil.copy(os.path.join(RELEASE, name), os.path.join(scratch, name))
    for name, blob in changes.items():
        with open(os.path.join(scratch, name), "wb") as fh:
            fh.write(blob)
    return scratch


@case
def building_a_manifest_refuses_what_it_must():
    with tempfile.TemporaryDirectory() as scratch:
        a_release(scratch)
        args = (make_test_release.VERSION, make_test_release.COMMIT, make_test_release.TAG,
                scratch, make_test_release.BUILT_AT)

        # It is written again over the same files and comes out the same bytes.
        before = read("manifest.json", RELEASE)
        build_release.build(*args, upstream=build_release.meshcore_pin())
        ok(read("manifest.json", scratch) == before, "the manifest is not written the same twice")

        ota = "meshmaster-agent-heltec_v3-%s.bin" % make_test_release.TAG
        merged = "meshmaster-agent-heltec_v3-%s-merged.bin" % make_test_release.TAG
        app = bytearray(read(ota, RELEASE))
        load = build_release.app_slot_offset("heltec_v3")

        def pair(changed):
            """The V3's two files, both made from one application - so a refusal below is about
            the descriptor and not about two files from two builds."""
            whole = bytearray(read(merged, RELEASE))
            whole[load:load + len(changed)] = changed
            return a_release(scratch, **{ota: bytes(changed), merged: bytes(whole)})

        # A descriptor taken out, and one put in twice.
        none = bytearray(app)
        none[0x400] ^= 0xFF
        pair(none)
        refused("0 valid descriptors", build_release.build, *args)
        twice = bytearray(app)
        twice[0x800:0x800 + mf.DESC_LEN] = app[0x400:0x400 + mf.DESC_LEN]
        pair(twice)
        refused("2 valid descriptors", build_release.build, *args)

        # A descriptor that disagrees with the entry above it.
        other = bytearray(app)
        other[0x400:0x400 + mf.DESC_LEN] = mf.make_descriptor(
            "exit-node", make_test_release.VERSION, "heltec_v9", "868")
        pair(other)
        refused("does not agree with the manifest entry", build_release.build, *args)

        # The merged image from another build.
        elsewhere = bytearray(read(merged, RELEASE))
        elsewhere[0x10000 + 7] ^= 0x01
        a_release(scratch, **{merged: bytes(elsewhere)})
        refused("does not carry", build_release.build, *args)

        # A test-key build.
        with_key = bytearray(read(merged, RELEASE))
        with_key[0x200:0x200 + 32] = mf.test_public_key(FIXTURES)
        a_release(scratch, **{merged: bytes(with_key)})
        refused("public test key", build_release.build, *args)

        # A file that is not there.
        a_release(scratch)
        os.remove(os.path.join(scratch, ota))
        refused("cannot read", build_release.build, *args)


# ------------------------------------------------------------------ the gate


@case
def the_gate_passes_the_fixture_release_and_refuses_what_it_must():
    test_key = [mf.test_public_key(FIXTURES)]
    checks, manifest = verify_release.verify(RELEASE, test_key, "the test key")
    ok(manifest["product"] == build_release.PRODUCT, "the fixture is not this product")
    ok(len(checks) >= 6, "the gate said %d things, expected at least six" % len(checks))

    with tempfile.TemporaryDirectory() as scratch:
        # A key that is not the one that signed it: check 1, named as such.
        a_release(scratch)
        why = refused("check 1", verify_release.verify, scratch,
                      verify_release.load_keys(KEYS), "keys/release-keys.txt")
        ok("c17969deb0026955" in why, "the refusal does not name the key the line names")

        # One byte of the manifest changed after signing: check 1.
        spoiled = bytearray(read("manifest.json", RELEASE))
        spoiled[spoiled.index(b"esp32s3")] = ord("E")
        a_release(scratch, **{"manifest.json": bytes(spoiled)})
        refused("check 1", verify_release.verify, scratch, test_key, "the test key")

        # Another product's manifest, signed: check 2.
        a_release(scratch, **{"manifest.json": read("manifest-walter.json"),
                              "manifest.json.sig": read("manifest-walter.json.sig")})
        refused("check 2", verify_release.verify, scratch, test_key, "the test key")

        # SHA256SUMS that does not agree with the manifest.
        a_release(scratch)
        sums = read("SHA256SUMS", RELEASE).decode("ascii").replace("a", "b", 1)
        a_release(scratch, **{"SHA256SUMS": sums.encode("ascii")})
        refused("SHA256SUMS", verify_release.verify, scratch, test_key, "the test key")

        # No key at all.
        a_release(scratch)
        refused("no release key is trusted", verify_release.verify, scratch, [], "nothing")


@case
def the_fixture_release_is_what_the_tooling_writes():
    ok(make_test_release.main(["make_test_release.py", "--check"]) == 0,
       "tests/fixtures/release is not what tools/release/ writes")


def main():
    for fn in CASES:
        try:
            fn()
            print("ok    %s" % fn.__name__.replace("_", " "))
        except Exception:
            FAILURES.append(fn.__name__)
            print("FAIL  %s" % fn.__name__.replace("_", " "))
            traceback.print_exc()
    print("\n%d case(s), %d failed" % (len(CASES), len(FAILURES)))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)
    sys.exit(main())
