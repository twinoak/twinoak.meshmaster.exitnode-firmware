# The boundary of lib/agent-core, compiled: every source of the library on the host, with the
# strictest flags any consumer will use.
#
#   python tools/check-library-host.py
#
# `pio test -e native` already compiles the library whole with -std=c++17 -Wall -Wextra -Werror
# (platformio.ini's [env:native] build_flags, which reach a library build too, and the library's
# own build.flags in library.json). What it does not do is compile it the way an ESP-IDF consumer
# will: **-fno-exceptions and -fno-rtti**. The ESP-IDF turns exceptions off by default, and a
# library that quietly needs a `throw` or a `dynamic_cast` would only fail on the Walter's first
# build (twinoak.meshcore.walter-firmware, phase 4) - months after the mistake. So this compiles
# every .cpp of the library, on its own, with those two flags on, and nothing of the board or the
# tests in the include path: only lib/agent-core/include and ArduinoJson.
#
# The Arduino-on-ESP32 framework this repository builds with uses -fexceptions -fno-rtti, not
# -fno-exceptions (packages/framework-arduinoespressif32/tools/platformio-build-esp32s3.py), so
# the task's "-fno-exceptions -fno-rtti (the board's flags)" is the ESP-IDF's pair and the stricter
# of the two; the board's own half is covered by the board build itself.
#
# It needs a host compiler. There is none on Windows, which is why a session runs it in the
# container tasks/RUNBOOK.md section 2 names, as CI runs it on Linux where the compiler is the
# runner's own.

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(ROOT, "lib", "agent-core")

# ArduinoJson, wherever the toolchain put it: the native environment's libdeps first, then a
# board's, so this runs after `pio test -e native` or after a board build.
CANDIDATES = [
    os.path.join(ROOT, ".pio", "libdeps", env, "ArduinoJson", "src")
    for env in ("native", "heltec_v3", "heltec_v4")
]

FLAGS = [
    "-std=c++17",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-fno-exceptions",
    "-fno-rtti",
    "-c",
]


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


def main():
    cxx = compiler()
    sources = sorted(
        f for f in os.listdir(os.path.join(LIB, "src")) if f.endswith(".cpp")
    )
    if not sources:
        sys.stderr.write("lib/agent-core/src has no sources\n")
        return 1

    includes = ["-I" + os.path.join(LIB, "include"), "-I" + arduinojson_include()]
    failed = []
    with tempfile.TemporaryDirectory() as out:
        for name in sources:
            src = os.path.join(LIB, "src", name)
            cmd = [cxx] + FLAGS + includes + [src, "-o", os.path.join(out, name + ".o")]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                failed.append(name)
                sys.stderr.write("--- %s\n%s%s" % (name, result.stdout, result.stderr))

    if failed:
        sys.stderr.write(
            "\n%d of %d sources did not compile with %s\n"
            % (len(failed), len(sources), " ".join(FLAGS[:-1]))
        )
        return 1

    print(
        "lib/agent-core: %d sources compiled with %s"
        % (len(sources), " ".join(FLAGS[:-1]))
    )
    print("the include path was lib/agent-core/include and ArduinoJson, and nothing else")
    return 0


if __name__ == "__main__":
    sys.exit(main())
