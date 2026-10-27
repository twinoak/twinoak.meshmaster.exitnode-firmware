// The observer's sign-in token: the observer firmware's JWT, exactly, signed by the node's own
// Ed25519 identity. Read from agessaman/MeshCore, branch observer-firmware, at
// d664d89df66bdacc1319900a2e2d152fa31b9dc9: src/helpers/JWTHelper.cpp and the bridge that calls it.
//
//   header     {"alg":"Ed25519","typ":"JWT"}                                    JWTHelper.cpp:144-157
//   claims     {"publicKey":"<64 UPPERCASE HEX>","aud":"<audience>","iat":N,    :159-202
//               "exp":N (iat + lifetime, when a lifetime is given),
//               "owner":"<64 UPPERCASE HEX>" (when set), "client":"<text>"
//               (when set), "email":"<text>" (when set)}
//   encoding   header and claims each base64url, the "-_" alphabet, no padding   :115-142
//   signature  Ed25519 over the bytes "<header>.<claims>", by the node's own    :57-82 (ed25519_sign, the same call
//              key - and written as 128 UPPERCASE HEX characters, not base64url:  mesh::LocalIdentity::sign makes:
//              "MeshCore Decoder format", the firmware says                       src/Identity.cpp:151-153 at d9296435)
//                                                                                 :91-110
//   token      "<header>.<claims>.<SIGNATURE HEX>"                              :98-110
//   username   "v1_<64 UPPERCASE HEX>" of the node's public key                 MQTTBridge.cpp:2961-2965
//   password   the token                                                         MQTTBridge.cpp:2051-2052
//   owner      mqtt.owner, uppercased before it is a claim                       MQTTBridge.cpp:2968-2975
//   client     "meshcore/<firmware_version>", the status message's client_version   MQTTBridge.cpp:2977-2978
//   email      mqtt.email when set                                               MQTTBridge.cpp:2979
//   lifetime   the preset's, else 86400 s, minus a stagger per slot so two      MQTTConnectionPolicy.h:17,159-169;
//              slots do not renew together: min(lifetime / 20, 300) * slot      MQTTBridge.cpp:2919-2927
//   renewal    the token is made again lifetime / 10 before its exp, never      MQTTConnectionPolicy.h:171-191
//              less than 60 s nor more than 300 s before; a token made without
//              a clock (exp 0) is made again as soon as there is one
//
// The private key never leaves the identity: the signer is an interface of one call, and the
// board's implementation is mesh::LocalIdentity::sign. Nothing of the token is ever logged.
//
// Host-portable: no Arduino, no MeshCore, no network.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {
namespace observer {

constexpr size_t kSignatureSize = 64;                 // MeshCore.h SIGNATURE_SIZE
constexpr size_t kPublicKeyHexLen = 64;               // 2 * PUB_KEY_SIZE
constexpr size_t kTokenCap = 768;                     // the observer firmware's AUTH_TOKEN_SIZE, a "768-byte token"
constexpr size_t kUsernameCap = 3 + kPublicKeyHexLen + 1;
constexpr uint32_t kDefaultTokenLifetime = 86400;     // MQTTConnectionPolicy.h:17
constexpr uint32_t kMaxTokenStagger = 300;            // :18
constexpr uint32_t kMinimumValidEpoch = 1000000000;   // :19, the clock is set once past this

// Ed25519 over `msg`, by the node's own key. The board's is mesh::LocalIdentity::sign; a test's
// is MeshCore's ed25519 with a fixed key.
class Signer {
 public:
  virtual bool sign(const uint8_t* msg, size_t len, uint8_t sig[kSignatureSize]) = 0;

 protected:
  ~Signer() {}
};

struct TokenClaims {
  const char* publicKeyHex = "";  // any case; uppercased in the claim
  const char* audience = "";
  uint32_t issuedAt = 0;          // UTC seconds
  uint32_t expiresIn = 0;         // seconds; 0 leaves "exp" out
  const char* owner = nullptr;    // 64 hex, any case; nullptr or "" leaves it out
  const char* client = nullptr;   // nullptr or "" leaves it out
  const char* email = nullptr;    // nullptr or "" leaves it out
};

// The token into `out`: true when it fit and was signed. `out` is "" on failure.
bool makeToken(const TokenClaims& claims, Signer& signer, char* out, size_t cap);

// "v1_<UPPERCASE HEX>" into `out` (kUsernameCap bytes).
void usernameFor(const char* publicKeyHex, char* out, size_t cap);

// base64url, no padding: the encoded length, or 0 when it does not fit. Both directions, so a
// test can read the header and the claims back.
size_t base64Url(const uint8_t* in, size_t len, char* out, size_t cap);
size_t base64UrlDecode(const char* in, size_t len, uint8_t* out, size_t cap);

// What goes in "exp" minus "iat" for slot `slotIndex` (0 or 1 here) of a preset with `base`
// seconds (0 = the default).
uint32_t tokenLifetime(uint32_t base, uint8_t slotIndex);

// How long before "exp" the token is made again.
uint32_t renewalBuffer(uint32_t lifetime);

// MQTTConnectionPolicy::tokenNeedsRenewal: whether to make a new token now.
bool tokenNeedsRenewal(bool timeSynced, uint32_t now, uint32_t expiresAt, uint32_t buffer);

}  // namespace observer
}  // namespace mm
