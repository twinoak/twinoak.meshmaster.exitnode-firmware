# The frame rail, in the build: `pio run -e <env>` says no when a function of ours puts more than
# 1 024 bytes on the stack (task p1.17-65).
#
# `-fstack-usage` (platformio.ini's [frames]) writes a .su file beside every object; this reads
# them once the image is linked and fails the build when one of ours is over the limit, or when the
# compiler could not bound one at all. tools/check-frames.py is the rail itself and has the limit,
# the named exceptions and its own self-test; this file is only how the build calls it, so that a
# board that would restart for ever is a red build on this machine and not a discovery at a window.
#
# It runs after the link rather than at every compile because the limit is a file a person reads
# with a reason beside each exception, which a compiler flag cannot be.

import os

Import("env")  # noqa: F821  (SCons injects it)

RAIL = os.path.join("$PROJECT_DIR", "tools", "check-frames.py")


def check_frames_action(source, target, env):  # noqa: ARG001  (SCons builder signature)
    # A non-zero return from a function action is a failed build, which is the point.
    return env.Execute(' '.join(['"$PYTHONEXE"', f'"{RAIL}"', '--build-dir', '"$BUILD_DIR"', '--top', '20']))


# `env.Action` and not PlatformIO's `VerboseAction`: that one hands SCons the bare function
# under `pio run -v`, and SCons then has no command string to print and dies in its own
# print_cmd_line. A named action prints the same line whether the build is verbose or not.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", env.Action(check_frames_action, "Frames: $BUILD_DIR"))
