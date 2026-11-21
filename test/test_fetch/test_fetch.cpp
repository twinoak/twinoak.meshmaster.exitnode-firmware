// A release fetched into the other application slot, and judged there (task p1.15-59).
//
// Three fakes stand for the three seams of src/update/Fetch.h:
//
//   ScriptedServer   meshmaster.dk's four routes of `docs/dev/unit-protocol.md` §4 over a map of
//                    files, with a log of every request it was asked - the path, the Range, the
//                    If-Range and the Authorization header - and a per-request *twist*: a status
//                    of its own, no reply at all, a body that stops halfway (with what the client
//                    returned, the TLS error and the largest free block of that moment, task
//                    p1.19-68), a range it ignores.
//                    **It answers 401 to a request with no Authorization header**, which is what
//                    makes every green test below a test that the token went with the request.
//   FakeSlot         a slot that keeps what is written to it, counts its erases and restarts, and
//                    can refuse the framework's verdict.
//   FakeRecord       the settings store as a map.
//
// The release served is `tests/fixtures/release/` - written and signed by task 58's own tooling
// with the specification's public test key, which `[env:native]` is the one build that trusts.
// Every refusal below is derived from it by changing a byte and, where the change is inside the
// manifest, **signing it again with the published test private key** (`TEST-KEY.txt`), so that
// what refuses a doctored release is the check the test names and never check 1 by accident.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <SHA256.h>
#include <agent/TextOut.h>
#include <ed_25519.h>

#include "observer/Board.h"
#include "observer/Config.h"
#include "observer/Connection.h"
#include "observer/Dialect.h"
#include "observer/Token.h"
#include "update/Fetch.h"
#include "update/FetchObservers.h"
#include "update/Release.h"
#include "update/SlotRecord.h"
#include "update/Trust.h"
#include "update/UpdateRecord.h"  // kMaxNoteLen: what an ask's journal note holds

#ifndef RELEASE_FIXTURES_DIR
#error "RELEASE_FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif
#ifndef FIRMWARE_FIXTURES_DIR
#error "FIRMWARE_FIXTURES_DIR must be defined; platformio.ini's [env:native] does it"
#endif
#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;
using mm::update::Ending;
using mm::update::FetchResult;
using mm::update::Fetcher;
using mm::update::HttpAnswer;
using mm::update::HttpBody;
using mm::update::HttpRequest;
using mm::update::RecordStore;
using mm::update::SlotRecord;
using mm::update::SlotSink;

const char* kBase = "/agent/v1/firmware/7";
const char* kToken = "mma_this-is-not-a-real-token";

// -- the fixtures ------------------------------------------------------------------------------

std::vector<uint8_t> readBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.good()) << "cannot read " << path.string();
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> release(const std::string& name) {
  return readBytes(fs::path(RELEASE_FIXTURES_DIR) / name);
}

std::vector<uint8_t> spec(const std::string& name) {
  return readBytes(fs::path(FIRMWARE_FIXTURES_DIR) / name);
}

std::string text(const std::vector<uint8_t>& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<uint8_t> bytesOf(const std::string& s) {
  return std::vector<uint8_t>(s.begin(), s.end());
}

std::string hexOf(const uint8_t* b, size_t n) {
  static const char* kDigits = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < n; i++) {
    out += kDigits[b[i] >> 4];
    out += kDigits[b[i] & 0x0F];
  }
  return out;
}

std::string sha256Hex(const std::vector<uint8_t>& bytes) {
  SHA256 sha;
  sha.reset();
  sha.update(bytes.data(), bytes.size());
  uint8_t digest[32];
  sha.finalize(digest, sizeof(digest));
  return hexOf(digest, sizeof(digest));
}

std::string base64(const uint8_t* data, size_t len) {
  static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t a = data[i];
    const uint32_t b = i + 1 < len ? data[i + 1] : 0;
    const uint32_t c = i + 2 < len ? data[i + 2] : 0;
    const uint32_t v = (a << 16) | (b << 8) | c;
    out += kAlphabet[(v >> 18) & 0x3F];
    out += kAlphabet[(v >> 12) & 0x3F];
    out += i + 1 < len ? kAlphabet[(v >> 6) & 0x3F] : '=';
    out += i + 2 < len ? kAlphabet[v & 0x3F] : '=';
  }
  return out;
}

// The specification's public test key, as TEST-KEY.txt publishes both halves of it: its seed is
// the 32 bytes `test test ...`, and `key id` is c17969deb0026955. A manifest signed here is
// signed with the same key task 58's tooling signed the fixture release with, so a doctored
// manifest passes check 1 and is refused by the check the test is about.
std::string signatureLineFor(const std::string& manifest) {
  const std::string seedHex = "7465737420746573742074657374207465737420746573742074657374207465";
  uint8_t seed[32];
  for (size_t i = 0; i < sizeof(seed); i++) {
    seed[i] = static_cast<uint8_t>(std::stoul(seedHex.substr(2 * i, 2), nullptr, 16));
  }
  uint8_t pub[32];
  uint8_t priv[64];
  ed25519_create_keypair(pub, priv, seed);
  // The published public half, so that a signing routine that is not RFC 8032's is seen here and
  // not as a mysterious check-1 failure three tests later.
  EXPECT_EQ("6a38124d9c1744ccff9cbd285f001922460ce8e5603495f81a7582223079625b", hexOf(pub, sizeof(pub)));

  uint8_t sig[64];
  ed25519_sign(sig, reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size(), pub, priv);
  return "ed25519 c17969deb0026955 " + base64(sig, sizeof(sig)) + "\n";
}

// -- the scripted server ------------------------------------------------------------------------

struct Logged {
  std::string path;
  std::string range;          // the Range header's value, or "" for none
  std::string ifRange;        // the If-Range header's value, or "" for none
  std::string authorization;  // what the transport attached, or "" for none
};

class ScriptedServer final : public mm::update::ReleaseHttp {
 public:
  // What to do with one request instead of serving it.
  struct Twist {
    int status = 0;                 // answer with this status and no body
    bool noReply = false;           // no status line at all: a connection that was refused
    uint32_t deliver = 0;           // deliver only this many bytes of the range, then stop
    bool deliverNothing = false;    // the body stops before its first byte: a request that brought nothing
    bool ignoreRange = false;       // answer 200 with the whole file, as a multi-range is answered
    std::string contentRange;       // a Content-Range of its own
    std::string etag;               // an ETag of its own
    // What the transport read of a body that stopped (task p1.19-68): what the client returned,
    // the TLS layer's error, the largest free block then, and the transport's own reading of it.
    bool bodyReturnKnown = false;
    int32_t bodyReturn = 0;
    int tlsError = 0;
    bool outOfMemory = false;
    uint32_t largestFreeBlock = 0;
  };

  std::map<std::string, std::vector<uint8_t>> files;  // by full path
  std::map<std::string, Twist> byPath;                // for the manifest and the signature
  std::map<uint32_t, Twist> byFileRequest;            // by 0-based index of the file requests
  bool attachToken = true;

  std::vector<Logged> log;
  uint32_t fileRequests = 0;

  bool get(const HttpRequest& req, HttpAnswer& out, HttpBody& body) override {
    Logged l;
    l.path = req.path;
    if (req.hasRange) {
      l.range = "bytes=" + std::to_string(req.from) + "-" + std::to_string(req.to);
    }
    if (req.ifRange != nullptr) l.ifRange = req.ifRange;
    // The stand-in for src/update/FetchSeams.cpp, whose implementation of this seam is what puts
    // the token on the wire: the engine itself never sees one.
    if (attachToken) l.authorization = std::string("Bearer ") + kToken;
    log.push_back(l);

    const bool isFile = l.path.find("/files/") != std::string::npos;
    const uint32_t index = fileRequests;
    if (isFile) fileRequests++;

    if (l.authorization.empty()) return answerWithStatus(out, 401);

    Twist t;
    if (isFile) {
      const auto it = byFileRequest.find(index);
      if (it != byFileRequest.end()) t = it->second;
    } else {
      const auto it = byPath.find(l.path);
      if (it != byPath.end()) t = it->second;
    }

    if (t.noReply) {
      out.reachable = false;
      std::snprintf(out.error, sizeof(out.error), "connection refused");
      return false;
    }
    if (t.status != 0) return answerWithStatus(out, t.status);

    const auto f = files.find(l.path);
    if (f == files.end()) return answerWithStatus(out, 404);  // §4's one 404 for everything
    const std::vector<uint8_t>& whole = f->second;
    const uint32_t size = static_cast<uint32_t>(whole.size());

    out.reachable = true;
    // Every answer carries a strong ETag: the quoted SHA-256 of the content (§4).
    const std::string tag = t.etag.empty() ? "\"" + sha256Hex(whole) + "\"" : t.etag;
    std::snprintf(out.etag, sizeof(out.etag), "%s", tag.c_str());

    uint32_t from = 0;
    uint32_t len = size;
    if (req.hasRange && !t.ignoreRange) {
      if (req.from >= size) {
        out.status = 416;
        const std::string cr = "bytes */" + std::to_string(size);
        std::snprintf(out.contentRange, sizeof(out.contentRange), "%s", cr.c_str());
        return true;
      }
      from = req.from;
      const uint32_t last = req.to < size - 1 ? req.to : size - 1;
      len = last - from + 1;
      out.status = 206;
      const std::string cr = t.contentRange.empty()
                                 ? "bytes " + std::to_string(from) + "-" + std::to_string(last) + "/" +
                                       std::to_string(size)
                                 : t.contentRange;
      std::snprintf(out.contentRange, sizeof(out.contentRange), "%s", cr.c_str());
    } else {
      out.status = 200;
    }

    out.bodyReturnKnown = t.bodyReturnKnown;
    out.bodyReturn = t.bodyReturn;
    out.tlsError = t.tlsError;
    out.outOfMemory = t.outOfMemory;
    out.largestFreeBlock = t.largestFreeBlock;

    if (!body.begin(out)) return true;
    if (t.deliverNothing) {
      out.bodyLen = 0;
      return true;
    }
    uint32_t deliver = t.deliver != 0 && t.deliver < len ? t.deliver : len;
    // In pieces, as a socket hands them over, so that a sink which stops mid-answer is exercised.
    uint32_t at = 0;
    while (at < deliver) {
      const uint32_t piece = deliver - at < 512 ? deliver - at : 512;
      if (!body.take(whole.data() + from + at, piece)) break;
      at += piece;
    }
    out.bodyLen = at;
    return true;
  }

 private:
  static bool answerWithStatus(HttpAnswer& out, int status) {
    out.reachable = true;
    out.status = status;
    return true;
  }
};

// -- the slot and the record --------------------------------------------------------------------

class FakeSlot : public SlotSink {
 public:
  FakeSlot() = default;
  FakeSlot(bool there, const char* why) : there_(there), why_(why) {}

  bool available() const override { return there_; }
  const char* why() const override { return why_; }
  uint32_t capacity() const override { return capacity_; }

  bool begin(uint32_t expected) override {
    opened++;
    bytes.clear();
    expected_ = expected;
    open_ = true;
    return !refuseBegin;
  }
  uint32_t held() const override { return static_cast<uint32_t>(bytes.size()); }
  bool write(const uint8_t* data, size_t len) override {
    if (!open_ || refuseWrites) return false;
    bytes.insert(bytes.end(), data, data + len);
    return true;
  }
  bool restart() override {
    restarts++;
    bytes.clear();
    return true;
  }
  bool finish() override {
    finished++;
    open_ = false;
    return verdict;
  }
  bool spoil() override {
    spoiled++;
    // The first sector erased: 0xFF where the image header was.
    for (size_t i = 0; i < 0x1000 && i < bytes.size(); i++) bytes[i] = 0xFF;
    return true;
  }
  bool read(uint32_t offset, uint8_t* out, uint32_t len) const override {
    if (offset > bytes.size() || len > bytes.size() - offset) return false;
    std::memcpy(out, bytes.data() + offset, len);
    return true;
  }

  std::vector<uint8_t> bytes;
  bool verdict = true;
  bool refuseBegin = false;
  bool refuseWrites = false;
  int opened = 0;
  int restarts = 0;
  int finished = 0;
  int spoiled = 0;
  uint32_t capacity_ = 0x320000;

 private:
  bool there_ = true;
  const char* why_ = "app1 at 0x350000, 0x320000 bytes";
  bool open_ = false;
  uint32_t expected_ = 0;
};

class FakeRecord final : public RecordStore {
 public:
  std::map<std::string, std::string> kv;
  int puts = 0;

  size_t get(const char* key, char* out, size_t cap) override {
    if (cap == 0) return 0;
    out[0] = '\0';
    const auto it = kv.find(key);
    if (it == kv.end()) return 0;
    std::snprintf(out, cap, "%s", it->second.c_str());
    return std::strlen(out);
  }
  bool put(const char* key, const char* value) override {
    puts++;
    if (value == nullptr || value[0] == '\0') {
      kv.erase(key);
    } else {
      kv[key] = value;
    }
    return true;
  }
};

class CapturedText final : public mm::TextOut {
 public:
  void write(const char* data, size_t len) override { all.append(data, len); }
  std::string all;
};

// -- the bench ----------------------------------------------------------------------------------

// Everything one fetch needs, with the fixture release served whole. `rangeBytes` is the one
// number a test turns: the fixture's `ota` file is 4096 bytes, so 1024 is four ranges of it.
struct Bench {
  ScriptedServer server;
  FakeSlot slot;
  FakeRecord record;
  CapturedText out;
  Fetcher fetcher;
  std::string otaSha;
  std::string otaPath;
  std::vector<uint8_t> ota;

  explicit Bench(const char* board = "heltec_v3", uint32_t rangeBytes = 1024, uint32_t tries = 3,
                 mm::update::Observers* observers = nullptr) {
    const std::vector<uint8_t> manifest = release("manifest.json");
    server.files[std::string(kBase) + "/manifest.json"] = manifest;
    server.files[std::string(kBase) + "/manifest.json.sig"] = release("manifest.json.sig");
    ota = release("meshmaster-agent-heltec_v3-v0.0.0-test.bin");
    otaSha = sha256Hex(ota);
    otaPath = std::string(kBase) + "/files/" + otaSha;
    server.files[otaPath] = ota;

    fetcher.begin(server, slot, record, mm::update::trustedKeys(), mm::update::trustedKeyCount(), board,
                  &out);
    if (observers != nullptr) fetcher.setObservers(observers);
    Fetcher::Limits limits;
    limits.rangeBytes = rangeBytes;
    limits.tries = tries;
    fetcher.setLimits(limits);
  }

  // Start and drive to the end, one step a turn as loop() does. The bound is there so that a
  // step machine that stopped advancing fails the test instead of hanging the suite.
  const FetchResult& run(const char* base = kBase) {
    fetcher.start(base, 1000);
    for (int i = 0; i < 200 && fetcher.running(); i++) fetcher.step(1000 + 10 * (i + 1));
    EXPECT_FALSE(fetcher.running()) << "the fetch never ended";
    return fetcher.result();
  }
};

// The doctored manifest and its new signature in the server, in place of the fixture's.
void serveManifest(ScriptedServer& server, const std::string& manifest) {
  server.files[std::string(kBase) + "/manifest.json"] = bytesOf(manifest);
  server.files[std::string(kBase) + "/manifest.json.sig"] = bytesOf(signatureLineFor(manifest));
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// A release served whole
// ---------------------------------------------------------------------------------------------

TEST(AWholeRelease, IsFetchedIntoTheSlotByteForByteAndRecorded) {
  Bench b;
  const FetchResult& r = b.run();

  ASSERT_EQ(Ending::Fetched, r.ending) << static_cast<int>(r.check) << ": " << r.why;
  EXPECT_EQ(0u, r.check);
  EXPECT_EQ("0.0.0-test+1a2b3c4", std::string(r.version));
  EXPECT_EQ(b.otaSha, std::string(r.sha256hex));
  EXPECT_EQ(4096u, r.bytes);

  // The slot holds the `ota` file and nothing else, and was never spoiled.
  EXPECT_EQ(b.ota, b.slot.bytes);
  EXPECT_EQ(1, b.slot.opened);
  EXPECT_EQ(1, b.slot.finished);
  EXPECT_EQ(0, b.slot.spoiled);
  EXPECT_EQ(0, b.slot.restarts);

  // Two small requests and four ranges of 1024.
  EXPECT_EQ(6u, r.requests);
  EXPECT_EQ(4u, r.fileRequests);
  EXPECT_EQ(0u, r.failures);
  EXPECT_TRUE(r.ifRangeSent) << "the tag of the first range goes out as If-Range on the ones after it";

  // And the record says what the slot holds, in this task's own five keys.
  const SlotRecord rec = mm::update::loadRecord(b.record);
  EXPECT_TRUE(rec.judged);
  EXPECT_EQ("0.0.0-test+1a2b3c4", std::string(rec.version));
  EXPECT_EQ(b.otaSha, std::string(rec.sha256hex));
  EXPECT_EQ(4096u, rec.size);
  EXPECT_EQ(std::string(kBase), std::string(rec.base));
}

TEST(AWholeRelease, EveryRangeAsksForTheBytesTheSlotDoesNotHaveYet) {
  Bench b;
  ASSERT_EQ(Ending::Fetched, b.run().ending);

  ASSERT_EQ(6u, b.server.log.size());
  EXPECT_EQ(std::string(kBase) + "/manifest.json", b.server.log[0].path);
  EXPECT_EQ("", b.server.log[0].range) << "the manifest is not asked for in ranges";
  EXPECT_EQ(std::string(kBase) + "/manifest.json.sig", b.server.log[1].path);

  const std::vector<std::string> wantRanges = {"bytes=0-1023", "bytes=1024-2047", "bytes=2048-3071",
                                               "bytes=3072-4095"};
  for (size_t i = 0; i < wantRanges.size(); i++) {
    EXPECT_EQ(b.otaPath, b.server.log[2 + i].path) << i;
    EXPECT_EQ(wantRanges[i], b.server.log[2 + i].range) << i;
    // §4: the tag the first range came back with goes out on every range after it, and not on it.
    if (i == 0) {
      EXPECT_EQ("", b.server.log[2].ifRange);
    } else {
      EXPECT_EQ("\"" + b.otaSha + "\"", b.server.log[2 + i].ifRange) << i;
    }
  }
}

TEST(AWholeRelease, TheFileIsAskedForByItsSha256UnderFiles) {
  Bench b;
  ASSERT_EQ(Ending::Fetched, b.run().ending);
  EXPECT_EQ(std::string(kBase) + "/files/" + b.otaSha, b.server.log[2].path)
      << "§4: {base}/files/{nameOrSha256}, and this firmware asks by the hash";
}

TEST(AWholeRelease, OneRangeThatIsTheWholeFileWorksToo) {
  Bench b("heltec_v3", 64u * 1024u);
  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << r.why;
  EXPECT_EQ(1u, r.fileRequests);
  EXPECT_EQ("bytes=0-4095", b.server.log[2].range) << "a range is never longer than the file";
  EXPECT_EQ(b.ota, b.slot.bytes);
}

// ---------------------------------------------------------------------------------------------
// Checks 1, 2 and 3 - before one byte of an image is asked for
// ---------------------------------------------------------------------------------------------

TEST(BeforeAFile, AManifestWithOneByteChangedIsCheckOneAfterTwoRequestsAndNoneForAFile) {
  Bench b;
  std::vector<uint8_t> manifest = release("manifest.json");
  manifest[manifest.size() / 2] = manifest[manifest.size() / 2] == 'a' ? 'b' : 'a';
  b.server.files[std::string(kBase) + "/manifest.json"] = manifest;

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending);
  EXPECT_EQ(mm::update::kCheckSignature, r.check) << r.why;
  EXPECT_EQ(2u, r.requests) << "the manifest and its signature, and nothing else";
  EXPECT_EQ(0u, r.fileRequests);
  EXPECT_EQ(0u, b.server.fileRequests) << "the server was never asked for a file";
  EXPECT_EQ(0, b.slot.opened) << "and the slot was never even opened";
}

TEST(BeforeAFile, AnotherProductsSignedManifestIsCheckTwo) {
  Bench b;
  b.server.files[std::string(kBase) + "/manifest.json"] = spec("manifest-repeater.json");
  b.server.files[std::string(kBase) + "/manifest.json.sig"] = spec("manifest-repeater.json.sig");

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending);
  EXPECT_EQ(mm::update::kCheckManifest, r.check) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "twinoak-repeater-firmware")) << r.why;
  EXPECT_EQ(0u, b.server.fileRequests);
}

TEST(BeforeAFile, ANoImageForThisBoardIsCheckThree) {
  Bench b("heltec_v9");
  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending);
  EXPECT_EQ(mm::update::kCheckImage, r.check) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "heltec_v9")) << r.why;
  EXPECT_EQ(0u, b.server.fileRequests);
}

TEST(BeforeAFile, AFileLongerThanTheSlotIsRefusedWithoutAskingForIt) {
  Bench b;
  b.slot.capacity_ = 2048;  // smaller than the 4096 the manifest names
  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending);
  EXPECT_EQ(mm::update::kCheckImage, r.check) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "2048")) << r.why;
  EXPECT_EQ(0u, b.server.fileRequests) << "a manifest cannot widen what may be written";
}

TEST(BeforeAFile, AManifestLongerThanThisReaderHoldsIsCheckTwo) {
  Bench b;
  std::string manifest = text(release("manifest.json"));
  manifest.insert(manifest.size() - 2, std::string(mm::update::kMaxManifestBytes, ' '));
  b.server.files[std::string(kBase) + "/manifest.json"] = bytesOf(manifest);

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending);
  EXPECT_EQ(mm::update::kCheckManifest, r.check) << r.why;
  EXPECT_EQ(1u, r.requests);
}

// ---------------------------------------------------------------------------------------------
// Ranges, resuming, and a 200 that means start again
// ---------------------------------------------------------------------------------------------

TEST(Ranges, AConnectionThatDropsAtHalfTheFileResumesFromTheSlotsOwnCountWithIfRange) {
  Bench b("heltec_v3", 4096);  // one range for the whole file
  ScriptedServer::Twist half;
  half.deliver = 2048;
  b.server.byFileRequest[0] = half;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << static_cast<int>(r.check) << ": " << r.why;
  // Task p1.19-68, deliverable 5: it brought 2 048 bytes that were kept, so it is a short range
  // and not a failure, and it used up no try.
  EXPECT_EQ(0u, r.failures);
  EXPECT_EQ(1u, r.shortRanges);
  EXPECT_EQ(2u, r.fileRequests);
  EXPECT_EQ(2048u, r.resumedFrom);
  EXPECT_EQ(b.ota, b.slot.bytes) << "and the result is the file";

  ASSERT_EQ(4u, b.server.log.size());
  EXPECT_EQ("bytes=0-4095", b.server.log[2].range);
  EXPECT_EQ("", b.server.log[2].ifRange);
  EXPECT_EQ("bytes=2048-4095", b.server.log[3].range) << "from where the slot's writing stands";
  EXPECT_EQ("\"" + b.otaSha + "\"", b.server.log[3].ifRange) << "with the tag it saw, as If-Range";
}

TEST(Ranges, ATwoHundredToARangeWritesTheSlotAgainFromZero) {
  Bench b;  // four ranges of 1024
  ScriptedServer::Twist whole;
  whole.ignoreRange = true;  // §4: a multi-range, or a tag that does not match, answers 200
  b.server.byFileRequest[1] = whole;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << static_cast<int>(r.check) << ": " << r.why;
  EXPECT_EQ(1u, r.restarts);
  EXPECT_EQ(1, b.slot.restarts) << "the slot was sent back to zero and not spliced onto";
  EXPECT_EQ(2u, r.fileRequests) << "the 200 was the whole file, so there was nothing more to ask";
  EXPECT_EQ(b.ota, b.slot.bytes);
}

TEST(Ranges, ATwoHundredThatStopsHalfwayStartsTheSlotAgainAndIsCarriedOn) {
  Bench b("heltec_v3", 4096);
  ScriptedServer::Twist shortWhole;
  shortWhole.ignoreRange = true;
  shortWhole.deliver = 1000;
  b.server.byFileRequest[0] = shortWhole;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << static_cast<int>(r.check) << ": " << r.why;
  // The 200 restarted the slot and then brought 1 000 of 4 096 bytes: kept, so no try was spent
  // (task p1.19-68, deliverable 5) and the next range asks on from 1 000.
  EXPECT_EQ(0u, r.failures);
  EXPECT_EQ(1u, r.shortRanges);
  EXPECT_EQ(1u, r.restarts);
  EXPECT_EQ(b.ota, b.slot.bytes);
  ASSERT_EQ(4u, b.server.log.size());
  EXPECT_EQ("bytes=1000-4095", b.server.log[3].range);
}

TEST(Ranges, ARangeThatBroughtNothingIsTheOneThatCostsATry) {
  Bench b("heltec_v3", 4096);
  ScriptedServer::Twist nothing;
  nothing.deliverNothing = true;
  b.server.byFileRequest[0] = nothing;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << static_cast<int>(r.check) << ": " << r.why;
  EXPECT_EQ(1u, r.failures);
  EXPECT_EQ(0u, r.shortRanges);
  EXPECT_EQ(2u, r.fileRequests);
}

TEST(Ranges, AContentRangeThatIsNotWhatWasAskedForIsAFailure) {
  Bench b("heltec_v3", 4096);
  ScriptedServer::Twist wrong;
  wrong.contentRange = "bytes 16-4095/4096";
  b.server.byFileRequest[0] = wrong;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << r.why;
  EXPECT_EQ(1u, r.failures);
  EXPECT_EQ(b.ota, b.slot.bytes) << "and no byte of the answer that was refused reached the slot";
}

TEST(Ranges, AMissingContentRangeOnATwoHundredAndSixIsAFailure) {
  Bench b("heltec_v3", 4096);
  ScriptedServer::Twist none;
  none.contentRange = "nothing of the sort";
  b.server.byFileRequest[0] = none;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << r.why;
  EXPECT_EQ(1u, r.failures);
}

TEST(Ranges, ThreeFailuresEndTheFetchAndTheRecordSaysNothingJudged) {
  Bench b;
  // A judged image is in the slot's record before this fetch begins, so that clearing it is seen.
  SlotRecord before;
  before.judged = true;
  std::snprintf(before.version, sizeof(before.version), "0.7.2+deadbee");
  std::snprintf(before.sha256hex, sizeof(before.sha256hex),
                "0000000000000000000000000000000000000000000000000000000000000000");
  std::snprintf(before.base, sizeof(before.base), "/agent/v1/firmware/6");
  before.size = 1234;
  ASSERT_TRUE(mm::update::saveRecord(b.record, before));
  ASSERT_TRUE(mm::update::loadRecord(b.record).judged);

  ScriptedServer::Twist dead;
  dead.noReply = true;
  for (uint32_t i = 0; i < 3; i++) b.server.byFileRequest[i] = dead;

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Fetch, r.ending);
  EXPECT_EQ(0u, r.check);
  EXPECT_EQ(3u, r.failures);
  EXPECT_EQ(3u, r.fileRequests);
  EXPECT_FALSE(mm::update::loadRecord(b.record).judged) << "the record says the slot holds nothing judged";
  EXPECT_EQ(1, b.slot.spoiled) << "and what is in the slot will not boot";
}

TEST(Ranges, AFailureThatIsNotTheLastIsAskedAgain) {
  Bench b("heltec_v3", 4096);
  ScriptedServer::Twist later;
  later.status = 503;
  b.server.byFileRequest[0] = later;
  b.server.byFileRequest[1] = later;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << r.why;
  EXPECT_EQ(2u, r.failures);
  EXPECT_EQ(3u, r.fileRequests);
}

// ---------------------------------------------------------------------------------------------
// Checks 4 and 5, over the bytes as they lie in the slot
// ---------------------------------------------------------------------------------------------

TEST(Checks45, AFileWithAByteChangedIsCheckFour) {
  Bench b;
  std::vector<uint8_t> bad = b.ota;
  bad[bad.size() / 2] ^= 0x01;
  b.server.files[b.otaPath] = bad;

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending);
  EXPECT_EQ(mm::update::kCheckHash, r.check) << r.why;
  EXPECT_EQ(1, b.slot.spoiled) << "the slot's first sector is erased";
  EXPECT_EQ(0xFF, b.slot.bytes[0]);
  EXPECT_FALSE(mm::update::loadRecord(b.record).judged);
  EXPECT_EQ(0, b.slot.finished) << "and the framework was never asked to have it";
}

TEST(Checks45, AFileOneByteLongerIsCheckFour) {
  Bench b;
  std::vector<uint8_t> longer = b.ota;
  longer.push_back(0x00);
  b.server.files[b.otaPath] = longer;

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending);
  EXPECT_EQ(mm::update::kCheckHash, r.check) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "4097")) << r.why;
  EXPECT_EQ(1u, r.fileRequests) << "the server's own Content-Range said so, in one request";
  EXPECT_EQ(1, b.slot.spoiled);
  EXPECT_FALSE(mm::update::loadRecord(b.record).judged);
}

TEST(Checks45, TheOtherBoardsFileUnderThisBoardsEntryIsCheckFive) {
  // The file swapped **and its hash swapped with it**, which is what a manifest written over the
  // wrong files looks like: checks 1 to 4 all pass, and the only thing left to catch it is the
  // descriptor inside the image. The manifest is signed again so that check 1 is not what fires.
  Bench b;
  const std::vector<uint8_t> theirs = release("meshmaster-agent-heltec_v4-v0.0.0-test.bin");
  const std::string theirSha = sha256Hex(theirs);
  std::string manifest = text(release("manifest.json"));
  ASSERT_NE(std::string::npos, manifest.find(b.otaSha));
  manifest.replace(manifest.find(b.otaSha), b.otaSha.size(), theirSha);
  serveManifest(b.server, manifest);
  b.server.files[std::string(kBase) + "/files/" + theirSha] = theirs;

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending);
  EXPECT_EQ(mm::update::kCheckDescriptor, r.check) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "heltec_v4")) << r.why;
  EXPECT_EQ(1, b.slot.spoiled);
  EXPECT_EQ(0xFF, b.slot.bytes[0]);
  EXPECT_FALSE(mm::update::loadRecord(b.record).judged);
}

TEST(Checks45, TheFrameworksOwnVerdictIsCheckFiveAndSpoilsTheSlot) {
  Bench b;
  b.slot.verdict = false;  // esp_ota_end() would answer ESP_ERR_OTA_VALIDATE_FAILED
  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending);
  EXPECT_EQ(mm::update::kCheckDescriptor, r.check) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "will not boot it")) << r.why;
  EXPECT_EQ(1, b.slot.finished);
  EXPECT_EQ(1, b.slot.spoiled);
  EXPECT_FALSE(mm::update::loadRecord(b.record).judged);
}

TEST(Checks45, WhatIsJudgedIsReadBackFromTheSlotAndNotWhatWentPast) {
  // The slot keeps something other than what was written to it: the fetch must refuse it, because
  // what is judged is what would be booted.
  class LyingSlot final : public FakeSlot {
   public:
    bool write(const uint8_t* data, size_t len) override {
      std::vector<uint8_t> flipped(data, data + len);
      if (!flipped.empty()) flipped[0] ^= 0x80;
      return FakeSlot::write(flipped.data(), flipped.size());
    }
  };
  ScriptedServer server;
  LyingSlot slot;
  FakeRecord record;
  CapturedText out;
  Fetcher fetcher;
  const std::vector<uint8_t> ota = release("meshmaster-agent-heltec_v3-v0.0.0-test.bin");
  server.files[std::string(kBase) + "/manifest.json"] = release("manifest.json");
  server.files[std::string(kBase) + "/manifest.json.sig"] = release("manifest.json.sig");
  server.files[std::string(kBase) + "/files/" + sha256Hex(ota)] = ota;
  fetcher.begin(server, slot, record, mm::update::trustedKeys(), mm::update::trustedKeyCount(), "heltec_v3",
                &out);

  fetcher.start(kBase, 0);
  for (int i = 0; i < 200 && fetcher.running(); i++) fetcher.step(10 * (i + 1));
  EXPECT_EQ(Ending::Check, fetcher.result().ending);
  EXPECT_EQ(mm::update::kCheckHash, fetcher.result().check) << fetcher.result().why;
}

// ---------------------------------------------------------------------------------------------
// The server said no
// ---------------------------------------------------------------------------------------------

TEST(TheServerSaidNo, AFourOhFourForTheManifestIsFetchAtOnce) {
  Bench b;
  b.server.files.erase(std::string(kBase) + "/manifest.json");
  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Fetch, r.ending);
  EXPECT_EQ(0u, r.check);
  EXPECT_NE(nullptr, std::strstr(r.why, "there is no such resource")) << r.why;
  EXPECT_EQ(1u, r.requests) << "not retried: §4's one 404 is not read as a glitch";
}

TEST(TheServerSaidNo, AFourOhFourForTheFileIsFetchAtOnce) {
  Bench b;
  ScriptedServer::Twist gone;
  gone.status = 404;
  b.server.byFileRequest[0] = gone;
  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Fetch, r.ending);
  EXPECT_EQ(1u, r.fileRequests);
  EXPECT_EQ(0u, r.failures) << "a 404 is not one of the three tries";
  EXPECT_EQ(1, b.slot.spoiled);
}

TEST(TheServerSaidNo, ARedirectIsNotFollowedAndEndsTheFetch) {
  Bench b;
  ScriptedServer::Twist moved;
  moved.status = 302;
  b.server.byPath[std::string(kBase) + "/manifest.json"] = moved;

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Fetch, r.ending);
  EXPECT_NE(nullptr, std::strstr(r.why, "redirect")) << r.why;
  EXPECT_EQ(1u, r.requests) << "and nowhere else was asked";
  ASSERT_EQ(1u, b.server.log.size());
  EXPECT_EQ(std::string(kBase) + "/manifest.json", b.server.log[0].path);
}

TEST(TheServerSaidNo, ARedirectOnAFileIsNotFollowedEither) {
  Bench b;
  ScriptedServer::Twist moved;
  moved.status = 301;
  b.server.byFileRequest[0] = moved;
  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Fetch, r.ending);
  EXPECT_NE(nullptr, std::strstr(r.why, "redirect")) << r.why;
  EXPECT_EQ(1u, r.fileRequests);
  EXPECT_EQ(0u, r.failures);
}

TEST(TheServerSaidNo, TheHttpClientIsToldNotToFollowOne) {
  // The host cannot run the board's HTTPClient, so what proves the board does not follow a
  // redirect is the one line that says so, in the one file that makes every call.
  const fs::path client = fs::path(PROJECT_DIR) / "src" / "agent" / "AgentClient.cpp";
  std::ifstream in(client, std::ios::binary);
  const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(std::string::npos, all.find("setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS)")) << client.string();
}

TEST(TheServerSaidNo, AFourOhOneEndsTheFetchAndIsNotRetried) {
  Bench b;
  b.server.attachToken = false;  // the server answers 401 to a request with no Authorization
  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Fetch, r.ending);
  EXPECT_EQ(1u, r.requests);
  EXPECT_NE(nullptr, std::strstr(r.why, "401")) << r.why;
}

TEST(TheServerSaidNo, AFourSixteenEndsTheFetchAndIsNotRetried) {
  // A 416 says the range is past the end of the server's file, which the manifest's size said it
  // was not. Not a glitch, so not one of the three tries.
  Bench b("heltec_v3", 1024);
  ScriptedServer::Twist past;
  past.status = 416;
  b.server.byFileRequest[0] = past;
  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Fetch, r.ending) << r.why;
  EXPECT_EQ(0u, r.failures);
  EXPECT_EQ(1u, r.fileRequests);
  EXPECT_EQ(1, b.slot.spoiled);
}

TEST(TheServerSaidNo, AFileShorterThanTheManifestSaysIsCheckFour) {
  Bench b("heltec_v3", 1024);
  b.server.files[b.otaPath] = std::vector<uint8_t>(b.ota.begin(), b.ota.begin() + 1024);
  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Check, r.ending) << r.why;
  EXPECT_EQ(mm::update::kCheckHash, r.check) << r.why;
  EXPECT_EQ(1u, r.fileRequests) << "the server's own Content-Range said so, in one request";
}

// ---------------------------------------------------------------------------------------------
// Only with a second slot
// ---------------------------------------------------------------------------------------------

TEST(OnlyWithASecondSlot, TheTableWordOneRefusesBeforeAnyRequest) {
  ScriptedServer server;
  FakeSlot slot(false, "this board's table has one application slot; type `update slot` to add the second");
  FakeRecord record;
  CapturedText out;
  Fetcher fetcher;
  fetcher.begin(server, slot, record, mm::update::trustedKeys(), mm::update::trustedKeyCount(), "heltec_v3",
                &out);

  EXPECT_FALSE(fetcher.start(kBase, 0));
  EXPECT_FALSE(fetcher.running());
  const FetchResult& r = fetcher.result();
  EXPECT_EQ(Ending::Slot, r.ending);
  EXPECT_EQ(std::string("slot"), std::string(mm::update::endingWord(r.ending)));
  EXPECT_NE(nullptr, std::strstr(r.why, "update slot")) << r.why;
  EXPECT_EQ(0u, r.requests);
  EXPECT_TRUE(server.log.empty()) << "nothing was asked of the server at all";
  EXPECT_EQ(0, record.puts) << "and the record was not even cleared";
}

TEST(OnlyWithASecondSlot, ABaseThatIsNotAPathIsRefusedBeforeAnyRequest) {
  Bench b;
  for (const char* bad : {"https://elsewhere.example/firmware/7", "agent/v1/firmware/7", ""}) {
    EXPECT_FALSE(b.fetcher.start(bad, 0)) << bad;
    EXPECT_EQ(Ending::Fetch, b.fetcher.result().ending) << bad;
  }
  EXPECT_TRUE(b.server.log.empty());
}

// ---------------------------------------------------------------------------------------------
// The token, and what is printed
// ---------------------------------------------------------------------------------------------

TEST(TheToken, EveryRequestOfAFetchCarriesItAndGoesToTheConfiguredServer) {
  Bench b;
  ASSERT_EQ(Ending::Fetched, b.run().ending);
  ASSERT_EQ(6u, b.server.log.size());
  for (const Logged& l : b.server.log) {
    EXPECT_EQ(std::string("Bearer ") + kToken, l.authorization) << l.path;
    // A path on the configured server, under this release's base and nowhere else: the origin is
    // the implementation's and this engine never names a host.
    EXPECT_EQ(0u, l.path.find(std::string(kBase) + "/")) << l.path;
    EXPECT_EQ(std::string::npos, l.path.find("://")) << l.path;
  }
}

TEST(TheToken, WithoutItEveryRequestIsRefusedByTheServer) {
  Bench b;
  b.server.attachToken = false;
  EXPECT_EQ(Ending::Fetch, b.run().ending);
  ASSERT_EQ(1u, b.server.log.size());
  EXPECT_EQ("", b.server.log[0].authorization) << "which is what the 401 above was about";
}

TEST(TheToken, OnlyOneFileOfSrcAndLibBuildsAnAuthorizationHeader) {
  // The engine, the seams and the console hold no token: there is one place in this firmware that
  // puts one on the wire, and this is the test that says which. The shape is test_tls_policy's.
  const char* const kTheOneFile = "src/agent/AgentClient.cpp";
  std::vector<std::string> hits;
  int scanned = 0;
  for (const char* dir : {"src", "lib"}) {
    const fs::path d = fs::path(PROJECT_DIR) / dir;
    if (!fs::is_directory(d)) continue;
    for (const auto& item : fs::recursive_directory_iterator(d)) {
      if (!item.is_regular_file()) continue;
      scanned++;
      std::ifstream in(item.path(), std::ios::binary);
      std::string line;
      bool found = false;
      while (std::getline(in, line)) {
        // The header built, not the word mentioned in a comment: an addHeader of it, or a Bearer
        // written into a buffer.
        if (line.find("addHeader(\"Authorization\"") != std::string::npos) found = true;
        if (line.find("\"Bearer %s\"") != std::string::npos) found = true;
      }
      if (found) hits.push_back(fs::relative(item.path(), fs::path(PROJECT_DIR)).generic_string());
    }
  }
  std::sort(hits.begin(), hits.end());
  EXPECT_GT(scanned, 20) << "is PROJECT_DIR " << PROJECT_DIR << " right?";
  EXPECT_EQ(std::vector<std::string>{kTheOneFile}, hits)
      << "a second file of src/ or lib/ builds an Authorization header. There is one place the "
         "token reaches the wire, and every call - the fetch's ranges included - goes through it.";
}

TEST(TheToken, NoStepLineAndNoRecordLineHoldsAByteOfIt) {
  Bench b;
  ASSERT_EQ(Ending::Fetched, b.run().ending);
  EXPECT_FALSE(b.out.all.empty());
  EXPECT_EQ(std::string::npos, b.out.all.find(kToken)) << b.out.all;
  EXPECT_EQ(std::string::npos, b.out.all.find("mma_")) << b.out.all;
  EXPECT_EQ(std::string::npos, b.out.all.find("Bearer")) << b.out.all;
  EXPECT_EQ(std::string::npos, b.out.all.find("Authorization")) << b.out.all;

  char line[200];
  mm::update::renderRecordLine(mm::update::loadRecord(b.record), line, sizeof(line));
  EXPECT_EQ(nullptr, std::strstr(line, "mma_")) << line;
  EXPECT_NE(nullptr, std::strstr(line, "0.0.0-test+1a2b3c4")) << line;
  EXPECT_NE(nullptr, std::strstr(line, "4096 bytes")) << line;
  EXPECT_NE(nullptr, std::strstr(line, kBase)) << line;
}

TEST(TheSteps, EachOneSaysWhatItDidAndTheEndingSaysItsWord) {
  Bench b;
  ASSERT_EQ(Ending::Fetched, b.run().ending);
  for (const char* want : {"manifest.json:", "manifest.json.sig:", "checks 1, 2 and 3 passed",
                           "range 0-1023", "checks 4 and 5 passed", "fetched 0.0.0-test+1a2b3c4"}) {
    EXPECT_NE(std::string::npos, b.out.all.find(want)) << want << " is missing from:\n" << b.out.all;
  }
}

TEST(TheSteps, ARefusalSaysItsNumber) {
  Bench b("heltec_v9");
  ASSERT_EQ(Ending::Check, b.run().ending);
  EXPECT_NE(std::string::npos, b.out.all.find("check 3:")) << b.out.all;
  EXPECT_NE(std::string::npos, b.out.all.find("heltec_v9")) << b.out.all;
}

// ---------------------------------------------------------------------------------------------
// Nothing is switched here
// ---------------------------------------------------------------------------------------------

TEST(NothingIsSwitched, AFetchedImageOnlySitsInTheSlotAndInTheRecord) {
  // There is no seam in this task that could change what a board boots: the slot's surface is
  // open, write, restart, finish, spoil and read, and the record is five keys of text. Task 60 is
  // what adds the switch. The test that nothing calls the framework's switch is
  // test/test_flash_writes.
  Bench b;
  ASSERT_EQ(Ending::Fetched, b.run().ending);
  EXPECT_EQ(1, b.slot.finished);
  EXPECT_EQ(0, b.slot.spoiled);
  EXPECT_TRUE(mm::update::loadRecord(b.record).judged);
}

TEST(NothingIsSwitched, ARestartKeepsTheRecordAndASecondFetchClearsItFirst) {
  Bench b;
  ASSERT_EQ(Ending::Fetched, b.run().ending);
  // "A restart keeps it": the record is read out of the store it was written to, by a reader that
  // holds nothing of the fetch.
  FakeRecord& store = b.record;
  EXPECT_TRUE(mm::update::loadRecord(store).judged);

  // And the moment a second fetch opens the slot, the record of the old image is gone - the slot
  // no longer holds it.
  b.server.files.erase(std::string(kBase) + "/manifest.json");
  EXPECT_EQ(Ending::Fetch, b.run().ending);
  EXPECT_FALSE(mm::update::loadRecord(store).judged);
}

// ---------------------------------------------------------------------------------------------
// The record, on its own
// ---------------------------------------------------------------------------------------------

TEST(TheRecord, RoundTripsAndSaysNothingJudgedWhenItIsCleared) {
  FakeRecord store;
  EXPECT_FALSE(mm::update::loadRecord(store).judged);

  SlotRecord rec;
  rec.judged = true;
  std::snprintf(rec.version, sizeof(rec.version), "0.8.0+1a2b3c4");
  std::snprintf(rec.base, sizeof(rec.base), "/agent/v1/firmware/12");
  rec.size = 1446640;
  // A 63-character hash is not a hash, and neither is one written in upper case.
  std::snprintf(rec.sha256hex, sizeof(rec.sha256hex),
                "4f2a0000000000000000000000000000000000000000000000000000000000f");
  EXPECT_TRUE(mm::update::saveRecord(store, rec));
  EXPECT_FALSE(mm::update::loadRecord(store).judged) << "a 63-character sha256 is not one";
  std::snprintf(rec.sha256hex, sizeof(rec.sha256hex),
                "4F2A0000000000000000000000000000000000000000000000000000000000F0");
  EXPECT_TRUE(mm::update::saveRecord(store, rec));
  EXPECT_FALSE(mm::update::loadRecord(store).judged) << "a sha256 is 64 lower-case hex digits";

  std::snprintf(rec.sha256hex, sizeof(rec.sha256hex),
                "4f2a0000000000000000000000000000000000000000000000000000000000f0");
  ASSERT_TRUE(mm::update::saveRecord(store, rec));
  const SlotRecord read = mm::update::loadRecord(store);
  ASSERT_TRUE(read.judged);
  EXPECT_EQ(std::string(rec.version), std::string(read.version));
  EXPECT_EQ(std::string(rec.sha256hex), std::string(read.sha256hex));
  EXPECT_EQ(std::string(rec.base), std::string(read.base));
  EXPECT_EQ(1446640u, read.size);

  SlotRecord none;
  ASSERT_TRUE(mm::update::saveRecord(store, none));
  EXPECT_FALSE(mm::update::loadRecord(store).judged);
  EXPECT_TRUE(store.kv.empty()) << "forgetting it forgets all five keys";
}

TEST(TheRecord, AHalfWrittenOneReadsAsNothingJudged) {
  FakeRecord store;
  SlotRecord rec;
  rec.judged = true;
  std::snprintf(rec.version, sizeof(rec.version), "0.8.0+1a2b3c4");
  std::snprintf(rec.sha256hex, sizeof(rec.sha256hex),
                "4f2a0000000000000000000000000000000000000000000000000000000000f0");
  std::snprintf(rec.base, sizeof(rec.base), "/agent/v1/firmware/12");
  rec.size = 1446640;
  ASSERT_TRUE(mm::update::saveRecord(store, rec));

  // Each of the four parts gone on its own, and the state left behind: nothing judged every time.
  for (const char* key : {mm::update::kRecordKeyVersion, mm::update::kRecordKeySha,
                          mm::update::kRecordKeyLength, mm::update::kRecordKeyBase}) {
    FakeRecord broken = store;
    broken.kv.erase(key);
    EXPECT_FALSE(mm::update::loadRecord(broken).judged) << key;
  }
  // And a state that is not the one word.
  FakeRecord odd = store;
  odd.kv[mm::update::kRecordKeyState] = "maybe";
  EXPECT_FALSE(mm::update::loadRecord(odd).judged);
  // A length that is not a decimal number, and 0.
  for (const char* bad : {"0", "-1", "12 34", "0x10", ""}) {
    FakeRecord odd2 = store;
    odd2.kv[mm::update::kRecordKeyLength] = bad;
    EXPECT_FALSE(mm::update::loadRecord(odd2).judged) << "[" << bad << "]";
  }
}

TEST(TheRecord, TheLineSaysWhatTheSlotHoldsOrThatItHoldsNothing) {
  char line[200];
  mm::update::renderRecordLine(SlotRecord(), line, sizeof(line));
  EXPECT_EQ(std::string("holds nothing this firmware will boot"), std::string(line));

  SlotRecord rec;
  rec.judged = true;
  std::snprintf(rec.version, sizeof(rec.version), "0.8.0+1a2b3c4");
  std::snprintf(rec.sha256hex, sizeof(rec.sha256hex),
                "4f2a0000000000000000000000000000000000000000000000000000000000f0");
  std::snprintf(rec.base, sizeof(rec.base), "/agent/v1/firmware/12");
  rec.size = 1446640;
  mm::update::renderRecordLine(rec, line, sizeof(line));
  EXPECT_NE(nullptr, std::strstr(line, "0.8.0+1a2b3c4")) << line;
  EXPECT_NE(nullptr, std::strstr(line, "1446640 bytes")) << line;
  EXPECT_NE(nullptr, std::strstr(line, "4f2a000000000000")) << line;
  EXPECT_NE(nullptr, std::strstr(line, "/agent/v1/firmware/12")) << line;
}

// ---------------------------------------------------------------------------------------------
// The two small parsers
// ---------------------------------------------------------------------------------------------

TEST(ContentRange, ReadsWhatSectionFourSends) {
  uint32_t a = 0, b = 0, t = 0;
  ASSERT_TRUE(mm::update::parseContentRange("bytes 1376256-1441791/1446640", a, b, t));
  EXPECT_EQ(1376256u, a);
  EXPECT_EQ(1441791u, b);
  EXPECT_EQ(1446640u, t);

  ASSERT_TRUE(mm::update::parseContentRange("bytes 0-0/1", a, b, t));
  EXPECT_EQ(0u, a);
  EXPECT_EQ(0u, b);
  EXPECT_EQ(1u, t);

  // And nothing else, a 416's own `bytes */length` most of all: it says nothing about what was
  // asked for, so a reader that took it for a range would believe a length it never got.
  for (const char* bad : {"bytes */1446640", "bytes 0-1023", "0-1023/1446640", "items 0-1/2",
                          "bytes 1023-0/1446640", "bytes 0-1023/1446640 and more", "", "bytes "}) {
    EXPECT_FALSE(mm::update::parseContentRange(bad, a, b, t)) << "[" << bad << "]";
  }
}

TEST(ReleaseId, IsDigitsAndNothingElse) {
  for (const char* good : {"1", "7", "12", "1234567890"}) {
    EXPECT_TRUE(mm::update::releaseIdIsSane(good)) << good;
  }
  for (const char* bad : {"", "0", "007", "12a", "-1", "1 2", "12345678901", "../6", "7/files",
                          "7?x=1", "7%2e"}) {
    EXPECT_FALSE(mm::update::releaseIdIsSane(bad)) << "[" << bad << "]";
  }
}

TEST(ReleaseId, BecomesThePathSectionFourNames) {
  char base[mm::update::kMaxBaseLen + 1];
  ASSERT_TRUE(mm::update::baseForRelease("12", base, sizeof(base)));
  EXPECT_EQ(std::string("/agent/v1/firmware/12"), std::string(base));
  EXPECT_FALSE(mm::update::baseForRelease("../6", base, sizeof(base)));
  char tiny[8];
  EXPECT_FALSE(mm::update::baseForRelease("12", tiny, sizeof(tiny)));
}

TEST(TheWords, AreTheOnesTaskSixtyOneWillPost) {
  EXPECT_EQ(std::string("fetched"), std::string(mm::update::endingWord(Ending::Fetched)));
  EXPECT_EQ(std::string("fetch"), std::string(mm::update::endingWord(Ending::Fetch)));
  EXPECT_EQ(std::string("check"), std::string(mm::update::endingWord(Ending::Check)));
  EXPECT_EQ(std::string("slot"), std::string(mm::update::endingWord(Ending::Slot)));
  EXPECT_EQ(std::string("running"), std::string(mm::update::endingWord(Ending::Running)));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// ---------------------------------------------------------------------------------------------
// Why a range stopped, and what a failure is (task p1.19-68, deliverables 4 and 5)
// ---------------------------------------------------------------------------------------------

// Aarhus C's own shape, at the size the server counted: 262 144 bytes in ranges of 64 kB, the
// third of them stopping at 48 208 - three TLS records to the byte - with an error from the
// client and 47 092 as the largest free block, which is what that board had at rest on
// 20 November. Nothing of the fixture release is 262 kB, so the manifest's `ota` entry is
// rewritten to a file this test makes and signed again with the published test key: checks 1 to
// 4 pass over it and the descriptor refuses it at the end, which is not what this test is about.
struct BigBench {
  ScriptedServer server;
  FakeSlot slot;
  FakeRecord record;
  CapturedText out;
  Fetcher fetcher;
  std::vector<uint8_t> file;
  std::string sha;
  std::string path;

  static const uint32_t kSize = 4u * 65536u;  // 262 144: the bytes the server counted for Aarhus C

  BigBench() {
    file.resize(kSize);
    uint32_t x = 0x1a2b3c4du;
    for (size_t i = 0; i < file.size(); i++) {
      x = x * 1664525u + 1013904223u;
      file[i] = static_cast<uint8_t>(x >> 24);
    }
    sha = sha256Hex(file);

    std::string manifest = text(release("manifest.json"));
    const std::string oldSha = sha256Hex(release("meshmaster-agent-heltec_v3-v0.0.0-test.bin"));
    const size_t at = manifest.find(oldSha);
    EXPECT_NE(std::string::npos, at);
    manifest.replace(at, oldSha.size(), sha);
    // The `ota` entry's size, which is the first "size": 4096 of the file above it.
    const size_t sizeAt = manifest.find("\"size\": 4096");
    EXPECT_NE(std::string::npos, sizeAt);
    manifest.replace(sizeAt, std::strlen("\"size\": 4096"), "\"size\": " + std::to_string(kSize));
    serveManifest(server, manifest);

    path = std::string(kBase) + "/files/" + sha;
    server.files[path] = file;
    fetcher.begin(server, slot, record, mm::update::trustedKeys(), mm::update::trustedKeyCount(), "heltec_v3",
                  &out);
    Fetcher::Limits limits;
    limits.rangeBytes = 65536;
    fetcher.setLimits(limits);
  }

  const FetchResult& run() {
    fetcher.start(kBase, 1000);
    for (int i = 0; i < 400 && fetcher.running(); i++) fetcher.step(1000 + 10 * (i + 1));
    EXPECT_FALSE(fetcher.running()) << "the fetch never ended";
    return fetcher.result();
  }
};

TEST(WhyARangeStopped, TheLineNamesTheReasonAndTheNextRequestAsksFromTheByteAfterTheLastKept) {
  BigBench b;
  ScriptedServer::Twist stop;
  stop.deliver = 48208;
  stop.bodyReturnKnown = true;
  stop.bodyReturn = -5;        // HTTPC_ERROR_CONNECTION_LOST
  stop.tlsError = -0x7F00;     // MBEDTLS_ERR_SSL_ALLOC_FAILED
  stop.outOfMemory = true;
  stop.largestFreeBlock = 47092;
  b.server.byFileRequest[2] = stop;

  const FetchResult& r = b.run();

  // Deliverable 4: the line says what the client returned, the TLS layer's own error and the
  // largest free block of that moment - all three of which Aarhus C's journal could not say.
  EXPECT_NE(std::string::npos, b.out.all.find("stopped at 48208 of 65536 bytes")) << b.out.all;
  EXPECT_NE(std::string::npos, b.out.all.find("client -5")) << b.out.all;
  EXPECT_NE(std::string::npos, b.out.all.find("tls -0x7f00")) << b.out.all;
  EXPECT_NE(std::string::npos, b.out.all.find("block 47092")) << b.out.all;
  EXPECT_NE(std::string::npos, b.out.all.find("no memory")) << b.out.all;

  // Deliverable 5: it brought bytes that were kept, so the tries are where they were, and the
  // next request asks from the byte after the last one kept - 131 072 + 48 208.
  EXPECT_EQ(0u, r.failures);
  EXPECT_EQ(0u, r.failuresInARow);
  EXPECT_EQ(1u, r.shortRanges);
  ASSERT_LE(6u, b.server.log.size());
  EXPECT_EQ("bytes=131072-196607", b.server.log[4].range);
  EXPECT_EQ("bytes=179280-244815", b.server.log[5].range) << "from 131072 + 48208";
  EXPECT_EQ(179280u, r.resumedFrom);
}

TEST(WhyARangeStopped, TenShortRangesInARowStillFetchTheFileAndJudgeIt) {
  // Every range stops at a fifth of what was asked for and brings it: ten of them in a row, and
  // the file is fetched and judged all the same. Ranges of 512 over the fixture's 4 096 bytes,
  // delivering 400 at a time, is eleven requests and no failure at all.
  Bench b("heltec_v3", 512);
  ScriptedServer::Twist bitAtATime;
  bitAtATime.deliver = 400;
  for (uint32_t i = 0; i < 10; i++) b.server.byFileRequest[i] = bitAtATime;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << static_cast<int>(r.check) << ": " << r.why;
  EXPECT_EQ(0u, r.failures) << "not one of them brought nothing";
  EXPECT_EQ(10u, r.shortRanges);
  EXPECT_EQ(b.ota, b.slot.bytes) << "and what the slot holds is the file";
  EXPECT_EQ(1, b.slot.finished);
  EXPECT_TRUE(mm::update::loadRecord(b.record).judged);
}

TEST(WhyARangeStopped, ThreeRequestsInARowThatBringNothingEndTheFetchAndTheSentenceSaysThree) {
  Bench b;
  ScriptedServer::Twist nothing;
  nothing.deliverNothing = true;
  nothing.bodyReturnKnown = true;
  nothing.bodyReturn = -11;  // HTTPC_ERROR_READ_TIMEOUT
  nothing.largestFreeBlock = 47092;
  for (uint32_t i = 0; i < 3; i++) b.server.byFileRequest[i] = nothing;

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Fetch, r.ending);
  EXPECT_EQ(3u, r.failures);
  EXPECT_EQ(3u, r.failuresInARow);
  EXPECT_EQ(3u, r.fileRequests);
  EXPECT_NE(nullptr, std::strstr(r.why, "brought nothing")) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "client -11")) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "block 47092")) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "(3 failure(s), which is all this fetch gets)")) << r.why;
  // And all of the reason fits the 96 characters an ask's journal note is, with the count of
  // failures at the end being what a long one loses rather than the reason itself.
  const char* const block = std::strstr(r.why, "block 47092");
  ASSERT_NE(nullptr, block);
  EXPECT_GE(mm::update::kMaxNoteLen, static_cast<size_t>(block - r.why) + std::strlen("block 47092"))
      << "the reason must be inside a journal note's bound: " << r.why;
  EXPECT_EQ(1, b.slot.spoiled);
}

TEST(WhyARangeStopped, AFailureThatARangeBroughtBytesAfterDoesNotCountTowardsTheThree) {
  // Nothing, bytes, nothing, bytes, nothing: five failures would have ended three fetches under
  // the old rule, and here the run is cleared twice and the file is fetched.
  Bench b("heltec_v3", 1024);
  ScriptedServer::Twist nothing;
  nothing.deliverNothing = true;
  b.server.byFileRequest[0] = nothing;
  b.server.byFileRequest[2] = nothing;
  b.server.byFileRequest[4] = nothing;
  b.server.byFileRequest[6] = nothing;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << static_cast<int>(r.check) << ": " << r.why;
  EXPECT_EQ(4u, r.failures) << "four requests brought nothing, over the whole fetch";
  EXPECT_EQ(0u, r.failuresInARow) << "and never three of them in a row";
  EXPECT_EQ(b.ota, b.slot.bytes);
}

TEST(WhyARangeStopped, TheBoundOnOneFilesRequestsEndsAFetchThatIsGettingNowhere) {
  // One byte a range, for ever: no request brings nothing, so no try is ever spent, and the only
  // thing that ends it is deliverable 5's bound - twice the file's ranges plus the tries. The
  // fixture is 4 096 bytes in ranges of 1 024: four ranges, so eight plus three is eleven.
  Bench b("heltec_v3", 1024);
  ScriptedServer::Twist oneByte;
  oneByte.deliver = 1;
  for (uint32_t i = 0; i < 40; i++) b.server.byFileRequest[i] = oneByte;

  const FetchResult& r = b.run();
  EXPECT_EQ(Ending::Fetch, r.ending) << r.why;
  EXPECT_EQ(11u, r.fileRequestBound);
  EXPECT_EQ(11u, r.fileRequests);
  EXPECT_EQ(0u, r.failures);
  EXPECT_NE(nullptr, std::strstr(r.why, "cost 11 request(s) for 4 range(s)")) << r.why;
  EXPECT_NE(nullptr, std::strstr(r.why, "all one file gets")) << r.why;
  EXPECT_EQ(1, b.slot.spoiled) << "and what is in the slot will not boot";
}

TEST(WhyARangeStopped, DescribeStopSaysTheThreeNumbersAndNothingWhenThereAreNone) {
  char line[112];
  mm::update::HttpAnswer a;
  mm::update::describeStop(a, line, sizeof(line));
  EXPECT_STREQ("nothing said why", line);

  a.bodyReturnKnown = true;
  a.bodyReturn = -5;
  a.tlsError = -0x7F00;
  a.largestFreeBlock = 47092;
  a.outOfMemory = true;
  mm::update::describeStop(a, line, sizeof(line));
  EXPECT_STREQ("client -5, tls -0x7f00, block 47092, no memory", line);

  mm::update::HttpAnswer plain;
  plain.largestFreeBlock = 100000;
  mm::update::describeStop(plain, line, sizeof(line));
  EXPECT_STREQ("block 100000", line);
}

// ---------------------------------------------------------------------------------------------
// The observers stand down for a fetch (task p1.19-68, deliverables 1, 2 and 3)
// ---------------------------------------------------------------------------------------------

namespace {

// The seam as a counter, for the order of things.
class CountedObservers final : public mm::update::Observers {
 public:
  int downs = 0;
  int ups = 0;
  bool lastSwitchFollowed = false;
  bool down = false;
  mutable uint32_t stillUpFor = 0;  // this many looks answer "not yet"
  int requestsWhenDown = -1;        // the server's request count at standDown()
  const ScriptedServer* server = nullptr;

  void standDown() override {
    downs++;
    down = true;
    if (server != nullptr) requestsWhenDown = static_cast<int>(server->log.size());
  }
  bool allDown() const override {
    if (stillUpFor > 0) {
      stillUpFor--;
      return false;
    }
    return down;
  }
  void standUp(bool aSwitchFollows) override {
    ups++;
    lastSwitchFollowed = aSwitchFollows;
    down = false;
  }
};

}  // namespace

TEST(TheObserversStandDown, TheyAreClosedBeforeTheFirstRequestAndOpenAgainAtTheEnd) {
  CountedObservers obs;
  Bench b("heltec_v3", 1024, 3, &obs);
  obs.server = &b.server;

  const FetchResult& r = b.run();
  ASSERT_EQ(Ending::Fetched, r.ending) << r.why;
  EXPECT_EQ(1, obs.downs);
  EXPECT_EQ(0, obs.requestsWhenDown) << "not one request had left when the slots were told to go";
  EXPECT_EQ(1, obs.ups);
  EXPECT_TRUE(obs.lastSwitchFollowed) << "a fetched ending is the one a switch follows";
  EXPECT_NE(std::string::npos, b.out.all.find("the observer's slots stand down before the first request"))
      << b.out.all;
}

TEST(TheObserversStandDown, ARefusedFetchOpensThemAgainWithNoSwitchToWaitFor) {
  CountedObservers obs;
  Bench b("heltec_v3", 1024, 3, &obs);
  b.server.files.erase(std::string(kBase) + "/manifest.json");

  EXPECT_EQ(Ending::Fetch, b.run().ending);
  EXPECT_EQ(1, obs.downs);
  EXPECT_EQ(1, obs.ups);
  EXPECT_FALSE(obs.lastSwitchFollowed);
  EXPECT_NE(std::string::npos, b.out.all.find("open again by their ordinary rule")) << b.out.all;
}

TEST(TheObserversStandDown, TheFirstRequestWaitsForTheSessionsToBeGone) {
  CountedObservers obs;
  obs.stillUpFor = 3;  // three passes of the loop before the sessions are really gone
  Bench b("heltec_v3", 1024, 3, &obs);
  obs.server = &b.server;

  b.fetcher.start(kBase, 1000);
  EXPECT_EQ(1, obs.downs);
  EXPECT_TRUE(b.fetcher.running());
  for (int i = 0; i < 3; i++) {
    b.fetcher.step(1000 + 10 * (i + 1));
    EXPECT_TRUE(b.server.log.empty()) << "a request left while a slot was still up, at pass " << i;
  }
  b.fetcher.step(1040);
  EXPECT_EQ(1u, b.server.log.size()) << "and the manifest is asked for the moment they are gone";
  for (int i = 0; i < 200 && b.fetcher.running(); i++) b.fetcher.step(1050 + 10 * i);
  EXPECT_EQ(Ending::Fetched, b.fetcher.result().ending);
  EXPECT_EQ(40u, b.fetcher.result().standDownMs) << "what the wait cost, in the result";
}

TEST(TheObserversStandDown, SlotsThatWillNotGoDoNotHoldAFetchUpForEver) {
  CountedObservers obs;
  obs.stillUpFor = 0xFFFFFFFFu;  // a client that never closes
  Bench b("heltec_v3", 1024, 3, &obs);

  b.fetcher.start(kBase, 0);
  for (int i = 0; i < 400 && b.fetcher.running(); i++) b.fetcher.step(100u * static_cast<uint32_t>(i + 1));
  EXPECT_EQ(Ending::Fetched, b.fetcher.result().ending) << "a release that can be had is had";
  EXPECT_LE(mm::update::kStandDownMs, b.fetcher.result().standDownMs);
  EXPECT_NE(std::string::npos, b.out.all.find("were still up after")) << b.out.all;
}

TEST(TheObserversStandDown, AFetchThatWouldNotEvenBeginStandsNobodyDown) {
  CountedObservers obs;
  Bench b("heltec_v3", 1024, 3, &obs);
  EXPECT_FALSE(b.fetcher.start("not-a-path", 1000));
  EXPECT_EQ(0, obs.downs);
  EXPECT_EQ(0, obs.ups);
}

// ---- and the same, with the observer's own two slots and their scripted clients ---------------

namespace {

using mm::observer::ClientEvent;
using mm::observer::ErrorKind;
using mm::observer::SlotState;

uint32_t g_slotMillis = 1000;
uint32_t g_slotMillisNow() { return g_slotMillis; }
uint32_t g_slotEpoch() { return 1793016000u; }
uint32_t g_slotFree() { return 200u * 1024u; }
uint32_t g_slotLargest() { return 100u * 1024u; }
uint32_t g_slotRandom() { return 0; }

// test/test_connection's fake, cut down to what these four tests read: a client that opens and
// closes when it is told to and reports what the test says the broker did.
class FakeMqtt final : public mm::observer::Client {
 public:
  int opens = 0;
  int closes = 0;
  bool isOpen = false;
  bool accepted = false;

  bool open(const mm::observer::ClientConfig&, char*, size_t) override {
    opens++;
    isOpen = true;
    return true;
  }
  void close() override {
    if (isOpen) closes++;
    isOpen = false;
    accepted = false;
  }
  bool publish(const char*, const char*, size_t, uint8_t, bool) override { return isOpen; }
  ClientEvent takeEvent(ErrorKind& kind, char* reason, size_t cap) override {
    kind = ErrorKind::Other;
    if (cap > 0) reason[0] = '\0';
    if (accepted) {
      accepted = false;
      return ClientEvent::Connected;
    }
    return ClientEvent::None;
  }
  size_t outboxBytes() override { return 0; }
  void accept() { accepted = true; }
};

struct FixedSigner final : public mm::observer::Signer {
  uint8_t pub[32];
  uint8_t prv[64];
  FixedSigner() {
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = static_cast<uint8_t>(0x5A ^ (i * 7));
    ed25519_create_keypair(pub, prv, seed);
  }
  bool sign(const uint8_t* msg, size_t len, uint8_t sig[mm::observer::kSignatureSize]) override {
    ed25519_sign(sig, msg, len, pub, prv);
    return true;
  }
  std::string pubHex() const {
    char hex[65];
    mm::observer::hexUpper(pub, 32, hex, sizeof hex);
    return hex;
  }
};

struct FixedSlotStatus final : public mm::observer::StatusSource {
  size_t formatStatus(const char* status, char* out, size_t cap) override {
    return static_cast<size_t>(std::snprintf(out, cap, "{\"status\":\"%s\"}", status));
  }
};

// A fetch over the real slots: both of them connected, then started and driven one pass at a
// time with the slots' own loop() running before each step, as src/main.cpp runs them.
struct SlotsBench {
  FakeMqtt client[mm::observer::kSlots];
  FixedSigner signer;
  FixedSlotStatus status;
  mm::observer::Connections link;
  mm::observer::ObserverSettings settings;
  std::string pubHex;
  mm::update::ConnectionObservers observers{link};
  Bench fetch;

  explicit SlotsBench(const char* secondPreset = "meshview")
      : fetch("heltec_v3", 1024, 3, &observers) {
    g_slotMillis = 1000;
    pubHex = signer.pubHex();
    mm::observer::Client* const clients[mm::observer::kSlots] = {&client[0], &client[1]};
    mm::observer::Environment env;
    env.millis = &g_slotMillisNow;
    env.epoch = &g_slotEpoch;
    env.freeHeap = &g_slotFree;
    env.largestBlock = &g_slotLargest;
    env.random = &g_slotRandom;
    env.caPem = "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n";
    env.board = &mm::observer::kHeltecV3;
    link.begin(clients, env, signer, status, nullptr, nullptr);

    settings.iata = "AAR";
    settings.owner = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    settings.email = "owner@example.dk";
    settings.origin = "MeshMaster-TEST";
    settings.slot[0].preset = "meshview";
    settings.slot[1].preset = secondPreset;
    link.configure(settings, mm::observer::observerEnabled(settings, nullptr, 0), pubHex.c_str(), "meshcore/test");
    link.setLinkUp(true);
  }

  SlotState state(int i) const { return link.view(i).state; }

  // Both slots up, as Aarhus C had them on 20 November. One handshake at a time is this board's
  // rule, so slot 2 waits out a heap re-check before it gets its turn.
  void connectBoth() {
    link.loop();                       // slot 1 opens
    client[0].accept();
    link.loop();                       // slot 1 connected; slot 2 is waiting for its handshake
    g_slotMillis += mm::observer::kHeapRecheckMs + 1000;
    link.loop();                       // slot 2 opens
    client[1].accept();
    link.loop();                       // slot 2 connected
  }
};

}  // namespace

TEST(TheSlotsThemselves, BothAreClosedBeforeTheFirstRequestAndSayTheirOwnWord) {
  SlotsBench b;
  b.connectBoth();
  ASSERT_EQ(SlotState::Connected, b.state(0));
  ASSERT_EQ(SlotState::Connected, b.state(1));

  b.fetch.fetcher.start(kBase, 1000);
  EXPECT_TRUE(b.fetch.server.log.empty()) << "nothing has been asked for yet";
  EXPECT_EQ(SlotState::StandingDown, b.state(0));
  EXPECT_EQ(SlotState::StandingDown, b.state(1));
  EXPECT_EQ(1, b.client[0].closes);
  EXPECT_EQ(1, b.client[1].closes);
  EXPECT_TRUE(b.link.allSlotsDown());
  EXPECT_TRUE(b.link.standingDownForFetch());

  // Deliverable 2: a word of its own, and `get mqtt.status` prints it.
  EXPECT_STREQ("standing-down", mm::observer::slotStateName(b.state(0)));
  char line[400];
  b.link.describe(0, line, sizeof line);
  EXPECT_NE(nullptr, std::strstr(line, "standing down")) << line;
  EXPECT_NE(nullptr, std::strstr(line, "a release being fetched")) << line;
  // And not *gave way*: nothing went wrong.
  EXPECT_EQ(0u, b.link.view(0).stats.heapYields);
  EXPECT_EQ(0u, b.link.view(1).stats.heapYields);
}

TEST(TheSlotsThemselves, NoSlotOpensWhileTheFetchRuns) {
  SlotsBench b;
  b.connectBoth();
  const int opens0 = b.client[0].opens;
  const int opens1 = b.client[1].opens;

  b.fetch.fetcher.start(kBase, 1000);
  for (int i = 0; i < 200 && b.fetch.fetcher.running(); i++) {
    b.link.loop();  // src/main.cpp runs the observer before the fetch's step, every pass
    g_slotMillis += 10;
    b.fetch.fetcher.step(1000 + 10u * static_cast<uint32_t>(i + 1));
    if (b.fetch.fetcher.running()) {
      EXPECT_EQ(SlotState::StandingDown, b.state(0)) << "pass " << i;
      EXPECT_EQ(SlotState::StandingDown, b.state(1)) << "pass " << i;
    }
  }
  ASSERT_EQ(Ending::Fetched, b.fetch.fetcher.result().ending) << b.fetch.fetcher.result().why;
  EXPECT_EQ(opens0, b.client[0].opens) << "slot 1 never opened during the fetch";
  EXPECT_EQ(opens1, b.client[1].opens) << "slot 2 never opened during the fetch";
}

TEST(TheSlotsThemselves, ARefusedFetchOpensThemBothAgainWithoutWaitingOutAHold) {
  SlotsBench b;
  b.connectBoth();
  const int opens0 = b.client[0].opens;
  b.fetch.server.files.erase(std::string(kBase) + "/manifest.json");

  b.fetch.fetcher.start(kBase, 1000);
  for (int i = 0; i < 200 && b.fetch.fetcher.running(); i++) {
    b.link.loop();
    g_slotMillis += 10;
    b.fetch.fetcher.step(1000 + 10u * static_cast<uint32_t>(i + 1));
  }
  ASSERT_EQ(Ending::Fetch, b.fetch.fetcher.result().ending);
  EXPECT_FALSE(b.link.standingDownForFetch());

  // At once: the next pass of the slots' own loop opens slot 1, with no ten minutes to wait out.
  b.link.loop();
  EXPECT_EQ(SlotState::Connecting, b.state(0));
  EXPECT_EQ(opens0 + 1, b.client[0].opens);
  b.client[0].accept();
  b.link.loop();
  EXPECT_EQ(SlotState::Connected, b.state(0));
  // And slot 2 after it, by the same one-handshake-at-a-time rule it opened under the first time.
  g_slotMillis += mm::observer::kHeapRecheckMs + 1000;
  b.link.loop();
  ASSERT_EQ(SlotState::Connecting, b.state(1));
  b.client[1].accept();
  b.link.loop();
  EXPECT_EQ(SlotState::Connected, b.state(1));
  // And neither gave way: the slots stood down, they did not lose.
  EXPECT_EQ(0u, b.link.view(0).stats.heapYields);
  EXPECT_EQ(0u, b.link.view(1).stats.heapYields);
}

TEST(TheSlotsThemselves, AFetchedReleaseAndTheSwitchThatFollowsOpensNothing) {
  SlotsBench b;
  b.connectBoth();
  const int opens0 = b.client[0].opens;
  const int opens1 = b.client[1].opens;

  b.fetch.fetcher.start(kBase, 1000);
  for (int i = 0; i < 200 && b.fetch.fetcher.running(); i++) {
    b.link.loop();
    g_slotMillis += 10;
    b.fetch.fetcher.step(1000 + 10u * static_cast<uint32_t>(i + 1));
  }
  ASSERT_EQ(Ending::Fetched, b.fetch.fetcher.result().ending) << b.fetch.fetcher.result().why;

  // The switch is the next pass or two of the loop and then the board restarts. Nothing opens in
  // the meantime: a session the restart would throw away is not worth a handshake.
  for (int i = 0; i < 50; i++) {
    b.link.loop();
    g_slotMillis += 100;
    EXPECT_EQ(SlotState::StandingDown, b.state(0)) << "pass " << i;
    EXPECT_EQ(SlotState::StandingDown, b.state(1)) << "pass " << i;
  }
  EXPECT_EQ(opens0, b.client[0].opens);
  EXPECT_EQ(opens1, b.client[1].opens);

  // And a board whose switch never came has them back a minute later rather than needing a reboot.
  g_slotMillis += mm::observer::kFetchedStandDownMs;
  b.link.loop();
  EXPECT_EQ(SlotState::Connecting, b.state(0));
}

TEST(TheSlotsThemselves, ASlotWhosePresetIsNoneStaysOffThroughAllOfIt) {
  SlotsBench b("none");
  b.link.loop();
  b.client[0].accept();
  b.link.loop();
  ASSERT_EQ(SlotState::Connected, b.state(0));
  ASSERT_EQ(SlotState::Off, b.state(1));

  b.fetch.fetcher.start(kBase, 1000);
  EXPECT_EQ(SlotState::StandingDown, b.state(0));
  EXPECT_EQ(SlotState::Off, b.state(1)) << "off is off: a fetch does not stand down what is not up";
  for (int i = 0; i < 200 && b.fetch.fetcher.running(); i++) {
    b.link.loop();
    g_slotMillis += 10;
    b.fetch.fetcher.step(1000 + 10u * static_cast<uint32_t>(i + 1));
    EXPECT_EQ(SlotState::Off, b.state(1)) << "pass " << i;
  }
  ASSERT_EQ(Ending::Fetched, b.fetch.fetcher.result().ending);
  for (int i = 0; i < 20; i++) {
    b.link.loop();
    g_slotMillis += 10000;
    EXPECT_EQ(SlotState::Off, b.state(1)) << "after, pass " << i;
  }
  EXPECT_EQ(0, b.client[1].opens);
}
