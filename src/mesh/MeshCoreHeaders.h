// Every MeshCore header this firmware uses, included from one place, as system headers.
//
// src/ is built with -Wall -Wextra -Werror (platformio.ini, build_src_flags) and MeshCore's
// headers are not written to that standard - a member initialised out of order, an unused
// parameter of an inline virtual, a C++17 inline variable under gnu++11 - and they are not ours
// to fix. `#pragma GCC system_header` makes the rest of this file a system header, and GCC
// treats what a system header includes as system headers too, so their warnings are not
// reported and -Werror stays about our own code. (An -isystem flag would do the same, but
// PlatformIO already passes MeshCore's directory with -I, and GCC then moves it behind every
// other -I - behind the ESP-IDF tree, whose bt/esp_ble_mesh/mesh_core/mesh.h shadows Mesh.h on
// a case-insensitive file system.)
//
// The one thing this does not silence is a `#warning` directive (GCC reports those from system
// headers too): RadioLib's "God mode active" is one, and -Wno-error=cpp in build_src_flags keeps
// it a warning.

#pragma once

#pragma GCC system_header

#include <Mesh.h>
#include <MeshCore.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/MomentaryButton.h>
#include <target.h>
