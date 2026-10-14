#include "DisplayLines.h"

#include <cstdio>
#include <cstring>

#include <agent/Iso8601.h>

namespace mm {

namespace {

// One line, written into kDisplayCols bytes and truncated at kDisplayWidth characters - the
// font's 21. snprintf truncates on its own; this is here so that every line goes through one
// place and the "no line is ever wider than the font" test has one thing to hold.
void put(char* line, const char* text) {
  std::snprintf(line, kDisplayCols, "%s", text);
}

// `left` at the margin, `right` against the far edge, the gap between them spaces. Written for
// the two halves of line one, which together are never wider than the line.
void putEnds(char* line, const char* left, const char* right) {
  const size_t l = std::strlen(left);
  const size_t r = std::strlen(right);
  if (l + r >= static_cast<size_t>(kDisplayWidth)) {
    // Nothing the wording below can produce, so the fallback is only to stay inside the line.
    std::snprintf(line, kDisplayCols, "%s %s", left, right);
    return;
  }
  const size_t gap = static_cast<size_t>(kDisplayWidth) - l - r;
  std::memcpy(line, left, l);
  std::memset(line + l, ' ', gap);
  std::memcpy(line + l + gap, right, r);
  line[kDisplayWidth] = '\0';
}

char upperHex(char c) {
  if (c >= 'a' && c <= 'f') return static_cast<char>(c - 'a' + 'A');
  return c;
}

}  // namespace

DisplayFault faultOf(const DisplayState& state) {
  if (state.tokenRefused) return DisplayFault::TokenRefused;
  if (!state.wifiConnected) return DisplayFault::NoWifi;
  if (!state.serverConfigured) return DisplayFault::NoToken;
  if (state.backingOff) return DisplayFault::Backoff;
  return DisplayFault::None;
}

void renderLines(const DisplayState& state, char lines[kDisplayLines][kDisplayCols]) {
  std::memset(lines, 0, static_cast<size_t>(kDisplayLines) * kDisplayCols);
  char buf[64];
  char when[12];

  // 1. Who this is, and what time it is. The four hex characters are the prefix meshmaster.dk
  // shows an exit node by; the clock is against the right edge so that the eye finds it in the
  // same place whichever of the two it is.
  if (state.publicKeyHex == nullptr || state.publicKeyHex[0] == '\0') {
    put(lines[0], "no identity");
  } else {
    char id[9] = "ID: ";
    for (int i = 0; i < 4 && state.publicKeyHex[i] != '\0'; i++) id[4 + i] = upperHex(state.publicKeyHex[i]);
    if (state.hasTime) {
      formatClock(state.nowEpoch, when, sizeof when);
      putEnds(lines[0], id, when);
    } else {
      putEnds(lines[0], id, "no time");
    }
  }

  // 2. The link and the server, in the two words a person at the window is asking for. Exactly
  // 21 characters with both answers *yes*, which is why there is no comma between them.
  std::snprintf(buf, sizeof buf, "WiFi: %s Online: %s", state.wifiConnected ? "yes" : "no",
                state.online ? "yes" : "no");
  put(lines[1], buf);

  // 3. When meshmaster.dk last heard from this board. (*Last heartbeat:* would be 24.)
  if (state.everHeartbeated) {
    formatClock(state.lastHeartbeatAtEpoch, when, sizeof when);
    std::snprintf(buf, sizeof buf, "Heartbeat: %s", when);
    put(lines[2], buf);
  } else {
    put(lines[2], "Heartbeat: none");
  }

  // 4. What it is doing - a job while one is open, which is live because the job's own pump
  // refreshes the display - and when it is doing nothing, whether that is because there is
  // nothing to do or because something is wrong.
  if (state.jobOpen) {
    std::snprintf(buf, sizeof buf, "Job %lld %s", state.jobId, state.jobType != nullptr ? state.jobType : "");
    put(lines[3], buf);
    return;
  }
  switch (faultOf(state)) {
    case DisplayFault::TokenRefused:
      put(lines[3], "token revoked/wrong");
      return;
    case DisplayFault::NoWifi:
      put(lines[3], "no wifi");
      return;
    case DisplayFault::NoToken:
      // WiFi is up and there is nothing to call the server with: the state the first merged
      // flash leaves, which used to read `Idle, no jobs yet` under `Online: no` (task p1.3-12).
      put(lines[3], "no token: set it");
      return;
    case DisplayFault::Backoff: {
      // Three digits at most: the prefix is 17 characters of the font's 21. The backoff's own
      // ceiling is ten minutes, and a Retry-After longer than 999 s reads as 999.
      const uint32_t left = state.retrySeconds > 999 ? 999 : state.retrySeconds;
      std::snprintf(buf, sizeof buf, "no server: retry %lus", static_cast<unsigned long>(left));
      put(lines[3], buf);
      return;
    }
    case DisplayFault::None:
    default:
      break;
  }
  if (state.hasLastJob) {
    formatClock(state.lastJobAtEpoch, when, sizeof when);
    std::snprintf(buf, sizeof buf, "Idle, last %s", when);
    put(lines[3], buf);
  } else {
    put(lines[3], "Idle, no jobs yet");
  }
}

}  // namespace mm
