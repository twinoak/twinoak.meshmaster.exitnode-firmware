# 74 — A name of its own on the network

**Depends on:** —
**Spec:** `src/net/WifiLink.cpp` (`WiFi.mode(WIFI_STA)` ~L47, `WiFi.begin` ~L75); `sdkconfig.defaults` (`CONFIG_LWIP_LOCAL_HOSTNAME="espressif"` ~L1464); `docs/console.md` (`status`); `docs/field.md`.
**Size:** a small session.
**Model:** claude-opus-5

## Goal

An owner of two boards reported on 22 November that one of them had *a new MAC address
and so a new IP* after an update. Nothing in this firmware sets or reads a MAC (read then:
`src/net/` unchanged between v0.6.0 and v0.7.6, the same platform pin, the same MAC lines in
`sdkconfig.defaults`); the station's address is the chip's own. What the firmware does do
is announce every board to the network under one name, `espressif`, so a router that
keeps its leases by name sees two boards as one and may hand them each other's address.

## Deliverables

1. **Each board announces a name of its own:** `meshmaster-` and the last four hex digits
   of its station MAC, lower case. Set where the framework at this repository's pin wants
   it set to take effect for DHCP (read it; say which call and why there). Nothing is
   stored and nothing is settable: the name follows the chip.
2. **`status` says both**, on the line that speaks of the WiFi: the MAC and the name.
3. **A rail:** nothing under `src/` or `lib/` sets a MAC (a grep, with the calls it looks
   for named), so that the answer given on 22 November stays true.
4. **The words:** `docs/console.md` (the line), `docs/field.md` (a board's address on the
   network is its MAC, shown in `status`; reserve by that); a line in
   `release-notes/unreleased/`.
5. **Tests:** the name from a MAC (host test, three addresses, one with leading zeros).

## Acceptance criteria

- [ ] The name is `meshmaster-xxxx` from the chip's MAC and is given to the network before the address is asked for (the call and its place; the host test's name).
- [ ] `status` prints the MAC and the name.
- [ ] The rail refuses a line that sets a MAC (shown by planting one, then removed).
- [ ] `pio test -e native` green, both boards build, the frame rail and the library's host rail (`tools/check-library-host.py`) green, CI green.

## What only a board can show

A router's list of leases with two boards under two names. Thomas's, with Risskov and
Aarhus C.

## Out of scope

A name the owner chooses. mDNS. A fixed address.
