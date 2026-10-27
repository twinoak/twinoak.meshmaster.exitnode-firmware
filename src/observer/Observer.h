// The observer on the board: the dialect of src/observer/ (Dialect.h, Token.h, Presets.h,
// Topics.h, Config.h - all host-portable) wired to this board's settings, identity, clock and
// radio. It is the tap AgentMesh hands every received frame to (AgentMesh.h, RxTap): when a slot
// is configured, the frame becomes a packet message in this object's buffer, and the status
// message is made on demand. **Nothing here opens a socket**: putting the messages on the air -
// the two TLS WebSocket connections, the sign-in, the queue, the retries - is task 27's, and it
// reads the messages from here. Until then `status` says how many were made.
//
// What it never does: decode a payload, write a packet's bytes to the log or the display, or
// let the private key out of the identity - the token is signed through Signer, whose board
// implementation is mesh::LocalIdentity::sign and nothing else (README.md, the rails; the pile's
// "Privacy, in one place").
//
// Board-only: Preferences through SettingsStore, MeshCore's LocalIdentity and the radio's clock.

#pragma once

#include <cstddef>
#include <cstdint>

#include "mesh/AgentMesh.h"
#include "observer/Config.h"
#include "observer/Dialect.h"
#include "observer/Token.h"
#include "settings/Store.h"

namespace mm {

// Signer over the board's own identity: Ed25519 by mesh::LocalIdentity::sign (src/Identity.cpp:151-153
// at d9296435), which is the call the observer firmware's JWTHelper makes.
class IdentitySigner : public observer::Signer {
 public:
  void setIdentity(const mesh::LocalIdentity* id) { id_ = id; }
  bool sign(const uint8_t* msg, size_t len, uint8_t sig[observer::kSignatureSize]) override {
    if (id_ == nullptr) return false;
    id_->sign(sig, msg, static_cast<int>(len));
    return true;
  }

 private:
  const mesh::LocalIdentity* id_ = nullptr;
};

class Observer : public RxTap {
 public:
  // `publicKeyHex` is the identity as hex (any case: stored uppercase, which is what {device},
  // origin_id and the token want); `model` is the board's name for the status message.
  void begin(SettingsStore& settings, const mesh::LocalIdentity* identity, const char* publicKeyHex, const char* model,
             uint32_t (*clock)());
  // Re-read every mqtt.* and mqttN.* key: after a `set` of one, and after `erase confirm`.
  void reconfigure();

  bool enabled() const { return enabled_; }
  const observer::ObserverSettings& settings() const { return view_; }
  observer::Signer& signer() { return signer_; }

  // RxTap: a received frame, after MeshCore accepted it. Formats it when enabled; counts always.
  void onRxFrame(const RxFrame& frame) override;

  // The status message as of now, into `out`; its length or 0.
  size_t formatStatus(const char* status, char* out, size_t cap);

  // The last packet message made ("" until one was), for the publisher of task 27.
  const char* lastPacketMessage() const { return packet_; }
  size_t lastPacketLength() const { return packetLen_; }

  // One line for `status`: the slots, the code, the origin, and the counts.
  void describe(char* out, size_t cap) const;

  uint32_t packetsFormatted() const { return formatted_; }
  uint32_t packetsNotFitting() const { return notFitting_; }

 private:
  void readSettings();
  const char* origin() const;

  SettingsStore* settings_ = nullptr;
  IdentitySigner signer_;
  uint32_t (*clock_)() = nullptr;
  const char* model_ = "";
  char publicKeyHex_[observer::kPublicKeyHexLen + 1] = {0};
  char defaultOrigin_[16] = {0};  // "MeshMaster-" + the first four of the key

  // The settings, as the store holds them (sizes: Keys.cpp's maxLen + 1).
  char iata_[4] = {0};
  char owner_[65] = {0};
  char email_[64] = {0};
  char origin_[32] = {0};
  struct SlotStore {
    char preset[11];
    char server[64];
    char audience[64];
    char topic[96];
    char username[32];
    char password[64];
  } slot_[observer::kSlots] = {};
  observer::ObserverSettings view_;
  bool enabled_ = false;
  char why_[64] = {0};

  char packet_[observer::kPacketMessageCap] = {0};
  size_t packetLen_ = 0;
  uint32_t formatted_ = 0;
  uint32_t notFitting_ = 0;
  uint32_t heardWhileOff_ = 0;
};

}  // namespace mm
