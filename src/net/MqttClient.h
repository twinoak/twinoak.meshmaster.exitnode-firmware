// The board's MQTT client: ESP-IDF's esp-mqtt as the Arduino core at this pin ships it
// (framework-arduinoespressif32 3.20017, ESP-IDF 4.4.7: tools/sdk/esp32s3/include/mqtt/esp-mqtt
// and libmqtt.a, with CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE=y in its sdkconfig), behind the
// Client interface the observer's connection state machine drives (src/observer/Connection.h).
// No library is added: the client, its WebSocket transport and esp-tls are the core's own.
//
// What this file decides and the state machine does not:
//
//   verification   every connection verifies the broker's chain. The CA store is the carried
//                  roots (src/agent/TlsRoots.h) or, when the state machine asks for it after a
//                  chain that ended elsewhere, the ESP32 core's own certificate bundle - the
//                  Mozilla roots - through the same attach call WiFiClientSecure uses. A config
//                  with neither is refused here, because esp-tls would then not verify at all,
//                  and the hostname is checked against the certificate as esp-tls does by
//                  default. test/test_tls_policy greps this file like every other.
//   its own task   esp-mqtt runs each connection in a task of its own (6 KB of stack, the
//                  core's default), so the TLS handshake, the keepalive and the writes never
//                  block the loop task the mesh and the agent run on. A publish here is an
//                  enqueue into esp-mqtt's outbox and returns at once; the outbox is read back
//                  so the state machine can bound it.
//   events         esp-mqtt reports on its own task; they are kept in a small ring under a
//                  spinlock and handed to the state machine one per loop() pass, as a kind and
//                  one short reason - a CONNACK code's meaning, an esp-tls error's name - and
//                  never a byte of what was sent or received.
//   closing        close() destroys the client, which stops its task (esp-mqtt joins it) and
//                  frees the TLS context and the buffers: the ~40 KB a connection holds goes back
//                  to the heap, which is the point when the uplink is short of it. The join can
//                  take as long as the client's current network operation, ten seconds at most.
//
// Board-only. Sizes: the receive buffer is small (this client subscribes to nothing; a CONNACK
// and a PUBACK are a few bytes), the send buffer holds the largest message whole.

#pragma once

#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>

#include "observer/Connection.h"

// esp-mqtt's handle, as mqtt_client.h declares it; the header itself is included by the .cpp only.
typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;

namespace mm {

class MqttClient : public observer::Client {
 public:
  MqttClient();

  bool open(const observer::ClientConfig& cfg, char* reason, size_t cap) override;
  void close() override;
  bool publish(const char* topic, const char* payload, size_t len, uint8_t qos, bool retain) override;
  observer::ClientEvent takeEvent(observer::ErrorKind& kind, char* reason, size_t cap) override;
  size_t outboxBytes() override;

  bool isOpen() const { return client_ != nullptr; }

  // esp-mqtt's callback lands here, on esp-mqtt's task: `event` is an esp_mqtt_event_handle_t.
  void onEvent(void* event);

 private:
  struct Pending {
    observer::ClientEvent event = observer::ClientEvent::None;
    observer::ErrorKind kind = observer::ErrorKind::Other;
    char reason[observer::kReasonCap] = {0};
  };
  static constexpr int kPendingCap = 4;

  void push(const Pending& p);

  esp_mqtt_client_handle_t client_ = nullptr;
  bool connected_ = false;
  portMUX_TYPE mux_;
  Pending pending_[kPendingCap];
  int pendingHead_ = 0;
  int pendingCount_ = 0;
};

}  // namespace mm
