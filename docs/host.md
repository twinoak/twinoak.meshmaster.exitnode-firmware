# The agent on a host

Since pile 1.5 the whole agent runs on a workstation: the loop, the runner,
every one of the thirteen job types, the login sequence, the ACL restore, the
airtime gate, the offline buffer and the backoff, against a scripted repeater, a
scripted server and a clock that only moves when a test says so. No radio, no
board, no network — and the whole suite in under a second of wall clock.

This is what the host harness of `tasks/phase-1.1/08-the-host-harness.md`
wanted, built the other way round: three interfaces in front of the mesh, the
uplink and the platform (task p1.5-15), the portable half of the agent as a
library (p1.5-16), and fakes of those three behind which the runner and the loop
run end to end (p1.5-17). There is no second `JobRunner` and no `[env:host]`
binary; there is `pio test -e native`.

## Running it

```bash
pio test -e native                 # every suite
pio test -e native -f test_runner  # the runner, end to end
pio test -e native -f test_loop    # the loop, end to end
```

On Windows that needs a C++ compiler the machine usually has only inside a
container; [`tasks/RUNBOOK.md §2`](../tasks/RUNBOOK.md) has the one-line
`docker run`, and CI runs the same command on Linux. PlatformIO rebuilds and
relinks once per suite, which is most of the minute a full run takes; the
**tests themselves** are milliseconds, because nothing in them sleeps.

The binary of the suite last built is `.pio/build/native/program`, and it is an
ordinary GoogleTest program:

```bash
.pio/build/native/program --gtest_filter='RunnerTest.APasswordSet*'
.pio/build/native/program --gtest_list_tests
```

## What the fakes are

They live in [`test/support/`](../test/support) and are header-only, so a suite
gets them by including one and PlatformIO needs no filter for them (`test/` is
on the include path of every suite).

| | |
|---|---|
| `support/FakeMesh.h` | **A stock repeater at the MeshCore pin.** `mm::IMesh` with one repeater behind it: a name, an ACL of keys at levels (which on stock firmware *is* the login table), an admin password, a guest password or none, the configuration `CONFIGURATION.md` lists, neighbours, a region map, a clock. It answers `sendLogin`, `sendRequest`, `sendCli`, `sendDiscovery` and `sendTrace` as that firmware does, and faults on cue: silence once, twice or always, a reply with the wrong tag, a reply past the wait, a session the repeater forgot, a login refused, a line it will not answer. Every send is recorded with the fake clock's time, which is what *one send per command*, the minimum gap and the per-minute cap are asserted against |
| `support/FakeUplink.h` | **A scripted meshmaster.dk.** `mm::IUplink` with a config, heartbeat replies that may say work is waiting, a claim queue of jobs built from the contract fixtures through the firmware's own `parseClaimResponse`, and every result, observation batch, extend and progress call captured as the bytes the library made. Faults: unreachable, 401, 429 with `Retry-After`, 5xx, a 4xx that discards, a slow call |
| `support/FakePlatform.h` | **A clock that only moves when the test says.** `mm::IPlatform` and `mm::IClock`: `delayMs` *is* the passage of time, so the budget's twenty-second gap is twenty thousand turns of arithmetic and a config read that is eleven minutes on the board is milliseconds here. A seeded random, so a failure is the same failure twice; a log captured rather than printed; and `rebootForHealth` that records its reason and throws, because it may not return and nothing restarts |
| `support/Fixtures.h` | The two fixture sets, read from disk: a companion frame with its wrapper stripped, a contract envelope, and `followsShapeOf` — one object's members against a fixture's, in the fixture's order |
| `support/Jobs.h` | A claimed job as the server writes one, in `ClaimedJob.json`'s shape |

## What they are, and are not

**They answer as the pinned firmware does, by citation.** `FakeMesh.h` opens
with a manifest: one line per answer it gives, each naming either a companion
fixture (`tests/fixtures/companion/*.hex`, themselves already cited to the
function that writes the bytes) or a function of MeshCore at the pin, with the
file and the line range. `Citations.EveryAnswerOfTheFakeCitesAFixtureOrAFirmwareFunction`
in `test/test_runner` walks that manifest against the file: every answer the
code gives is cited, every citation names an answer the code gives, every
fixture named is in the tree, and every `fw` citation's tag and commit are
`platformio.ini`'s `[meshcore]`. When the pinned MeshCore is on disk — it is,
after a build, under `.pio/libdeps/heltec_v3/MeshCore` — it also checks that the
file is there, is long enough, and defines the function at or above the lines
cited.

**It is a second reading of the firmware, on purpose.** meshmaster.dk has its
own fake companion (`src/MeshMaster.MeshCore/Fake/FakeCompanion.cs`), written
from the same source by somebody else. These two are meant to be able to
disagree: when they do, one of the two readings is wrong, and that is worth
knowing. `FakeMesh` is not a port of it.

**A MeshCore bump re-reads them.** A fake is only as true as its citations, and
a citation is a claim about a file that upstream may have moved. So the fakes
are on the list a bump works through
([`tasks/sync/TEMPLATE.md`](../tasks/sync/TEMPLATE.md) step 4,
[`docs/upstream.md`](upstream.md) tier two), beside the frame fixtures: the
citation test says *whether* a cited function still exists where it is claimed
to; whether it still does the same thing is a person's reading.

**It is a repeater and not a radio.** Nothing here models airtime, collisions,
the real region loader or a path that changes under a job. A reply arrives after
a fixed latency on the fake clock. The first disagreement with a real repeater
on the bench is a fixture to add in meshmaster.dk and to sync here
(`tools/sync-fixtures.ps1`), not a knob to add to the fake.

## The two older fakes, and why they stay

`test/test_config_apply`'s `FakeRepeater` and `test/test_rotation_probe`'s
`ScriptedRepeater` are **not** repeaters behind `IMesh`, and `FakeMesh` does not
replace them: they implement `mm::ApplyExchange` and `mm::LoginExchange`, which
are a level above the mesh — the seams through which the apply engine
(`agent/ConfigApply.h`) and the login sequence (`agent/LoginSequence.h`) ask for
*a line on the air* and *a login*, without knowing what a packet is. A script at
that level can say "this line was refused, this one was eaten, the lease ran out
here" in one word, which is what those two suites walk every branch of the
policy with. Running the same branches through `FakeMesh` would mean arranging
radio-level silences to provoke a policy-level answer, and would test the
arrangement.

So there is no duplicated repeater behaviour across three fakes: there is one
repeater (`FakeMesh`) and two exchange scripts. `test_runner` is where the
policies and the repeater meet — the apply's lines really go to a repeater that
really answers them, and `test_rotation_probe`'s four paths are one scenario
there as well.

## Reusing it in another firmware

`test/support/` is this repository's and is not part of the library
(`lib/agent-core/` ships no tests), but it is written to be taken: the three
fakes include nothing but the library's own headers, GoogleTest and the C++
standard library, and they are header-only. A consumer of `agent-core` — the
Walter of a repeater-v2 unit is the first — can copy or vendor the directory and
get a repeater, a server and a clock for its own `IMesh`, `IUplink` and
`IPlatform` for free, and then test the three implementations it actually wrote
against the same scenarios. Two things are worth knowing before it does:

- **The paths of the two fixture sets come in as defines.** `FIXTURES_DIR` and
  `COMPANION_DIR` (and `PROJECT_DIR`) are set by `[env:native]` in
  `platformio.ini`; a consumer sets its own, pointing at its own copy of the
  fixtures, which are meshmaster.dk's and are synced, never edited
  (`tests/fixtures/*/README.md`).
- **`FakeMesh` answers as the *in-process* mesh does**, because that is the
  mesh this repository has. A firmware whose mesh is a link client has one more
  layer to fake — the link itself — and `FakeMesh` is then the thing on the far
  side of it, not the thing behind `IMesh`. The scenarios of `test_runner` still
  apply; the object under test changes.

`docs/library.md` (task p1.5-19) is the page for writing those three
implementations.
