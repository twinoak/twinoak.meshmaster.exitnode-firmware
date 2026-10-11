# Pile 1.4 — the guest door

Written on 11 October, beside meshmaster.dk's pile 4.11, from one question
of Thomas's: *"I think we need to update the exit node firmware as well,
right?"* Right, for two things that are one door. One task.

**Prerequisite:** pile 1.3 merged (it is: PR #15). 08 is still stopped on
its branch and is not touched.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 14 | [the-guest-door](14-the-guest-door.md) | — | `discover` runs at class guest instead of being answered `unsupported`, and a guest-class job that arrives with no password logs in blank, as the .NET agent does and as a repeater with no guest password expects, instead of ending `unauthorized`; so a guest pair made at the empty guest door - own or, after meshmaster.dk's 111, foreign - can be made and probed through a Heltec |

```powershell
.\tasks\run-all.ps1 -Phase '1.4'
```

## Models

- **Opus** — 14.

## The finding

meshmaster.dk's task p4.7-94 built the guest door: a pair whose probes run
at class `guest` with the guest password the owner gave - **or with none**,
because a repeater whose owner never set a guest password answers a blank
login *at guest* (DECISIONS A-4, task p4.5-81). Task p4.11-111 opened that
door to somebody else's repeater. Through a Heltec it fails twice over, and
the field showed both on 11 October (meshmaster.dk repeater 10, foreign,
through exit node 4 running v0.1.3):

1. **The pair cannot be made.** The door's first step is a `discover` job
   at class `guest` (`JobTypes.CredentialClasses`: `Discover => [Guest,
   Admin]`). This firmware's `typeAllowsClass` admits `discover` at admin
   only, so `canRun` answers *a discover job cannot run at class guest on
   this firmware* and the association ends `failed` - *discover at guest:
   unsupported* - before anything goes on the air.
2. **Its probes cannot run.** For an empty password the server sends the
   job **with no credential at all** (`AgentProtocol`: *"the job goes out
   with none and the agent logs in blank"*) and the .NET agent logs in
   blank. `JobRunner::loginMaterial` at `CredentialClass::Guest` with
   `!hasGuestPassword` logs *the claim carried no guest material; nothing
   to log in with*, returns `nullptr`, and `ensureSession` turns that into
   `Outcome::Unauthorized` - which the server reads as the repeater
   refusing the key and raises a *credential rejected* alert on.

The third failure in the same screenshot - the Neighbours tab's read at
`acl`, silent through two exit nodes - is a consequence of the first: with
no active pair the read asks at the type's lowest class, `acl`, a login by
our own key that a foreign repeater holds no entry for; once the pair is
active the read asks at guest (`ReadingService.ClassOfRead`, p4.11-111).

## Watch

- 08's resume: two more files (`JobEnvelopes.cpp`'s `typeAllowsClass`,
  `JobRunner.cpp`'s `loginMaterial` and `runDiscover`) in its rebase.
- The field: the first guest pair with no password through the Heltec, own
  or foreign - meshmaster.dk's repeater 10 through exit node 4 is the one
  waiting - and its `neighbours` read. A repeater that *has* a guest
  password answers the blank login with silence: `unreachable`, by design.
