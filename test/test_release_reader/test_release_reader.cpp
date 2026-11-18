// The firmware's own reader of a release (task p1.15-58, deliverable 7), against two sets of
// bytes it did not make:
//
//   tests/fixtures/firmware   the specification's own fixtures, copied from meshmaster.dk: two
//                             other products' manifests and signatures, the images with one, two
//                             and a broken descriptor, and RFC 8032's first test vector
//   tests/fixtures/release    a release of THIS product, written and signed with the public test
//                             key by tools/release/ (tools/release/make_test_release.py)
//
// Every refusal this file asks for is derived from those by changing a byte, so what is tested is
// the reader and never a second copy of the specification written by the same hand.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <SHA256.h>
#include <agent/Json.h>
#include <ed_25519.h>

#include "update/Descriptor.h"
#include "update/Release.h"
#include "update/Trust.h"

#ifndef FIRMWARE_FIXTURES_DIR
#error "FIRMWARE_FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif
#ifndef RELEASE_FIXTURES_DIR
#error "RELEASE_FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;
using mm::update::Descriptor;
using mm::update::MemoryBytes;
using mm::update::Outcome;
using mm::update::ReleaseFile;
using mm::update::ReleaseInfo;
using mm::update::TrustedKey;

const char* kTag = "v0.0.0-test";

std::vector<uint8_t> readBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.good()) << "cannot read " << path.string();
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> spec(const std::string& name) {
  return readBytes(fs::path(FIRMWARE_FIXTURES_DIR) / name);
}

std::vector<uint8_t> release(const std::string& name) {
  return readBytes(fs::path(RELEASE_FIXTURES_DIR) / name);
}

std::string text(const std::vector<uint8_t>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string imageName(const std::string& board, bool merged) {
  return "meshmaster-agent-" + board + "-" + kTag + (merged ? "-merged.bin" : ".bin");
}

std::vector<uint8_t> fromHex(const std::string& hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  }
  return out;
}

// The key id of a public key: the first eight bytes of SHA-256 over it (section 3). Computed here
// so that a test can build a trust table of its own without the generated one.
TrustedKey keyFromHex(const std::string& hex) {
  TrustedKey key{};
  const std::vector<uint8_t> raw = fromHex(hex);
  EXPECT_EQ(32u, raw.size());
  std::memcpy(key.key, raw.data(), sizeof(key.key));
  SHA256 sha;
  uint8_t digest[32];
  sha.reset();
  sha.update(key.key, sizeof(key.key));
  sha.finalize(digest, sizeof(digest));
  std::memcpy(key.id, digest, sizeof(key.id));
  return key;
}

JsonDocument index() {
  JsonDocument doc;
  const std::vector<uint8_t> raw = spec("index.json");
  EXPECT_FALSE(deserializeJson(doc, raw.data(), raw.size()));
  return doc;
}

std::vector<TrustedKey> testKeyOnly() {
  JsonDocument idx = index();
  return {keyFromHex(idx["test_public_key"].as<std::string>())};
}

// A key nobody signed anything with, for "and against no other".
std::vector<TrustedKey> someOtherKey() {
  return {keyFromHex("0000000000000000000000000000000000000000000000000000000000000001")};
}

// The release of this product, read for one board.
ReleaseInfo readFixture(const std::string& board) {
  const std::vector<uint8_t> manifest = release("manifest.json");
  const std::string sig = text(release("manifest.json.sig"));
  const std::vector<TrustedKey> keys = testKeyOnly();
  ReleaseInfo info{};
  const Outcome out = mm::update::readRelease(manifest.data(), manifest.size(), sig.c_str(),
                                              keys.data(), keys.size(), board.c_str(), info);
  EXPECT_TRUE(out.ok) << "check " << static_cast<int>(out.check) << ": " << out.why;
  return info;
}

Outcome readWith(const std::vector<uint8_t>& manifest, const std::string& sig,
                 const std::vector<TrustedKey>& keys, const char* board, ReleaseInfo& info) {
  return mm::update::readRelease(manifest.data(), manifest.size(), sig.c_str(), keys.data(),
                                 keys.size(), board, info);
}

}  // namespace

// --------------------------------------------------------------------- the Ed25519 under it all

TEST(Ed25519, Rfc8032TestOneVerifies) {
  JsonDocument idx = index();
  JsonObject vector = idx["rfc8032_test_1"];
  const std::vector<uint8_t> key = fromHex(vector["public"].as<std::string>());
  std::vector<uint8_t> message = fromHex(vector["message"].as<std::string>());
  const std::vector<uint8_t> signature = fromHex(vector["signature"].as<std::string>());
  ASSERT_EQ(32u, key.size());
  ASSERT_EQ(64u, signature.size());
  ASSERT_TRUE(message.empty()) << "the RFC's first test vector is the empty message";
  // A buffer that is never null, with nothing in it: the RFC's first vector is the empty message.
  const uint8_t empty[1] = {0};
  EXPECT_EQ(1, ed25519_verify(signature.data(), empty, 0, key.data()));
}

TEST(Ed25519, Rfc8032TestOneFailsWithOneBitChanged) {
  JsonDocument idx = index();
  JsonObject vector = idx["rfc8032_test_1"];
  const std::vector<uint8_t> key = fromHex(vector["public"].as<std::string>());
  const std::vector<uint8_t> signature = fromHex(vector["signature"].as<std::string>());

  // The message: the RFC's first vector is the empty one, so one bit changed is one byte added.
  const uint8_t message[1] = {0x00};
  EXPECT_EQ(0, ed25519_verify(signature.data(), message, sizeof(message), key.data()));

  std::vector<uint8_t> wrongSignature = signature;
  wrongSignature[5] ^= 0x01;
  EXPECT_EQ(0, ed25519_verify(wrongSignature.data(), message, 0, key.data()));

  std::vector<uint8_t> wrongKey = key;
  wrongKey[0] ^= 0x01;
  EXPECT_EQ(0, ed25519_verify(signature.data(), message, 0, wrongKey.data()));
}

// --------------------------------------------------------------------- section 4, the descriptor

TEST(Descriptors, TheSpecificationsImagesReadAsItsIndexSays) {
  JsonDocument idx = index();
  for (JsonObject entry : idx["descriptors"].as<JsonArray>()) {
    const std::string name = entry["image"].as<std::string>();
    const std::vector<uint8_t> blob = spec(name);
    MemoryBytes bytes(blob.data(), static_cast<uint32_t>(blob.size()));
    Descriptor found{};
    uint32_t offset = 0;
    size_t count = 0;
    const bool one = mm::update::findOneDescriptor(bytes, found, offset, count);
    EXPECT_EQ(entry["valid"].as<bool>(), one) << name;
    if (!entry["valid"].as<bool>()) continue;
    EXPECT_EQ(entry["offset"].as<uint32_t>(), offset) << name;
    JsonObject fields = entry["fields"];
    EXPECT_EQ(fields["version"].as<std::string>(), std::string(found.version)) << name;
    EXPECT_EQ(fields["board"].as<std::string>(), std::string(found.board)) << name;
    EXPECT_EQ(fields["band"].as<std::string>(), std::string(found.band)) << name;
    EXPECT_EQ(fields["slot"].as<unsigned>(), found.slot) << name;
    EXPECT_EQ(fields["link_major"].as<unsigned>(), found.linkMajor) << name;
    EXPECT_EQ(fields["link_minor"].as<unsigned>(), found.linkMinor) << name;
    char role[16];
    EXPECT_EQ(fields["role"].as<std::string>(),
              std::string(mm::update::roleName(found.role, role, sizeof(role))))
        << name;
  }
}

TEST(Descriptors, TwoAreRefusedAndSoIsNone) {
  const std::vector<uint8_t> two = spec("image-two-descriptors.bin");
  const std::vector<uint8_t> broken = spec("image-broken-descriptor.bin");
  Descriptor found{};
  uint32_t offset = 0;
  size_t count = 0;

  MemoryBytes twoBytes(two.data(), static_cast<uint32_t>(two.size()));
  EXPECT_FALSE(mm::update::findOneDescriptor(twoBytes, found, offset, count));
  EXPECT_EQ(2u, count) << "image-two-descriptors.bin holds two";

  MemoryBytes brokenBytes(broken.data(), static_cast<uint32_t>(broken.size()));
  EXPECT_FALSE(mm::update::findOneDescriptor(brokenBytes, found, offset, count));
  EXPECT_EQ(0u, count) << "the only descriptor fails its crc, so there is none";
}

TEST(Descriptors, TheWalterImageYieldsItsOne) {
  const std::vector<uint8_t> blob = spec("image-walter-ota.bin");
  MemoryBytes bytes(blob.data(), static_cast<uint32_t>(blob.size()));
  Descriptor found{};
  uint32_t offset = 0;
  size_t count = 0;
  ASSERT_TRUE(mm::update::findOneDescriptor(bytes, found, offset, count));
  EXPECT_EQ(1u, count);
  EXPECT_EQ(0x20u, offset);
  EXPECT_EQ(16, found.role);  // the manager's
  EXPECT_STREQ("walter", found.board);
  EXPECT_STREQ("lte-m", found.band);
  EXPECT_STREQ("0.3.0+def5678", found.version);
}

TEST(Descriptors, OneThatStraddlesTwoReadsIsStillFound) {
  // The reader walks a file a kilobyte at a time; a descriptor that begins near the end of one
  // window has to be seen whole all the same.
  std::vector<uint8_t> blob(8192, 0x5A);
  const std::vector<uint8_t> source = spec("image-walter-ota.bin");
  const uint32_t at = 1024 - 20;  // inside the first window, running past its end
  std::memcpy(blob.data() + at, source.data() + 0x20, mm::update::kDescriptorLength);
  MemoryBytes bytes(blob.data(), static_cast<uint32_t>(blob.size()));
  Descriptor found{};
  uint32_t offset = 0;
  size_t count = 0;
  ASSERT_TRUE(mm::update::findOneDescriptor(bytes, found, offset, count));
  EXPECT_EQ(at, offset);
  EXPECT_STREQ("walter", found.board);
}

// --------------------------------------------------------------------- the specification's releases

TEST(TheSpecificationsManifests, VerifyWithTheTestKeyAndAreThenAnotherProducts) {
  for (const std::string name : {"manifest-walter.json", "manifest-repeater.json",
                                 "manifest-repeater-base.json"}) {
    const std::vector<uint8_t> manifest = spec(name);
    const std::string sig = text(spec(name + ".sig"));
    ReleaseInfo info{};
    const Outcome out = readWith(manifest, sig, testKeyOnly(), "heltec_v3", info);
    EXPECT_FALSE(out.ok) << name;
    EXPECT_EQ(mm::update::kCheckManifest, out.check)
        << name << ": the signature passed and the product did not, which is check 2 - got " << out.why;
    EXPECT_NE(nullptr, std::strstr(out.why, "twinoak-")) << out.why;
  }
}

TEST(TheSpecificationsManifests, VerifyAgainstNoOtherKey) {
  const std::vector<uint8_t> manifest = spec("manifest-repeater.json");
  const std::string sig = text(spec("manifest-repeater.json.sig"));
  ReleaseInfo info{};
  const Outcome out = readWith(manifest, sig, someOtherKey(), "heltec_v3", info);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckSignature, out.check) << out.why;
  EXPECT_NE(nullptr, std::strstr(out.why, "does not trust")) << out.why;
}

TEST(TheSpecificationsManifests, ATamperedOneFailsCheckOne) {
  const std::vector<uint8_t> manifest = spec("manifest-repeater-tampered.json");
  const std::string sig = text(spec("manifest-repeater.json.sig"));
  ReleaseInfo info{};
  const Outcome out = readWith(manifest, sig, testKeyOnly(), "heltec_v3", info);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckSignature, out.check) << out.why;
}

TEST(TheSpecificationsManifests, ASignatureByAKeyNobodyHoldsFailsCheckOne) {
  const std::vector<uint8_t> manifest = spec("manifest-repeater.json");
  const std::string sig = text(spec("manifest-repeater-unknown-key.json.sig"));
  ReleaseInfo info{};
  const Outcome out = readWith(manifest, sig, testKeyOnly(), "heltec_v3", info);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckSignature, out.check) << out.why;
}

// --------------------------------------------------------------------- this product's own release

TEST(ThisProductsRelease, ReadsWithNoRefusalForBothBoards) {
  const std::vector<uint8_t> manifest = release("manifest.json");
  size_t files = 0;
  for (const std::string board : {"heltec_v3", "heltec_v4"}) {
    const ReleaseInfo info = readFixture(board);
    EXPECT_STREQ("twinoak-exitnode-firmware", info.product);
    EXPECT_STREQ("0.0.0-test+1a2b3c4", info.version);
    EXPECT_STREQ(kTag, info.tag);
    EXPECT_EQ(40u, std::strlen(info.commit));
    EXPECT_STREQ("exit-node", info.role);
    EXPECT_STREQ("868", info.band);
    EXPECT_STREQ("esp32s3", info.mcu);
    EXPECT_EQ(board, std::string(info.board));
    EXPECT_EQ("exit-node." + board + ".868", std::string(info.imageId));
    EXPECT_EQ(0, info.linkMajor);
    EXPECT_EQ(0, info.linkMinor);
    ASSERT_EQ(2u, info.fileCount);
    files += info.fileCount;

    const ReleaseFile* ota = mm::update::fileWithUse(info, "ota");
    const ReleaseFile* factory = mm::update::fileWithUse(info, "factory");
    ASSERT_NE(nullptr, ota);
    ASSERT_NE(nullptr, factory);
    EXPECT_EQ(imageName(board, false), std::string(ota->name));
    EXPECT_EQ(imageName(board, true), std::string(factory->name));
    EXPECT_TRUE(ota->hasLoadAddress);
    EXPECT_EQ(0x10000u, ota->loadAddress);
    EXPECT_TRUE(factory->hasLoadAddress);
    EXPECT_EQ(0x0u, factory->loadAddress);

    // The size the manifest gives is the file's own.
    EXPECT_EQ(release(ota->name).size(), ota->size);
    EXPECT_EQ(release(factory->name).size(), factory->size);
  }
  EXPECT_EQ(4u, files) << "the manifest names four files: two boards, two files each";
  EXPECT_NE(nullptr, std::strstr(text(manifest).c_str(), "\"schema\": 1"));
}

TEST(ThisProductsRelease, ChecksFourAndFivePassForEveryFileOfEveryBoard) {
  for (const std::string board : {"heltec_v3", "heltec_v4"}) {
    const ReleaseInfo info = readFixture(board);
    for (size_t i = 0; i < info.fileCount; i++) {
      const std::vector<uint8_t> blob = release(info.files[i].name);
      MemoryBytes bytes(blob.data(), static_cast<uint32_t>(blob.size()));
      const Outcome out = mm::update::checkFile(info, info.files[i], bytes);
      EXPECT_TRUE(out.ok) << info.files[i].name << ": check " << static_cast<int>(out.check) << ": "
                          << out.why;
    }
  }
}

TEST(ThisProductsRelease, OneByteOfTheManifestChangedIsCheckOne) {
  std::vector<uint8_t> manifest = release("manifest.json");
  const std::string sig = text(release("manifest.json.sig"));
  const size_t at = text(manifest).find("esp32s3");
  ASSERT_NE(std::string::npos, at);
  manifest[at] = 'E';
  ReleaseInfo info{};
  const Outcome out = readWith(manifest, sig, testKeyOnly(), "heltec_v3", info);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckSignature, out.check) << out.why;
  EXPECT_NE(nullptr, std::strstr(out.why, "changed after it was signed")) << out.why;
}

TEST(ThisProductsRelease, AnotherProductsSignedManifestIsCheckTwo) {
  const std::vector<uint8_t> manifest = spec("manifest-walter.json");
  const std::string sig = text(spec("manifest-walter.json.sig"));
  ReleaseInfo info{};
  const Outcome out = readWith(manifest, sig, testKeyOnly(), "heltec_v3", info);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckManifest, out.check) << out.why;
}

TEST(ThisProductsRelease, ABoardItHasNoImageForIsCheckThree) {
  const std::vector<uint8_t> manifest = release("manifest.json");
  const std::string sig = text(release("manifest.json.sig"));
  ReleaseInfo info{};
  const Outcome out = readWith(manifest, sig, testKeyOnly(), "heltec_v9", info);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckImage, out.check) << out.why;
  EXPECT_NE(nullptr, std::strstr(out.why, "heltec_v9")) << out.why;
}

TEST(ThisProductsRelease, OneByteOfTheOtaFileChangedIsCheckFour) {
  const ReleaseInfo info = readFixture("heltec_v3");
  const ReleaseFile* ota = mm::update::fileWithUse(info, "ota");
  ASSERT_NE(nullptr, ota);
  std::vector<uint8_t> blob = release(ota->name);
  blob[7] ^= 0x01;
  MemoryBytes bytes(blob.data(), static_cast<uint32_t>(blob.size()));
  const Outcome out = mm::update::checkFile(info, *ota, bytes);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckHash, out.check) << out.why;
}

TEST(ThisProductsRelease, AFileOfTheWrongLengthIsCheckFour) {
  const ReleaseInfo info = readFixture("heltec_v3");
  const ReleaseFile* ota = mm::update::fileWithUse(info, "ota");
  ASSERT_NE(nullptr, ota);
  std::vector<uint8_t> blob = release(ota->name);
  blob.pop_back();
  MemoryBytes bytes(blob.data(), static_cast<uint32_t>(blob.size()));
  const Outcome out = mm::update::checkFile(info, *ota, bytes);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckHash, out.check) << out.why;
  EXPECT_NE(nullptr, std::strstr(out.why, "bytes and the manifest says")) << out.why;
}

TEST(ThisProductsRelease, TheOtherBoardsFileUnderThisBoardsEntryIsCheckFive) {
  // The file swapped and its hash swapped with it - so checks 1 to 4 all pass, and what is left
  // is the one thing a hash cannot say: the image inside says it is for the other board.
  ReleaseInfo info = readFixture("heltec_v3");
  const ReleaseInfo other = readFixture("heltec_v4");
  const ReleaseFile* mine = mm::update::fileWithUse(info, "ota");
  const ReleaseFile* theirs = mm::update::fileWithUse(other, "ota");
  ASSERT_NE(nullptr, mine);
  ASSERT_NE(nullptr, theirs);

  ReleaseFile entry = *mine;  // this board's name, under this board's image
  entry.size = theirs->size;
  std::memcpy(entry.sha256, theirs->sha256, sizeof(entry.sha256));

  const std::vector<uint8_t> blob = release(theirs->name);
  MemoryBytes bytes(blob.data(), static_cast<uint32_t>(blob.size()));
  const Outcome out = mm::update::checkFile(info, entry, bytes);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckDescriptor, out.check) << out.why;
  EXPECT_NE(nullptr, std::strstr(out.why, "heltec_v4")) << out.why;
}

TEST(ThisProductsRelease, AFileWithNoDescriptorOrTwoIsCheckFive) {
  const ReleaseInfo info = readFixture("heltec_v3");
  const ReleaseFile* ota = mm::update::fileWithUse(info, "ota");
  ASSERT_NE(nullptr, ota);
  const std::vector<uint8_t> good = release(ota->name);

  // The hash is recomputed for each, so that check 4 passes and check 5 is what refuses. The test
  // puts the manifest's hash where the file's own is.
  for (int which = 0; which < 2; which++) {
    std::vector<uint8_t> blob = good;
    if (which == 0) {
      blob[0x400] ^= 0xFF;  // the magic broken: no descriptor at all
    } else {
      std::memcpy(blob.data() + 0x800, good.data() + 0x400, mm::update::kDescriptorLength);
    }
    ReleaseFile entry = *ota;
    SHA256 sha;
    sha.reset();
    sha.update(blob.data(), blob.size());
    sha.finalize(entry.sha256, sizeof(entry.sha256));
    MemoryBytes bytes(blob.data(), static_cast<uint32_t>(blob.size()));
    const Outcome out = mm::update::checkFile(info, entry, bytes);
    EXPECT_FALSE(out.ok) << which;
    EXPECT_EQ(mm::update::kCheckDescriptor, out.check) << out.why;
    EXPECT_NE(nullptr, std::strstr(out.why, which == 0 ? "0 descriptors" : "2 descriptors")) << out.why;
  }
}

// --------------------------------------------------------------------- the signature line, read strictly

TEST(TheSignatureLine, IsRefusedAtCheckOneWhenItIsNotSectionThrees) {
  const std::vector<uint8_t> manifest = release("manifest.json");
  const std::string good = text(release("manifest.json.sig"));
  const size_t firstSpace = good.find(' ');
  const size_t secondSpace = good.find(' ', firstSpace + 1);
  const std::string algorithm = good.substr(0, firstSpace);
  const std::string key = good.substr(firstSpace + 1, secondSpace - firstSpace - 1);
  std::string signature = good.substr(secondSpace + 1);
  while (!signature.empty() && (signature.back() == '\n' || signature.back() == '\r')) {
    signature.pop_back();
  }
  ASSERT_EQ(16u, key.size());
  ASSERT_EQ(88u, signature.size());

  const std::vector<std::string> bad = {
      algorithm + "  " + key + " " + signature + "\n",            // a second space
      algorithm + " " + key.substr(0, 15) + " " + signature + "\n",  // a key id of 15 characters
      algorithm + " " + key + " " + signature.substr(0, 10) + "-" + signature.substr(11) + "\n",
      algorithm + " " + key + " " + signature.substr(0, 87) + "\n",  // 87 characters of base64
      algorithm + " " + key + "\n",                                  // two words
      "ed25518 " + key + " " + signature + "\n",                     // another algorithm
  };
  for (const std::string& line : bad) {
    ReleaseInfo info{};
    const Outcome out = readWith(manifest, line, testKeyOnly(), "heltec_v3", info);
    EXPECT_FALSE(out.ok) << line;
    EXPECT_EQ(mm::update::kCheckSignature, out.check) << line << " -> " << out.why;
  }

  // And the good one, with and without the newline it was written with.
  for (const std::string& line : {good, algorithm + " " + key + " " + signature}) {
    ReleaseInfo info{};
    const Outcome out = readWith(manifest, line, testKeyOnly(), "heltec_v3", info);
    EXPECT_TRUE(out.ok) << out.why;
  }
}

// --------------------------------------------------------------------- the build with no key

TEST(TrustTable, ABuildWithNoKeyRefusesAtCheckOneAndSaysSo) {
  const std::vector<uint8_t> manifest = release("manifest.json");
  const std::string sig = text(release("manifest.json.sig"));
  ReleaseInfo info{};
  const Outcome out = mm::update::readRelease(manifest.data(), manifest.size(), sig.c_str(),
                                              nullptr, 0, "heltec_v3", info);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(mm::update::kCheckSignature, out.check);
  EXPECT_NE(nullptr, std::strstr(out.why, "no release key in this build")) << out.why;
}

TEST(TrustTable, TheLineUpdatePrints) {
  char line[192];
  mm::update::renderKeysLine(nullptr, 0, false, line, sizeof(line));
  EXPECT_STREQ("no release key in this build: no update will be taken", line);

  const TrustedKey one = keyFromHex("dad705e5374be34b9c42528a3b1215cc30deb1d575d2ed19e7645defcae2f1fe");
  mm::update::renderKeysLine(&one, 1, false, line, sizeof(line));
  EXPECT_STREQ("1 release key trusted: 73efdd383a957596", line);

  mm::update::renderKeysLine(&one, 1, true, line, sizeof(line));
  EXPECT_NE(nullptr, std::strstr(line, "a test build")) << line;

  // A buffer too small is cut short and never written past.
  char small[20];
  std::memset(small, 0x7F, sizeof(small));
  mm::update::renderKeysLine(&one, 1, true, small, sizeof(small) - 1);
  EXPECT_EQ('\0', small[sizeof(small) - 2]) << "the line ran past the buffer";
  EXPECT_EQ(0x7F, small[sizeof(small) - 1]) << "the line ran past the buffer";
}

TEST(TrustTable, ThisBuildTrustsTheReleaseKeyAndTheTestKey) {
  // [env:native] is a TEST BUILD: tools/pio/release_headers.py passes --test-key for it and for no
  // board environment, which is what lets the tests above verify the specification's signatures.
  ASSERT_GE(mm::update::trustedKeyCount(), 2u);
  EXPECT_TRUE(mm::update::trustsTestKey());
  char line[192];
  mm::update::renderKeysLine(mm::update::trustedKeys(), mm::update::trustedKeyCount(),
                             mm::update::trustsTestKey(), line, sizeof(line));
  EXPECT_NE(nullptr, std::strstr(line, "73efdd383a957596")) << line;  // keys/release-keys.txt's
  EXPECT_NE(nullptr, std::strstr(line, "a test build")) << line;
}

TEST(TrustTable, TheCompiledTableReadsTheReleaseItWasBuiltFor) {
  // The whole way through, with the build's own table rather than one the test made.
  const std::vector<uint8_t> manifest = release("manifest.json");
  const std::string sig = text(release("manifest.json.sig"));
  ReleaseInfo info{};
  const Outcome out = mm::update::readRelease(manifest.data(), manifest.size(), sig.c_str(),
                                              mm::update::trustedKeys(),
                                              mm::update::trustedKeyCount(), "heltec_v3", info);
  EXPECT_TRUE(out.ok) << "check " << static_cast<int>(out.check) << ": " << out.why;
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
