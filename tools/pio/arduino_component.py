# The Arduino core as an ESP-IDF component, compiled with what the Arduino-only build gave it
# (task p1.9-36). A `pre:` script of the board environments.
#
# With `framework = arduino, espidf` the core's own sources (cores/esp32, its libraries) are
# compiled by the ESP-IDF component build from the CMake code model, which knows nothing of
# platformio.ini. Two things the Arduino-only build gave the core are therefore handed over here,
# through `board_build.cmake_extra_args`, and CMakeLists.txt applies them to the core's component:
#
#   the board's extra_flags defines   boards/heltec_v4.json's -DBOARD_HAS_PSRAM,
#                                     -DARDUINO_USB_CDC_ON_BOOT=1, -DARDUINO_USB_MODE=1 ...; the
#                                     V3's are PlatformIO's esp32-s3-devkitc-1's. The core's
#                                     esp32-hal-psram.c compiles to a stub without the first and
#                                     HWCDC.cpp defines no `Serial` without the second.
#   MeshCore's pins_arduino.h         variants/<build.variant>/pins_arduino.h in the pinned
#                                     checkout, when there is one (the V4's; the V3 has none and
#                                     the core's own variants/esp32s3 serves, as before). The
#                                     Arduino-only build reached it through the -I in build_flags,
#                                     which came before the core's variant directory; the component
#                                     build puts the core's directory first, so the file goes to
#                                     both compiles as an -iquote, which a quoted #include searches
#                                     before any -I.
#
# And the include order of the project's own sources. The Arduino-only build searched the
# libraries' include directories before the framework's (the dependency finder prepends them);
# the component build prepends the ESP-IDF's 170-odd directories first and appends the
# libraries' after them, for src/ alone - a library's own sources still see their own directories
# first. On a case-insensitive filesystem that is a wrong header: the ESP-IDF's
# bt/esp_ble_mesh/mesh_core, a public include directory of its Bluetooth component, holds a
# mesh.h and a crypto.h, which `#include <Mesh.h>` (MeshCore) and `<Crypto.h>` (rweather/Crypto)
# then resolve to on Windows and macOS (out/36-cmd-AgentMesh-v1.txt is the compile line this was
# read from). So the libraries' directories are put first again here, for src/ only: the same
# list the builder appends, asked of the same dependency finder, given as GCC's long spelling of
# -I, `--include-directory=`, which PlatformIO does not recognise as an include flag and therefore
# leaves in the compiler flags - and compiler flags come before every -I on the command line.
# build_src_flags is the channel because it reaches the project's sources and nothing else: the
# ESP-IDF's own components are compiled from the same environment, and a MeshCore directory in
# front of *their* includes would turn the Bluetooth component's `#include "mesh.h"` into
# MeshCore's. Linux, where CI builds, has none of this; Thomas's machine does.
#
# And one thing of the ESP-IDF's: sdkconfig.<env>, the generated configuration, is seeded from
# sdkconfig.defaults only when it does not exist - an existing one wins over the defaults - so a
# change to the committed defaults would not reach a working tree that had built before. The
# generated file is deleted here when the defaults are newer, and the build regenerates it.
#
# docs/upstream.md lists this file among the places a pin bump re-reads (the variant directory).

import os

from platformio.builder.tools.piolib import ProjectAsLibBuilder
from SCons.Script import DefaultEnvironment

Import("env")  # noqa: F821  (SCons injects it)


def project_lib_include_dirs(env):
    """The libraries src/ depends on, their include directories, in the finder's order - the
    espidf builder's own get_project_lib_includes, which is not importable from a script."""
    # The core's own libraries (SPI, Wire ...), which the espidf builder adds to the search path
    # later than this script runs; without them the finder warns that it cannot find 'SPI'.
    core_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    if core_dir:
        env.AppendUnique(LIBSOURCE_DIRS=[os.path.join(core_dir, "libraries")])
    project = ProjectAsLibBuilder(env, "$PROJECT_DIR")
    project.install_dependencies()
    project.search_deps_recursive()
    paths = []
    for lb in env.GetLibBuilders():
        if not lb.dependent:
            continue
        for d in lb.get_include_dirs():
            if d not in paths:
                paths.append(d)
    DefaultEnvironment().Replace(__PIO_LIB_BUILDERS=None)
    return paths

if "espidf" in env.subst("$PIOFRAMEWORK"):
    board = env.BoardConfig()
    project_dir = env.subst("$PROJECT_DIR")

    flags = board.get("build.extra_flags", [])
    if isinstance(flags, str):
        flags = flags.split()
    defines = [f[2:] for f in flags if f.startswith("-D") and len(f) > 2]

    libdeps = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))
    pins_dir = os.path.join(libdeps, "MeshCore", "variants", board.get("build.variant", ""))
    pins = pins_dir if os.path.isfile(os.path.join(pins_dir, "pins_arduino.h")) else None

    args = []
    if defines:
        args.append('"-DMESHMASTER_CORE_DEFINES=%s"' % ";".join(defines))
    if pins:
        pins = pins.replace("\\", "/")
        args.append('"-DMESHMASTER_CORE_PINS_DIR=%s"' % pins)
        env.Append(CCFLAGS=["-iquote", pins])
    existing = board.get("build.cmake_extra_args", "")
    board.update("build.cmake_extra_args", " ".join([existing] + args).strip())
    print(
        "arduino_component: the core gets %s and %s"
        % (", ".join("-D" + d for d in defines) or "no extra define", pins or "its own pins_arduino.h")
    )

    lib_dirs = [d.replace("\\", "/") for d in project_lib_include_dirs(env)]
    src_flags = env.get("SRC_BUILD_FLAGS") or []
    if isinstance(src_flags, str):
        src_flags = [src_flags]
    # The path is quoted: PlatformIO hands build_src_flags to SCons's ParseFlags, which splits
    # them with shlex and files anything that does not start with a dash under LIBS, and a library
    # directory can have spaces in it ("Adafruit AHTX0 Library"). shlex strips the quotes and
    # keeps the flag one token; SCons quotes a word with a space in it again when it writes the
    # compiler's command line, on the command line itself and in a response file alike.
    env.Replace(SRC_BUILD_FLAGS=list(src_flags) + ['--include-directory="%s"' % d for d in lib_dirs])
    print("arduino_component: src/ searches %d library include directories before the ESP-IDF's" % len(lib_dirs))

    defaults = os.path.join(project_dir, "sdkconfig.defaults")
    generated = os.path.join(project_dir, "sdkconfig.%s" % env.subst("$PIOENV"))
    if os.path.isfile(generated) and os.path.isfile(defaults):
        if os.path.getmtime(defaults) > os.path.getmtime(generated):
            print("arduino_component: sdkconfig.defaults is newer than %s - regenerating" % os.path.basename(generated))
            os.remove(generated)
