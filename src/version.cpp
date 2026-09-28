// The one thing src/ holds before F-01: the version this build reports.
//
// .github/workflows/release.yml bakes the tag into every board build as
// -D MESHMASTER_AGENT_VERSION="X.Y.Z+<sha7>"; a local build says 0.0.0-dev. F-01's `status`
// command and the heartbeat's agentVersion read this string and nothing else, so there is one
// place where the version comes from.
//
// Until something calls it the linker drops the string from the image - nothing references it yet,
// which is why `grep` finds the version in version.cpp.o and not in firmware.bin. F-01 is where
// that changes.

#ifndef MESHMASTER_AGENT_VERSION
#define MESHMASTER_AGENT_VERSION "0.0.0-dev"
#endif

extern "C" const char* meshmaster_agent_version(void) {
  return MESHMASTER_AGENT_VERSION;
}
