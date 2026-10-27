#include "Topics.h"

#include <cstring>

namespace mm {
namespace observer {

const char* typeName(MessageType type) { return type == MessageType::Status ? "status" : "packets"; }

bool iataUsable(const char* iata) {
  // MQTTObserverValidation.h:15-24 (three ASCII letters or digits) and MQTTTopicRouter.h:68 (not "XXX").
  if (iata == nullptr || std::strlen(iata) != 3) return false;
  for (int i = 0; i < 3; i++) {
    const char c = iata[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
  }
  return std::strcmp(iata, "XXX") != 0;
}

namespace {

// MQTTTopicTemplate.h:14-56, mqttSubstituteTopic.
bool substitute(const char* tmpl, const char* iata, const char* device, const char* type, char* out, size_t cap) {
  if (iata == nullptr) iata = "";
  if (device == nullptr) device = "";
  size_t o = 0;
  const char* p = tmpl;
  while (*p && o < cap - 1) {
    const char* sub = nullptr;
    size_t adv = 0;
    if (std::strncmp(p, "{iata}", 6) == 0) {
      sub = iata;
      adv = 6;
    } else if (std::strncmp(p, "{device}", 8) == 0) {
      sub = device;
      adv = 8;
    } else if (std::strncmp(p, "{type}", 6) == 0) {
      sub = type;
      adv = 6;
    }
    if (sub != nullptr) {
      const size_t n = std::strlen(sub);
      if (o + n >= cap) {
        out[o] = '\0';
        return false;
      }
      std::memcpy(out + o, sub, n);
      o += n;
      p += adv;
    } else {
      out[o++] = *p++;
    }
  }
  out[o] = '\0';
  if (*p) return false;  // the template was longer than the buffer: no topic, not a cut one
  return o > 0;
}

}  // namespace

bool buildTopic(const char* tmpl, const char* iata, const char* device, MessageType type, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  if (tmpl == nullptr || tmpl[0] == '\0') {
    // MQTTTopicRouter.h:67-71: the meshcore layout needs a usable code and a device.
    if (!iataUsable(iata) || device == nullptr || device[0] == '\0') return false;
    return substitute(kDefaultTopic, iata, device, typeName(type), out, cap);
  }
  return substitute(tmpl, iata, device, typeName(type), out, cap);
}

}  // namespace observer
}  // namespace mm
