#!/usr/bin/env python3
"""The frame rail: no function of ours puts more than 1 024 bytes on the stack.

0.7.3 did not start. `loadUpdate` built a 2 764-byte record and returned it by value into the
member that keeps it, inside `Updater::atBoot`, inside `setup()`: 9 232 bytes on a loop task that
had 8 192, and a board that restarted for ever with *Stack canary watchpoint triggered*. Nothing
said so before a board did - a frame is invisible in the source, and no test here runs on a
board's stack (tasks/phase-1.15/FOLLOWUPS.md, *0.7.3 does not boot*).

So the compiler says it. Every board build compiles with ``-fstack-usage`` (platformio.ini's
``[frames]``), which writes ``<object>.su`` beside every object file: one line a function, with
where it is defined, the bytes its frame came to and whether the compiler could bound it. This
reads the ones under **src/** and **lib/agent-core/** - upstream's and the framework's are not
ours to hold - and refuses:

  * a frame over LIMIT bytes, unless the function is named in CEILINGS below with a ceiling of its
    own and a reason, and is inside that ceiling;
  * a frame the compiler could not bound at all (``dynamic`` without ``bounded``: alloca, or a
    variable-length array whose length nothing bounds). A function whose stack use nothing can
    state is one nothing can rail;
  * no .su file of ours at all, or none from the files named in ANCHORS - which is what a build
    that lost the flag, or a stale build directory, looks like. A rail that passes because it read
    nothing is worse than no rail.

**Nothing in src/update/ may have a ceiling.** That is the code 0.7.3 died in, and the point of
this task is that it is inside the limit rather than excused from it.

    python tools/check-frames.py --env heltec_v3            the rail, and the 20 largest
    python tools/check-frames.py --env heltec_v3 --top 40   more of them
    python tools/check-frames.py --self-test                the planted cases it must refuse

The build runs it itself (tools/pio/check_frames.py, a ``post:`` script of both boards), so a
frame over the limit is a red build on this machine and in CI alike. Python 3 and nothing else.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# What a function of ours may put on the stack, in bytes. The loop task has 16 384 and everything
# the network does runs on it, nested: the console's command, the agent's call, the TLS handshake,
# a release's signature. 1 024 is a frame a person can hold in their head, and sixteen of them
# nested still fit.
LIMIT = 1024

# The trees this holds, repository-relative. Everything else a build compiles - MeshCore, the
# framework, the ESP-IDF, the Arduino core, the sensor libraries - writes .su files too and none
# of them is read: upstream's frames are not ours to fix (platformio.ini's note on -w and -Werror).
OURS = ("src/", "lib/agent-core/")

# A build that read none of these is not a build of this tree: the flag was dropped, the objects
# are stale, or the paths moved. Every one of them must contribute at least one frame.
ANCHORS = (
    "src/main.cpp",
    "src/update/UpdateRecord.cpp",
    "src/update/Ask.cpp",
    "src/update/Trial.cpp",
    "lib/agent-core/src/AgentLoop.cpp",
)

# The named exceptions: (file, function) -> (ceiling in bytes, why). A function here is over LIMIT
# today and bringing it under was not a small change; the ceiling is **today's size**, so it may
# not grow, and the entry is what a later task deletes. Nothing in src/update/ may appear here.
CEILINGS: dict[tuple[str, str], tuple[int, str]] = {
    ("src/observer/Observer.cpp", "void mm::Observer::reportNeighbours()"): (1888,
        "the neighbour report is built from two arrays of kNeighbourCap entries - 20 keys of 65 "
        "characters and their 20 fields - which is 1 780 of these bytes; the only way under the "
        "limit is to make them members of the Observer, and that is 1.8 kB of .bss held for the "
        "life of a board that has 60 % of its RAM in use, to save a frame that is taken for the "
        "few milliseconds a report is built in once a sweep. It is a leaf of the loop task: it "
        "calls formatNeighbours (720) and nothing deeper"),
    ("src/observer/Token.cpp", "bool mm::observer::makeToken(const mm::observer::TokenClaims&, mm::observer::Signer&, char*, size_t)"): (1296,
        "the JWT is three buffers that have to coexist - the claims as JSON (512), the header "
        "base64url (64) and the claims base64url (512) - and the sizes are MeshCore's JWTHelper's "
        "own, which this file is a reading of; shrinking one of them would mean a token refused "
        "rather than a smaller token, and an exit node that cannot sign in to the broker. It runs "
        "once per broker connection, from the loop task, with nothing deeper than base64Url"),
}


class FrameError(Exception):
    pass


@dataclass(frozen=True)
class Frame:
    unit: str  # the source file the object was compiled from, repository-relative
    at: str  # `basename:line:column`, which is all GCC writes of where the function is
    name: str  # the signature as the compiler prints it
    bytes_: int
    qualifier: str  # static | dynamic | dynamic,bounded | bounded

    @property
    def bounded(self) -> bool:
        parts = {p.strip() for p in self.qualifier.split(",")}
        return "dynamic" not in parts or "bounded" in parts

    @property
    def where(self) -> str:
        return f"{self.unit} ({self.at})"


# `file:line:column:signature\tbytes\tqualifier`. **`file` is a bare basename**: GCC writes it
# through `lbasename`, so `src/update/UpdateRecord.cpp` reads as `UpdateRecord.cpp` and a function
# defined in a header reads as that header's name - which is why the source file a frame belongs to
# is taken from the .su file's own place in the build directory instead (see `unit_of`). A signature
# may hold colons of its own (`mm::update::Journal::add(...)`), so the position is matched from the
# left with the digits anchored.
LINE = re.compile(r"^(?P<at>[^:]+:\d+:\d+):(?P<name>.*)$")

# PlatformIO compiles a project library into `lib<hash>/<library>/`, with the library's own src/
# flattened away: `lib/agent-core/src/AgentLoop.cpp` becomes `lib0f0/agent-core/AgentLoop.cpp.o`.
# The hash is the environment's and changes with its flags, so it is matched rather than written.
PROJECT_LIB = re.compile(r"^lib[0-9a-f]+/(?P<library>[^/]+)/(?P<rest>.*)$")


def unit_of(su: Path, build_dir: Path) -> str | None:
    """The source file a .su file belongs to, as this repository names it; None when not ours.

    The build directory mirrors the project: our own sources are under `src/`, and a project
    library's are under `lib<hash>/<library>/`. Everything else in there - the ESP-IDF's
    components, the Arduino core, MeshCore, the registry libraries - is not ours to hold.
    """
    rel = su.relative_to(build_dir).as_posix()
    if not rel.endswith(".su"):
        return None
    rel = rel[: -len(".su")]
    if rel.startswith("src/"):
        return rel
    m = PROJECT_LIB.match(rel)
    if m is not None:
        unit = f"lib/{m.group('library')}/src/{m.group('rest')}"
        if unit.startswith(OURS):
            return unit
    return None


def parse(text: str, where: Path, unit: str) -> list[Frame]:
    out: list[Frame] = []
    for n, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        fields = line.split("\t")
        if len(fields) != 3:
            raise FrameError(f"{where}:{n}: not a -fstack-usage line: {raw!r}")
        m = LINE.match(fields[0])
        if m is None:
            raise FrameError(f"{where}:{n}: no file:line:column in {fields[0]!r}")
        try:
            size = int(fields[1])
        except ValueError:
            raise FrameError(f"{where}:{n}: {fields[1]!r} is not a byte count") from None
        out.append(Frame(unit, m.group("at"), m.group("name"), size, fields[2].strip()))
    return out


def collect(build_dir: Path) -> list[Frame]:
    """The largest frame of every (source file, function) the build wrote a .su line for."""
    if not build_dir.is_dir():
        raise FrameError(f"{build_dir} is not a directory - run pio run -e <env> first")
    biggest: dict[tuple[str, str], Frame] = {}
    files = 0
    ours = 0
    for su in sorted(build_dir.rglob("*.su")):
        files += 1
        unit = unit_of(su, build_dir)
        if unit is None:
            continue
        ours += 1
        for f in parse(su.read_text(encoding="utf-8", errors="replace"), su, unit):
            key = (f.unit, f.name)
            if key not in biggest or f.bytes_ > biggest[key].bytes_:
                biggest[key] = f
    if files == 0:
        raise FrameError(
            f"{build_dir} holds no .su file at all: the -fstack-usage flag of platformio.ini's "
            f"[frames] did not reach the compiler, or nothing was compiled"
        )
    if ours == 0:
        raise FrameError(
            f"{build_dir} holds {files} .su file(s) and not one of ours: src/ and lib/agent-core "
            f"were not compiled into it, or the build directory's layout moved"
        )
    return sorted(biggest.values(), key=lambda f: (-f.bytes_, f.unit, f.name))


def check(frames: list[Frame]) -> tuple[list[str], list[str]]:
    """(problems, notes). Problems are one line each; an empty list is a pass."""
    problems: list[str] = []
    notes: list[str] = []

    for (unit, name), (ceiling, why) in sorted(CEILINGS.items()):
        if unit.startswith("src/update/"):
            problems.append(
                f"{unit}: {name} is named in CEILINGS, and nothing in src/update/ may be: that is "
                f"the code 0.7.3 died in"
            )
        if ceiling <= LIMIT:
            problems.append(
                f"{unit}: {name} has a ceiling of {ceiling} bytes, which is not over the limit of "
                f"{LIMIT}: delete the entry rather than keep it"
            )
        if not any(f.unit == unit and f.name == name for f in frames):
            problems.append(
                f"{unit}: {name} is named in CEILINGS and this build has no such frame: the "
                f"function moved or went, so the entry is stale"
            )
        else:
            notes.append(f"{unit}: {name} is allowed {ceiling} bytes ({why})")

    for f in frames:
        allowed = CEILINGS.get((f.unit, f.name))
        limit = allowed[0] if allowed is not None else LIMIT
        if not f.bounded:
            problems.append(
                f"{f.where}: {f.name} has a frame the compiler cannot bound ({f.qualifier}, "
                f"{f.bytes_} bytes counted): nothing can say what this costs on a board"
            )
            continue
        if f.bytes_ > limit:
            problems.append(
                f"{f.where}: {f.name} puts {f.bytes_} bytes on the stack, over the "
                f"{'ceiling' if allowed else 'limit'} of {limit}"
            )
    return problems, notes


def report(frames: list[Frame], top: int) -> None:
    print(f"frames    {len(frames)} function(s) of ours, limit {LIMIT} bytes")
    if top <= 0:
        return
    print(f"the {min(top, len(frames))} largest:")
    for f in frames[:top]:
        print(f"  {f.bytes_:6d}  {f.qualifier:<16}  {f.unit}  {f.at}  {f.name}")


def run_check(build_dir: Path, top: int) -> int:
    try:
        frames = collect(build_dir)
    except (FrameError, OSError) as e:
        print(f"FAIL  {e}")
        return 1
    print(f"build     {build_dir}")
    report(frames, top)
    problems: list[str] = []
    seen = {f.unit for f in frames}
    for anchor in ANCHORS:
        if anchor not in seen:
            problems.append(
                f"{anchor} contributed no frame: the build is stale, the file moved, or "
                f"-fstack-usage did not reach it"
            )
    more, notes = check(frames)
    problems += more
    for n in notes:
        print(f"ok    {n}")
    for p in problems:
        print(f"FAIL  {p}")
    if not problems:
        largest = frames[0] if frames else None
        print(
            f"ok    no frame of ours is over {LIMIT} bytes and every one of them is bounded"
            + (f"; the largest is {largest.bytes_} ({largest.name})" if largest else "")
        )
    return 1 if problems else 0


# ---- the planted cases ------------------------------------------------------------------------


def self_test() -> int:
    """A 2 000-byte local in src/update/, one in lib/agent-core, a frame nothing can bound.

    The planted builds are .su files laid out as a real build directory lays them: ours under
    `src/` and `lib<hash>/agent-core/`, upstream's anywhere else. The byte counts and the
    qualifiers are the compiler's own format.
    """
    failures = 0
    # The planted builds hold the planted frames and no others, so the real exceptions - which
    # name functions no planted build has - are set aside for the length of this and put back
    # after it. A case that is about the mechanism plants an entry of its own.
    real = dict(CEILINGS)
    CEILINGS.clear()

    # Every anchor contributes one small frame, so that a case says what it is about and not that
    # the build was empty. `planted` is what the case under test adds.
    def anchor_files() -> dict[str, list[str]]:
        out: dict[str, list[str]] = {}
        for n, a in enumerate(ANCHORS):
            if a.startswith("lib/agent-core/src/"):
                rel = "libdead/agent-core/" + a[len("lib/agent-core/src/"):]
            else:
                rel = a
            out[rel + ".su"] = [f"{Path(a).name}:1:1:anchor_{n}()\t64\tstatic"]
        return out

    def case(what: str, planted: dict[str, list[str]], must_pass: bool, word: str = "",
             anchors: bool = True) -> None:
        nonlocal failures
        files = anchor_files() if anchors else {}
        for rel, lines in planted.items():
            files.setdefault(rel, []).extend(lines)
        with tempfile.TemporaryDirectory() as d:
            build = Path(d) / "build"
            for rel, lines in files.items():
                p = build / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text("\n".join(lines) + "\n", encoding="utf-8")
            try:
                frames = collect(build)
            except FrameError as e:
                ok = not must_pass and (not word or word in str(e))
                failures += 0 if ok else 1
                print(f"{'ok  ' if ok else 'FAIL'}  {what} ({e})")
                return
            problems, _ = check(frames)
            seen = {f.unit for f in frames}
            problems += [f"{a} contributed no frame" for a in ANCHORS if a not in seen]
            passed = not problems
            ok = passed == must_pass and (passed or any(word in p for p in problems))
            failures += 0 if ok else 1
            print(f"{'ok  ' if ok else 'FAIL'}  {what}")
            for p in problems:
                print(f"        {'refused: ' if not must_pass else 'unexpected: '}{p}")

    case(
        "a frame inside the limit passes",
        {"src/net/WifiLink.cpp.su": ["WifiLink.cpp:40:6:scan()\t1024\tstatic"]},
        True,
    )
    case(
        "a function with a local of 2 000 bytes in src/update/ is refused",
        {"src/update/UpdateRecord.cpp.su": [
            "UpdateRecord.cpp:265:6:void mm::update::loadUpdate(mm::update::RecordStore&)\t2016\tstatic"]},
        False,
        "over the limit of 1024",
    )
    case(
        "a function with a local of 2 000 bytes in lib/agent-core is refused",
        {"lib9ab/agent-core/Airtime.cpp.su": ["Airtime.cpp:88:6:mm::Airtime::gate()\t2016\tstatic"]},
        False,
        "over the limit of 1024",
    )
    case(
        "a frame the compiler cannot bound is refused, however small it is counted",
        {"src/console/Console.cpp.su": ["Console.cpp:12:6:void mm::Console::line(char const*)\t96\tdynamic"]},
        False,
        "cannot bound",
    )
    case(
        "and one it can bound is not",
        {"src/console/Console.cpp.su": ["Console.cpp:12:6:void mm::Console::line(char const*)\t512\tdynamic,bounded"]},
        True,
    )
    case(
        "a build with no frame from one of the anchors is refused",
        {"src/net/WifiLink.cpp.su": ["WifiLink.cpp:40:6:scan()\t64\tstatic"]},
        False,
        "contributed no frame",
        anchors=False,
    )
    case(
        "upstream's frames are not read: a huge one of the ESP-IDF's passes, and one of MeshCore's",
        {
            "bt/host/bluedroid/stack/gatt/gatt_auth.c.su": ["gatt_auth.c:9:6:big()\t40000\tstatic"],
            "lib7f1/MeshCore/Mesh.cpp.su": ["Mesh.cpp:9:6:bigger()\t40000\tdynamic"],
        },
        True,
    )
    case(
        "a build directory with nothing of ours in it is refused",
        {"bt/host/x.c.su": ["x.c:1:1:f()\t64\tstatic"]},
        False,
        "not one of ours",
        anchors=False,
    )

    # The ceiling mechanism, with an entry planted the way a task would write one.
    key = ("src/observer/Observer.cpp", "void mm::observer::Observer::report()")
    CEILINGS[key] = (1600, "planted by the self-test")
    case(
        "a function named in CEILINGS passes inside its ceiling",
        {"src/observer/Observer.cpp.su": [f"Observer.cpp:10:6:{key[1]}\t1600\tstatic"]},
        True,
    )
    case(
        "and is refused over it",
        {"src/observer/Observer.cpp.su": [f"Observer.cpp:10:6:{key[1]}\t1601\tstatic"]},
        False,
        "over the ceiling of 1600",
    )
    case(
        "a stale CEILINGS entry is refused",
        {"src/net/WifiLink.cpp.su": ["WifiLink.cpp:40:6:scan()\t64\tstatic"]},
        False,
        "is stale",
    )
    del CEILINGS[key]

    bad = ("src/update/Ask.cpp", "void mm::update::loadAsk(mm::update::RecordStore&)")
    CEILINGS[bad] = (2048, "planted by the self-test")
    case(
        "a ceiling in src/update/ is refused whatever it says",
        {"src/update/Ask.cpp.su": [f"Ask.cpp:134:6:{bad[1]}\t2048\tstatic"]},
        False,
        "nothing in src/update/ may be",
    )
    del CEILINGS[bad]

    with tempfile.TemporaryDirectory() as d:
        build = Path(d) / "build"
        build.mkdir(parents=True)
        try:
            collect(build)
            print("FAIL  a build directory with no .su file at all is accepted")
            failures += 1
        except FrameError as e:
            print(f"ok    a build directory with no .su file at all is refused ({e})")
    with tempfile.TemporaryDirectory() as d:
        build = Path(d) / "build" / "src"
        build.mkdir(parents=True)
        (build / "x.cpp.su").write_text("x.cpp:1:1:f()\tnot-a-number\tstatic\n", encoding="utf-8")
        try:
            collect(build.parent)
            print("FAIL  a .su line whose byte count is not a number is accepted")
            failures += 1
        except FrameError as e:
            print(f"ok    a .su line whose byte count is not a number is refused ({e})")

    CEILINGS.clear()
    CEILINGS.update(real)
    print(f"{failures} failed")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--env", help="the PlatformIO environment whose .pio/build/<env> to read")
    src.add_argument("--build-dir", type=Path, help="a build directory to read directly")
    src.add_argument("--self-test", action="store_true", help="run the script's own planted cases")
    ap.add_argument("--top", type=int, default=20, help="how many of the largest frames to print (0 for none)")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    build = args.build_dir if args.build_dir is not None else ROOT / ".pio" / "build" / args.env
    return run_check(build, args.top)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
