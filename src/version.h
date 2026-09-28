// The one place the version comes from (src/version.cpp).

#pragma once

extern "C" const char* meshmaster_agent_version(void);

namespace mm {

// "meshmaster-agent <version> on MeshCore <tag>" - what `status` prints and, from task 02, what
// the heartbeat's companionFirmware and agentVersion are cut from.
const char* firmwareString();

// The MeshCore commit the build is pinned at (platformio.ini's [meshcore].pin), full length.
const char* meshcorePin();

// The MeshCore release the pin names ("companion-v1.17.1"): what the heartbeat sends as
// `companionFirmware`, because on this board the companion firmware is the one it is built from.
const char* meshcoreTag();

// The board this image is for, as the heartbeat's `board` field names it ("heltec_v3").
const char* boardName();

}  // namespace mm
