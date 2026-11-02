# The ESP-IDF project file (task p1.9-36).
#
# Since pile 1.9 the boards are built with `framework = arduino, espidf` (platformio.ini): the
# Arduino core 2.0.17 as a component of an ESP-IDF 4.4.7 built from source, under sdkconfig.defaults,
# so that mbedtls's buffer sizes - compile-time options of the ESP-IDF, precompiled in the core -
# are this repository's to set. PlatformIO runs CMake for the *code model* only (which sources,
# which flags) and compiles everything itself with SCons; nothing here is built by CMake or ninja.
#
# The two files are the ones PlatformIO would write for a project without them (its
# create_default_project_files), written down so that they are read rather than generated:
# this one, and src/CMakeLists.txt, which registers src/ as the main component.
#
# [env:native] does not come here: it has no framework, and lib/agent-core's own CMakeLists.txt is
# the library's ESP-IDF face for a consumer, not a component of this build.
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(meshmaster-agent-firmware)

# The Arduino core is compiled by the component build above, which does not see platformio.ini's
# flags - and in the Arduino-only build it did: the board definition's extra_flags
# (ARDUINO_USB_CDC_ON_BOOT, ARDUINO_USB_MODE, BOARD_HAS_PSRAM on the V4 ...) and the pins_arduino.h
# of MeshCore's variant, which the -I in build_flags put in front of the core's own. Without them
# the core's HWCDC.cpp would not define the V4's `Serial`, and esp32-hal-psram.c would compile to
# a stub that never finds the PSRAM. tools/pio/arduino_component.py reads the same two things from
# the same places and hands them to this file through CMake variables, so that the core is
# compiled with exactly what it was compiled with before.
if(MESHMASTER_CORE_DEFINES OR MESHMASTER_CORE_PINS_DIR)
  idf_component_get_property(meshmaster_arduino_lib framework-arduinoespressif32 COMPONENT_LIB)
  if(MESHMASTER_CORE_DEFINES)
    target_compile_definitions(${meshmaster_arduino_lib} PRIVATE ${MESHMASTER_CORE_DEFINES})
  endif()
  if(MESHMASTER_CORE_PINS_DIR)
    # -iquote: searched before the component's own include directories for a quoted #include,
    # which is how the core includes "pins_arduino.h".
    target_compile_options(${meshmaster_arduino_lib} PRIVATE "-iquote${MESHMASTER_CORE_PINS_DIR}")
  endif()
endif()
