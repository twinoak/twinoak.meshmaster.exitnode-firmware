// The observer dialect: the two messages an observer publishes, byte for byte as the observer
// firmware writes them - agessaman/MeshCore, branch observer-firmware, at d664d89df66bdacc1319900a2e2d152fa31b9dc9
// (24 October 2026), the firmware observer.gessaman.com flashes and the one meshview.dk and
// meshmapper.net already take. Every field below names the file and line it was read from in
// that tree; a field that is a string there is a string here, a field it leaves out is left out
// here, and the order is the order its ArduinoJson document was filled in, because that is the
// order ArduinoJson serialises.
//
// The packet message (src/helpers/MQTTPayloadBuilder.cpp:259-345, buildPacketMessage; the values
// it is given are from MQTTMessageBuilder.cpp:496-571, buildPacketJSONFromRaw):
//
//   timestamp    "%Y-%m-%dT%H:%M:%S.%06u+00:00" of the wall clock, UTC   MQTTMessageBuilder.cpp:288-307
//   hash         the packet hash as 16 uppercase hex                     MQTTMessageBuilder.cpp:543-547, bytesToHex :606-622
//   origin       the node's name (mqtt.origin, else the node name)       MQTTBridge.cpp:38-47
//   type         "PACKET"                                                 MQTTPayloadBuilder.cpp:300
//   direction    "rx"                                                     MQTTMessageBuilder.cpp:556 (only rx here: an exit node forwards nothing)
//   time         "%H:%M:%S" of the same clock read, UTC                   MQTTMessageBuilder.cpp:521-532
//   date         "%d/%m/%Y" of the same clock read, UTC                   same
//   len          the radio frame's length, as a string                    MQTTMessageBuilder.cpp:558; the string: MQTTPayloadBuilder.cpp:293,304
//   packet_type  the payload type, 0..15, as a string                     MQTTMessageBuilder.cpp:540; :294,305
//   route        "D" for a direct route, "F" for everything else          MQTTMessageBuilder.cpp:541, getRouteTypeString :597-604
//   payload_len  the payload's length, as a string                        MQTTMessageBuilder.cpp:560; :295,307
//   raw          the whole frame as uppercase hex                         MQTTMessageBuilder.cpp:534-537
//   origin_id    the node's public key, 64 uppercase hex                  examples/simple_repeater/MyMesh.cpp:1222-1226
//   SNR          "%.1f" of the radio's SNR; left out when unknown         MQTTPayloadBuilder.cpp:311-317
//   RSSI         "%d" of the radio's RSSI, truncated; left out when unknown   :318-321 (the float arrives through an int parameter)
//   score        "%d" of (int)(score * 1000); left out when unknown       :322-325
//   path         on a direct route with hops: one lowercase hex token     :328-342
//                per hop, hashSize bytes each (at most four)
//
// The status message (MQTTPayloadBuilder.cpp:177-257, buildStatusMessage; the values from
// MQTTBridge.cpp:4035-4106, publishStatus):
//
//   status            "online" (the firmware at this commit writes nothing else: no will is set)   MQTTBridge.cpp:4100
//   timestamp         as above                                                                       MQTTBridge.cpp:4056-4059
//   origin, origin_id as above                                                                       :4040, :4063
//   model             the board's name, MainBoard::getManufacturerName()                             examples/simple_repeater/MyMesh.cpp:1232
//   firmware_version  the firmware's version string                                                  :1229
//   radio             "%.6f,%.1f,%d,%d" of freq (MHz, a float), bw (kHz, a float), sf, cr            MQTTBridge.cpp:4061-4062
//   client_version    "meshcore/" + firmware_version                                                 MQTTBridge.cpp:5117-5123
//   repeat            "on" | "off"; left out when null. Always "off" here                            MQTTPayloadBuilder.cpp:214-216; MQTTBridge.cpp:4104
//   stats             left out entirely when nothing below is known                                  MQTTPayloadBuilder.cpp:218-254
//     battery_mv, uptime_secs, packets_sent, packets_received, errors, queue_len (each when >= 0),
//     noise_floor (when > -999), tx_air_secs, rx_air_secs, recv_errors, internal_heap, heap_largest,
//     mqtt_slots_up, mqtt_slots_total (each when >= 0), mqtt_outage_secs (when >= 0),
//     mqtt_connect_failures, mqtt_slots_breaker (each when > 0) - integers, in this order
//
// How the two computed numbers are made, read at the MeshCore pin this firmware builds against
// (d9296435, companion-v1.17.1), which is also what the observer branch does:
//
//   hash    SHA-256 over the payload type byte, then - for a TRACE only - the two bytes of
//           path_len (a uint16_t, Packet.h:47), then the payload; the first eight bytes of the
//           digest. mesh::Packet::calculatePacketHash, src/Packet.cpp:41-50. On the board the
//           Packet computes it (AgentMesh::logRx); here it arrives as bytes.
//   score   RadioLibWrapper::packetScoreInt(snr, sf, len), src/helpers/radiolib/RadioLibWrappers.cpp:221-239:
//           0 below the spreading factor's SNR threshold, else min(1, (snr - threshold) / 10 *
//           (1 - len / 256)). The Dispatcher computes it for every received frame
//           (src/Dispatcher.cpp:207) and hands it to logRx; packetScore() below is the same
//           arithmetic for the host, so a fixture can be held to it.
//
// The JSON is written with the library's JsonWriter rather than ArduinoJson. For every string
// these messages can carry - hex, a hostname, a version, a name of printable ASCII (the keys
// refuse anything else) - the two write the same bytes: ArduinoJson escapes the quote, the
// backslash and the five short controls (src/ArduinoJson/Json/EscapeSequence.hpp) and the
// writer does the same, and differs only on the control characters and non-ASCII the settings
// never admit. test/test_observer holds every message to a fixture written from the observer
// firmware's source by hand.
//
// Host-portable: no Arduino, no MeshCore, no network.

#pragma once

#include <cstddef>
#include <cstdint>

#include "Frame.h"

namespace mm {
namespace observer {

// MQTTPayloadBuilder.h:42: the RSSI value that means "no measurement"; SNR and score use NaN.
constexpr int kRssiUnknown = -32768;

// The message buffer a caller needs: the frame as hex (2 * 255), the path as tokens (64 hops of
// "xx", quotes and commas), the fields - comfortably under this.
constexpr size_t kPacketMessageCap = 1280;
constexpr size_t kStatusMessageCap = 768;
constexpr size_t kTimestampCap = 40;

struct PacketFields {
  const char* origin = "";
  const char* originId = "";   // the node's public key as 64 uppercase hex
  uint32_t atEpoch = 0;        // the wall clock, UTC seconds
  uint32_t atMicros = 0;       // the same read's microseconds, 0..999999 (clamped)
  const uint8_t* raw = nullptr;
  size_t rawLen = 0;
  const uint8_t* hash = nullptr;  // kHashSize bytes, or nullptr for an all-zero hash
  float snr = 0;               // NaN to leave SNR out
  float rssi = 0;              // kRssiUnknown to leave RSSI out
  float score = 0;             // NaN to leave score out
};

// The packet message into `out`: its length, or 0 when the frame does not parse or the buffer is
// too small - and then `out` is "" rather than half a message (MQTTPayloadBuilder.cpp:161-173).
size_t formatPacket(const PacketFields& f, char* out, size_t cap);

struct StatusFields {
  const char* status = "online";
  const char* origin = "";
  const char* originId = "";
  const char* model = "";
  const char* firmwareVersion = "";
  const char* radio = "";          // formatRadio()
  const char* clientVersion = "";  // formatClientVersion()
  uint32_t atEpoch = 0;
  uint32_t atMicros = 0;
  const char* repeat = "off";      // nullptr leaves the field out
  // The stats, each with the observer firmware's own "unknown" sentinel (MQTTPayloadBuilder.h:58-70).
  int batteryMv = -1;
  int uptimeSecs = -1;
  int packetsSent = -1;
  int packetsReceived = -1;
  int errors = -1;
  int queueLen = -1;
  int noiseFloor = -999;
  int txAirSecs = -1;
  int rxAirSecs = -1;
  int recvErrors = -1;
  int internalHeap = -1;
  int heapLargest = -1;
  int mqttSlotsUp = -1;
  int mqttSlotsTotal = -1;
  int mqttOutageSecs = -1;
  int mqttConnectFailures = -1;
  int mqttSlotsBreaker = -1;
};

size_t formatStatus(const StatusFields& f, char* out, size_t cap);

// "%Y-%m-%dT%H:%M:%S.%06u+00:00" of a UTC clock read (MQTTMessageBuilder.cpp:288-307).
void formatTimestamp(uint32_t epoch, uint32_t micros, char* out, size_t cap);

// The status message's radio field: "%.6f,%.1f,%d,%d" (MQTTBridge.cpp:4061-4062). The two
// floats are floats there (NodePrefs::freq and ::bw, src/helpers/CommonCLI.h:30,44 at the pin),
// so 869.618 is written as a float writes it: 869.617981.
void formatRadio(float freqMHz, float bwKHz, int sf, int cr, char* out, size_t cap);

// "meshcore/<firmware_version>" (MQTTBridge.cpp:5117-5123).
void formatClientVersion(const char* firmwareVersion, char* out, size_t cap);

// RadioLibWrapper::packetScoreInt at the pin, for the host: the rebroadcast score in 0..1.
float packetScore(float snr, int sf, int packetLen);

// Uppercase hex of `len` bytes into `out`; "" when it does not fit (bytesToHex :606-622).
void hexUpper(const uint8_t* bytes, size_t len, char* out, size_t cap);

}  // namespace observer
}  // namespace mm
