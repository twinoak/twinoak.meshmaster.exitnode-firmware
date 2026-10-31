# Pile 1.8 — built at home, the observer fits, a scope of its own, and its neighbours

Written on 31 October from Thomas's bench figures of 28 October, which showed
that the observer of pile 1.7 does not fit a Heltec V3 beside the MeshMaster
uplink as it stands; and from two questions of Thomas's of 31 October: why
MeshView shows *Direct neighbours* for his Gessaman observer and nothing for FD9B,
and whether the exit nodes use flood scopes at all (they do not); and his note of
the same evening that all CI is to run on the Unraid runners, first. Five tasks.

**Prerequisite:** pile 1.7 merged and `v0.4.0` tagged (both are).

### Measured (Heltec, 28 October, `v0.4.0`)

| | free | min ever | largest block |
|---|---|---|---|
| No observer slot, after a MeshMaster call | 136 580 | 55 464 | 73 716 |
| Slot 1 (MeshView) connected, after a job | 73 016 | **3 088** | 59 380 |

So: one slot about **63 KB**; the uplink's call dips about **80 KB** (136.6 →
55.5); and with one slot open, a call took the board to 3 KB free. The gate of
27 asks for 88 KB free and a 34 KB block before a slot opens — from the observer
firmware's figure of 40 KB per connection — and checks nothing after. Slot 2
(MeshMapper) has stayed in *waiting-for-heap*, rightly.

**Decided 31 October: one slot on a V3, two on a V4.** Two TLS connections to
brokers beside the MeshMaster uplink do not fit a board without PSRAM, even with
30 and 31 done; one does, with the gate watching. A V3 owner picks MeshView or
MeshMapper; a V4 carries both.

Until this pile has landed and been walked: **the observer stays off on the
exit nodes MeshMaster depends on**; one slot, on the test node, watched.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 29 | [built-at-home](29-built-at-home.md) | — | `ci.yml` and `upstream.yml` on `[self-hosted, unraid]`, their caches only on GitHub's runners; `release.yml` stays on GitHub's runners (what ships is built on a clean machine); a guard in `ci.yml` that holds both |
| 30 | [the-uplink-holds-its-line](30-the-uplink-holds-its-line.md) | — | one TLS connection to meshmaster.dk kept between calls; the server's idle timeout found; each call's lowest free heap and the handshake count on `status` |
| 31 | [the-observer-fits](31-the-observer-fits.md) | 30 | smaller MQTT buffers and stack; the gate from measured, per-board figures; a gate that keeps watching and gives way when a call dips under a floor; TLS buffers in PSRAM on a V4 if the core allows; one slot on a V3, two on a V4; a per-board table of what fits |
| 32 | [a-scope-of-its-own](32-a-scope-of-its-own.md) | — | `flood.scope` (default `dk`): logins without a path and path discoveries flood with the region's transport code; the second try of a flooded login unscoped while `flood.unscoped retry`; scoped/unscoped/none answer counts on `status`; the heartbeat's `flood` if meshmaster.dk p4.20-151 has merged, a `[1.9]` follow-up if not |
| 33 | [its-neighbours-and-their-scopes](33-its-neighbours-and-their-scopes.md) | 31, 32 | a 20-entry zero-hop repeater table from adverts and a 60 s discovery; each neighbour's scopes by an anonymous regions request between jobs; Gessaman's `/neighbors` report once a day through 31's gate; `mqtt.neighbors`, `discover.scopes`, `nbr:` in `mqtt.status`; noise floor, air time and battery in the status message |

29 → 30 → 31 → 32 → 33. 29 first: until it lands, every pull request's CI runs on
GitHub's runners. 32 touches no observer file; 33 needs both 31 and 32.

```powershell
.\tasks\run-all.ps1 -Phase '1.8'
```

(The quotes: PowerShell reads a bare `1.8` as a number.)

## Models

- **Fable** — 30, 31, 33. Memory on a board with no room to spare, the one
  connection that must not be the one to fail, and a report that must publish
  inside both.
- **Opus** — 29, 32.

## Between 30 and 31 (Thomas, if you can)

After 30 has merged, flash its image on the test node with the observer off,
let it run ten minutes, and paste `status`'s heap line and the new uplink line
into the *Measured* table above (a row *after 30, no slot*). 31 reads the table;
without the row it uses 28 October's figures and says so. To pause the driver
between the two, run `-To 30`, then `-From 31`.

## Afterwards

On the bench: one slot on a V3 for an hour beside the uplink, then `status`'s
`min ever`; on a V4, both slots. Then `discover.scopes` on FD9B and its MeshView
page (*Direct neighbours*, Battery, Noise floor, TX/RX), and a flooded login to a
repeater with no stored path, to see `flood:` count a scoped answer. Then tag
`v0.5.0`.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md), tagged with the task that
must pick them up.
