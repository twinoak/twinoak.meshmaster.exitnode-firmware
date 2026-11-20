// FIRMWARE-MANIFEST.md section 5, as this firmware makes it. Release.h is the page.

#include "Release.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <agent/Json.h>  // ArduinoJson, the one way this repository includes it
#include <SHA256.h>      // rweather/Crypto, as src/mesh/TransportScope.cpp uses it
#include <ed_25519.h>    // MeshCore's lib/ed25519 at the pin: Ed25519 as in RFC 8032

namespace mm {
namespace update {

const char kProduct[] = "twinoak-exitnode-firmware";
const char kRole[] = "exit-node";
const char kBand[] = "868";

namespace {

// Section 2's `use` values. A file whose use this reader does not know is a manifest it does not
// understand, and an unknown one is refused rather than ignored (section 2: "Unknown fields are
// ignored; an unknown schema or product is refused" - a use is neither a field nor optional).
const char* const kUses[] = {"swd", "usb-dfu", "usb-uf2", "ota", "factory", "debug", "provision"};

const int kSchema = 1;
const size_t kSignatureLength = 64;
const size_t kKeyIdHex = 16;
const size_t kSignatureBase64 = 88;

Outcome refuse(uint8_t check, const char* format, ...) __attribute__((format(printf, 2, 3)));
Outcome accept(const char* format, ...) __attribute__((format(printf, 1, 2)));

Outcome refuse(uint8_t check, const char* format, ...) {
  Outcome out;
  out.ok = false;
  out.check = check;
  va_list args;
  va_start(args, format);
  vsnprintf(out.why, sizeof(out.why), format, args);
  va_end(args);
  return out;
}

Outcome accept(const char* format, ...) {
  Outcome out;
  out.ok = true;
  out.check = 0;
  va_list args;
  va_start(args, format);
  vsnprintf(out.why, sizeof(out.why), format, args);
  va_end(args);
  return out;
}

bool copyString(char* dst, size_t dstLen, const char* src) {
  if (src == nullptr) return false;
  const size_t len = strlen(src);
  if (len + 1 > dstLen) return false;
  memcpy(dst, src, len + 1);
  return true;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return -1;  // lowercase, says section 2 and section 3
}

bool parseHex(const char* text, size_t bytes, uint8_t* out) {
  if (text == nullptr || strlen(text) != bytes * 2) return false;
  for (size_t i = 0; i < bytes; i++) {
    const int hi = hexValue(text[2 * i]);
    const int lo = hexValue(text[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

bool parseLoadAddress(const char* text, uint32_t& out) {
  if (text == nullptr || text[0] != '0' || text[1] != 'x') return false;
  const size_t digits = strlen(text) - 2;
  if (digits == 0 || digits > 8) return false;
  uint32_t value = 0;
  for (size_t i = 0; i < digits; i++) {
    const char c = text[2 + i];
    int v = hexValue(c);
    if (v < 0 && c >= 'A' && c <= 'F') v = 10 + (c - 'A');  // 0x%08X: the reference writes capitals
    if (v < 0) return false;
    value = (value << 4) | static_cast<uint32_t>(v);
  }
  out = value;
  return true;
}

int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return 26 + (c - 'a');
  if (c >= '0' && c <= '9') return 52 + (c - '0');
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Standard base64, strictly: 88 characters, the last two of them padding, the alphabet and
// nothing else, and the bits a 64-byte value cannot use are zero. 88 characters hold 66 bytes'
// worth of bits, so the last character's low four bits must be zero or two encodings would
// decode to one signature.
bool decodeSignature(const char* text, uint8_t* out) {
  if (strlen(text) != kSignatureBase64) return false;
  if (text[kSignatureBase64 - 1] != '=' || text[kSignatureBase64 - 2] != '=') return false;
  uint32_t bits = 0;
  int have = 0;
  size_t at = 0;
  for (size_t i = 0; i + 2 < kSignatureBase64; i++) {
    const int v = base64Value(text[i]);
    if (v < 0) return false;
    bits = (bits << 6) | static_cast<uint32_t>(v);
    have += 6;
    if (have >= 8) {
      have -= 8;
      if (at >= kSignatureLength) return false;
      out[at++] = static_cast<uint8_t>((bits >> have) & 0xFF);
    }
  }
  if (at != kSignatureLength) return false;
  return (bits & ((1u << have) - 1u)) == 0;  // no bits that decode to nothing
}

void keyIdOf(const uint8_t* key, uint8_t* out) {
  SHA256 sha;
  uint8_t digest[32];
  sha.reset();
  sha.update(key, 32);
  sha.finalize(digest, sizeof(digest));
  memcpy(out, digest, 8);
}

void hexOf(const uint8_t* data, size_t len, char* out) {
  static const char kDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[2 * i] = kDigits[data[i] >> 4];
    out[2 * i + 1] = kDigits[data[i] & 0x0F];
  }
  out[2 * len] = '\0';
}

// ------------------------------------------------------------------ check 1

Outcome checkSignature(const uint8_t* manifest, size_t manifestLen, const char* line,
                       const TrustedKey* keys, size_t keyCount) {
  if (keyCount == 0 || keys == nullptr) {
    return refuse(kCheckSignature,
                  "no release key in this build: no update will be taken (section 3)");
  }
  if (line == nullptr) return refuse(kCheckSignature, "there is no signature");

  // One line of ASCII, with at most the newline it was written with.
  size_t len = strlen(line);
  if (len > 0 && line[len - 1] == '\n') len--;
  if (len > 0 && line[len - 1] == '\r') len--;
  char text[kSignatureBase64 + kKeyIdHex + 32];
  if (len + 1 > sizeof(text)) {
    return refuse(kCheckSignature, "the signature line is %u bytes; section 3's is one short line",
                  static_cast<unsigned>(len));
  }
  memcpy(text, line, len);
  text[len] = '\0';

  // `ed25519 <key id> <signature>`: three words, one space between each pair and nowhere else.
  const char* firstSpace = strchr(text, ' ');
  if (firstSpace == nullptr) return refuse(kCheckSignature, "the signature line is not three words");
  const char* secondSpace = strchr(firstSpace + 1, ' ');
  if (secondSpace == nullptr) return refuse(kCheckSignature, "the signature line is not three words");
  if (strchr(secondSpace + 1, ' ') != nullptr) {
    return refuse(kCheckSignature, "the signature line has more than the two spaces section 3 gives it");
  }
  const size_t algorithmLen = static_cast<size_t>(firstSpace - text);
  if (algorithmLen != 7 || memcmp(text, "ed25519", 7) != 0) {
    return refuse(kCheckSignature, "the signature's algorithm is not ed25519");
  }
  const size_t idLen = static_cast<size_t>(secondSpace - firstSpace - 1);
  if (idLen != kKeyIdHex) {
    return refuse(kCheckSignature,
                  "the key id is %u characters; section 3's is 16 lowercase hex characters",
                  static_cast<unsigned>(idLen));
  }
  char idText[kKeyIdHex + 1];
  memcpy(idText, firstSpace + 1, kKeyIdHex);
  idText[kKeyIdHex] = '\0';
  uint8_t wantedId[8];
  if (!parseHex(idText, sizeof(wantedId), wantedId)) {
    return refuse(kCheckSignature, "the key id is not 16 lowercase hex characters");
  }
  uint8_t signature[kSignatureLength];
  if (!decodeSignature(secondSpace + 1, signature)) {
    return refuse(kCheckSignature,
                  "the signature is not 64 bytes in strict standard base64 (section 3)");
  }

  for (size_t i = 0; i < keyCount; i++) {
    uint8_t id[8];
    keyIdOf(keys[i].key, id);
    if (memcmp(id, keys[i].id, sizeof(id)) != 0) {
      // The table this build was compiled with disagrees with itself. tools/gen-trust-table.py
      // recomputes every id, so this is a corrupted image rather than a bad key file.
      continue;
    }
    if (memcmp(id, wantedId, sizeof(id)) != 0) continue;
    const int verified = ed25519_verify(signature, manifest, manifestLen, keys[i].key);
    if (verified != 1) {
      return refuse(kCheckSignature,
                    "the signature by the trusted key %s does not verify over the manifest's "
                    "bytes: it changed after it was signed",
                    idText);
    }
    return accept("the signature verifies with the trusted key %s", idText);
  }
  return refuse(kCheckSignature, "the signature names the key %s, which this build does not trust",
                idText);
}

// ------------------------------------------------------------------ check 2

const char* validateFile(JsonObjectConst file, JsonObjectConst image, char* detail, size_t detailLen);

// Section 2's rules over one image. Returns nullptr when it is good, else what is wrong.
const char* validateImage(JsonObjectConst image, char* detail, size_t detailLen) {
  static const char* const kStrings[] = {"id", "role", "board", "band", "mcu"};
  for (size_t i = 0; i < sizeof(kStrings) / sizeof(kStrings[0]); i++) {
    JsonVariantConst v = image[kStrings[i]];
    if (!v.is<const char*>() || v.as<const char*>()[0] == '\0') {
      snprintf(detail, detailLen, "image.%s is missing", kStrings[i]);
      return detail;
    }
  }
  JsonArrayConst files = image["files"];
  if (files.isNull() || files.size() == 0) return "an image has no files";
  size_t provisionFiles = 0;
  for (JsonObjectConst file : files) {
    const char* wrong = validateFile(file, image, detail, detailLen);
    if (wrong != nullptr) return wrong;
    const char* use = file["use"];
    if (strcmp(use, "provision") == 0) provisionFiles++;
  }
  if (provisionFiles > 1) return "more than one provision file in an image";
  return nullptr;
}

const char* validateFile(JsonObjectConst file, JsonObjectConst image, char* detail, size_t detailLen) {
  JsonVariantConst use = file["use"];
  if (!use.is<const char*>()) return "a file has no use";
  bool known = false;
  for (size_t i = 0; i < sizeof(kUses) / sizeof(kUses[0]); i++) {
    if (strcmp(use.as<const char*>(), kUses[i]) == 0) known = true;
  }
  if (!known) {
    snprintf(detail, detailLen, "unknown use '%.16s'", use.as<const char*>());
    return detail;
  }
  JsonVariantConst name = file["name"];
  if (!name.is<const char*>() || name.as<const char*>()[0] == '\0' ||
      strchr(name.as<const char*>(), '/') != nullptr ||
      strchr(name.as<const char*>(), '\\') != nullptr) {
    return "a file's name is missing or holds a path";
  }
  JsonVariantConst size = file["size"];
  if (!size.is<uint32_t>() || size.as<uint32_t>() == 0) return "a file's size is missing or zero";
  JsonVariantConst sha = file["sha256"];
  uint8_t digest[32];
  if (!sha.is<const char*>() || !parseHex(sha.as<const char*>(), sizeof(digest), digest)) {
    return "a file's sha256 is not 64 lowercase hex characters";
  }

  const char* useText = use.as<const char*>();
  const bool wantsLoad =
      strcmp(useText, "swd") == 0 || strcmp(useText, "ota") == 0 || strcmp(useText, "factory") == 0;
  uint32_t load = 0;
  if (wantsLoad) {
    JsonVariantConst at = file["load_address"];
    if (!at.is<const char*>() || !parseLoadAddress(at.as<const char*>(), load)) {
      return "a file that is written somewhere has no load_address";
    }
  }
  JsonVariantConst format = file["format"];
  const char* formatText = format.is<const char*>() ? format.as<const char*>() : "";
  if (strcmp(formatText, "hex") == 0 && strcmp(useText, "provision") != 0) {
    return "format hex is for a provision file only";
  }
  if (strcmp(useText, "swd") == 0) {
    // It must lie inside its image's app_region: a manifest cannot widen what may be written.
    JsonObjectConst region = image["app_region"];
    uint32_t start = 0;
    uint32_t end = 0;
    if (region.isNull() || !parseLoadAddress(region["start"] | "", start) ||
        !parseLoadAddress(region["end"] | "", end)) {
      return "an swd file's image has no app_region";
    }
    if (load < start || load + size.as<uint32_t>() > end) {
      return "an swd file does not fit its image's app_region";
    }
  }
  if (strcmp(useText, "provision") == 0) {
    if (strcmp(formatText, "hex") != 0) return "a provision file is Intel HEX (format hex)";
    if (!file["load_address"].isNull()) {
      return "a provision file has no load_address: its records say where";
    }
    if (strcmp(image["mcu"] | "", "nrf52840") != 0) {
      return "a provision file is for an nRF52840 board";
    }
    JsonObjectConst region = image["app_region"];
    if (region.isNull() || !region["start"].is<const char*>() || !region["end"].is<const char*>()) {
      return "a provision file needs its image's app_region";
    }
  }
  return nullptr;
}

}  // namespace

bool MemoryBytes::read(uint32_t offset, uint8_t* out, uint32_t len) const {
  if (data_ == nullptr) return false;
  if (offset > len_ || len > len_ - offset) return false;
  memcpy(out, data_ + offset, len);
  return true;
}

const ReleaseFile* fileWithUse(const ReleaseInfo& info, const char* use) {
  for (size_t i = 0; i < info.fileCount; i++) {
    if (strcmp(info.files[i].use, use) == 0) return &info.files[i];
  }
  return nullptr;
}

Outcome readRelease(const uint8_t* manifest, size_t manifestLen, const char* signatureLine,
                    const TrustedKey* keys, size_t keyCount, const char* board, ReleaseInfo& out) {
  // The caller's info is **emptied first and filled in place**: it is 872 bytes, and a second one
  // of them beside it was 872 bytes of this function's frame, over the rail's limit all by itself
  // (task p1.17-65, tools/check-frames.py). Nothing of a release half read is left in it either
  // way - every way out of this function but the one that read a whole image leaves it empty,
  // which is the same rule an update's record is loaded by.
  memset(&out, 0, sizeof(out));

  // 1.
  const Outcome signature = checkSignature(manifest, manifestLen, signatureLine, keys, keyCount);
  if (!signature.ok) return signature;

  // 2.
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, manifest, manifestLen);
  if (error) {
    return refuse(kCheckManifest, "the manifest is not JSON this board can read: %s", error.c_str());
  }
  if (!doc["schema"].is<int>() || doc["schema"].as<int>() != kSchema) {
    return refuse(kCheckManifest, "the manifest's schema is not %d", kSchema);
  }
  static const char* const kTextFields[] = {"product", "version", "tag", "commit", "built_at"};
  for (size_t i = 0; i < sizeof(kTextFields) / sizeof(kTextFields[0]); i++) {
    JsonVariantConst v = doc[kTextFields[i]];
    if (!v.is<const char*>() || v.as<const char*>()[0] == '\0') {
      return refuse(kCheckManifest, "the manifest's %s is missing", kTextFields[i]);
    }
  }
  if (strlen(doc["commit"].as<const char*>()) != 40) {
    return refuse(kCheckManifest, "the manifest's commit is not 40 characters");
  }
  JsonObjectConst link = doc["link"];
  if (link.isNull() || !link["major"].is<int>() || !link["minor"].is<int>()) {
    return refuse(kCheckManifest, "the manifest has no link version");
  }
  JsonArrayConst images = doc["images"];
  if (images.isNull() || images.size() == 0) {
    return refuse(kCheckManifest, "the manifest has no images");
  }
  char detail[96];
  for (size_t i = 0; i < images.size(); i++) {
    const char* wrong = validateImage(images[i], detail, sizeof(detail));
    if (wrong != nullptr) {
      return refuse(kCheckManifest, "the manifest does not validate (section 2): %s", wrong);
    }
    for (size_t j = i + 1; j < images.size(); j++) {
      JsonObjectConst other = images[j];
      if (strcmp(images[i]["id"].as<const char*>(), other["id"] | "") == 0) {
        return refuse(kCheckManifest, "the manifest has two images called %.48s",
                      images[i]["id"].as<const char*>());
      }
    }
  }
  if (strcmp(doc["product"].as<const char*>(), kProduct) != 0) {
    return refuse(kCheckManifest, "this release is %.40s and this board runs %s",
                  doc["product"].as<const char*>(), kProduct);
  }

  // 3.
  if (board == nullptr || board[0] == '\0') return refuse(kCheckImage, "this board has no name");
  for (JsonObjectConst image : images) {
    if (strcmp(image["board"].as<const char*>(), board) != 0) continue;
    if (strcmp(image["role"].as<const char*>(), kRole) != 0 ||
        strcmp(image["band"].as<const char*>(), kBand) != 0) {
      return refuse(kCheckImage, "the image for %s is %.16s on %.8s, and this board is %s on %s",
                    board, image["role"].as<const char*>(), image["band"].as<const char*>(), kRole,
                    kBand);
    }
    JsonArrayConst files = image["files"];
    if (files.size() > kMaxFilesPerImage) {
      return refuse(kCheckImage, "the image for %s names %u files and this reader holds %u", board,
                    static_cast<unsigned>(files.size()), static_cast<unsigned>(kMaxFilesPerImage));
    }

    const bool fits =
        copyString(out.product, sizeof(out.product), doc["product"].as<const char*>()) &&
        copyString(out.version, sizeof(out.version), doc["version"].as<const char*>()) &&
        copyString(out.tag, sizeof(out.tag), doc["tag"].as<const char*>()) &&
        copyString(out.commit, sizeof(out.commit), doc["commit"].as<const char*>()) &&
        copyString(out.imageId, sizeof(out.imageId), image["id"].as<const char*>()) &&
        copyString(out.role, sizeof(out.role), image["role"].as<const char*>()) &&
        copyString(out.board, sizeof(out.board), image["board"].as<const char*>()) &&
        copyString(out.band, sizeof(out.band), image["band"].as<const char*>()) &&
        copyString(out.mcu, sizeof(out.mcu), image["mcu"].as<const char*>());
    if (!fits) {
      memset(&out, 0, sizeof(out));
      return refuse(kCheckImage, "the image for %s says more about itself than this reader holds",
                    board);
    }
    out.linkMajor = static_cast<uint8_t>(link["major"].as<int>());
    out.linkMinor = static_cast<uint8_t>(link["minor"].as<int>());
    for (JsonObjectConst file : files) {
      ReleaseFile& entry = out.files[out.fileCount];
      if (!copyString(entry.use, sizeof(entry.use), file["use"].as<const char*>()) ||
          !copyString(entry.name, sizeof(entry.name), file["name"].as<const char*>())) {
        memset(&out, 0, sizeof(out));
        return refuse(kCheckImage, "a file of the image for %s is named at more length than this "
                                   "reader holds",
                      board);
      }
      entry.size = file["size"].as<uint32_t>();
      parseHex(file["sha256"].as<const char*>(), sizeof(entry.sha256), entry.sha256);
      entry.hasLoadAddress = file["load_address"].is<const char*>() &&
                             parseLoadAddress(file["load_address"].as<const char*>(), entry.loadAddress);
      out.fileCount++;
    }
    return accept("%s %s: the image %s, %u file(s)", out.product, out.version, out.imageId,
                  static_cast<unsigned>(out.fileCount));
  }
  return refuse(kCheckImage, "this release has no image for %s", board);
}

bool findOneDescriptor(const ReleaseBytes& bytes, Descriptor& out, uint32_t& offset, size_t& count) {
  // A window that always holds a whole descriptor and overlaps the last one by 92 bytes, so a
  // descriptor that straddles two reads is still seen whole. **512 bytes and not 1 024** since
  // task p1.17-65: a kilobyte of window was a frame of 1 168 bytes, over the rail's limit, and the
  // only cost of halving it is twice as many reads of a medium this already reads whole.
  static const uint32_t kWindow = 512;
  const uint32_t total = bytes.size();
  count = 0;
  if (total < kDescriptorLength) return false;
  uint8_t window[kWindow];
  uint32_t at = 0;
  while (at + kDescriptorLength <= total) {
    const uint32_t len = (total - at < kWindow) ? (total - at) : kWindow;
    if (!bytes.read(at, window, len)) return false;
    for (uint32_t i = 0; i + kDescriptorLength <= len; i += 4) {
      if ((at + i) % 4 != 0) continue;
      if (memcmp(window + i, kDescriptorMagic, kDescriptorMagicLength) != 0) continue;
      Descriptor read;
      if (!parseDescriptor(window + i, read)) continue;
      count++;
      if (count == 1) {
        out = read;
        offset = at + i;
      }
    }
    if (len < kWindow) break;
    at += kWindow - (kDescriptorLength - 4);  // the overlap, kept on a 4-byte boundary
  }
  return count == 1;
}

Outcome checkFile(const ReleaseInfo& info, const ReleaseFile& file, const ReleaseBytes& bytes) {
  // 4.
  if (bytes.size() != file.size) {
    return refuse(kCheckHash, "%s is %lu bytes and the manifest says %lu", file.name,
                  static_cast<unsigned long>(bytes.size()), static_cast<unsigned long>(file.size));
  }
  SHA256 sha;
  sha.reset();
  uint8_t chunk[512];
  for (uint32_t at = 0; at < file.size;) {
    const uint32_t len = (file.size - at < sizeof(chunk)) ? (file.size - at) : sizeof(chunk);
    if (!bytes.read(at, chunk, len)) {
      return refuse(kCheckHash, "%s could not be read at %lu", file.name,
                    static_cast<unsigned long>(at));
    }
    sha.update(chunk, len);
    at += len;
  }
  uint8_t digest[32];
  sha.finalize(digest, sizeof(digest));
  if (memcmp(digest, file.sha256, sizeof(digest)) != 0) {
    char got[2 * sizeof(digest) + 1];
    char want[2 * sizeof(digest) + 1];
    hexOf(digest, sizeof(digest), got);
    hexOf(file.sha256, sizeof(file.sha256), want);
    return refuse(kCheckHash, "%s hashes to %.16s... and the manifest says %.16s...", file.name,
                  got, want);
  }

  // 5.
  Descriptor found;
  uint32_t offset = 0;
  size_t count = 0;
  if (!findOneDescriptor(bytes, found, offset, count)) {
    return refuse(kCheckDescriptor, "%s holds %u descriptors and section 4 wants exactly one",
                  file.name, static_cast<unsigned>(count));
  }
  if (found.role != kRoleExitNode || strcmp(info.role, kRole) != 0) {
    char name[16];
    return refuse(kCheckDescriptor, "%s says it is a %s and the manifest says %.16s", file.name,
                  roleName(found.role, name, sizeof(name)), info.role);
  }
  if (strcmp(found.board, info.board) != 0) {
    return refuse(kCheckDescriptor, "%s says it is for %.24s and the manifest says %.24s",
                  file.name, found.board, info.board);
  }
  if (strcmp(found.band, info.band) != 0) {
    return refuse(kCheckDescriptor, "%s says band %.8s and the manifest says %.8s", file.name,
                  found.band, info.band);
  }
  if (strcmp(found.version, info.version) != 0) {
    return refuse(kCheckDescriptor, "%s says version %.32s and the manifest says %.32s", file.name,
                  found.version, info.version);
  }
  if (found.slot != kSlotAny) {
    return refuse(kCheckDescriptor, "%s is for the one slot %u and this image is for any",
                  file.name, static_cast<unsigned>(found.slot));
  }
  if (found.linkMajor != info.linkMajor || found.linkMinor != info.linkMinor) {
    return refuse(kCheckDescriptor, "%s says link %u.%u and the manifest says %u.%u", file.name,
                  static_cast<unsigned>(found.linkMajor), static_cast<unsigned>(found.linkMinor),
                  static_cast<unsigned>(info.linkMajor), static_cast<unsigned>(info.linkMinor));
  }
  return accept("%s: the hash is the manifest's and the one descriptor at 0x%lX agrees with it",
                file.name, static_cast<unsigned long>(offset));
}

}  // namespace update
}  // namespace mm
