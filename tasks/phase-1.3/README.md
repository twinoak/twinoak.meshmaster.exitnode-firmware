# Pile 1.3 — the rotation probe

Written on 5 October from the review of pile 1.2
([`../phase-1.2/FOLLOWUPS.md`](../phase-1.2/FOLLOWUPS.md)) and the finding
meshmaster.dk's task p4.7-95 left for this repository. Two tasks, on
master; 08 is still stopped on its branch for Thomas's four clicks and is
not a prerequisite - but 13 is the first task that would have been better
proved by its harness, and says so.

It runs **beside** meshmaster.dk's pile 4.9: nothing here changes the
agent's contract; the one contract record 13 reads (`LoginSilentResult`) is
already in the fixtures at the commit 10 synced.

**Prerequisite:** pile 1.2 merged (it is: PR #13). The bench walk of 1.2's
follow-ups is Thomas's and independent of this pile.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 12 | [the-small-ones](12-the-small-ones.md) | — | a display that says *no token* when that is what is wrong, and *retry* through a deploy's `Retry-After`; `online()` false while the server has said to wait; a `config.read` of up to thirty-two keys with the server's own default list; one sentence in boards/README.md |
| 13 | [the-rotation-probe](13-the-rotation-probe.md) | 12 | a blank login between the two tries of a silent new candidate, so that a wrong password during a rotation is reported as the refusal it is and meshmaster.dk's fallback (CONFIGURATION §7 step 4) works for an onboard exit node |

13 runs after 12 because both touch `AgentLoop`'s state for the display and
13's result record is one more thing line four can say.

```powershell
.\tasks\run-all.ps1 -Phase 1.3
```

## Models

- **Fable** — 13 (the rails of a login's silence, re-read against the
  firmware source and the .NET agent's `Handlers.cs`).
- **Opus** — 12.

## The findings

### From the review of pile 1.2

1. **Line four has no *not configured* state** (09): a board with WiFi up
   and no token - exactly the state after the first merged flash of 11 -
   says *Idle, no jobs yet* under *Online: no*. Task 12.
2. **A `Retry-After` hold is invisible to the display** (09 + 10):
   `online()` is `everHeartbeated && consecutiveFailures == 0 && !halted`,
   and a 503 with `Retry-After` is by design not a consecutive failure - so
   during a deploy the OLED says *Online: yes* and *Idle* while the server is
   answering 503. Task 12.
3. **The merged image's fill still covers `otadata`** at `0xE000` -
   harmless, since `boot_app0.bin` is written there by the same merge - but
   one sentence in `boards/README.md` keeps the next reader from moving it
   to "protect" it. Task 12.

### From meshmaster.dk, 5 October

5. **The read's key cap.** Thomas found nine keys the Configuration tab
   shows and the daily read never asks for; meshmaster.dk's task p4.9-102
   makes the read carry the full list, about twenty-six keys, and this
   firmware refuses a claim with more than twenty. Raised to thirty-two in
   task 12, and the default list gains the `agc.reset.interval` 08 found
   missing. **Flash the Heltec with this pile's build before the next
   meshmaster.dk release**, or its daily read ends `unsupported` until then.

### From meshmaster.dk, task p4.7-95

4. **The onboard agent reports a wrong candidate as `unreachable` and the
   server can never fall back.** `JobRunner::ensureSession` is the .NET
   agent's loop before 95: two `loginOnce`, `NoReply` twice is
   `Outcome::Unreachable`, and a password the repeater does not know is
   exactly that on stock firmware. 95 wrote the change in this firmware's
   own terms (its report, *A finding for the firmware repository*). Task 13.

### Watch

- The bench walk of 1.2 (the application image, then two merged flashes)
  and the `v0.2.0` tag: Thomas's, before or beside this pile.
- 08: its resume rebases over 09's `main.cpp`, master's `AgentMesh`, and
  now 13's `JobRunner`.
