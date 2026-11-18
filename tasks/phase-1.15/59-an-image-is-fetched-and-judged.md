# 59 — An image is fetched and judged

**Depends on:** 57, 58
**Spec:** meshmaster.dk's `docs/dev/unit-protocol.md` §4 (the four routes under `/agent/v1/firmware`: no redirect, `Range`, `If-Range`, the strong `ETag`, the one 404) and its `tests/fixtures/firmware/FIRMWARE-MANIFEST.md` §5; task 58's reader, and the release its tooling writes for the tests; task 57's table word; `src/agent/AgentClient.cpp` (~L348–535: how a call is made, the token, the root) and `src/agent/TlsRoots.h`; `src/platform/TlsHeap.cpp`; `README.md`'s rails (TLS is verified; secrets stay where they are; it stays up); `test/test_tls_policy`, `test/test_server_policy`, `test/test_buffers`; `src/settings/Store.cpp` and `Keys.cpp`; `src/Health.h` (the loop's watchdog); `docs/console.md`. In the pinned ESP-IDF: `components/app_update` (`esp_ota_begin`, `esp_ota_write`, `esp_ota_end`, `esp_ota_get_next_update_partition`). In the Walter firmware's repository, read only, as the model: `components/core/include/core/update/fetch.h`.
**Size:** one session, a long one.
**Model:** claude-opus-5

## Goal

A board has a second slot (57), and the firmware can check a release (58). This task joins
them: given a release on meshmaster.dk, the firmware fetches its manifest and signature,
checks them, fetches the file for this board into the slot that is not running, and checks
what it wrote. At the end the other slot holds a judged image, or holds nothing the firmware
will ever boot.

Nothing is switched here. The running firmware and the boot choice are as they were.

## Deliverables

1. **The fetch.** For a release's `base` on the configured server, with the board's token
   and the rails every call has:
   - the manifest and its signature, a few kB each; checks 1, 2 and 3 before one byte of an
     image is asked for;
   - the image's `ota` file from `{base}/files/`, by its SHA-256, in ranges, into the other
     slot as it arrives. Not held in RAM. A range that fails is asked for again from where
     the slot's writing stands, with `If-Range`; a `200` to a range means from zero;
   - a file that fails three times in all ends the fetch.
2. **Judged from what was written.** Check 4: the size and the SHA-256 of the bytes in the
   slot, read back, are the manifest's. Check 5: those bytes hold exactly one descriptor, and
   it is the manifest entry's and this board's. And the ESP-IDF's own verdict on the image.
   An image that fails any of them is made unbootable where it lies: its first sector is
   erased.
3. **What the slot holds is recorded**, in the settings store under keys of this task's own:
   the version, the SHA-256, the size and the release's `base`, or that it holds nothing
   judged. A restart keeps it. The record is what task 60 switches by.
4. **Each ending has a word and a number**, for task 61's result: *fetched* with the
   version, the hash and the bytes; `fetch` when the release could not be had (no link, a
   404, three failures); `check` with 1 to 5.
5. **The loop stays alive.** A download of 1.4 MB does not starve the mesh, the console or
   the watchdog: it is done in pieces from the loop, and the report says how long a fetch
   took on a board and what the largest piece of heap it held was.
6. **Only with a second slot.** With the table word `one` or `other` the fetch refuses
   before any request.
7. **From the console**, for the bench: `update fetch <release id>` fetches release
   `/agent/v1/firmware/<release id>` and prints each step and the ending. `update` gains a
   line for what the other slot holds. Neither prints the token or a header.
8. **The words.** `docs/console.md`. `docs/field.md`: a ⚠ bench item, on a board with a
   second slot and a release of this firmware served to it on meshmaster.dk:
   `update fetch <id>` ends *fetched* with the release's version; `update` shows it in the
   other slot; `status` and the heartbeat are as before; after `reboot` the old firmware
   runs and `update` still shows the fetched one. One line in `release-notes/unreleased/`.

## Acceptance criteria

The server is scripted, as the uplink's tests script one; the slot is a fake that keeps what
is written to it.

- [ ] A release of task 58's tooling served whole: *fetched*, the slot holds the `ota` file byte for byte, and the record says its version, hash and size (test).
- [ ] No request for a file is made before the signature and the manifest have passed: a manifest with one byte changed ends `check` 1 with two requests made and none for a file (test).
- [ ] A release of another product, signed: `check` 2. One with no image for this board: `check` 3 (tests).
- [ ] A connection that drops at half the file: the next request is a `Range` from the slot's own count with `If-Range`, and the result is the file (test).
- [ ] A `200` to that range: the slot is written again from zero (test).
- [ ] Three failures: `fetch`, and the record says the slot holds nothing judged (test).
- [ ] A file one byte longer, and one with a byte changed: `check` 4. The other board's file under this board's hash: `check` 5. In each the slot's first sector is erased and the record says nothing judged (tests).
- [ ] A `404`, and a redirect: `fetch`, and the redirect is not followed (tests).
- [ ] With the table word `one`: refused, and no request is made (test).
- [ ] Every request carries the token and goes to the configured server over verified TLS: `test/test_tls_policy` still passes, and a test reads the scripted server's log for the header.
- [ ] `update fetch` and `update` print no byte of the token (test).
- [ ] `pio test -e native` green, both boards build, CI green.

## Out of scope

Switching to the image, the trial and going back (60). The server asking (61). A fetch
that is carried over a restart: one that is interrupted by a restart begins again. Any
release that is not this product's. Writing anywhere but the other application slot and
this task's own keys.
