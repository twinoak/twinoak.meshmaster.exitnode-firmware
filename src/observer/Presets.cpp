#include "Presets.h"

#include <cstdio>
#include <cstring>

namespace mm {
namespace observer {

const Preset kPresets[kPresetCount] = {
    //  name          url                                     audience               auth       life  retain keepalive
    {"meshview", "wss://mqtt.meshview.dk:443/mqtt", "mqtt.meshview.dk", Auth::Jwt, 0, false, 0},
    {"meshmapper", "wss://mqtt.meshmapper.net:443/mqtt", "mqtt.meshmapper.net", Auth::Jwt, 0, true, 55},
};

const Preset* findPreset(const char* name) {
  if (name == nullptr || name[0] == '\0') return nullptr;
  for (const Preset& p : kPresets) {
    if (std::strcmp(p.name, name) == 0) return &p;
  }
  return nullptr;
}

bool isPresetName(const char* name) {
  if (name == nullptr) return false;
  return findPreset(name) != nullptr || std::strcmp(name, kPresetCustom) == 0 || std::strcmp(name, kPresetNone) == 0;
}

void describePresets(void (*line)(void* ctx, const char* text), void* ctx) {
  char buf[160];
  for (const Preset& p : kPresets) {
    std::snprintf(buf, sizeof buf, "  %-11s %s  token for %s%s%s", p.name, p.serverUrl, p.audience,
                  p.allowRetain ? ", status retained" : "", p.keepalive != 0 ? ", keepalive " : "");
    if (p.keepalive != 0) {
      const size_t n = std::strlen(buf);
      std::snprintf(buf + n, sizeof buf - n, "%u s", static_cast<unsigned>(p.keepalive));
    }
    line(ctx, buf);
  }
  line(ctx, "  custom      set mqttN.server wss://host:port/path, then mqttN.audience for a token or");
  line(ctx, "              mqttN.username and mqttN.password; mqttN.topic for a template other than");
  line(ctx, "              meshcore/{iata}/{device}/{type}");
  line(ctx, "  none        the slot is off");
}

}  // namespace observer
}  // namespace mm
