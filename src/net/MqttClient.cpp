#include "MqttClient.h"

#include <cstdio>
#include <cstring>

#include <esp_err.h>
#include <esp_tls_errors.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>

// The ESP32 core's certificate bundle, the same blob src/agent/AgentClient.cpp names: the Mozilla
// roots as libmbedtls.a of the framework carries them.
extern const uint8_t kCoreCertBundle[] asm("_binary_x509_crt_bundle_start");

// The Arduino core's two calls over that bundle (libraries/WiFiClientSecure/src/esp_crt_bundle.h,
// the ones WiFiClientSecure::setCACertBundle and ssl_client.cpp make), declared here rather than
// included: the ESP-IDF ships a header of the same name for its own bundle, and which of the two
// an include finds depends on the include order.
extern "C" {
esp_err_t arduino_esp_crt_bundle_attach(void* conf);
void arduino_esp_crt_bundle_set(const uint8_t* x509_bundle);
}

namespace mm {

namespace {

constexpr int kMbedtlsCertVerifyFailed = -0x2700;  // MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
constexpr int kMbedtlsSslAllocFailed = -0x7F00;    // MBEDTLS_ERR_SSL_ALLOC_FAILED
constexpr int kMbedtlsX509AllocFailed = -0x2880;   // MBEDTLS_ERR_X509_ALLOC_FAILED
constexpr int kMbedtlsPkAllocFailed = -0x3F80;     // MBEDTLS_ERR_PK_ALLOC_FAILED
constexpr int kMbedtlsEcpAllocFailed = -0x4D80;    // MBEDTLS_ERR_ECP_ALLOC_FAILED
constexpr int kMbedtlsMpiAllocFailed = -0x0010;    // MBEDTLS_ERR_MPI_ALLOC_FAILED

// The sizes (task p1.8-31), each from what goes through it. esp-mqtt at this pin names them
// buffer_size, out_buffer_size and task_stack (mqtt_client.h, esp_mqtt_client_config_t).
//
// The receive buffer: nothing is subscribed, so what arrives is a CONNACK (4 bytes), a PUBACK
// (4) and a PINGRESP (2); the WebSocket transport reads its own HTTP upgrade reply into a
// buffer of its own, and a WebSocket control frame carries 125 bytes at most. 512 is task 27's
// figure, kept: esp-mqtt's source is not on disk at this pin to prove a smaller floor against,
// and the quarter kilobyte it would save is not what a slot costs.
constexpr int kInBufferBytes = 512;
// The send buffer holds the largest thing ever sent, whole, so that nothing is fragmented. That
// is the CONNECT, not a packet message: a fixed header (a type byte and up to four of remaining
// length), the variable header ("MQTT" with its length, the level, the flags, the keepalive),
// the client id (esp-mqtt's ESP32_<MAC> is 12 bytes; MQTT 3.1.1's bound is 23), the will's
// topic and payload (the status message), the username and the password (the token) - each
// string with its two length bytes.
constexpr int kMqttFixedHeaderBytes = 5;
constexpr int kConnectVariableHeaderBytes = 10;
constexpr int kClientIdBytes = 2 + 23;
constexpr int kConnectBytes = kMqttFixedHeaderBytes + kConnectVariableHeaderBytes + kClientIdBytes +
                              (2 + static_cast<int>(mm::observer::kTopicCap)) + (2 + static_cast<int>(mm::observer::kStatusMessageCap)) +
                              (2 + static_cast<int>(mm::observer::kUsernameCap)) + (2 + static_cast<int>(mm::observer::kTokenCap));
// ...and a PUBLISH of the largest packet message with its topic and, at QoS 1, a message id.
constexpr int kPublishBytes = kMqttFixedHeaderBytes + (2 + static_cast<int>(mm::observer::kTopicCap)) + 2 +
                              static_cast<int>(mm::observer::kPacketMessageCap);
constexpr int kLargestSentBytes = kConnectBytes > kPublishBytes ? kConnectBytes : kPublishBytes;
constexpr int kOutBufferBytes = ((kLargestSentBytes + 63) / 64) * 64;
static_assert(kConnectBytes == 1780 && kPublishBytes == 1417 && kOutBufferBytes == 1792,
              "the send buffer's arithmetic moved: re-read the caps it is made of");
// The task's stack: esp-mqtt's own default, set here rather than inherited. What runs on it is
// the TLS handshake - mbedtls's parse of the broker's four-certificate chain and its ECDSA
// P-256 verification, the deepest call mbedtls makes, about 3 KB of stack at this version with
// its hardware MPI - esp-tls, the WebSocket transport's framing, esp-mqtt's own loop and this
// file's event handler (a 96-byte reason). 6 144 leaves about half over; stackLowWater() is the
// measurement, read on that task, and the bench decides whether 5 120 would do.
constexpr int kTaskStackBytes = static_cast<int>(mm::MqttClient::kTaskStackBytes);
// QoS 1 retransmit of an unacknowledged status: the observer firmware's 15 s rather than
// esp-mqtt's 1 s, which resends a byte-identical status every second while a PUBACK is slow
// (MQTTBridge.cpp:5136-5143 at d664d89d).
constexpr int kRetransmitMs = 15000;

static_assert(kOutBufferBytes >= kConnectBytes && kOutBufferBytes >= kPublishBytes,
              "the send buffer must hold the CONNECT and a packet message whole");

const char* connackReason(int code) {
  // MQTT 3.1.1 CONNACK return codes, as esp_mqtt_connect_return_code_t numbers them.
  switch (code) {
    case MQTT_CONNECTION_REFUSE_PROTOCOL: return "protocol rejected";
    case MQTT_CONNECTION_REFUSE_ID_REJECTED: return "client id rejected";
    case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE: return "server unavailable";
    case MQTT_CONNECTION_REFUSE_BAD_USERNAME: return "bad username or password";
    case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED: return "not authorized";
    default: return "refused";
  }
}

}  // namespace

MqttClient::MqttClient() : mux_(portMUX_INITIALIZER_UNLOCKED) {}

// ---- events, on esp-mqtt's task -------------------------------------------------------------

namespace {
esp_err_t eventThunk(esp_mqtt_event_handle_t event) {
  if (event != nullptr && event->user_context != nullptr) static_cast<mm::MqttClient*>(event->user_context)->onEvent(event);
  return ESP_OK;
}
}  // namespace

void MqttClient::push(const Pending& p) {
  portENTER_CRITICAL(&mux_);
  if (pendingCount_ < kPendingCap) {
    pending_[(pendingHead_ + pendingCount_) % kPendingCap] = p;
    pendingCount_++;
  } else {
    // Full: the newest replaces the newest kept. Four is more than a connection attempt
    // produces before the loop reads them (an error, a disconnect).
    pending_[(pendingHead_ + kPendingCap - 1) % kPendingCap] = p;
  }
  portEXIT_CRITICAL(&mux_);
}

void MqttClient::onEvent(void* raw) {
  esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(raw);
  // On esp-mqtt's task: how much of its stack has never been touched (bytes, on this port).
  const uint32_t left = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
  if (stackLowWater_ == 0 || left < stackLowWater_) stackLowWater_ = left;
  Pending p;
  switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
      connected_ = true;
      p.event = observer::ClientEvent::Connected;
      push(p);
      return;
    case MQTT_EVENT_DISCONNECTED:
      connected_ = false;
      p.event = observer::ClientEvent::Disconnected;
      push(p);
      return;
    case MQTT_EVENT_ERROR: {
      const esp_mqtt_error_codes_t* e = event->error_handle;
      if (e == nullptr) {
        p.event = observer::ClientEvent::Error;
        std::snprintf(p.reason, sizeof p.reason, "transport error");
        push(p);
        return;
      }
      if (e->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        p.event = observer::ClientEvent::Refused;
        std::snprintf(p.reason, sizeof p.reason, "%s (CONNACK %d)", connackReason(e->connect_return_code),
                      static_cast<int>(e->connect_return_code));
        push(p);
        return;
      }
      p.event = observer::ClientEvent::Error;
      const int stack = e->esp_tls_stack_err;
      const int tls = e->esp_tls_last_esp_err;
      if (stack == kMbedtlsCertVerifyFailed || e->esp_tls_cert_verify_flags != 0) {
        p.kind = observer::ErrorKind::ChainNotVerified;
        std::snprintf(p.reason, sizeof p.reason, "the certificate chain did not verify (flags 0x%x)",
                      static_cast<unsigned>(e->esp_tls_cert_verify_flags));
      } else if (stack == kMbedtlsSslAllocFailed || stack == kMbedtlsX509AllocFailed || stack == kMbedtlsPkAllocFailed ||
                 stack == kMbedtlsEcpAllocFailed || stack == kMbedtlsMpiAllocFailed || tls == ESP_ERR_NO_MEM) {
        p.kind = observer::ErrorKind::NoMemory;
        std::snprintf(p.reason, sizeof p.reason, "no memory for the TLS session (-0x%04x)", static_cast<unsigned>(-stack) & 0xFFFF);
      } else {
        const char* what = nullptr;
        switch (tls) {
          case ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME: what = "dns failed"; break;
          case ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET: what = "no socket"; break;
          case ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST: what = "the host did not answer"; break;
          case ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT: what = "connection timeout"; break;
          case ESP_ERR_ESP_TLS_TCP_CLOSED_FIN: what = "the server closed the connection"; break;
          case ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED: what = "tls handshake failed"; break;
          case ESP_ERR_MBEDTLS_SSL_SETUP_FAILED: what = "tls setup failed"; break;
          case ESP_ERR_MBEDTLS_X509_CRT_PARSE_FAILED: what = "the CA store did not parse"; break;
          default: break;
        }
        if (what != nullptr) {
          std::snprintf(p.reason, sizeof p.reason, "%s (tls -0x%04x, errno %d)", what, static_cast<unsigned>(-stack) & 0xFFFF,
                        e->esp_transport_sock_errno);
        } else if (e->esp_transport_sock_errno != 0) {
          std::snprintf(p.reason, sizeof p.reason, "socket error (errno %d)", e->esp_transport_sock_errno);
        } else {
          std::snprintf(p.reason, sizeof p.reason, "transport error (esp 0x%x, tls -0x%04x)", static_cast<unsigned>(tls),
                        static_cast<unsigned>(-stack) & 0xFFFF);
        }
      }
      push(p);
      return;
    }
    default:
      // BEFORE_CONNECT, PUBLISHED, DELETED: nothing the state machine acts on.
      return;
  }
}

// ---- the Client interface, on the loop task ------------------------------------------------

bool MqttClient::open(const observer::ClientConfig& cfg, char* reason, size_t cap) {
  if (reason != nullptr && cap > 0) reason[0] = '\0';
  close();
  stackLowWater_ = 0;

  esp_mqtt_client_config_t c = {};
  c.uri = cfg.uri;
  c.event_handle = &eventThunk;
  c.user_context = this;
  c.disable_auto_reconnect = true;  // the state machine's backoff is the reconnect policy, and it is visible
  c.keepalive = cfg.keepaliveS;
  c.buffer_size = kInBufferBytes;
  c.out_buffer_size = kOutBufferBytes;
  c.task_stack = kTaskStackBytes;
  c.message_retransmit_timeout = kRetransmitMs;
  c.username = (cfg.username != nullptr && cfg.username[0] != '\0') ? cfg.username : nullptr;
  c.password = (cfg.password != nullptr && cfg.password[0] != '\0') ? cfg.password : nullptr;
  if (cfg.willTopic != nullptr && cfg.willPayload != nullptr && cfg.willLen > 0) {
    c.lwt_topic = cfg.willTopic;
    c.lwt_msg = cfg.willPayload;
    c.lwt_msg_len = static_cast<int>(cfg.willLen);
    c.lwt_qos = 1;
    c.lwt_retain = cfg.willRetain ? 1 : 0;
  }
  // The CA store: the carried roots, else the core's bundle. One of the two, always - with
  // neither esp-tls would verify nothing, and this firmware never opens such a connection.
  if (cfg.caPem != nullptr) {
    c.cert_pem = cfg.caPem;
  } else {
    arduino_esp_crt_bundle_set(kCoreCertBundle);
    c.crt_bundle_attach = &arduino_esp_crt_bundle_attach;
  }
  if (c.cert_pem == nullptr && c.crt_bundle_attach == nullptr) {
    if (reason != nullptr) std::snprintf(reason, cap, "no CA store: not opened");
    return false;
  }

  client_ = esp_mqtt_client_init(&c);
  if (client_ == nullptr) {
    if (reason != nullptr) std::snprintf(reason, cap, "no memory for the client");
    return false;
  }
  const esp_err_t started = esp_mqtt_client_start(client_);
  if (started != ESP_OK) {
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    if (reason != nullptr) std::snprintf(reason, cap, "the client did not start (esp %d)", static_cast<int>(started));
    return false;
  }
  return true;
}

void MqttClient::close() {
  if (client_ != nullptr) {
    // destroy() stops the task first when it is running (esp-mqtt joins it), then frees the
    // client, its buffers and the TLS context.
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
  }
  connected_ = false;
  portENTER_CRITICAL(&mux_);
  pendingHead_ = 0;
  pendingCount_ = 0;
  portEXIT_CRITICAL(&mux_);
}

bool MqttClient::publish(const char* topic, const char* payload, size_t len, uint8_t qos, bool retain) {
  if (client_ == nullptr || !connected_ || topic == nullptr || payload == nullptr || len == 0) return false;
  // store=true: into the outbox, which esp-mqtt's task drains - never a write on this task.
  const int id = esp_mqtt_client_enqueue(client_, topic, payload, static_cast<int>(len), qos, retain ? 1 : 0, true);
  return id >= 0;
}

observer::ClientEvent MqttClient::takeEvent(observer::ErrorKind& kind, char* reason, size_t cap) {
  Pending p;
  bool have = false;
  portENTER_CRITICAL(&mux_);
  if (pendingCount_ > 0) {
    p = pending_[pendingHead_];
    pendingHead_ = (pendingHead_ + 1) % kPendingCap;
    pendingCount_--;
    have = true;
  }
  portEXIT_CRITICAL(&mux_);
  kind = p.kind;
  if (reason != nullptr && cap > 0) std::snprintf(reason, cap, "%s", have ? p.reason : "");
  return have ? p.event : observer::ClientEvent::None;
}

size_t MqttClient::outboxBytes() {
  if (client_ == nullptr) return 0;
  const int n = esp_mqtt_client_get_outbox_size(client_);
  return n > 0 ? static_cast<size_t>(n) : 0;
}

}  // namespace mm
