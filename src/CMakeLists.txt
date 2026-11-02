# src/ as the ESP-IDF project's main component (task p1.9-36; the root CMakeLists.txt says why
# there is a CMake file in an Arduino project). PlatformIO reads this component's source list from
# the CMake code model and compiles the files itself, with platformio.ini's build_flags and
# build_src_flags - so -Werror is still on for every file here, and the include paths, MeshCore's
# among them, are still PlatformIO's. CMake compiles nothing, which is why no REQUIRES or
# INCLUDE_DIRS are named: the list is the whole of what this file is for.
file(GLOB_RECURSE meshmaster_sources ${CMAKE_CURRENT_LIST_DIR}/*.cpp ${CMAKE_CURRENT_LIST_DIR}/*.c)
idf_component_register(SRCS ${meshmaster_sources})
