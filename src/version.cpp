// The version this build reports, and the one place it comes from.
//
// .github/workflows/release.yml bakes the tag into every board build as
// -D MESHMASTER_AGENT_VERSION="X.Y.Z+<sha7>"; a local build says 0.0.0-dev. The `status`
// command and, from task 02, the heartbeat's agentVersion read this string and nothing else.
// The MeshCore tag and pin come from platformio.ini's [meshcore] section through two defines,
// so that a bump of the pin moves the firmware string with it.

#include "version.h"

#include <cstdio>

#ifndef MESHMASTER_AGENT_VERSION
#define MESHMASTER_AGENT_VERSION "0.0.0-dev"
#endif

#ifndef MESHMASTER_MESHCORE_TAG
#define MESHMASTER_MESHCORE_TAG "unpinned"
#endif

#ifndef MESHMASTER_MESHCORE_PIN
#define MESHMASTER_MESHCORE_PIN "0000000000000000000000000000000000000000"
#endif

#ifndef MESHMASTER_BOARD
#define MESHMASTER_BOARD "unknown"
#endif

extern "C" const char* meshmaster_agent_version(void) { return MESHMASTER_AGENT_VERSION; }

namespace mm {

namespace {
// Set by the host harness alone, and only from its own argument list.
char s_boardOverride[33] = {0};
}  // namespace

const char* firmwareString() { return "meshmaster-agent " MESHMASTER_AGENT_VERSION " on MeshCore " MESHMASTER_MESHCORE_TAG; }

const char* meshcorePin() { return MESHMASTER_MESHCORE_PIN; }

const char* meshcoreTag() { return MESHMASTER_MESHCORE_TAG; }

const char* boardName() { return s_boardOverride[0] != '\0' ? s_boardOverride : MESHMASTER_BOARD; }

void setBoardName(const char* name) {
  if (name == nullptr) {
    s_boardOverride[0] = '\0';
    return;
  }
  std::snprintf(s_boardOverride, sizeof s_boardOverride, "%s", name);
}

}  // namespace mm
