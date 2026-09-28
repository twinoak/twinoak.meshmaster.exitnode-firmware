# The merged image for a fresh board: `pio run -e <env> -t mergebin`.
#
# A board out of the box has no bootloader and no partition table, so the application image
# alone cannot be flashed at 0x0 - the three (or four) pieces have to be merged first. What goes
# where is the board definition's business, not ours: PlatformIO already knows it as
# FLASH_EXTRA_IMAGES (bootloader, partitions, boot_app0 ...) and ESP32_APP_OFFSET, so the offsets
# are never written down here and a new board needs no change. The approach is MeshCore's own
# merge-bin.py, which a consumer of the package cannot reach (extra_scripts are resolved before
# the dependency is cloned).
#
# .github/workflows/release.yml calls this for every board and publishes the result beside the
# application image; MERGED_BIN_PATH names the output file.
#
#   esptool.py --chip esp32s3 write_flash 0x0 <...>-merged.bin        <- what the notes print

import os

Import("env")  # noqa: F821  (SCons injects it)

board = env.BoardConfig()
firmware_bin = "${BUILD_DIR}/${PROGNAME}.bin"
merged_bin = os.environ.get("MERGED_BIN_PATH", "${BUILD_DIR}/${PROGNAME}-merged.bin")


def merge_bin_action(source, target, env):  # noqa: ARG001  (SCons builder signature)
    images = [
        *env.Flatten(env.get("FLASH_EXTRA_IMAGES", [])),
        "$ESP32_APP_OFFSET",
        source[0].get_abspath(),
    ]
    env.Execute(
        " ".join(
            [
                '"$PYTHONEXE"',
                '"$OBJCOPY"',
                "--chip",
                board.get("build.mcu", "esp32"),
                "merge_bin",
                "-o",
                merged_bin,
                "--flash_mode",
                board.get("build.flash_mode", "dio"),
                "--flash_freq",
                "${__get_board_f_flash(__env__)}",
                "--flash_size",
                board.get("upload.flash_size", "4MB"),
                *images,
            ]
        )
    )


env.AddCustomTarget(
    name="mergebin",
    dependencies=firmware_bin,
    actions=merge_bin_action,
    title="Merged image",
    description="bootloader + partitions + application, one file flashed at 0x0",
    always_build=True,
)
