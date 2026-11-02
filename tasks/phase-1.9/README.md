# Pile 1.9 — a lighter TLS

Written on 2 November from Thomas's `get mqtt.status` on FD9B, a Heltec V3 on
`v0.5.0`: slot 1 *waiting for heap*, 71 612 free where opening it needs 86 560.
Pile 1.8's task 30 keeps the TLS connection to meshmaster.dk open between calls,
and a kept connection holds the same ~63 KB a broker slot does, which is the
mbedtls content buffers (2 × 16 KB), the certificates and the context. Two
connections are ~127 KB on a board with ~136 KB. A handshake per call (`v0.4.0`)
did not fit either: it dipped 81 KB beside a slot and took the board to 3 088
free on 28 October. Those buffers are fixed in the Arduino core's precompiled
ESP-IDF, so the fix is to build ESP-IDF ourselves with smaller TLS settings and
change nothing else. Two tasks, and a bench walk between them.

**Prerequisite:** pile 1.8 merged and `v0.5.0` tagged (both are).

### Measured

| | free at rest | min ever | largest block | uplink: lowest during a kept call / a handshake |
|---|---|---|---|---|
| `v0.4.0`, no slot, after a call (28 Oct) | 136 580 | 55 464 | 73 716 | — / 55 464 |
| `v0.4.0`, one slot, after a job (28 Oct) | 73 016 | **3 088** | 59 380 | — |
| `v0.5.0`, V3, kept uplink, no slot (2 Nov) | 71 612 | — | 51 188 | 69 232 / — |
| after 36, V3, no slot | | | | |
| after 36, V3, one slot (MeshView), an hour | | | | |
| after 36, V4, two slots, an hour | | | | |

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 36 | [lighter-tls](36-lighter-tls.md) | — | ESP-IDF built from source under the same Arduino core (`framework = arduino, espidf`), its config the core's own but for the mbedtls lines (buffers allocated only while in use, 16 KB in / 4 KB out, certificates freed after the handshake); a CI check that holds the config to the core's; same partitions, flash layout, merged image and behaviour; RAM/Flash before and after |
| 37 | [measured-and-released](37-measured-and-released.md) | 36, and the bench rows above | the boards' memory figures from the walk, the gate and the docs on them, the V3/V4 rule as measured; `v0.6.0` published as *Latest* |

```powershell
.\tasks\run-all.ps1 -Phase '1.9' -To 36
```

then the bench walk below, then

```powershell
.\tasks\run-all.ps1 -Phase '1.9' -From 37
```

(The quotes: PowerShell reads a bare `1.9` as a number.)

## Models

- **Fable** — 36. A build system change on a board with no room to spare, where
  a wrong line of config shows only on hardware.
- **Opus** — 37.

## Between 36 and 37 (Thomas)

Flash 36's images (CI's artifacts of its pull request, or `out/`; the plain
`.bin` keeps the settings) on FD9B and on a V4:

1. **V3, observer off** (`set mqtt1.preset none`), ten minutes: `status`'s `heap`
   and `uplink` lines → row *after 36, V3, no slot*.
2. **V3, MeshView on** (`set mqtt1.preset meshview`), an hour beside the uplink,
   with a job or two through it: `get mqtt.status` (`memory`, `uplink`, slot 1's
   state, `gave way`) and `status`'s `heap` → row *after 36, V3, one slot*. Note
   whether the slot connected at all, and `min ever`.
3. **V4, both slots**, an hour: the same → row *after 36, V4, two slots*.
4. The web flasher's **first install** with 36's `-merged.bin` on one board, to
   see that the bootloader built from source boots.

37 reads the rows. Without them it stops on purpose and says so.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md), tagged with the task that
must pick them up.
