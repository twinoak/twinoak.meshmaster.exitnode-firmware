# The furthest a session in this repository can go towards building lib/agent-core as an ESP-IDF
# component: a syntax check of its CMakeLists.txt with no ESP-IDF present, and then a check that
# what it registers is what is on disk.
#
#   cmake -P tools/check-idf-component.cmake
#
# `idf_component_register` is the ESP-IDF's own function and does not exist here, so this script
# defines a stub that records what it was given, includes the component's CMakeLists.txt in script
# mode - which is the syntax check: a malformed file fails to parse - and then holds the recorded
# SRCS against `lib/agent-core/src/*.cpp` in both directions. A source added to the library and
# forgotten in the component's list is a failure, and so is a line in the list whose file is gone.
#
# The component is not *compiled* here: no ESP-IDF, no toolchain, no sdkconfig. That first happens
# in the Walter firmware (twinoak.meshcore.walter-firmware, phase 4, task 01), and until then
# this is what CI and a session can say about the file.

cmake_minimum_required(VERSION 3.16)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(LIB_DIR "${REPO_ROOT}/lib/agent-core")

if(NOT EXISTS "${LIB_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "lib/agent-core/CMakeLists.txt is not there")
endif()

# The stub: the ESP-IDF's signature, as much of it as this component uses.
function(idf_component_register)
  cmake_parse_arguments(ARG "" "" "SRCS;INCLUDE_DIRS;REQUIRES;PRIV_REQUIRES;PRIV_INCLUDE_DIRS;EMBED_FILES;EMBED_TXTFILES;LDFRAGMENTS" ${ARGN})
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "idf_component_register was given arguments it does not take: ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  set_property(GLOBAL PROPERTY AGENT_CORE_SRCS "${ARG_SRCS}")
  set_property(GLOBAL PROPERTY AGENT_CORE_INCLUDE_DIRS "${ARG_INCLUDE_DIRS}")
  set_property(GLOBAL PROPERTY AGENT_CORE_REGISTERED TRUE)
endfunction()

include("${LIB_DIR}/CMakeLists.txt")

get_property(REGISTERED GLOBAL PROPERTY AGENT_CORE_REGISTERED)
if(NOT REGISTERED)
  message(FATAL_ERROR "lib/agent-core/CMakeLists.txt never called idf_component_register")
endif()

get_property(REGISTERED_SRCS GLOBAL PROPERTY AGENT_CORE_SRCS)
get_property(REGISTERED_INCLUDE_DIRS GLOBAL PROPERTY AGENT_CORE_INCLUDE_DIRS)

if(NOT REGISTERED_INCLUDE_DIRS STREQUAL "include")
  message(FATAL_ERROR "INCLUDE_DIRS is '${REGISTERED_INCLUDE_DIRS}', not 'include'")
endif()
if(NOT IS_DIRECTORY "${LIB_DIR}/include/agent")
  message(FATAL_ERROR "lib/agent-core/include/agent is not there")
endif()

file(GLOB ON_DISK RELATIVE "${LIB_DIR}" "${LIB_DIR}/src/*.cpp")
list(SORT ON_DISK)
set(REGISTERED_SORTED ${REGISTERED_SRCS})
list(SORT REGISTERED_SORTED)

foreach(src IN LISTS ON_DISK)
  if(NOT src IN_LIST REGISTERED_SORTED)
    message(FATAL_ERROR "${src} is in the library and not in CMakeLists.txt's SRCS")
  endif()
endforeach()
foreach(src IN LISTS REGISTERED_SORTED)
  if(NOT EXISTS "${LIB_DIR}/${src}")
    message(FATAL_ERROR "CMakeLists.txt registers ${src}, which is not in the tree")
  endif()
endforeach()

list(LENGTH ON_DISK COUNT)
message(STATUS "lib/agent-core/CMakeLists.txt parses, and registers all ${COUNT} sources of src/ and include/ as the component's")
