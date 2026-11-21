#include "Fetch.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <agent/TextOut.h>

namespace mm {
namespace update {

namespace {

void hexOf(const uint8_t* bytes, size_t len, char* out) {
  static const char* kDigits = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[2 * i] = kDigits[bytes[i] >> 4];
    out[2 * i + 1] = kDigits[bytes[i] & 0x0F];
  }
  out[2 * len] = '\0';
}

// A body into a fixed buffer: the manifest and its signature, the only two things this fetch
// holds in RAM. A body longer than the buffer is refused rather than cut, so that nothing is
// checked against half a manifest.
class BufferBody : public HttpBody {
 public:
  BufferBody(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}
  bool begin(const HttpAnswer& head) override { return head.status == 200; }
  bool take(const uint8_t* data, size_t len) override {
    if (len_ + len > cap_) {
      overflowed = true;
      return false;
    }
    memcpy(buf_ + len_, data, len);
    len_ += len;
    return true;
  }
  size_t length() const { return len_; }

  bool overflowed = false;

 private:
  uint8_t* buf_;
  size_t cap_;
  size_t len_ = 0;
};

}  // namespace

const char* endingWord(Ending ending) {
  switch (ending) {
    case Ending::Fetched:
      return "fetched";
    case Ending::Fetch:
      return "fetch";
    case Ending::Check:
      return "check";
    case Ending::Slot:
      return "slot";
    default:
      return "running";
  }
}

bool parseContentRange(const char* value, uint32_t& first, uint32_t& last, uint32_t& total) {
  if (value == nullptr) return false;
  while (*value == ' ') value++;
  static const char kUnit[] = "bytes ";
  if (strncmp(value, kUnit, sizeof(kUnit) - 1) != 0) return false;
  const char* p = value + sizeof(kUnit) - 1;
  while (*p == ' ') p++;

  char* end = nullptr;
  const unsigned long a = strtoul(p, &end, 10);
  if (end == p || *end != '-') return false;
  p = end + 1;
  const unsigned long b = strtoul(p, &end, 10);
  if (end == p || *end != '/') return false;
  p = end + 1;
  const unsigned long t = strtoul(p, &end, 10);
  if (end == p) return false;
  while (*end == ' ' || *end == '\r' || *end == '\n') end++;
  if (*end != '\0') return false;
  if (b < a) return false;

  first = static_cast<uint32_t>(a);
  last = static_cast<uint32_t>(b);
  total = static_cast<uint32_t>(t);
  return true;
}

void describeStop(const HttpAnswer& answer, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  // **Short words on purpose.** This phrase goes into the refusal a fetch ends with, and an
  // ask's journal note is 96 characters (update/UpdateRecord.h, the contract's bound on the
  // journal as a whole); a reason spelled out in full would be cut off the one line meshmaster.dk
  // shows a person. `docs/field.md` §9 is where each word is spelled out.
  size_t at = 0;
  const char* sep = "";
  // What the client returned, which is the thing nothing read before task 68.
  if (answer.bodyReturnKnown) {
    at += static_cast<size_t>(snprintf(out + at, cap - at, "client %ld",
                                       static_cast<long>(answer.bodyReturn)));
    sep = ", ";
  }
  // The TLS layer's own error, where it has one, in the notation mbedtls's own header uses for
  // it: -0x7f00 is what MBEDTLS_ERR_SSL_ALLOC_FAILED is called there.
  if (at < cap && answer.tlsError != 0) {
    const long e = static_cast<long>(answer.tlsError);
    at += static_cast<size_t>(snprintf(out + at, cap - at, "%stls %s0x%lx", sep, e < 0 ? "-" : "",
                                       e < 0 ? -e : e));
    sep = ", ";
  }
  // The largest allocation the heap could still make: what a TLS record's buffer had to fit in.
  if (at < cap && answer.largestFreeBlock != 0) {
    at += static_cast<size_t>(snprintf(out + at, cap - at, "%sblock %lu", sep,
                                       static_cast<unsigned long>(answer.largestFreeBlock)));
    sep = ", ";
  }
  if (at < cap && answer.outOfMemory) {
    at += static_cast<size_t>(snprintf(out + at, cap - at, "%sno memory", sep));
  }
  if (out[0] == '\0') snprintf(out, cap, "nothing said why");
}

bool releaseIdIsSane(const char* id) {
  if (id == nullptr) return false;
  const size_t n = strlen(id);
  if (n == 0 || n > 10) return false;
  for (size_t i = 0; i < n; i++) {
    if (id[i] < '0' || id[i] > '9') return false;
  }
  if (id[0] == '0') return false;  // no leading zero, and `0` is not a release
  return true;
}

bool baseForRelease(const char* id, char* out, size_t cap) {
  if (!releaseIdIsSane(id)) return false;
  const int n = snprintf(out, cap, "/agent/v1/firmware/%s", id);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool SlotBytes::read(uint32_t offset, uint8_t* out, uint32_t len) const {
  if (offset > len_ || len > len_ - offset) return false;
  return slot_.read(offset, out, len);
}

// -- one range's bytes, into the slot -----------------------------------------------------------

// What a range's answer may do to the slot, and the three things that can be wrong with it before
// a byte is believed: a status that is not 206 or 200, a Content-Range that is not exactly what
// was asked for, and a total that is not the manifest's size.
class Fetcher::RangeBody : public HttpBody {
 public:
  RangeBody(SlotSink& slot, uint32_t from, uint32_t want, uint32_t fileSize)
      : slot_(slot), from_(from), want_(want), fileSize_(fileSize) {}

  bool begin(const HttpAnswer& head) override {
    status = head.status;
    if (head.status == 200) {
      // §4: a 200 in answer to a range means *start again from zero* - the file under this URL
      // changed, or the server ignored the range. Never spliced onto what is held.
      if (!slot_.restart()) {
        sinkFailed = true;
        return false;
      }
      whole = true;
      limit_ = fileSize_;
      return true;
    }
    if (head.status != 206) return false;  // nothing of a 4xx or a 5xx body is wanted

    uint32_t first = 0, last = 0, total = 0;
    if (!parseContentRange(head.contentRange, first, last, total)) {
      snprintf(why, sizeof(why), "the 206 carried no Content-Range this firmware reads");
      return false;
    }
    if (total != fileSize_) {
      wrongFile = true;
      serverTotal = total;
      return false;
    }
    if (first != from_ || last != from_ + want_ - 1) {
      snprintf(why, sizeof(why), "the 206 is bytes %lu-%lu and %lu-%lu was asked for",
               static_cast<unsigned long>(first), static_cast<unsigned long>(last),
               static_cast<unsigned long>(from_), static_cast<unsigned long>(from_ + want_ - 1));
      return false;
    }
    limit_ = want_;
    return true;
  }

  bool take(const uint8_t* data, size_t len) override {
    if (written + len > limit_) {
      // Never more than expected: the size is the manifest's and a server that sends past it is
      // cut off here rather than at check 4.
      over = true;
      return false;
    }
    if (!slot_.write(data, len)) {
      sinkFailed = true;
      return false;
    }
    written += static_cast<uint32_t>(len);
    return true;
  }

  int status = 0;
  bool whole = false;       // a 200: the slot was restarted and this is the whole file
  bool sinkFailed = false;  // the slot refused a write or a restart
  bool wrongFile = false;   // the Content-Range's total is not the manifest's size
  bool over = false;        // the server sent past what was asked for
  uint32_t serverTotal = 0;
  uint32_t written = 0;
  char why[96] = {0};

 private:
  SlotSink& slot_;
  uint32_t from_;
  uint32_t want_;
  uint32_t fileSize_;
  uint32_t limit_ = 0;
};

// -- the fetch ---------------------------------------------------------------------------------

void Fetcher::begin(ReleaseHttp& http, SlotSink& slot, RecordStore& record, const TrustedKey* keys,
                    size_t keyCount, const char* board, TextOut* out) {
  http_ = &http;
  slot_ = &slot;
  record_ = &record;
  keys_ = keys;
  keyCount_ = keyCount;
  board_ = board == nullptr ? "" : board;
  out_ = out;
}

bool Fetcher::start(const char* base, uint32_t nowMs) {
  // A fetch already running is left alone: its result is its own, and the caller asks `running()`
  // before it offers a second one (src/console/Console.cpp).
  if (http_ == nullptr || slot_ == nullptr || record_ == nullptr || running()) return false;

  result_ = FetchResult();
  result_.ending = Ending::Running;
  info_ = ReleaseInfo();
  file_ = nullptr;
  manifestLen_ = 0;
  signatureLen_ = 0;
  signature_[0] = '\0';
  etag_[0] = '\0';
  shaHex_[0] = '\0';
  slotOpen_ = false;
  stoodDown_ = false;
  startedMs_ = nowMs;
  lastMs_ = nowMs;
  step_ = Step::Manifest;

  if (base == nullptr || base[0] != '/' || strlen(base) > kMaxBaseLen) {
    end(Ending::Fetch, 0, "a release's base is a path on the configured server, at most %u characters",
        static_cast<unsigned>(kMaxBaseLen));
    return false;
  }
  snprintf(base_, sizeof(base_), "%s", base);

  // Deliverable 6: with the table word `one` or `other` there is nowhere to write, and the fetch
  // refuses before any request is made - no byte of anybody's plan is spent finding that out.
  if (!slot_->available()) {
    step_ = Step::Ended;
    end(Ending::Slot, 0, "%s", slot_->why());
    return false;
  }

  // The slot is about to be written over, so whatever it held is no longer there. The record goes
  // first and comes back only when an image has passed every check.
  SlotRecord none;
  if (!saveRecord(*record_, none)) {
    end(Ending::Fetch, 0, "the record of what the other slot holds could not be cleared");
    return false;
  }

  // Deliverable 1 of task p1.19-68: an exit node stops all its observers before it tries to
  // update. Every slot that is up or opening is closed here, before the manifest is asked for,
  // and none opens again until this fetch has ended - so the whole of it, the handshake with
  // meshmaster.dk included, has the heap to itself. The first request waits for the sessions to
  // be gone, which is the StandDown step below.
  if (observers_ != nullptr) {
    observers_->standDown();
    stoodDown_ = true;
    step_ = Step::StandDown;
    say("%s", "the observer's slots stand down before the first request");
  }

  say("fetch %s into the other slot (0x%lX bytes)", base_, static_cast<unsigned long>(slot_->capacity()));
  return true;
}

void Fetcher::step(uint32_t nowMs) {
  lastMs_ = nowMs;
  switch (step_) {
    case Step::StandDown:
      stepStandDown();
      break;
    case Step::Manifest:
      stepManifest();
      break;
    case Step::Signature:
      stepSignature();
      break;
    case Step::Judge:
      stepJudge();
      break;
    case Step::File:
      stepFile();
      break;
    case Step::Judge45:
      stepJudge45();
      break;
    default:
      break;
  }
}

void Fetcher::stepStandDown() {
  // No request has been made yet. The slots' sessions have to be gone first, and on the board
  // they already are: closing a slot destroys its client, which joins the client's task and frees
  // its TLS context before it returns (src/net/MqttClient.cpp), so the first look here is true
  // and the fetch goes on in the same pass it started. kStandDownMs is the bound for an
  // implementation that one day closes in the background; reaching it does **not** end the fetch,
  // because a release that can be had with one slot still up is better than no release at all.
  const uint32_t waited = lastMs_ - startedMs_;
  if (!observers_->allDown() && waited < kStandDownMs) return;
  result_.standDownMs = waited;
  if (!observers_->allDown()) {
    say("the observer's slots were still up after %lu ms; asking all the same",
        static_cast<unsigned long>(waited));
  }
  step_ = Step::Manifest;
  stepManifest();
}

void Fetcher::stepManifest() {
  snprintf(path_, sizeof(path_), "%s/manifest.json", base_);
  HttpRequest req;
  req.path = path_;
  BufferBody body(manifest_, kMaxManifestBytes);
  HttpAnswer answer;
  result_.requests++;
  http_->get(req, answer, body);
  result_.lastStatus = answer.status;

  if (body.overflowed) {
    end(Ending::Check, kCheckManifest, "manifest.json is longer than the %u bytes this firmware reads",
        static_cast<unsigned>(kMaxManifestBytes));
    return;
  }
  if (!tookTheAnswer(answer, "manifest.json")) return;
  manifestLen_ = body.length();
  if (manifestLen_ == 0) {
    end(Ending::Check, kCheckManifest, "manifest.json came back empty");
    return;
  }
  itBroughtBytes();
  say("manifest.json: %u bytes", static_cast<unsigned>(manifestLen_));
  step_ = Step::Signature;
}

void Fetcher::stepSignature() {
  snprintf(path_, sizeof(path_), "%s/manifest.json.sig", base_);
  HttpRequest req;
  req.path = path_;
  BufferBody body(reinterpret_cast<uint8_t*>(signature_), kMaxSignatureBytes);
  HttpAnswer answer;
  result_.requests++;
  http_->get(req, answer, body);
  result_.lastStatus = answer.status;

  if (body.overflowed) {
    end(Ending::Check, kCheckSignature,
        "manifest.json.sig is longer than the %u bytes one signature line can be",
        static_cast<unsigned>(kMaxSignatureBytes));
    return;
  }
  if (!tookTheAnswer(answer, "manifest.json.sig")) return;
  signatureLen_ = body.length();
  signature_[signatureLen_] = '\0';
  if (signatureLen_ == 0) {
    end(Ending::Check, kCheckSignature, "manifest.json.sig came back empty");
    return;
  }
  itBroughtBytes();
  say("manifest.json.sig: %u bytes", static_cast<unsigned>(signatureLen_));
  step_ = Step::Judge;
}

void Fetcher::stepJudge() {
  // Checks 1, 2 and 3, over bytes in RAM: the signature by a key this build trusts, the manifest,
  // and the image for this board. **No request for a file has been made yet, and none is made if
  // any of the three refuses** - which is the whole reason they are done here and not later.
  const Outcome o = readRelease(manifest_, manifestLen_, signature_, keys_, keyCount_, board_, info_);
  if (!o.ok) {
    end(Ending::Check, o.check, "%s", o.why);
    return;
  }
  say("checks 1, 2 and 3 passed: %s", o.why);

  file_ = fileWithUse(info_, "ota");
  if (file_ == nullptr) {
    end(Ending::Check, kCheckImage, "the image %.56s has no `ota` file, which is the one a board fetches",
        info_.imageId);
    return;
  }
  if (file_->size == 0) {
    end(Ending::Check, kCheckImage, "%.96s is said to be no bytes at all", file_->name);
    return;
  }
  if (file_->size > slot_->capacity()) {
    // A manifest cannot widen what may be written (FIRMWARE-MANIFEST.md §5, check 6's rule for a
    // `swd` file, which is the same rule here): the slot is the board's and the manifest is not.
    end(Ending::Check, kCheckImage, "%.96s is %lu bytes and the other slot holds %lu", file_->name,
        static_cast<unsigned long>(file_->size), static_cast<unsigned long>(slot_->capacity()));
    return;
  }

  // Deliverable 5's bound, now that the file's size is known: twice the ranges it needs, plus
  // the tries. A range that brings a byte moves the file forward and costs no try, so this is
  // what stops a server which answers every range with a handful of bytes from being asked for
  // ever (kFileRequestsPerRange).
  {
    uint32_t range = limits_.rangeBytes;
    if (range == 0) range = kRangeBytes;
    const uint32_t ranges = (file_->size + range - 1) / range;
    const uint32_t tries = limits_.tries == 0 ? kFileTries : limits_.tries;
    result_.fileRequestBound = ranges * kFileRequestsPerRange + tries;
  }

  hexOf(file_->sha256, sizeof(file_->sha256), shaHex_);
  if (!slot_->begin(file_->size)) {
    end(Ending::Fetch, 0, "the other slot would not open for a write of %lu bytes",
        static_cast<unsigned long>(file_->size));
    return;
  }
  slotOpen_ = true;
  say("%.96s: %lu bytes wanted, by its hash", file_->name, static_cast<unsigned long>(file_->size));
  step_ = Step::File;
}

void Fetcher::stepFile() {
  const uint32_t have = slot_->held();
  if (have >= file_->size) {
    step_ = Step::Judge45;
    return;
  }
  if (spentTheRequests()) return;
  uint32_t want = limits_.rangeBytes;
  if (want == 0) want = kRangeBytes;
  if (file_->size - have < want) want = file_->size - have;
  const uint32_t from = have;

  // §4: the file by its SHA-256, under {base}/files/.
  snprintf(path_, sizeof(path_), "%s/files/%s", base_, shaHex_);
  HttpRequest req;
  req.path = path_;
  req.hasRange = true;
  req.from = from;
  req.to = from + want - 1;
  // The tag the first range came back with, on every range after it: a server whose file was
  // replaced under the same URL then answers 200 with the new whole file, and begin() restarts.
  req.ifRange = etag_[0] != '\0' ? etag_ : nullptr;
  if (req.ifRange != nullptr) result_.ifRangeSent = true;

  RangeBody body(*slot_, from, want, file_->size);
  HttpAnswer answer;
  result_.requests++;
  result_.fileRequests++;
  http_->get(req, answer, body);
  result_.lastStatus = answer.status;
  if (etag_[0] == '\0' && answer.etag[0] != '\0' && (answer.status == 206 || answer.status == 200)) {
    snprintf(etag_, sizeof(etag_), "%s", answer.etag);
  }

  if (body.sinkFailed) {
    end(Ending::Fetch, 0, "the other slot refused the bytes at %lu", static_cast<unsigned long>(from));
    return;
  }
  if (body.wrongFile) {
    // Check 4 is *the file's size and SHA-256 are the manifest's*, and a Content-Range whose total
    // is not the manifest's size is that check failing on the server's own word - before 1.4 MB
    // is fetched to find out. Not retried: the file under that URL is not the file.
    end(Ending::Check, kCheckHash, "the server's %.96s is %lu bytes and the manifest says %lu",
        file_->name, static_cast<unsigned long>(body.serverTotal),
        static_cast<unsigned long>(file_->size));
    return;
  }
  if (!answer.reachable) {
    countFailure("range %lu-%lu: no reply: %.80s", static_cast<unsigned long>(from),
                 static_cast<unsigned long>(req.to), answer.error);
    return;
  }
  if (!endableStatus(answer.status, "a file")) return;
  if (answer.status == 416) {
    end(Ending::Fetch, 0, "range %lu-%lu is past the end of the server's file",
        static_cast<unsigned long>(from), static_cast<unsigned long>(req.to));
    return;
  }
  if (answer.status != 206 && answer.status != 200) {
    countFailure("range %lu-%lu: http %d", static_cast<unsigned long>(from),
                 static_cast<unsigned long>(req.to), answer.status);
    return;
  }
  if (body.why[0] != '\0') {
    countFailure("range %lu-%lu: %s", static_cast<unsigned long>(from),
                 static_cast<unsigned long>(req.to), body.why);
    return;
  }
  if (body.over) {
    countFailure("range %lu-%lu: the server sent past what was asked for",
                 static_cast<unsigned long>(from), static_cast<unsigned long>(req.to));
    return;
  }
  // Deliverables 4 and 5. A request that brought **nothing** is a failure and three in a row end
  // the fetch; one that brought bytes which were kept is asked on from where it stopped, clears
  // the run of failures and costs no try - and says why it stopped, which is what Aarhus C's
  // journal of 20 November could not.
  char stopped[112];
  if (body.whole) {
    result_.restarts++;
    say("range %lu-%lu: http 200, so the whole file: the slot starts again from zero (%lu bytes held)",
        static_cast<unsigned long>(from), static_cast<unsigned long>(req.to),
        static_cast<unsigned long>(slot_->held()));
    if (body.written == 0) {
      describeStop(answer, stopped, sizeof(stopped));
      countFailure("the 200 brought nothing: %s", stopped);
      return;
    }
    if (body.written < file_->size) {
      describeStop(answer, stopped, sizeof(stopped));
      result_.shortRanges++;
      itBroughtBytes();
      if (result_.resumedFrom == 0) result_.resumedFrom = slot_->held();
      say("the 200 stopped at %lu of %lu bytes: %s; asking on from there",
          static_cast<unsigned long>(body.written), static_cast<unsigned long>(file_->size), stopped);
      return;
    }
  } else {
    if (body.written == 0) {
      describeStop(answer, stopped, sizeof(stopped));
      countFailure("range %lu-%lu brought nothing: %s", static_cast<unsigned long>(from),
                   static_cast<unsigned long>(req.to), stopped);
      return;
    }
    if (body.written < want) {
      describeStop(answer, stopped, sizeof(stopped));
      result_.shortRanges++;
      itBroughtBytes();
      if (result_.resumedFrom == 0) result_.resumedFrom = slot_->held();
      say("range %lu-%lu stopped at %lu of %lu bytes: %s; asking on from %lu (%lu of %lu held)",
          static_cast<unsigned long>(from), static_cast<unsigned long>(req.to),
          static_cast<unsigned long>(body.written), static_cast<unsigned long>(want), stopped,
          static_cast<unsigned long>(slot_->held()), static_cast<unsigned long>(slot_->held()),
          static_cast<unsigned long>(file_->size));
      return;
    }
    itBroughtBytes();
    say("range %lu-%lu: http 206, %lu bytes (%lu of %lu held)", static_cast<unsigned long>(from),
        static_cast<unsigned long>(req.to), static_cast<unsigned long>(body.written),
        static_cast<unsigned long>(slot_->held()), static_cast<unsigned long>(file_->size));
  }
  if (slot_->held() >= file_->size) step_ = Step::Judge45;
}

void Fetcher::stepJudge45() {
  // Checks 4 and 5, over the bytes **as they lie in the slot** - read back, not as they went past
  // on the way in: what is judged is what would be booted (update/Release.h, checkFile).
  const SlotBytes bytes(*slot_, file_->size);
  const Outcome o = checkFile(info_, *file_, bytes);
  if (!o.ok) {
    end(Ending::Check, o.check, "%s", o.why);
    return;
  }
  say("checks 4 and 5 passed: %s", o.why);

  // And the ESP-IDF's own verdict on the image, which is the end of the write. It is not one of
  // section 5's five checks; an image the framework will not have is reported as check 5, because
  // what it says is that the file does not hold what a bootable image of this board holds.
  if (!slot_->finish()) {
    end(Ending::Check, kCheckDescriptor,
        "the image is the manifest's bytes and the framework will not boot it");
    return;
  }

  SlotRecord rec;
  rec.judged = true;
  snprintf(rec.version, sizeof(rec.version), "%s", info_.version);
  snprintf(rec.sha256hex, sizeof(rec.sha256hex), "%s", shaHex_);
  snprintf(rec.base, sizeof(rec.base), "%s", base_);
  rec.size = file_->size;
  if (!saveRecord(*record_, rec)) {
    end(Ending::Fetch, 0, "the image is judged and the record of it could not be written");
    return;
  }

  snprintf(result_.version, sizeof(result_.version), "%s", info_.version);
  snprintf(result_.sha256hex, sizeof(result_.sha256hex), "%s", shaHex_);
  result_.bytes = file_->size;
  end(Ending::Fetched, 0, "%s, %lu bytes, sha %.16s...", info_.version,
      static_cast<unsigned long>(file_->size), shaHex_);
}

// -- the endings -------------------------------------------------------------------------------

// The statuses that end a fetch where they are, for any of the three routes. True when the answer
// is still worth looking at.
bool Fetcher::endableStatus(int status, const char* what) {
  if (status >= 300 && status < 400) {
    // §4: *nothing on them is ever a redirect*. One that comes anyway is not followed and not
    // retried - the firmware asked the configured server and something else answered.
    end(Ending::Fetch, 0, "http %d for %s is a redirect, and these routes never are", status, what);
    return false;
  }
  if (status == 404) {
    // One 404 for everything: a release that is not there, one this board may not see, a file the
    // release has not. It does not say which and it is not read as *withdrawn*.
    end(Ending::Fetch, 0, "http 404 for %s: there is no such resource", what);
    return false;
  }
  if (status == 401 || status == 403) {
    end(Ending::Fetch, 0, "http %d for %s: this board may not have it", status, what);
    return false;
  }
  return true;
}

// A manifest's or a signature's answer: 200 and nothing else will do. Everything that may be
// tried again is counted; everything that may not ends the fetch.
bool Fetcher::tookTheAnswer(const HttpAnswer& answer, const char* what) {
  if (!answer.reachable) {
    countFailure("%s: no reply: %.80s", what, answer.error);
    return false;
  }
  if (!endableStatus(answer.status, what)) return false;
  if (answer.status != 200) {
    countFailure("%s: http %d", what, answer.status);
    return false;
  }
  return true;
}

void Fetcher::countFailure(const char* fmt, ...) {
  result_.failures++;
  result_.failuresInARow++;
  char line[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  if (slotOpen_ && result_.resumedFrom == 0) result_.resumedFrom = slot_->held();
  const uint32_t tries = limits_.tries == 0 ? kFileTries : limits_.tries;
  if (result_.failuresInARow >= tries) {
    // Today's sentence, with the run and not the total in it: three requests in a row that
    // brought nothing is what a fetch gets, and a range that brought bytes cleared the run.
    end(Ending::Fetch, 0, "%s (%lu failure(s), which is all this fetch gets)", line,
        static_cast<unsigned long>(result_.failuresInARow));
    return;
  }
  say("%s; asking again (%lu of %lu)", line, static_cast<unsigned long>(result_.failuresInARow),
      static_cast<unsigned long>(tries));
}

void Fetcher::itBroughtBytes() { result_.failuresInARow = 0; }

bool Fetcher::spentTheRequests() {
  if (result_.fileRequestBound == 0 || result_.fileRequests < result_.fileRequestBound) return false;
  const uint32_t tries = limits_.tries == 0 ? kFileTries : limits_.tries;
  end(Ending::Fetch, 0, "%.64s cost %lu request(s) for %lu range(s), which is all one file gets",
      file_->name, static_cast<unsigned long>(result_.fileRequests),
      static_cast<unsigned long>((result_.fileRequestBound - tries) / kFileRequestsPerRange));
  return true;
}

void Fetcher::end(Ending ending, uint8_t check, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(result_.why, sizeof(result_.why), fmt, args);
  va_end(args);
  result_.ending = ending;
  result_.check = check;
  result_.elapsedMs = lastMs_ - startedMs_;
  step_ = Step::Ended;

  // Deliverable 3: the slots come back. A *fetched* ending is the one a switch follows - an ask
  // begins switching in the next pass of the loop and the board restarts - so they stay down over
  // it rather than opening a session the restart would throw away; every other ending opens them
  // again at once, by their ordinary rule and with no hold, because nothing went wrong.
  if (stoodDown_) {
    stoodDown_ = false;
    observers_->standUp(ending == Ending::Fetched);
    say("the observer's slots %s", ending == Ending::Fetched
                                       ? "stay down: a switch to the judged image follows"
                                       : "open again by their ordinary rule");
  }

  // Every ending but *fetched* leaves the other slot holding nothing this firmware will boot: its
  // first sector is erased where it lies, so that a half-written or refused image cannot be
  // switched to by anything later (task 60 switches by the record, and a spoiled slot is a second
  // answer to the same question).
  if (ending != Ending::Fetched && slotOpen_) {
    const bool spoiled = slot_->spoil();
    slotOpen_ = false;
    say("the other slot's first sector is erased: nothing there will boot%s",
        spoiled ? "" : " - and the erase itself was refused");
  }
  if (ending == Ending::Fetched) {
    say("fetched %s in %lu ms: %lu request(s), %lu for the file, %lu failure(s), %lu short range(s), %lu restart(s)",
        result_.why, static_cast<unsigned long>(result_.elapsedMs),
        static_cast<unsigned long>(result_.requests), static_cast<unsigned long>(result_.fileRequests),
        static_cast<unsigned long>(result_.failures), static_cast<unsigned long>(result_.shortRanges),
        static_cast<unsigned long>(result_.restarts));
  } else if (check != 0) {
    say("check %u: %s", static_cast<unsigned>(check), result_.why);
  } else {
    say("%s: %s", endingWord(ending), result_.why);
  }
}

void Fetcher::say(const char* fmt, ...) {
  if (out_ == nullptr) return;
  char line[256];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if (n <= 0) return;
  out_->printf("fetch: %s", line);
  out_->println();
}

}  // namespace update
}  // namespace mm
