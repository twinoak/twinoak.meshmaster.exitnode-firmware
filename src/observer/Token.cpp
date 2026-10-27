#include "Token.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include <agent/JsonWriter.h>

namespace mm {
namespace observer {

namespace {

// JWTHelper.cpp:14 - the alphabet, and the firmware's mbedtls encode with '+' and '/' swapped and
// the '=' padding dropped (:127-141) is the same as encoding with it directly.
const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int alphabetIndex(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

void upperCopy(const char* in, char* out, size_t cap) {
  size_t i = 0;
  for (; in != nullptr && in[i] != '\0' && i + 1 < cap; i++) out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(in[i])));
  out[i] = '\0';
}

bool present(const char* s) { return s != nullptr && s[0] != '\0'; }

}  // namespace

size_t base64Url(const uint8_t* in, size_t len, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';
  if (in == nullptr && len > 0) return 0;
  const size_t need = (len / 3) * 4 + (len % 3 == 0 ? 0 : len % 3 + 1);
  if (need + 1 > cap) return 0;
  size_t o = 0;
  size_t i = 0;
  for (; i + 3 <= len; i += 3) {
    const uint32_t v = (static_cast<uint32_t>(in[i]) << 16) | (static_cast<uint32_t>(in[i + 1]) << 8) | in[i + 2];
    out[o++] = kAlphabet[(v >> 18) & 63];
    out[o++] = kAlphabet[(v >> 12) & 63];
    out[o++] = kAlphabet[(v >> 6) & 63];
    out[o++] = kAlphabet[v & 63];
  }
  if (len - i == 1) {
    const uint32_t v = static_cast<uint32_t>(in[i]) << 16;
    out[o++] = kAlphabet[(v >> 18) & 63];
    out[o++] = kAlphabet[(v >> 12) & 63];
  } else if (len - i == 2) {
    const uint32_t v = (static_cast<uint32_t>(in[i]) << 16) | (static_cast<uint32_t>(in[i + 1]) << 8);
    out[o++] = kAlphabet[(v >> 18) & 63];
    out[o++] = kAlphabet[(v >> 12) & 63];
    out[o++] = kAlphabet[(v >> 6) & 63];
  }
  out[o] = '\0';
  return o;
}

size_t base64UrlDecode(const char* in, size_t len, uint8_t* out, size_t cap) {
  if (in == nullptr || out == nullptr) return 0;
  if (len % 4 == 1) return 0;  // no encoding leaves one character over
  size_t o = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < len; i++) {
    const int v = alphabetIndex(in[i]);
    if (v < 0) return 0;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o >= cap) return 0;
      out[o++] = static_cast<uint8_t>((acc >> bits) & 0xFF);
    }
  }
  return o;
}

void usernameFor(const char* publicKeyHex, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  char hex[kPublicKeyHexLen + 1];
  upperCopy(publicKeyHex, hex, sizeof hex);
  std::snprintf(out, cap, "v1_%s", hex);
}

bool makeToken(const TokenClaims& claims, Signer& signer, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return false;
  out[0] = '\0';
  if (!present(claims.audience)) return false;  // JWTHelper.cpp:27: no audience, no token

  // The header (JWTHelper.cpp:144-157), then the claims (:159-202), each as JSON and then base64url.
  char json[512];
  char header[64];
  char payload[512];
  {
    JsonWriter w(json, sizeof json);
    w.beginObject();
    w.key("alg");
    w.string("Ed25519");
    w.key("typ");
    w.string("JWT");
    w.endObject();
    if (w.overflowed() || base64Url(reinterpret_cast<const uint8_t*>(json), w.length(), header, sizeof header) == 0) return false;
  }
  {
    char publicKey[kPublicKeyHexLen + 1];
    char owner[kPublicKeyHexLen + 1];
    upperCopy(claims.publicKeyHex, publicKey, sizeof publicKey);  // JWTHelper.cpp:43-48
    upperCopy(claims.owner, owner, sizeof owner);                 // MQTTBridge.cpp:2968-2975

    JsonWriter w(json, sizeof json);
    w.beginObject();
    w.key("publicKey");
    w.string(publicKey);
    w.key("aud");
    w.string(claims.audience);
    w.key("iat");
    w.integer(claims.issuedAt);
    if (claims.expiresIn > 0) {
      w.key("exp");
      w.integer(static_cast<long long>(claims.issuedAt) + claims.expiresIn);
    }
    if (owner[0] != '\0') {
      w.key("owner");
      w.string(owner);
    }
    if (present(claims.client)) {
      w.key("client");
      w.string(claims.client);
    }
    if (present(claims.email)) {
      w.key("email");
      w.string(claims.email);
    }
    w.endObject();
    if (w.overflowed() || base64Url(reinterpret_cast<const uint8_t*>(json), w.length(), payload, sizeof payload) == 0) return false;
  }

  // The signing input "<header>.<claims>" (:57-66), the signature over it (:68-82), as hex (:91-96).
  const size_t headerLen = std::strlen(header);
  const size_t payloadLen = std::strlen(payload);
  const size_t signingLen = headerLen + 1 + payloadLen;
  if (signingLen + 1 + 2 * kSignatureSize + 1 > cap) return false;  // :98-103, against the caller's buffer
  std::memcpy(out, header, headerLen);
  out[headerLen] = '.';
  std::memcpy(out + headerLen + 1, payload, payloadLen);
  out[signingLen] = '\0';

  uint8_t signature[kSignatureSize];
  if (!signer.sign(reinterpret_cast<const uint8_t*>(out), signingLen, signature)) {
    out[0] = '\0';
    return false;
  }
  char* p = out + signingLen;
  *p++ = '.';
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < kSignatureSize; i++) {
    *p++ = kHex[signature[i] >> 4];
    *p++ = kHex[signature[i] & 0x0F];
  }
  *p = '\0';
  std::memset(signature, 0, sizeof signature);
  return true;
}

uint32_t tokenLifetime(uint32_t base, uint8_t slotIndex) {
  // MQTTConnectionPolicy.h:159-169, with the preset-or-default choice of MQTTBridge.cpp:2919-2927.
  if (base == 0) base = kDefaultTokenLifetime;
  uint32_t perSlot = base / 20;
  if (perSlot > kMaxTokenStagger) perSlot = kMaxTokenStagger;
  uint64_t stagger = static_cast<uint64_t>(slotIndex) * perSlot;
  if (stagger > base) stagger = base;
  return base - static_cast<uint32_t>(stagger);
}

uint32_t renewalBuffer(uint32_t lifetime) {
  // :171-176
  uint32_t buffer = lifetime / 10;
  if (buffer < 60) buffer = 60;
  if (buffer > 300) buffer = 300;
  return buffer;
}

bool tokenNeedsRenewal(bool timeSynced, uint32_t now, uint32_t expiresAt, uint32_t buffer) {
  // :178-191
  if (!timeSynced) return expiresAt == 0;
  if (expiresAt < kMinimumValidEpoch) return true;
  if (now >= expiresAt) return true;
  return now >= expiresAt - buffer;
}

}  // namespace observer
}  // namespace mm
