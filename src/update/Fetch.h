// Fetching a release into the other application slot, and judging what was written (task p1.15-59).
//
// Task 57 gave a board a second slot and task 58 gave the firmware a reader of a release
// (update/Release.h). This joins them: given a release's `base` on the configured server, the
// manifest and its signature are fetched and checked (1, 2, 3) **before one byte of an image is
// asked for**; then the image's `ota` file is fetched in ranges straight into the slot that is not
// running, and what the slot now holds is judged from the bytes themselves (4, 5, and the
// ESP-IDF's own verdict). At the end the other slot holds a judged image, or holds nothing this
// firmware will ever boot - a refused one has its first sector erased where it lies.
//
// **Nothing is switched here.** The running firmware and the boot choice are as they were; task 60
// is what switches to a judged image and proves it on trial.
//
// The four routes are meshmaster.dk's `docs/dev/unit-protocol.md` §4, and what matters of them:
//
//   {base}/manifest.json       the bytes as they were signed
//   {base}/manifest.json.sig   the one `ed25519 <key id> <base64>` line
//   {base}/files/{sha256}      one file, streamed, by its manifest name or its SHA-256
//
//   * nothing on them is ever a redirect, and this firmware follows none: a 3xx ends the fetch;
//   * every answer carries a strong `ETag` (the quoted SHA-256 of the content) and
//     `Accept-Ranges: bytes`; a `Range` is answered `206` with `Content-Range: bytes a-b/length`;
//   * the tag seen on the first range goes out as `If-Range` on every range after it, and a
//     **`200` to a range means start again from zero** - the file under that URL changed, or the
//     server ignored the range. The slot is then rewritten from zero and never spliced;
//   * one 404 for everything: a release that is not there, one this board may not see, a file the
//     release has not. It is not *withdrawn* and it is not asked about.
//
// **The loop stays alive.** A file of 1.4 MB is fetched in pieces from the loop: `step()` makes at
// most one request and returns, so the mesh, the console, the observer and the watchdog all get
// their turn between two of them (src/Health.h). Nothing is held in RAM but one range's bytes on
// their way through, and the manifest and its signature, which are bounded here.
//
// Everything in this file is portable and host-tested (test/test_fetch): the server is behind
// ReleaseHttp, the slot behind SlotSink and the record behind RecordStore, and the board's three
// implementations are src/update/FetchSeams.h and src/platform/OtaSlot.h. **The token is not
// here**: ReleaseHttp's implementation is what adds the Authorization header, as it does for every
// other call this firmware makes (src/agent/AgentClient.cpp has the one place that builds it).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Release.h"
#include "SlotRecord.h"

namespace mm {
class TextOut;
}

namespace mm {
namespace update {

// -- the numbers, each a sentence ---------------------------------------------------------------

// One range. Every range is a request on the kept connection (no handshake), so a smaller range
// costs round trips and a larger one costs more when a connection drops halfway. 64 kB is 23
// ranges for the 1.4 MB this firmware is today, and one of them is a fraction of a second of
// WiFi. The bench's own `update fetch` is what settles it (tasks/phase-1.15/FOLLOWUPS.md).
static const uint32_t kRangeBytes = 64u * 1024u;

// How many times a file may fail before the fetch does - **three in all**, not three per range:
// a link that drops three times during one file is a link that is not working, and the next
// `update fetch` begins again.
static const uint32_t kFileTries = 3;

// The manifest and its signature are the only two things read into RAM. Our manifest with two
// boards and two files each is about 1.6 kB; four kilobytes is room for twice that and a bound
// that is refused in a sentence rather than overrun.
static const size_t kMaxManifestBytes = 4096;
static const size_t kMaxSignatureBytes = 160;

// A release's `base`: `/agent/v1/firmware/<release id>`, which is what the server's offer carries
// (its `FirmwareOfferPayload.json`). A path on the configured server, never a URL of its own.
static const size_t kMaxBaseLen = 64;

// What a header this firmware reads may be. An `ETag` is the quoted SHA-256 of the content, 66
// characters; `Content-Range: bytes 1376256-1441791/1446640` is under 40.
static const size_t kMaxEtagLen = 96;
static const size_t kMaxContentRangeLen = 64;

// -- the three seams ---------------------------------------------------------------------------

// One request's worth of what this engine decides. `path` is a path on the configured server;
// the origin, the token, `X-Agent-Version` and the verified chain are the implementation's.
struct HttpRequest {
  const char* path = "";
  bool hasRange = false;
  uint32_t from = 0;  // inclusive
  uint32_t to = 0;    // inclusive
  const char* ifRange = nullptr;  // the ETag as it was seen, quotes and all, or nullptr
};

// The answer's status line and the two headers this firmware acts on, **unparsed**: parsing them
// is this file's own (parseContentRange), so that a host test holds the parser and not a second
// copy of it written into the board's implementation.
struct HttpAnswer {
  bool reachable = false;  // a status line came back at all
  int status = 0;
  uint32_t bodyLen = 0;  // bytes that reached the body
  char etag[kMaxEtagLen + 1] = {0};
  char contentRange[kMaxContentRangeLen + 1] = {0};
  // When !reachable: what the transport said, never a secret. The same length as a Call's error
  // (agent/IUplink.h), which is where the board's implementation copies it from.
  char error[128] = {0};
};

// Where an answer's bytes go as they arrive.
class HttpBody {
 public:
  virtual ~HttpBody() {}
  // The status line and the headers are in and the body has not started. False refuses the body,
  // which the transport then drops rather than delivering.
  virtual bool begin(const HttpAnswer& head) = 0;
  // The next bytes, in order. False stops the answer.
  virtual bool take(const uint8_t* data, size_t len) = 0;
};

// One GET on the configured server. The implementation adds the Authorization header with the
// board's token, the `X-Agent-Version`, and verifies the certificate chain - this engine holds no
// secret and names no server. A 3xx is handed back as the status it is and **never followed**.
class ReleaseHttp {
 public:
  virtual ~ReleaseHttp() {}
  virtual bool get(const HttpRequest& req, HttpAnswer& out, HttpBody& body) = 0;
};

// The application slot that is not running. Offsets are implicit: bytes go in in order, and
// `held()` is how many of them are there - which is where a dropped range is asked for again
// from. A fetch does not survive a restart (out of scope), so `held()` is this fetch's own count.
class SlotSink {
 public:
  virtual ~SlotSink() {}

  // False when this board has no second application slot to write - the table word is `one` or
  // `other` (src/platform/PartitionTable.h). Then `why()` is the one line a person is given, and
  // the fetch refuses before any request is made.
  virtual bool available() const = 0;
  virtual const char* why() const = 0;

  virtual uint32_t capacity() const = 0;

  // Open a write for a file of `expected` bytes. The slot's old contents go: whatever was judged
  // before is no longer there, which is why the record is cleared at the same moment.
  virtual bool begin(uint32_t expected) = 0;
  virtual uint32_t held() const = 0;
  virtual bool write(const uint8_t* data, size_t len) = 0;

  // Throw away what is held and begin again at zero: what a `200` to a range means.
  virtual bool restart() = 0;

  // The ESP-IDF's own verdict on the image now in the slot, and the end of the write.
  virtual bool finish() = 0;

  // Make what lies in the slot unbootable: erase its first sector. Every refusal calls this.
  virtual bool spoil() = 0;

  // Read back what was written, for checks 4 and 5. False past the end or when the medium refused.
  virtual bool read(uint32_t offset, uint8_t* out, uint32_t len) const = 0;
};

// -- what a fetch ends in ----------------------------------------------------------------------

// The word task 61 posts, and the word a person reads on the console.
enum class Ending : uint8_t {
  Running = 0,  // not an ending: a fetch is still going
  Fetched = 1,  // the slot holds a judged image
  Fetch = 2,    // the release could not be had: no link, a 404, a redirect, three failures
  Check = 3,    // the release was had and refused: `check` is 1 to 5
  Slot = 4,     // this board has no second application slot; nothing was asked for
};

const char* endingWord(Ending ending);

struct FetchResult {
  Ending ending = Ending::Running;
  uint8_t check = 0;  // 1 to 5 for Ending::Check, else 0
  char why[192] = {0};

  // Fetched: what the slot holds, and what it cost.
  char version[33] = {0};
  char sha256hex[2 * 32 + 1] = {0};
  uint32_t bytes = 0;  // the image's bytes in the slot

  // What a person reads at the end of `update fetch`, and what a report is written from.
  uint32_t requests = 0;      // every request this fetch made
  uint32_t fileRequests = 0;  // of those, requests for a file
  uint32_t failures = 0;      // attempts that failed and were counted against kFileTries
  uint32_t restarts = 0;      // times a `200` to a range sent the slot back to zero
  uint32_t resumedFrom = 0;   // what the slot held when the first retry asked again
  int lastStatus = 0;
  bool ifRangeSent = false;
  uint32_t elapsedMs = 0;
};

// -- the fetch ---------------------------------------------------------------------------------

class Fetcher {
 public:
  // One range's size and the tries a file gets, so that a test can drive a four-kilobyte release
  // through more than one range. The defaults are the constants above and are what the board uses.
  struct Limits {
    uint32_t rangeBytes = kRangeBytes;
    uint32_t tries = kFileTries;
  };

  // `board` is what this board calls itself (mm::boardName()); `out` may be nullptr, and with one
  // each step prints a line as it ends. Nothing printed here is a token, a header or a byte of a
  // secret.
  void begin(ReleaseHttp& http, SlotSink& slot, RecordStore& record, const TrustedKey* keys,
             size_t keyCount, const char* board, TextOut* out);

  void setLimits(const Limits& limits) { limits_ = limits; }

  // Start a fetch of `base` (`/agent/v1/firmware/7`). False when it did not start at all - no
  // second slot, or a base this firmware will not ask for - and then `result()` holds the ending
  // and its word. True when it started: `step()` carries it on. A fetch already running is left
  // alone and false is the only answer.
  bool start(const char* base, uint32_t nowMs);

  bool running() const { return step_ != Step::Idle && step_ != Step::Ended; }

  // One piece of work: **at most one request**, and then back to the loop.
  void step(uint32_t nowMs);

  const FetchResult& result() const { return result_; }
  const ReleaseInfo& info() const { return info_; }

 private:
  enum class Step : uint8_t { Idle, Manifest, Signature, Judge, File, Judge45, Ended };

  class RangeBody;

  void stepManifest();
  void stepSignature();
  void stepJudge();
  void stepFile();
  void stepJudge45();

  // The statuses that end a fetch where they are - a redirect, a 404, a 401 or a 403. False when
  // the fetch has ended and there is nothing more to read of the answer.
  bool endableStatus(int status, const char* what);
  // A manifest's or a signature's answer: 200 and nothing else. False when the answer was not
  // taken, whether that counted a failure or ended the fetch.
  bool tookTheAnswer(const HttpAnswer& answer, const char* what);

  // An ending, with its word and its number. Every ending but *fetched* spoils a slot that was
  // opened for a write, so that nothing half written or refused can ever be booted.
  void end(Ending ending, uint8_t check, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
  // An attempt that failed and may be tried again: counted, and the fetch ends when the tries are
  // spent.
  void countFailure(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  ReleaseHttp* http_ = nullptr;
  SlotSink* slot_ = nullptr;
  RecordStore* record_ = nullptr;
  const TrustedKey* keys_ = nullptr;
  size_t keyCount_ = 0;
  const char* board_ = "";
  TextOut* out_ = nullptr;
  Limits limits_;

  Step step_ = Step::Idle;
  char base_[kMaxBaseLen + 1] = {0};
  char path_[kMaxBaseLen + 96] = {0};
  uint8_t manifest_[kMaxManifestBytes];
  size_t manifestLen_ = 0;
  char signature_[kMaxSignatureBytes + 1] = {0};
  size_t signatureLen_ = 0;
  char etag_[kMaxEtagLen + 1] = {0};  // the first range's, for If-Range on the ones after it
  char shaHex_[2 * 32 + 1] = {0};     // the file's SHA-256, which is also how it is asked for

  ReleaseInfo info_;
  const ReleaseFile* file_ = nullptr;
  bool slotOpen_ = false;  // a write was opened: an ending that is not *fetched* spoils it
  uint32_t startedMs_ = 0;
  uint32_t lastMs_ = 0;
  FetchResult result_;
};

// `Content-Range: bytes a-b/total`, as §4's answer to a range carries it. The value without the
// header name. False for anything that is not exactly that shape - a `bytes */length` of a 416
// among them, which says nothing about what was asked for.
bool parseContentRange(const char* value, uint32_t& first, uint32_t& last, uint32_t& total);

// A release id as `update fetch` and task 61's ask carry it: digits, at least one, at most ten,
// and no leading zero unless it is the whole of it. Anything else is refused before a path is
// built from it, because that path goes on the wire.
bool releaseIdIsSane(const char* id);

// `/agent/v1/firmware/<id>` into `out`, or false when it does not fit.
bool baseForRelease(const char* id, char* out, size_t cap);

// A ReleaseBytes over a slot, for checks 4 and 5 over what was written (update/Release.h).
class SlotBytes : public ReleaseBytes {
 public:
  SlotBytes(const SlotSink& slot, uint32_t len) : slot_(slot), len_(len) {}
  uint32_t size() const override { return len_; }
  bool read(uint32_t offset, uint8_t* out, uint32_t len) const override;

 private:
  const SlotSink& slot_;
  uint32_t len_;
};

}  // namespace update
}  // namespace mm
