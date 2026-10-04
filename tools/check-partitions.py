#!/usr/bin/env python3
"""The partition-table check: the thing that stands between a wrong offset and a lost identity.

Nothing about a partition table can be proved on a board from a headless session, and a wrong
table is found only there - so this reads the files the build produces and holds them against
the framework's defaults, in CI, on every build of every board (.github/workflows/ci.yml):

  * ``spiffs``, ``coredump``, ``otadata`` and ``app0`` have exactly the framework default's offset
    and size for the board's flash size. spiffs holds the identity, the one thing on a board that
    cannot be re-provisioned; app0 is where the application image and ``pio run -t upload`` write;
    otadata is what boot_app0.bin fills at 0xE000.
  * ``nvs`` keeps its label (Preferences finds it by that name, src/settings/Store.cpp), keeps the
    default's size, and begins above app0's end - above anything a merged image can reach.
  * nothing lives in the gap 0x9000-0xDFFF, which ``esptool merge_bin`` fills with 0xFF.
  * the merged image (bootloader + partitions + application, one file from 0x0) ends before nvs
    begins - so the day the application grows past its region, CI says so before a release does.

The framework defaults are embedded below, as read from the installed package on the day, and
when that package is on disk the embedded copy is held against it too: a framework bump that moves
a default is then a red build, which is right, because our tables are derived from them.

    python tools/check-partitions.py --env heltec_v3 --merged .pio/build/heltec_v3/firmware-merged.bin
    python tools/check-partitions.py --csv boards/partitions/meshmaster_8MB.csv --flash-size 8MB
    python tools/check-partitions.py --self-test

``--env`` reads the csv the environment names in platformio.ini (``board_build.partitions``), and
the flash size from the merged image's own header unless ``--flash-size`` says: esptool writes the
``--flash_size`` it merged for into the bootloader header's fourth byte (tools/pio/merge_bin.py
passes the board's ``upload.flash_size``), so the size checked is the size the image was built for.
``--self-test`` runs the two shipped tables and the two wrong fixtures of
tests/fixtures/partitions/ and expects the wrong ones to be refused. Python 3 and nothing else.
"""

from __future__ import annotations

import argparse
import configparser
import os
import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# The framework's own tables, copied from ~/.platformio/packages/framework-arduinoespressif32/
# tools/partitions/default_8MB.csv and default_16MB.csv at framework 3.20017.241212+sha.dcc1105b
# (platformio/espressif32@6.11.0, Arduino core 2.0.17) on 5 October 2026. Offsets and sizes only.
FRAMEWORK_VERSION_READ = "3.20017.241212+sha.dcc1105b"
FRAMEWORK_DEFAULTS: dict[str, dict[str, tuple[int, int]]] = {
    "8MB": {
        "nvs": (0x9000, 0x5000),
        "otadata": (0xE000, 0x2000),
        "app0": (0x10000, 0x330000),
        "app1": (0x340000, 0x330000),
        "spiffs": (0x670000, 0x180000),
        "coredump": (0x7F0000, 0x10000),
    },
    "16MB": {
        "nvs": (0x9000, 0x5000),
        "otadata": (0xE000, 0x2000),
        "app0": (0x10000, 0x640000),
        "app1": (0x650000, 0x640000),
        "spiffs": (0xC90000, 0x360000),
        "coredump": (0xFF0000, 0x10000),
    },
}

# What must not move, and why (the message when it does).
PINNED = ("spiffs", "coredump", "otadata", "app0")

# The partition table itself is one sector at 0x8000; boot_app0 is at 0xE000. Between them is the
# span a merged image fills with 0xFF, and nothing of ours may live there.
GAP_START = 0x9000
GAP_END = 0xE000  # exclusive

# esptool's flash-size nibble in the image header's fourth byte (esptool/bin_image.py).
FLASH_SIZE_NIBBLE = {0: "1MB", 1: "2MB", 2: "4MB", 3: "8MB", 4: "16MB", 5: "32MB", 6: "64MB", 7: "128MB"}
ESP_IMAGE_MAGIC = 0xE9


@dataclass(frozen=True)
class Partition:
    name: str
    type: str
    subtype: str
    offset: int
    size: int

    @property
    def end(self) -> int:  # exclusive
        return self.offset + self.size


class TableError(Exception):
    pass


def parse_size(text: str, what: str) -> int:
    t = text.strip()
    if not t:
        raise TableError(f"{what} is empty; every offset and size is written down here on purpose")
    mult = 1
    if t[-1] in "kK":
        mult, t = 1024, t[:-1]
    elif t[-1] in "mM":
        mult, t = 1024 * 1024, t[:-1]
    try:
        return int(t, 0) * mult
    except ValueError as e:
        raise TableError(f"{what}: cannot read '{text.strip()}' as a number") from e


def parse_csv(path: Path) -> list[Partition]:
    """The csv as gen_esp32part.py reads it: name, type, subtype, offset, size[, flags]."""
    rows: list[Partition] = []
    for n, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5:
            raise TableError(f"{path.name}:{n}: fewer than five columns")
        name, ptype, subtype = cols[0], cols[1], cols[2]
        rows.append(
            Partition(
                name,
                ptype,
                subtype,
                parse_size(cols[3], f"{path.name}:{n} ({name}) offset"),
                parse_size(cols[4], f"{path.name}:{n} ({name}) size"),
            )
        )
    if not rows:
        raise TableError(f"{path}: no partitions")
    names = [p.name for p in rows]
    if len(set(names)) != len(names):
        raise TableError(f"{path.name}: a partition name repeats")
    return rows


def check_table(parts: list[Partition], flash_size: str, merged_len: int | None) -> list[str]:
    """Every problem found, as one line each; an empty list is a pass."""
    if flash_size not in FRAMEWORK_DEFAULTS:
        return [f"no framework default is known for a {flash_size} flash (known: {', '.join(FRAMEWORK_DEFAULTS)})"]
    defaults = FRAMEWORK_DEFAULTS[flash_size]
    by_name = {p.name: p for p in parts}
    problems: list[str] = []

    # The ones that must not move.
    for name in PINNED:
        want = defaults[name]
        have = by_name.get(name)
        if have is None:
            problems.append(f"{name} is missing; the default has it at 0x{want[0]:X} size 0x{want[1]:X}")
            continue
        if (have.offset, have.size) != want:
            problems.append(
                f"{name} moved: 0x{have.offset:X} size 0x{have.size:X}, the {flash_size} default is "
                f"0x{want[0]:X} size 0x{want[1]:X}"
                + (" - spiffs holds the identity, and a moved spiffs is a stranger's key on every repeater" if name == "spiffs" else "")
            )

    # nvs: label, size, and above the application.
    nvs = by_name.get("nvs")
    app0 = by_name.get("app0")
    if nvs is None:
        problems.append("there is no partition labelled nvs; Preferences opens NVS by that label")
    else:
        if (nvs.type, nvs.subtype) != ("data", "nvs"):
            problems.append(f"nvs is {nvs.type}/{nvs.subtype}, not data/nvs")
        if nvs.size != defaults["nvs"][1]:
            problems.append(f"nvs size is 0x{nvs.size:X}; the default's is 0x{defaults['nvs'][1]:X}")
        if app0 is not None and nvs.offset < app0.end:
            problems.append(
                f"nvs at 0x{nvs.offset:X} is not above app0's end (0x{app0.end:X}): a merged image reaches it"
            )
        spiffs = by_name.get("spiffs")
        if spiffs is not None and nvs.end > spiffs.offset and nvs.offset < spiffs.end:
            problems.append(f"nvs (0x{nvs.offset:X}-0x{nvs.end - 1:X}) overlaps spiffs")

    # The gap stays empty, and the table starts where a table may start.
    for p in parts:
        if p.offset < GAP_END and p.end > GAP_START:
            problems.append(
                f"{p.name} at 0x{p.offset:X} lies in the gap 0x{GAP_START:X}-0x{GAP_END - 1:X} that a merged image fills with 0xFF"
            )

    # No two partitions overlap (gen_esp32part.py refuses this too; the fixtures never reach it).
    ordered = sorted(parts, key=lambda p: p.offset)
    for a, b in zip(ordered, ordered[1:]):
        if b.offset < a.end:
            problems.append(f"{b.name} at 0x{b.offset:X} overlaps {a.name} (ends 0x{a.end - 1:X})")

    # The merged image ends before nvs begins. An image of length L occupies bytes 0..L-1, so
    # L <= nvs.offset is exactly "does not reach nvs"; the headroom is printed by the caller.
    if merged_len is not None and nvs is not None:
        if app0 is not None and merged_len <= app0.offset:
            problems.append(f"the merged image is {merged_len} bytes, which does not even reach app0 at 0x{app0.offset:X}")
        if merged_len > nvs.offset:
            problems.append(
                f"the merged image is {merged_len} bytes (0x{merged_len:X}) and reaches nvs at 0x{nvs.offset:X}: "
                f"flashing it would erase the settings"
            )
    return problems


def describe(parts: list[Partition]) -> str:
    width = max(len(p.name) for p in parts)
    return "\n".join(f"  {p.name:<{width}}  {p.type:<4} {p.subtype:<8} 0x{p.offset:07X}  0x{p.size:07X}" for p in parts)


# ---- where the inputs come from --------------------------------------------------------------


def csv_for_env(ini: Path, env: str) -> Path:
    cfg = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";",))
    cfg.read(ini, encoding="utf-8")
    section = f"env:{env}"
    seen: set[str] = set()
    while section in cfg and section not in seen:
        seen.add(section)
        if cfg.has_option(section, "board_build.partitions"):
            return ini.parent / cfg.get(section, "board_build.partitions").strip()
        section = cfg.get(section, "extends", fallback="").strip()
    raise TableError(f"[env:{env}] in {ini.name} names no board_build.partitions - the framework's default would be built")


def flash_size_of_merged(merged: Path) -> str:
    head = merged.read_bytes()[:4]
    if len(head) < 4 or head[0] != ESP_IMAGE_MAGIC:
        raise TableError(f"{merged}: does not start with an ESP image header (magic 0x{ESP_IMAGE_MAGIC:X})")
    nibble = head[3] >> 4
    if nibble not in FLASH_SIZE_NIBBLE:
        raise TableError(f"{merged}: unknown flash-size nibble {nibble}")
    return FLASH_SIZE_NIBBLE[nibble]


def framework_partitions_dir() -> Path | None:
    core = os.environ.get("PLATFORMIO_CORE_DIR") or str(Path.home() / ".platformio")
    d = Path(core) / "packages" / "framework-arduinoespressif32" / "tools" / "partitions"
    return d if d.is_dir() else None


def installed_defaults_match(flash_size: str) -> tuple[str, bool]:
    """Hold the embedded copy of default_<size>.csv against the installed package, if it is there."""
    d = framework_partitions_dir()
    if d is None:
        return ("framework package not on disk; the embedded copy of the defaults stands alone", True)
    path = d / f"default_{flash_size}.csv"
    if not path.is_file():
        return (f"{path} is not in the installed framework", False)
    installed = {p.name: (p.offset, p.size) for p in parse_csv(path)}
    embedded = FRAMEWORK_DEFAULTS[flash_size]
    if installed != embedded:
        diff = [n for n in sorted(set(installed) | set(embedded)) if installed.get(n) != embedded.get(n)]
        return (f"the installed {path.name} differs from the copy read at {FRAMEWORK_VERSION_READ}: {', '.join(diff)} - re-derive boards/partitions/", False)
    return (f"the installed {path.name} is the copy read at {FRAMEWORK_VERSION_READ}", True)


# ---- the two entry points ---------------------------------------------------------------------


def run_check(csv: Path, flash_size: str, merged: Path | None, merged_len: int | None = None) -> int:
    print(f"table     {csv}")
    print(f"flash     {flash_size}")
    try:
        parts = parse_csv(csv)
    except (TableError, OSError) as e:
        print(f"FAIL  {e}")
        return 1
    print(describe(parts))
    if merged is not None:
        merged_len = merged.stat().st_size
        print(f"merged    {merged} ({merged_len} bytes, 0x{merged_len:X})")
    elif merged_len is not None:
        print(f"merged    (a length of {merged_len} bytes, 0x{merged_len:X})")

    note, ok = installed_defaults_match(flash_size)
    print(f"{'ok  ' if ok else 'FAIL'}  {note}")
    found = check_table(parts, flash_size, merged_len)
    for p in found:
        print(f"FAIL  {p}")
    problems = found + ([] if ok else [note])
    if not problems:
        nvs = next(p for p in parts if p.name == "nvs")
        if merged_len is not None:
            print(f"ok    the merged image ends 0x{nvs.offset - merged_len:X} ({nvs.offset - merged_len} bytes) before nvs at 0x{nvs.offset:X}")
        print(f"ok    {', '.join(PINNED)} are the {flash_size} default's; nvs at 0x{nvs.offset:X} is above app0; the gap at 0x{GAP_START:X} is empty")
    return 1 if problems else 0


def self_test() -> int:
    """The script's own tests: the shipped tables pass, the wrong fixtures are refused."""
    tables = ROOT / "boards" / "partitions"
    fixtures = ROOT / "tests" / "fixtures" / "partitions"
    app_bytes = 1_400_000  # about today's application; any length between app0 and nvs will do
    cases: list[tuple[str, Path, str, int | None, bool, str]] = [
        # (what, csv, flash size, merged length, must pass, a word the failure must contain)
        ("the 8 MB table, with an image the size of today's", tables / "meshmaster_8MB.csv", "8MB", 0x10000 + app_bytes, True, ""),
        ("the 16 MB table, the same", tables / "meshmaster_16MB.csv", "16MB", 0x10000 + app_bytes, True, ""),
        ("the 8 MB table read as a 16 MB one is refused", tables / "meshmaster_8MB.csv", "16MB", None, False, "moved"),
        ("a table that moved spiffs is refused", fixtures / "spiffs-moved_8MB.csv", "8MB", 0x10000 + app_bytes, False, "spiffs"),
        ("the old default, nvs in the gap, is refused", fixtures / "nvs-in-the-gap_8MB.csv", "8MB", 0x10000 + app_bytes, False, "nvs"),
        ("a merged image that reaches nvs is refused", tables / "meshmaster_8MB.csv", "8MB", 0x340000 + 1, False, "merged image"),
        ("a merged image that ends exactly at nvs is accepted", tables / "meshmaster_8MB.csv", "8MB", 0x340000, True, ""),
    ]
    failures = 0
    for what, csv, size, merged_len, must_pass, word in cases:
        parts = parse_csv(csv)
        problems = check_table(parts, size, merged_len)
        passed = not problems
        ok = passed == must_pass and (passed or any(word in p for p in problems))
        failures += 0 if ok else 1
        print(f"{'ok  ' if ok else 'FAIL'}  {what}")
        for p in problems:
            print(f"        {'refused: ' if must_pass is False else 'unexpected: '}{p}")
    for size in FRAMEWORK_DEFAULTS:
        note, ok = installed_defaults_match(size)
        failures += 0 if ok else 1
        print(f"{'ok  ' if ok else 'FAIL'}  {note}")
    print(f"{len(cases)} cases, {failures} failed")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--env", help="the PlatformIO environment whose board_build.partitions to check")
    src.add_argument("--csv", type=Path, help="a partition table to check directly")
    src.add_argument("--self-test", action="store_true", help="run the script's own fixture tests")
    ap.add_argument("--merged", type=Path, help="the merged image (pio run -t mergebin); its length must end before nvs")
    ap.add_argument("--flash-size", choices=sorted(FRAMEWORK_DEFAULTS), help="8MB or 16MB; read from the merged image's header when absent")
    ap.add_argument("--ini", type=Path, default=ROOT / "platformio.ini")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()
    try:
        csv = csv_for_env(args.ini, args.env) if args.env else args.csv
        if args.merged is not None and not args.merged.is_file():
            raise TableError(f"{args.merged}: no such merged image - run pio run -e <env> -t mergebin first")
        flash_size = args.flash_size or (flash_size_of_merged(args.merged) if args.merged else None)
        if flash_size is None:
            raise TableError("say --flash-size, or give --merged and the size is read from its header")
    except TableError as e:
        print(f"FAIL  {e}")
        return 2
    return run_check(csv, flash_size, args.merged)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
