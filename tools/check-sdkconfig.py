#!/usr/bin/env python3
"""The sdkconfig check: the committed ESP-IDF configuration is the Arduino core's own, but for the TLS keys.

Since task p1.9-36 the boards are built with the ESP-IDF compiled from source under
sdkconfig.defaults, so that mbedtls's buffer sizes are this repository's to set - and so that
every other key is this repository's to get wrong. This holds the committed file against the
configuration the Arduino core was built with (framework-arduinoespressif32's
tools/sdk/esp32s3/sdkconfig, on disk after a build), in CI, on every build of every board
(.github/workflows/ci.yml):

  * every key of either file that differs - a changed value, a key that is in one file and not the
    other - is in ALLOWED below, with the core's value, this repository's value and the reason, and
    differs exactly that way; anything else is a red build.
  * no key of FORBIDDEN is set: the ways esp-tls and mbedtls can be told not to verify a
    certificate, which test/test_tls_policy greps the tree for and which this file is not
    walked for (it is at the root, where the test does not look, and it contains those names
    as "is not set" lines by the core's own writing).
  * the core on disk is the version the file was copied from; a platform bump that moves the core
    is a red build, which is right, because the file is derived from it (re-derive it then:
    out/36-make-sdkconfig.py in task p1.9-36's evidence is how it was made).

    python tools/check-sdkconfig.py                         sdkconfig.defaults against the core's
    python tools/check-sdkconfig.py --built heltec_v3       and sdkconfig.heltec_v3, the generated one,
                                                            carries every allowed value (the build used them)
    python tools/check-sdkconfig.py --self-test             plants one difference that is not allowed, one
                                                            that is, and one forbidden key, and expects the
                                                            first and the last to be refused

A key is read as kconfgen writes it: ``CONFIG_X=value`` or ``# CONFIG_X is not set``; a key that is
in neither form is absent, which for a bool is the same as not set and is compared as such. Python 3
and nothing else.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DEFAULTS = ROOT / "sdkconfig.defaults"

# The core the committed file was copied from: platformio/espressif32@6.11.0's Arduino core 2.0.17,
# built on ESP-IDF 4.4.7. The check reads its package.json; a different version is a stop.
CORE_VERSION = "3.20017.241212"
CORE_SDKCONFIG = ("tools", "sdk", "esp32s3", "sdkconfig")

# The keys that may differ, and how: (the core's value, this repository's value, why).
# None is "not set" / absent. tasks/phase-1.9/36-lighter-tls.md is where they were asked for.
ALLOWED: dict[str, tuple[str | None, str | None, str]] = {
    "MBEDTLS_DYNAMIC_BUFFER": (None, "y",
        "the TLS content buffers are allocated while a record is in flight and freed after it, not held for the life of the connection"),
    "MBEDTLS_DYNAMIC_FREE_PEER_CERT": (None, "y",
        "the peer's certificate chain is freed once the handshake has verified it"),
    "MBEDTLS_DYNAMIC_FREE_CONFIG_DATA": (None, "y",
        "the handshake's key and DHM data are freed after use (a client has none, but the option gates the next one)"),
    "MBEDTLS_DYNAMIC_FREE_CA_CERT": (None, "y",
        "the parsed CA store is freed after the handshake; safe because both clients here parse their CA again for every connection (the report of p1.9-36 reads WiFiClientSecure and esp-tls for it)"),
    "MBEDTLS_ASYMMETRIC_CONTENT_LEN": (None, "y",
        "in and out buffers sized apart, which MBEDTLS_DYNAMIC_BUFFER selects"),
    "MBEDTLS_SSL_IN_CONTENT_LEN": (None, "16384",
        "a server may send a full 16 KB record and a client cannot refuse it"),
    "MBEDTLS_SSL_OUT_CONTENT_LEN": (None, "4096",
        "nothing this firmware sends needs a record over 4 KB; mbedtls fragments larger writes"),
    "MBEDTLS_SSL_MAX_CONTENT_LEN": ("16384", None,
        "the symmetric size, which the asymmetric pair above replaces (the key is invisible once ASYMMETRIC is set)"),
    "MBEDTLS_SSL_KEEP_PEER_CERTIFICATE": ("y", None,
        "off, as MBEDTLS_DYNAMIC_FREE_PEER_CERT requires; nothing here reads the peer's certificate after the handshake (no fingerprint or DN check: src/ has no verify() call)"),
    "MBEDTLS_SSL_PROTO_DTLS": ("y", None,
        "MBEDTLS_DYNAMIC_BUFFER is not available with DTLS compiled in, and nothing here speaks TLS over UDP"),
}

# Never set, whatever the core says: each is a way of not verifying a certificate. The first is
# spelt in two pieces for the same reason test/test_tls_policy spells its patterns so: that test
# greps tools/ for the word, and this file must not be the one place in the tree that has it.
FORBIDDEN = (
    "ESP_TLS_" + "INSE" + "CURE",
    "ESP_TLS_SKIP_SERVER_CERT_VERIFY",
    "MBEDTLS_SSL_PROTO_SSL3",
)


class ConfigError(Exception):
    pass


def parse(path: Path) -> dict[str, str | None]:
    """kconfgen's format: CONFIG_X=value, or '# CONFIG_X is not set' (None)."""
    out: dict[str, str | None] = {}
    for n, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            body = line[1:].strip()
            if body.startswith("CONFIG_") and body.endswith(" is not set"):
                key = body[len("CONFIG_"):-len(" is not set")].strip()
                if key in out and out[key] is not None:
                    raise ConfigError(f"{path.name}:{n}: CONFIG_{key} appears twice with different values")
                out[key] = None
            continue
        if not line.startswith("CONFIG_") or "=" not in line:
            raise ConfigError(f"{path.name}:{n}: not a config line: {raw!r}")
        key, value = line[len("CONFIG_"):].split("=", 1)
        key, value = key.strip(), value.strip()
        if not key:
            raise ConfigError(f"{path.name}:{n}: empty key")
        # The core's own file repeats CONFIG_USB_DEBUG_LEVEL=0 (a lib-builder artefact); a repeat
        # that agrees is let through, as kconfgen lets it, and one that disagrees is refused.
        if key in out and out[key] != value:
            raise ConfigError(f"{path.name}:{n}: CONFIG_{key} appears twice with different values")
        out[key] = value
    if not out:
        raise ConfigError(f"{path}: no config lines")
    return out


def show(v: str | None) -> str:
    return "not set" if v is None else v


def compare(core: dict[str, str | None], ours: dict[str, str | None]) -> tuple[list[str], list[str]]:
    """(problems, notes). Problems are one line each; an empty list is a pass."""
    problems: list[str] = []
    notes: list[str] = []
    for key in sorted(set(core) | set(ours)):
        a, b = core.get(key), ours.get(key)
        if a == b:
            continue
        if key not in ALLOWED:
            problems.append(f"CONFIG_{key} differs from the core's and is not an allowed key: core {show(a)}, ours {show(b)}")
            continue
        want_core, want_ours, why = ALLOWED[key]
        if a != want_core or b != want_ours:
            problems.append(
                f"CONFIG_{key} is allowed to differ as {show(want_core)} -> {show(want_ours)}, "
                f"but reads core {show(a)}, ours {show(b)}"
            )
            continue
        notes.append(f"CONFIG_{key}: {show(a)} -> {show(b)}  ({why})")
    for key, (want_core, want_ours, _) in ALLOWED.items():
        if core.get(key) == ours.get(key):
            problems.append(
                f"CONFIG_{key} is in the allowed list ({show(want_core)} -> {show(want_ours)}) but does not differ: "
                f"both read {show(core.get(key))} - the change is not in the file, or the list is stale"
            )
    for key in FORBIDDEN:
        if ours.get(key) not in (None, "n"):
            problems.append(f"CONFIG_{key} is set to {ours[key]}: a way of not verifying a certificate, and never allowed")
    return problems, notes


def check_built(built: dict[str, str | None]) -> list[str]:
    """The generated sdkconfig.<env> carries every allowed value: the build really used them."""
    problems: list[str] = []
    for key, (_, want_ours, _) in ALLOWED.items():
        have = built.get(key)
        if have != want_ours:
            problems.append(f"the generated configuration has CONFIG_{key} {show(have)}, the committed one {show(want_ours)}")
    for key in FORBIDDEN:
        if built.get(key) not in (None, "n"):
            problems.append(f"the generated configuration has CONFIG_{key} set to {built[key]}")
    return problems


# ---- where the core is ------------------------------------------------------------------------


def core_package_dir() -> Path:
    core = os.environ.get("PLATFORMIO_CORE_DIR") or str(Path.home() / ".platformio")
    return Path(core) / "packages" / "framework-arduinoespressif32"


def core_sdkconfig() -> Path:
    pkg = core_package_dir()
    manifest = pkg / "package.json"
    if not manifest.is_file():
        raise ConfigError(f"{pkg} is not on disk (no package.json): run pio run -e heltec_v3 first, the core is installed by the build")
    version = json.loads(manifest.read_text(encoding="utf-8")).get("version", "")
    if not version.startswith(CORE_VERSION):
        raise ConfigError(
            f"the installed core is {version}, and sdkconfig.defaults was copied from {CORE_VERSION}: "
            f"re-derive it from the new core's tools/sdk/esp32s3/sdkconfig and update CORE_VERSION here"
        )
    path = pkg.joinpath(*CORE_SDKCONFIG)
    if not path.is_file():
        raise ConfigError(f"{path} is not in the installed core")
    return path


# ---- the entry points -------------------------------------------------------------------------


def run_check(defaults: Path, built: Path | None) -> int:
    try:
        core_path = core_sdkconfig()
        core = parse(core_path)
        ours = parse(defaults)
    except (ConfigError, OSError) as e:
        print(f"FAIL  {e}")
        return 1
    print(f"core      {core_path} ({len(core)} keys, {CORE_VERSION})")
    print(f"ours      {defaults} ({len(ours)} keys)")
    problems, notes = compare(core, ours)
    for n in notes:
        print(f"ok    {n}")
    for p in problems:
        print(f"FAIL  {p}")
    if built is not None:
        try:
            b = parse(built)
            more = check_built(b)
            print(f"built     {built} ({len(b)} keys)")
        except (ConfigError, OSError) as e:
            more = [str(e)]
            print(f"built     {built}")
        for p in more:
            print(f"FAIL  {p}")
        if not more:
            print(f"ok    the generated configuration carries all {len(ALLOWED)} allowed values and none of the forbidden keys")
        problems += more
    if not problems:
        print(f"ok    sdkconfig.defaults is the core's configuration but for the {len(notes)} keys above; none of {', '.join('CONFIG_' + k for k in FORBIDDEN)} is set")
    return 1 if problems else 0


def self_test() -> int:
    """The planted cases: a disallowed difference, an allowed one, a forbidden key."""
    try:
        core_path = core_sdkconfig()
        core = parse(core_path)
        ours = parse(DEFAULTS)
    except (ConfigError, OSError) as e:
        print(f"FAIL  {e}")
        return 1
    failures = 0

    def case(what: str, cfg: dict[str, str | None], must_pass: bool, word: str = "") -> None:
        nonlocal failures
        problems, _ = compare(core, cfg)
        passed = not problems
        ok = passed == must_pass and (passed or any(word in p for p in problems))
        failures += 0 if ok else 1
        print(f"{'ok  ' if ok else 'FAIL'}  {what}")
        for p in problems:
            print(f"        {'refused: ' if not must_pass else 'unexpected: '}{p}")

    case("the committed sdkconfig.defaults passes", ours, True)
    planted = dict(ours)
    planted["FREERTOS_HZ"] = "100"  # a key that is not allowed to move (and the core's own CMake would refuse it too)
    case("a changed key that is not in the allowed list is refused", planted, False, "FREERTOS_HZ")
    planted = dict(ours)
    del planted["ESP_WIFI_SOFTAP_SUPPORT"]  # a key the core sets, dropped
    case("a key of the core's that went missing is refused", planted, False, "not an allowed key")
    planted = dict(ours)
    planted["MBEDTLS_SSL_OUT_CONTENT_LEN"] = "2048"  # allowed to differ, but not that way
    case("an allowed key with a value other than the listed one is refused", planted, False, "allowed to differ as")
    planted = dict(ours)
    planted[FORBIDDEN[0]] = "y"
    case("a forbidden key set is refused", planted, False, FORBIDDEN[0])
    case("the core's own file is refused: the allowed keys must differ", dict(core), False, "does not differ")
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "sdkconfig.planted"
        p.write_text(DEFAULTS.read_text(encoding="utf-8") + "CONFIG_FREERTOS_HZ=100\n", encoding="utf-8")
        try:
            parse(p)
            print("FAIL  a file that sets a key twice, differently, is accepted by the parser")
            failures += 1
        except ConfigError as e:
            print(f"ok    a file that sets a key twice, differently, is refused by the parser ({e})")
    print(f"{failures} failed")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--defaults", type=Path, default=DEFAULTS, help="the committed configuration (sdkconfig.defaults)")
    ap.add_argument("--built", metavar="ENV", help="also check the generated sdkconfig.<ENV> of a build")
    ap.add_argument("--self-test", action="store_true", help="run the script's own planted cases")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    built = (ROOT / f"sdkconfig.{args.built}") if args.built else None
    if built is not None and not built.is_file():
        print(f"FAIL  {built}: no generated configuration - run pio run -e {args.built} first")
        return 2
    return run_check(args.defaults, built)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
