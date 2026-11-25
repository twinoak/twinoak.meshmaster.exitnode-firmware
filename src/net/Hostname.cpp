#include "Hostname.h"

#include <stdio.h>

namespace mm {

void hostnameFromMac(const uint8_t mac[6], char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (mac == nullptr) return;
  snprintf(out, cap, "meshmaster-%02x%02x", static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
}

void macToText(const uint8_t mac[6], char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (mac == nullptr) return;
  snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x", static_cast<unsigned>(mac[0]), static_cast<unsigned>(mac[1]),
           static_cast<unsigned>(mac[2]), static_cast<unsigned>(mac[3]), static_cast<unsigned>(mac[4]),
           static_cast<unsigned>(mac[5]));
}

}  // namespace mm
