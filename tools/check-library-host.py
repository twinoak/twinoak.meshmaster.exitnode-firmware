# The boundary of lib/agent-core, compiled: every source of the library on the host, in every
# configuration a consumer will build it in, with the strictest flags any of them uses.
#
#   python tools/check-library-host.py
#   python tools/check-library-host.py --self-test     only the name check, on planted sources
#
# `pio test -e native` already compiles the library whole with -std=c++17 -Wall -Wextra -Werror
# (platformio.ini's [env:native] build_flags, which reach a library build too, and the library's
# own build.flags in library.json). What it does not do is compile it the way a consumer will:
#
#   **-fno-exceptions and -fno-rtti.** The ESP-IDF turns exceptions off by default, and a library
#   that quietly needs a `throw` or a `dynamic_cast` would only fail on a consumer's first build.
#
#   **A GNU dialect.** `-std=gnu++17` is what a tree built with the ESP-IDF's own defaults is in,
#   and it predefines names - `unix`, `linux` - that a strict dialect does not. The first consumer
#   of this library found `ConfigReply.cpp` unbuildable for exactly that reason: a parameter named
#   `unix` became `uint32_t& 1` and the file was fifteen errors
#   (twinoak.meshcore.walter-firmware, its p4-01; task p1.6-20 renamed it `epoch`).
#
#   **An optimiser.** Half of GCC's warnings are raised by the middle end after inlining and are
#   not reachable at -O0 at all. The same consumer found ArduinoJson's own
#   `-Wmaybe-uninitialized` that way, with `-Werror` carried into their tree by `library.json`.
#
# So this compiles every .cpp of the library, on its own, in five configurations, with nothing of
# the board or the tests in the include path: only lib/agent-core/include and ArduinoJson. And
# before any of them, it reads the compiler's own list of predefined macros in each dialect and
# checks that no identifier of the library is one of those names - which is the `unix` mistake,
# caught by a grep instead of by a consumer.
#
# The Arduino-on-ESP32 framework this repository builds with uses -fexceptions -fno-rtti, not
# -fno-exceptions (packages/framework-arduinoespressif32/tools/platformio-build-esp32s3.py), so
# the -fno-exceptions below is the ESP-IDF's pair and the stricter of the two; the board's own
# half is covered by the board build itself.
#
# It needs a host compiler. There is none on Windows, which is why a session runs it in the
# container tasks/RUNBOOK.md section 2 names, as CI runs it on Linux where the compiler is the
# runner's own - and CI's compiler is the consumer's, which is why the version is printed first.

import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(ROOT, "lib", "agent-core")

# ArduinoJson, wherever the toolchain put it: the native environment's libdeps first, then a
# board's, so this runs after `pio test -e native` or after a board build.
CANDIDATES = [
    os.path.join(ROOT, ".pio", "libdeps", env, "ArduinoJson", "src")
    for env in ("native", "heltec_v3", "heltec_v4")
]

# What every configuration carries: a consumer's strictness, and the ESP-IDF's two.
COMMON = ["-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti", "-c"]

# The dialects a consumer compiles the library in. The library carries no `-std=` of its own
# (docs/library.md section 2), so both of these are the consumer's choice and both have to work.
DIALECTS = ["-std=c++17", "-std=gnu++17"]

# The first consumer's own host build is RelWithDebInfo under AddressSanitizer and
# UndefinedBehaviorSanitizer, and that configuration is the only one of the five that raises the
# `-Wmaybe-uninitialized` inside ArduinoJson 7.4.3 which `agent/Json.h` turns off - plain -O2 on
# GCC 13.3 does not. Compile only, so no sanitizer runtime is needed and it costs no more than
# the others.
SANITIZERS = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-DNDEBUG"]

# The configurations, as (name, flags): the matrix of dialect x optimisation level, and the
# consumer's own beside it.
CONFIGURATIONS = [
    ("c++17 -O0", ["-std=c++17", "-O0"]),
    ("c++17 -O2", ["-std=c++17", "-O2"]),
    ("gnu++17 -O0", ["-std=gnu++17", "-O0"]),
    ("gnu++17 -O2", ["-std=gnu++17", "-O2"]),
    ("c++17 -O2 asan+ubsan", ["-std=c++17", "-O2"] + SANITIZERS),
]

# Names a target or a GNU dialect predefines that are **not** reserved to the implementation -
# they have no leading underscore, so they are names a parameter, a local or a member could
# plausibly have, and a collision is a compile error in the consumer's tree and not in ours. The
# compiler is asked for its own list in each dialect below; this list is the ones GCC defines on
# targets this runner is not, which the compiler here will therefore not name. Historical and
# short: these are the System V and Berkeley names GCC still defines in a non-strict dialect.
OTHER_TARGETS_PREDEFINE = ["unix", "linux", "i386", "mips", "sparc", "vax", "sun", "bsd"]


def library_files():
    """What the name check reads: every header and every source of the library."""
    files = []
    for directory, _dirs, names in os.walk(LIB):
        for name in sorted(names):
            if name.endswith((".h", ".cpp")):
                files.append(os.path.join(directory, name))
    return sorted(files)


def without_comments_and_literals(code):
    """The code with every comment, string and character literal blanked out.

    A name check has to read code and not prose: `ReplyCodec.cpp` has a field called "unixTime"
    and `JobRunner.h` a comment about the `time <unix>` CLI command, and neither is an identifier.
    Blanked rather than removed, so that a line number is still a line number.
    """
    out = []
    i = 0
    n = len(code)
    while i < n:
        c = code[i]
        if c == "/" and i + 1 < n and code[i + 1] == "/":
            while i < n and code[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and i + 1 < n and code[i + 1] == "*":
            while i < n and not (code[i] == "*" and i + 1 < n and code[i + 1] == "/"):
                out.append("\n" if code[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif c in "\"'":
            quote = c
            out.append(" ")
            i += 1
            while i < n and code[i] != quote:
                if code[i] == "\\":
                    out.append(" ")
                    i += 1
                    if i < n:
                        out.append("\n" if code[i] == "\n" else " ")
                        i += 1
                    continue
                out.append("\n" if code[i] == "\n" else " ")
                i += 1
            out.append(" ")
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def predefined_names(cxx, dialects):
    """The compiler's own predefined macros, in these dialects, that are not reserved names."""
    names = set()
    for dialect in dialects:
        result = subprocess.run(
            [cxx, dialect, "-dM", "-E", "-x", "c++", os.devnull],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            sys.stderr.write("%s %s -dM -E failed:\n%s" % (cxx, dialect, result.stderr))
            sys.exit(2)
        for line in result.stdout.splitlines():
            parts = line.split(None, 2)
            if len(parts) >= 2 and parts[0] == "#define":
                name = parts[1].split("(")[0]
                if not name.startswith("_"):
                    names.add(name)
    return names


def names_used_in(path, names):
    """Every (line, name) where this file uses one of `names` as an identifier."""
    with open(path, "r", encoding="utf-8") as handle:
        code = without_comments_and_literals(handle.read())
    hits = []
    for name in sorted(names):
        for match in re.finditer(r"\b%s\b" % re.escape(name), code):
            line = 1 + code.count("\n", 0, match.start())
            hits.append((line, name))
    return sorted(hits)


def no_identifier_is_a_predefined_macro(names, files):
    """Report every identifier of the library that is one of these predefined names."""
    bad = []
    for path in files:
        for line, name in names_used_in(path, names):
            bad.append((os.path.relpath(path, ROOT).replace(os.sep, "/"), line, name))
    for rel, line, name in bad:
        sys.stderr.write(
            "%s:%d uses `%s`, which a compiler predefines as a macro: in that dialect this file "
            "is not code any more. Rename it.\n" % (rel, line, name)
        )
    if bad:
        sys.stderr.write(
            "\n%d identifier(s) of the library are predefined macro names. The first consumer of "
            "this library hit one (`unix` in ConfigReply.cpp) and could not compile the file at "
            "all; see this script's header.\n" % len(bad)
        )
    return not bad


def self_test():
    """The name check, against sources planted for it: the mistake, and the false alarms."""
    names = set(OTHER_TARGETS_PREDEFINE)
    cases = [
        ("the mistake, as a parameter", "bool f(unsigned& unix) { return unix == 0; }\n", True),
        ("the mistake, as a local", "void f() { int unix = 1; (void)unix; }\n", True),
        ("the mistake, as a member", "struct S { int linux; };\n", True),
        ("a string (unixTime, ReplyCodec.cpp)", 'const char* k = "unixTime";\n', False),
        ("a comment (time <unix>, JobRunner.h)", "// `time <unix>` is a CLI command\n", False),
        ("a block comment", "/* unix\n   linux */\nint ok = 0;\n", False),
        ("a longer identifier that contains one", "int unixish = 0, prelinux = 1;\n", False),
    ]
    ok = True
    with tempfile.TemporaryDirectory() as out:
        for label, code, should_fail in cases:
            path = os.path.join(out, "planted.cpp")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(code)
            hits = names_used_in(path, names)
            flagged = bool(hits)
            if flagged != should_fail:
                ok = False
            print(
                "  %-5s %s: %s"
                % (
                    "ok" if flagged == should_fail else "WRONG",
                    label,
                    ("flagged " + ", ".join(n for _l, n in hits)) if hits else "not flagged",
                )
            )
    print("the name check %s on its planted sources" % ("holds" if ok else "DOES NOT HOLD"))
    return 0 if ok else 1


def arduinojson_include():
    for path in CANDIDATES:
        if os.path.isdir(path):
            return path
    sys.stderr.write(
        "ArduinoJson is not on disk: run `pio test -e native` (or a board build) first, so that\n"
        ".pio/libdeps/<env>/ArduinoJson/src is there for the include path.\n"
    )
    sys.exit(2)


def compiler():
    for name in ("g++", "clang++", "c++"):
        found = shutil.which(name)
        if found:
            return found
    sys.stderr.write(
        "No host C++ compiler. On Windows there is none; run this in the container\n"
        "tasks/RUNBOOK.md section 2 names, which is what CI does on Linux.\n"
    )
    sys.exit(2)


def main(argv):
    if "--self-test" in argv:
        return self_test()

    cxx = compiler()
    # The compiler first, and its version: CI's is the consumer's, and a warning that is an error
    # in one GCC is not in another (task p1.6-20).
    version = subprocess.run([cxx, "--version"], capture_output=True, text=True)
    print("compiler: %s" % cxx)
    print(version.stdout.strip().splitlines()[0] if version.stdout else "(no --version)")

    sources = sorted(
        f for f in os.listdir(os.path.join(LIB, "src")) if f.endswith(".cpp")
    )
    if not sources:
        sys.stderr.write("lib/agent-core/src has no sources\n")
        return 1

    # The name check, before any compiling: the compiler's own predefined names in both dialects,
    # plus the ones other targets define, against every identifier of the library.
    files = library_files()
    names = predefined_names(cxx, DIALECTS) | set(OTHER_TARGETS_PREDEFINE)
    if not no_identifier_is_a_predefined_macro(names, files):
        return 1
    print(
        "no identifier in %d headers and sources is one of %d predefined macro names (%s ...)"
        % (len(files), len(names), ", ".join(sorted(names)[:4]))
    )

    includes = ["-I" + os.path.join(LIB, "include"), "-I" + arduinojson_include()]
    with tempfile.TemporaryDirectory() as out:

        def compile_one(job):
            label, flags, name = job
            src = os.path.join(LIB, "src", name)
            obj = os.path.join(out, "%s-%s.o" % (label.replace(" ", "_"), name))
            cmd = [cxx] + flags + COMMON + includes + [src, "-o", obj]
            result = subprocess.run(cmd, capture_output=True, text=True)
            return (label, name, result.returncode, result.stdout + result.stderr)

        jobs = [
            (label, flags, name) for label, flags in CONFIGURATIONS for name in sources
        ]
        with ThreadPoolExecutor(max_workers=(os.cpu_count() or 2)) as pool:
            results = list(pool.map(compile_one, jobs))

    failures = []
    for label, name, code, output in results:
        if code != 0:
            failures.append((label, name))
            sys.stderr.write("--- %s, %s\n%s" % (label, name, output))

    for label, _flags in CONFIGURATIONS:
        bad = [n for lbl, n in failures if lbl == label]
        print(
            "  %-22s %d sources, %s"
            % (label, len(sources), "clean" if not bad else "FAILED: " + ", ".join(bad))
        )

    if failures:
        sys.stderr.write(
            "\n%d of %d compiles failed. Every configuration carries %s.\n"
            % (len(failures), len(results), " ".join(COMMON[:-1]))
        )
        return 1

    print(
        "lib/agent-core: %d sources x %d configurations = %d compiles, all clean, with %s"
        % (len(sources), len(CONFIGURATIONS), len(results), " ".join(COMMON[:-1]))
    )
    print("the include path was lib/agent-core/include and ArduinoJson, and nothing else")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
