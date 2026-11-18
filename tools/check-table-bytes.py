#!/usr/bin/env python3
"""The partition tables the firmware carries as bytes, held against the files and the image.

Task p1.15-57 gave the firmware two partition tables per board, as byte arrays in
``src/platform/TableBytes.cpp``:

  * the table it **writes** (``kTable<size>TwoSlots``): the new table, with the second application
    slot. It has to be, byte for byte, what the merged image of the same build carries at 0x8000 -
    a board that takes `update slot` must end up with exactly the table a board flashed with the
    merged image has, or the two diverge and only one of them was ever tested.
  * the table it recognises as ``one`` (``kTable<size>OneSlot``): what every merged image from
    v0.2.0 to v0.7.1 wrote, which is what ``gen_esp32part.py`` makes of
    ``boards/partitions/meshmaster_<size>_one_slot.csv``. The firmware refuses to write over a
    sector that is not this, so a stale copy here is a board that cannot be updated - or, worse,
    one whose table is written over when it should not have been.

Both are held in CI, per board, after the merged image is built (.github/workflows/ci.yml):

    python tools/check-table-bytes.py --env heltec_v3 --merged .pio/build/heltec_v3/firmware-merged.bin
    python tools/check-table-bytes.py --self-test     the planted differences it must refuse
    python tools/check-table-bytes.py --write         regenerate TableBytes.cpp from the csv files

``gen_esp32part.py`` is the ESP-IDF's own, from the installed framework-espidf package (the same
one the build used), so the bytes are the build system's and not this script's idea of them.
Python 3 and nothing else.
"""

from __future__ import annotations

import argparse
import configparser
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
TABLES = ROOT / "boards" / "partitions"
BYTES_CPP = ROOT / "src" / "platform" / "TableBytes.cpp"

TABLE_OFFSET = 0x8000  # where the partition table lives; src/platform/TableFlash.cpp agrees
TABLE_LEN = 0xC00  # what gen_esp32part.py writes (MAX_PARTITION_LENGTH)

# (flash size, the csv of the table with two slots, the csv of the table boards hold today).
BOARDS: dict[str, tuple[str, str]] = {
    "8MB": ("meshmaster_8MB.csv", "meshmaster_8MB_one_slot.csv"),
    "16MB": ("meshmaster_16MB.csv", "meshmaster_16MB_one_slot.csv"),
}


class BytesError(Exception):
    pass


# ---- the inputs --------------------------------------------------------------------------------


def gen_esp32part() -> Path:
    core = os.environ.get("PLATFORMIO_CORE_DIR") or str(Path.home() / ".platformio")
    p = Path(core) / "packages" / "framework-espidf" / "components" / "partition_table" / "gen_esp32part.py"
    if not p.is_file():
        raise BytesError(f"{p} is not on disk: run pio run -e heltec_v3 first, the ESP-IDF is installed by the build")
    return p


def table_binary(csv: Path) -> bytes:
    """What the build system makes of a csv: gen_esp32part.py's own output, 0xC00 bytes."""
    if not csv.is_file():
        raise BytesError(f"{csv}: no such partition table")
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "partitions.bin"
        r = subprocess.run(
            [sys.executable, str(gen_esp32part()), "--quiet", str(csv), str(out)],
            capture_output=True,
            text=True,
        )
        if r.returncode != 0:
            raise BytesError(f"gen_esp32part.py refused {csv.name}: {(r.stderr or r.stdout).strip()}")
        data = out.read_bytes()
    if len(data) != TABLE_LEN:
        raise BytesError(f"gen_esp32part.py made {len(data)} bytes of {csv.name}, not 0x{TABLE_LEN:X}")
    return data


def merged_table(merged: Path) -> bytes:
    if not merged.is_file():
        raise BytesError(f"{merged}: no such merged image - run pio run -e <env> -t mergebin first")
    with merged.open("rb") as f:
        f.seek(TABLE_OFFSET)
        data = f.read(TABLE_LEN)
    if len(data) != TABLE_LEN:
        raise BytesError(f"{merged.name} has only {len(data)} bytes at 0x{TABLE_OFFSET:X}")
    return data


def flash_size_for_env(ini: Path, env: str) -> str:
    """The flash size the environment's table is for, read from the csv it names."""
    cfg = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";",))
    cfg.read(ini, encoding="utf-8")
    section = f"env:{env}"
    seen: set[str] = set()
    while section in cfg and section not in seen:
        seen.add(section)
        if cfg.has_option(section, "board_build.partitions"):
            name = Path(cfg.get(section, "board_build.partitions").strip()).name
            for size, (two, _one) in BOARDS.items():
                if name == two:
                    return size
            raise BytesError(f"[env:{env}] names {name}, which is not one of {', '.join(t for t, _ in BOARDS.values())}")
        section = cfg.get(section, "extends", fallback="").strip()
    raise BytesError(f"[env:{env}] in {ini.name} names no board_build.partitions")


# ---- the committed arrays ----------------------------------------------------------------------


def array_name(size: str, which: str) -> str:
    return f"kTable{size}{'TwoSlots' if which == 'two' else 'OneSlot'}"


def read_arrays(cpp: Path) -> dict[str, bytes]:
    """Every kTable... array of the generated file, by name."""
    text = cpp.read_text(encoding="utf-8")
    out: dict[str, bytes] = {}
    for m in re.finditer(r"const unsigned char (kTable\w+)\[kTableBytesLen\] = \{(.*?)\};", text, re.S):
        body = m.group(2)
        values = re.findall(r"0x([0-9A-Fa-f]{2})", body)
        if len(values) != TABLE_LEN:
            raise BytesError(f"{cpp.name}: {m.group(1)} has {len(values)} bytes, not 0x{TABLE_LEN:X}")
        out[m.group(1)] = bytes(int(v, 16) for v in values)
    if not out:
        raise BytesError(f"{cpp.name}: no kTable... array in it")
    return out


def render(arrays: dict[str, bytes]) -> str:
    """TableBytes.cpp, generated. The guards keep a board build to its own board's two tables."""
    lines: list[str] = []
    a = lines.append
    a("// GENERATED by tools/check-table-bytes.py --write. Do not edit: edit the csv files in")
    a("// boards/partitions/ and run that, and CI holds these bytes against them and against the")
    a("// merged image on every build (the script's own comment says why, task p1.15-57).")
    a("//")
    a("// Each array is the 0xC00 bytes gen_esp32part.py makes of one table - the entries, the MD5")
    a("// entry the bootloader checks, and 0xFF to the end - which is what a merged image carries at")
    a("// 0x8000 and what `update slot` writes there.")
    a("")
    a('#include "TableBytes.h"')
    a("")
    a("namespace mm {")
    for size, (two_csv, one_csv) in BOARDS.items():
        a("")
        a(f"#if defined(MM_TABLES_{size}) || defined(MM_TABLES_EVERY_BOARD)")
        for which, csv in (("one", one_csv), ("two", two_csv)):
            name = array_name(size, which)
            data = arrays[name]
            a("")
            a(f"// boards/partitions/{csv}")
            a(f"const unsigned char {name}[kTableBytesLen] = {{")
            for at in range(0, len(data), 16):
                row = ", ".join(f"0x{b:02X}" for b in data[at : at + 16])
                a(f"    {row},")
            a("};")
        a("")
        a(f"#endif  // {size}")
    a("")
    a("}  // namespace mm")
    a("")
    return "\n".join(lines)


# ---- the check ----------------------------------------------------------------------------------


def check(size: str, merged: Path | None, arrays: dict[str, bytes]) -> list[str]:
    two_csv, one_csv = BOARDS[size]
    problems: list[str] = []

    want_two = table_binary(TABLES / two_csv)
    want_one = table_binary(TABLES / one_csv)
    for which, want, csv in (("two", want_two, two_csv), ("one", want_one, one_csv)):
        name = array_name(size, which)
        have = arrays.get(name)
        if have is None:
            problems.append(f"{BYTES_CPP.name} has no {name}: run tools/check-table-bytes.py --write")
            continue
        if have != want:
            at = next(i for i in range(TABLE_LEN) if have[i] != want[i])
            problems.append(
                f"{name} is not what gen_esp32part.py makes of {csv}: it reads 0x{have[at]:02X} at 0x{at:03X} "
                f"where the table has 0x{want[at]:02X} - run tools/check-table-bytes.py --write"
            )
        else:
            print(f"ok    {name} is gen_esp32part.py's {csv} ({hashlib.sha256(want).hexdigest()[:16]})")

    if merged is not None:
        in_image = merged_table(merged)
        if in_image != want_two:
            at = next(i for i in range(TABLE_LEN) if in_image[i] != want_two[i])
            problems.append(
                f"the merged image carries 0x{in_image[at]:02X} at 0x{TABLE_OFFSET + at:X} where the table the firmware "
                f"would write has 0x{want_two[at]:02X}: a board flashed over USB and a board told `update slot` would "
                f"not end up with the same table"
            )
        else:
            print(f"ok    {merged} carries that table at 0x{TABLE_OFFSET:X}, byte for byte")

    # The two tables of a board differ by the app1 entry and by nothing else: one entry of 32 bytes
    # inserted, and the MD5 entry that follows them changed because the entries did.
    diff = sum(1 for a, b in zip(want_one, want_two) if a != b)
    if diff == 0:
        problems.append(f"{one_csv} and {two_csv} make the same bytes: the second slot is not in the new table")
    return problems


def run_check(env: str | None, size: str | None, merged: Path | None) -> int:
    try:
        if size is None:
            size = flash_size_for_env(ROOT / "platformio.ini", env or "")
        arrays = read_arrays(BYTES_CPP)
        print(f"board     {env or size} ({size})")
        print(f"arrays    {BYTES_CPP} ({len(arrays)} tables)")
        problems = check(size, merged, arrays)
    except (BytesError, OSError) as e:
        print(f"FAIL  {e}")
        return 2
    for p in problems:
        print(f"FAIL  {p}")
    if not problems:
        print("ok    the firmware's tables are the files' bytes, and the one it writes is the merged image's")
    return 1 if problems else 0


def self_test() -> int:
    """The planted differences: a byte changed in either array, and in the merged image."""
    try:
        arrays = read_arrays(BYTES_CPP)
        good = {size: table_binary(TABLES / BOARDS[size][0]) for size in BOARDS}
    except (BytesError, OSError) as e:
        print(f"FAIL  {e}")
        return 1
    failures = 0

    def case(what: str, size: str, planted: dict[str, bytes], merged: Path | None, word: str) -> None:
        nonlocal failures
        problems = check(size, merged, planted)
        ok = any(word in p for p in problems)
        failures += 0 if ok else 1
        print(f"{'ok  ' if ok else 'FAIL'}  {what}")
        for p in problems:
            print(f"        refused: {p}")

    for size in BOARDS:
        for which in ("one", "two"):
            name = array_name(size, which)
            planted = dict(arrays)
            body = bytearray(planted[name])
            body[0x40] ^= 0x01  # inside the entries, so the table really is a different table
            planted[name] = bytes(body)
            case(f"{name} with one byte changed is refused", size, planted, None, "0x040")

    with tempfile.TemporaryDirectory() as d:
        size = "8MB"
        image = Path(d) / "firmware-merged.bin"
        body = bytearray(0x8000 + TABLE_LEN)
        body[TABLE_OFFSET : TABLE_OFFSET + TABLE_LEN] = good[size]
        body[TABLE_OFFSET + 0x41] ^= 0x01
        image.write_bytes(bytes(body))
        case("a merged image whose table differs by one byte is refused", size, arrays, image, "merged image carries")

    # And the whole thing as CI runs it, with a merged image that is right.
    with tempfile.TemporaryDirectory() as d:
        size = "8MB"
        image = Path(d) / "firmware-merged.bin"
        body = bytearray(0x8000 + TABLE_LEN)
        body[TABLE_OFFSET : TABLE_OFFSET + TABLE_LEN] = good[size]
        image.write_bytes(bytes(body))
        problems = check(size, image, arrays)
        ok = not problems
        failures += 0 if ok else 1
        print(f"{'ok  ' if ok else 'FAIL'}  the committed arrays and a right merged image pass")
        for p in problems:
            print(f"        unexpected: {p}")

    print(f"{failures} failed")
    return 1 if failures else 0


def write_file() -> int:
    arrays: dict[str, bytes] = {}
    try:
        for size, (two_csv, one_csv) in BOARDS.items():
            arrays[array_name(size, "two")] = table_binary(TABLES / two_csv)
            arrays[array_name(size, "one")] = table_binary(TABLES / one_csv)
    except (BytesError, OSError) as e:
        print(f"FAIL  {e}")
        return 2
    BYTES_CPP.write_text(render(arrays), encoding="utf-8", newline="\n")
    print(f"ok    wrote {BYTES_CPP} ({len(arrays)} tables of 0x{TABLE_LEN:X} bytes)")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--env", help="the PlatformIO environment whose board to check")
    ap.add_argument("--flash-size", choices=sorted(BOARDS), help="8MB or 16MB, instead of --env")
    ap.add_argument("--merged", type=Path, help="the merged image (pio run -t mergebin); its bytes at 0x8000 must be the table")
    ap.add_argument("--self-test", action="store_true", help="run the script's own planted cases")
    ap.add_argument("--write", action="store_true", help="regenerate src/platform/TableBytes.cpp from the csv files")
    args = ap.parse_args(argv)

    if args.write:
        return write_file()
    if args.self_test:
        return self_test()
    if not args.env and not args.flash_size:
        print("FAIL  say --env <environment> or --flash-size <8MB|16MB>")
        return 2
    return run_check(args.env, args.flash_size, args.merged)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
