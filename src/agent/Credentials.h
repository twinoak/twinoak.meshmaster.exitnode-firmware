// A job's credential material: what the claim carried for the job's class, and nothing else
// (EXIT-NODES §7, SECURITY §5.2, tasks p3.5-40, p4.1-60).
//
// Where a repeater's password lives on this board, decided here for every task after this one:
//
//   - in **this struct and nowhere else**: a fixed buffer inside the claimed job, filled by the
//     claim parser straight out of the reply's bytes (zero-copy, so ArduinoJson keeps no copy of
//     its own) and read by exactly one thing, the login packet's plaintext, which is wiped the
//     moment the packet is encrypted;
//   - **for the length of the job**: `wipe()` runs when the job ends, whatever it came to, and
//     once more before the next claim, and a test reads the bytes back;
//   - **never in NVS, never on the file system**: this module includes no store and no file
//     API, and test/test_credentials greps it and the three files the material passes through
//     for the names of those APIs;
//   - **never printed**: no log line takes it as an argument, the job's log names the class
//     and never the value, and `log.frames on` prints the login frame with the password's
//     bytes replaced by `*` - the redaction the .NET agent's --log-frames has, and on a board
//     the bytes replaced are the encrypted body, which is the only place the password's shape
//     is on the air at all.
//
// The one copy this module cannot wipe is the transient the TLS library and HTTPClient move the
// claim's bytes through on the way in; the buffer they land in is wiped as soon as the claim
// is parsed (agent/AgentClient.cpp). The same shape as the .NET agent's HttpClient.
//
// Host-portable: no Arduino, no store, no allocation.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {

// BaseChatMesh::sendLogin at d9296435 keeps at most 15 characters of a password, and so does
// this board: a longer one is cut here, once, and the job's log says so - without the value.
constexpr size_t kMaxPasswordLen = 15;

struct JobCredential {
  bool hasAdminPassword = false;
  bool hasGuestPassword = false;
  char adminPassword[kMaxPasswordLen + 1] = {0};
  char guestPassword[kMaxPasswordLen + 1] = {0};
  bool adminPasswordCut = false;  // the claim carried more than kMaxPasswordLen bytes
  bool guestPasswordCut = false;

  // Take a password from the claim's text: at most kMaxPasswordLen bytes, and whether it was
  // cut. A nullptr or an empty string is "the claim carried none".
  void setAdminPassword(const char* text);
  void setGuestPassword(const char* text);

  // Overwrite every byte, through a volatile pointer so the compiler cannot drop the stores as
  // dead, and forget that anything was ever here.
  void wipe();

  // Every byte zero and every flag false: what a test reads back after a job.
  bool isWiped() const;
};

// Zero `len` bytes at `p` in a way the optimiser keeps.
void wipeBytes(void* p, size_t len);

// A login frame for the console's frame log, redacted as the .NET agent's --log-frames redacts
// CMD_SEND_LOGIN: the bytes that are not the password stay hex, and every byte that was made
// from it is one `*`. On the air a login is an ANON_REQ whose payload is the destination's hash
// (1), our public key (32) and then the encrypted body (Mesh::createAnonDatagram at d9296435),
// so the first kLoginPayloadClearBytes are printed and the rest is stars. Returns the length
// written; `out` is NUL-terminated and bounded by `cap`.
constexpr size_t kLoginPayloadClearBytes = 1 + 32;
size_t redactLoginPayloadHex(const uint8_t* payload, size_t len, char* out, size_t cap);

}  // namespace mm
