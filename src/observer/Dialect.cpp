#include "Dialect.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <agent/Iso8601.h>
#include <agent/JsonWriter.h>

namespace mm {
namespace observer {

namespace {

const char kHexDigits[] = "0123456789ABCDEF";

// The observer firmware's own fallback when gmtime() fails (MQTTMessageBuilder.cpp:305).
const char kFallbackTimestamp[] = "2024-01-01T12:00:00.000000+00:00";

// A finished message, or nothing: serializeComplete (MQTTPayloadBuilder.cpp:161-173) clears the
// buffer when the document did not fit, so a caller can never publish half a message.
size_t finish(const JsonWriter& w, char* out, size_t cap) {
  if (cap == 0) return 0;
  if (w.overflowed()) {
    out[0] = '\0';
    return 0;
  }
  return w.length();
}

void numberAsString(JsonWriter& w, const char* key, long value) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%ld", value);
  w.key(key);
  w.string(buf);
}

void statIfKnown(JsonWriter& w, const char* key, int value, int unknownBelow) {
  if (value < unknownBelow) return;
  w.key(key);
  w.integer(value);
}

}  // namespace

void hexUpper(const uint8_t* bytes, size_t len, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (bytes == nullptr || cap < len * 2 + 1) return;
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = kHexDigits[bytes[i] >> 4];
    out[i * 2 + 1] = kHexDigits[bytes[i] & 0x0F];
  }
  out[len * 2] = '\0';
}

void formatTimestamp(uint32_t epoch, uint32_t micros, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  if (micros > 999999) micros = 999999;  // MQTTMessageBuilder.cpp:296-297: the field is six digits
  // The observer firmware's strftime("%Y-%m-%dT%H:%M:%S") of gmtime(); here the library's own
  // UTC arithmetic (agent/Iso8601.h, "YYYY-MM-DDTHH:MM:SSZ") with the 'Z' replaced by the
  // fraction and the explicit offset - the same characters, without newlib's strftime.
  char utc[24];
  formatUtc(epoch, utc, sizeof utc);
  if (std::strlen(utc) != 20) {
    std::snprintf(out, cap, "%s", kFallbackTimestamp);
    return;
  }
  utc[19] = '\0';
  std::snprintf(out, cap, "%s.%06lu+00:00", utc, static_cast<unsigned long>(micros));
}

void formatRadio(float freqMHz, float bwKHz, int sf, int cr, char* out, size_t cap) {
  std::snprintf(out, cap, "%.6f,%.1f,%d,%d", static_cast<double>(freqMHz), static_cast<double>(bwKHz), sf, cr);
}

void formatClientVersion(const char* firmwareVersion, char* out, size_t cap) {
  std::snprintf(out, cap, "meshcore/%s", firmwareVersion != nullptr ? firmwareVersion : "");
}

float packetScore(float snr, int sf, int packetLen) {
  // RadioLibWrappers.cpp:221-228, "based on Semtech datasheets": the SNR below which a frame at
  // this spreading factor has no chance. SF 12 is the table's last row and MeshCore's own limit
  // (radio.sf is 5..12), so a factor past it is answered as one below 7 is.
  static const float kSnrThreshold[] = {-7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f};
  if (sf < 7 || sf > 12) return 0.0f;
  const float threshold = kSnrThreshold[sf - 7];
  if (snr < threshold) return 0.0f;
  // :235-238, in the source's own types: the difference in float, then double arithmetic, then
  // the double clamped to 0..1 and returned as a float.
  const float diff = snr - threshold;
  const double successRateBasedOnSnr = diff / 10.0;
  const double collisionPenalty = 1 - (packetLen / 256.0);
  double score = successRateBasedOnSnr * collisionPenalty;
  if (score > 1.0) score = 1.0;
  if (score < 0.0) score = 0.0;
  return static_cast<float>(score);
}

size_t formatPacket(const PacketFields& f, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  Frame frame;
  if (!parseFrame(f.raw, f.rawLen, frame)) return 0;

  char timestamp[kTimestampCap];
  char clock[16];
  char date[16];
  formatTimestamp(f.atEpoch, f.atMicros, timestamp, sizeof timestamp);
  {
    // "%H:%M:%S" and "%d/%m/%Y" of the same UTC instant (MQTTMessageBuilder.cpp:521-532), cut
    // from the library's "YYYY-MM-DDTHH:MM:SSZ".
    char utc[24];
    formatUtc(f.atEpoch, utc, sizeof utc);
    if (std::strlen(utc) == 20) {
      std::snprintf(clock, sizeof clock, "%.8s", utc + 11);
      std::snprintf(date, sizeof date, "%.2s/%.2s/%.4s", utc + 8, utc + 5, utc);
    } else {
      std::snprintf(clock, sizeof clock, "12:00:00");  // MQTTMessageBuilder.cpp:529-531
      std::snprintf(date, sizeof date, "01/01/2024");
    }
  }

  char hashHex[2 * kHashSize + 1];
  const uint8_t zero[kHashSize] = {0};
  hexUpper(f.hash != nullptr ? f.hash : zero, kHashSize, hashHex, sizeof hashHex);

  char rawHex[2 * kMaxFrame + 1];
  hexUpper(f.raw, f.rawLen, rawHex, sizeof rawHex);

  JsonWriter w(out, cap);
  w.beginObject();
  w.key("timestamp");
  w.string(timestamp);
  w.key("hash");
  w.string(hashHex);
  w.key("origin");
  w.string(f.origin);
  w.key("type");
  w.string("PACKET");
  w.key("direction");
  w.string("rx");
  w.key("time");
  w.string(clock);
  w.key("date");
  w.string(date);
  numberAsString(w, "len", static_cast<long>(f.rawLen));
  numberAsString(w, "packet_type", frame.payloadType());
  w.key("route");
  w.string(frame.routeDirect() ? "D" : "F");
  numberAsString(w, "payload_len", frame.payloadLen);
  w.key("raw");
  w.string(rawHex);
  w.key("origin_id");
  w.string(f.originId);

  // MQTTPayloadBuilder.cpp:311-326: the three radio numbers, each left out when unknown.
  if (!std::isnan(f.snr)) {
    char snr[16];
    std::snprintf(snr, sizeof snr, "%.1f", static_cast<double>(f.snr));
    w.key("SNR");
    w.string(snr);
  }
  if (!(f.rssi == static_cast<float>(kRssiUnknown))) {
    numberAsString(w, "RSSI", static_cast<long>(static_cast<int>(f.rssi)));
  }
  if (!std::isnan(f.score)) {
    numberAsString(w, "score", static_cast<long>(static_cast<int>(f.score * 1000)));
  }

  // :328-342 with has_path from MQTTMessageBuilder.cpp:551: a direct route with at least one hop.
  if (frame.routeDirect() && frame.hopCount() > 0) {
    w.key("path");
    w.beginArray();
    const size_t hashSize = frame.hashSize();
    const size_t hops = frame.hopCount();
    char hop[2 * 4 + 1];
    for (size_t i = 0; i < hops; i++) {
      size_t pos = 0;
      for (size_t b = 0; b < hashSize && b < 4; b++) {
        const size_t idx = i * hashSize + b;
        if (idx >= kMaxPath) break;
        std::snprintf(hop + pos, 3, "%02x", frame.path[idx]);
        pos += 2;
      }
      hop[pos] = '\0';
      w.comma();
      w.string(hop);
    }
    w.endArray();
  }
  w.endObject();
  return finish(w, out, cap);
}

size_t formatStatus(const StatusFields& f, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  char timestamp[kTimestampCap];
  formatTimestamp(f.atEpoch, f.atMicros, timestamp, sizeof timestamp);

  JsonWriter w(out, cap);
  w.beginObject();
  w.key("status");
  w.string(f.status);
  w.key("timestamp");
  w.string(timestamp);
  w.key("origin");
  w.string(f.origin);
  w.key("origin_id");
  w.string(f.originId);
  w.key("model");
  w.string(f.model);
  w.key("firmware_version");
  w.string(f.firmwareVersion);
  w.key("radio");
  w.string(f.radio);
  w.key("client_version");
  w.string(f.clientVersion);
  if (f.repeat != nullptr) {
    w.key("repeat");
    w.string(f.repeat);
  }

  // MQTTPayloadBuilder.cpp:218-224: the object exists when any one of its fields is known.
  const bool connHealth = f.mqttSlotsUp >= 0 || f.mqttSlotsTotal >= 0 || f.mqttOutageSecs >= 0 || f.heapLargest >= 0 ||
                          f.mqttConnectFailures > 0 || f.mqttSlotsBreaker > 0;
  const bool anyStat = f.batteryMv >= 0 || f.uptimeSecs >= 0 || f.errors >= 0 || f.queueLen >= 0 || f.noiseFloor > -999 ||
                       f.txAirSecs >= 0 || f.rxAirSecs >= 0 || f.recvErrors >= 0 || f.internalHeap >= 0 ||
                       f.packetsSent >= 0 || f.packetsReceived >= 0 || connHealth;
  if (anyStat) {
    w.key("stats");
    w.beginObject();
    statIfKnown(w, "battery_mv", f.batteryMv, 0);
    statIfKnown(w, "uptime_secs", f.uptimeSecs, 0);
    statIfKnown(w, "packets_sent", f.packetsSent, 0);
    statIfKnown(w, "packets_received", f.packetsReceived, 0);
    statIfKnown(w, "errors", f.errors, 0);
    statIfKnown(w, "queue_len", f.queueLen, 0);
    statIfKnown(w, "noise_floor", f.noiseFloor, -998);
    statIfKnown(w, "tx_air_secs", f.txAirSecs, 0);
    statIfKnown(w, "rx_air_secs", f.rxAirSecs, 0);
    statIfKnown(w, "recv_errors", f.recvErrors, 0);
    statIfKnown(w, "internal_heap", f.internalHeap, 0);
    statIfKnown(w, "heap_largest", f.heapLargest, 0);
    statIfKnown(w, "mqtt_slots_up", f.mqttSlotsUp, 0);
    statIfKnown(w, "mqtt_slots_total", f.mqttSlotsTotal, 0);
    statIfKnown(w, "mqtt_outage_secs", f.mqttOutageSecs, 0);
    statIfKnown(w, "mqtt_connect_failures", f.mqttConnectFailures, 1);
    statIfKnown(w, "mqtt_slots_breaker", f.mqttSlotsBreaker, 1);
    w.endObject();
  }
  w.endObject();
  return finish(w, out, cap);
}

}  // namespace observer
}  // namespace mm
