# 04 — Logins, and the config read

**Depends on:** 03.
**Spec:** [EXIT-NODES §4, §7](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/EXIT-NODES.md), [CONFIGURATION §3, §8](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/CONFIGURATION.md), [SECURITY §5.2, §6](https://github.com/twinoak/meshmaster.dk/blob/fbf4625a0352663e1c26bd6877e45e6c555d4f7c/spec/SECURITY.md), tasks p3.5-40, 41, 43, 44, p4.1-60.
**Size:** one session.
**Model:** claude-fable-5-1

## Goal

The jobs that carry a password: `discover` (an admin login that learns the
path), `acl.set` (`setperm` on the session it opened), and `config.read`
(the `get` list, the access-list request, the region listing, `ver` and
`board`). It is Fable's because a repeater's admin password now lives in
this board's RAM for the length of a job, the ACL entry the board holds on
every associated repeater is the entry task 60 made the agent responsible
for restoring, and a mistake in either is the T5 case of SECURITY §2 on a
device somebody can pick up off a windowsill.

## Deliverables

- **Credential handling.** The claim's `credential` (`guestPassword`,
  `adminPassword`, `acl: <level>`) is held in a fixed buffer for the job,
  zeroed when the job ends whatever the outcome, never written to NVS or
  the file system, never printed — the console's `log.frames` redacts the
  login frame's password bytes as the .NET agent's does (a count of `*`).
  The guest password is never used through a pair whose probes rely on the
  ACL entry (the claim refuses it; the firmware refuses too, belt beside
  braces).
- **`discover`**: an admin login, flooded when no path is known, direct
  when one is; the path from `PUSH_CODE_PATH_UPDATED`'s equivalent
  (`onContactPathUpdated`) and the reply; `CMD_SEND_PATH_DISCOVERY_REQ`'s
  equivalent last and only when neither login was answered (task 39);
  `loggedInAs` in the result; a login answered below the class is
  `unauthorized` with the path still reported (task 41).
- **`acl.set`**: `setperm <key> <level>` on the admin session, the reply
  captured, the cached session's level updated as task 60 says (the entry
  is the session).
- **`config.read`**: the `get` list from the payload in order, the
  access-list request (`REQ_TYPE_GET_ACCESS_LIST`, admin only, 23 entries at
  most, the padding rule, silence tolerated when keys were read — tasks 43
  and 49), `region` and `region default` (task 57's two transactions, the
  160-character truncation as no evidence), `ver` and `board`; the result
  shape of `ConfigReadResult` including `aclRestored`.
- **The ACL restore** (task 60): every admin session through an associated
  pair ends with `setperm <own key> <granted level>` as its last command,
  the level from the claim's `acl`; the cached session drops to that level.
- **Session cache** per repeater with the .NET agent's rules: a session is
  handed to a job only when its level satisfies the class; the lifetime;
  dropped on `password.set` (05).
- **The console**: `sessions` (per repeater: level, age) with no secret in
  it.

## Acceptance criteria

- [ ] Native tests: the credential buffer is zeroed after a job (a test reads it); no code path writes it to NVS (a grep test for the NVS API in the credential module); the login frame's redaction; the config-read result for the fixture bytes equals the .NET expectation, `aclRestored` included; the access-list parse stops at padding and refuses a non-zero tail.
- [ ] The session cache refuses a below-class session and drops on an `acl.set` of our own key.
- [ ] `pio run -e heltec_v3` green; the heartbeat advertises `discover`, `acl.set`, `config.read`.

## Not verified here (Thomas, with the board)

Re-run the association through the board; *Read the configuration*; the
`setperm … 1` as the read's last frame in `log.frames`; the entry at
read-only in the next read's list; a night of probes with the association
`active`.

## Out of scope

The writes (05).
