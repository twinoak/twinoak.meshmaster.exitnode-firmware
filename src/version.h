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

// The host harness runs this firmware's own agent loop with no board at all (docs/host.md), and
// `--board` is how a run says which board it is standing in for - so the exit node's page says
// *onboard . Heltec V3* for the board that is in the post. Nothing on a board calls this: there the
// compile-time MESHMASTER_BOARD stands, and `nullptr` or "" puts it back.
void setBoardName(const char* name);

}  // namespace mm
