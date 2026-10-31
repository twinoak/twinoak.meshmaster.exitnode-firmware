# 32 — A scope of its own

**Depends on:** nothing (runs after 31; touches no observer file). Uses meshmaster.dk's p4.20-151 if it has merged, and leaves a follow-up if not (deliverable 5).
**Spec:** MeshCore at the pin (`d9296435`, `.pio/libdeps/*/MeshCore`): `src/Mesh.h` / `Mesh.cpp` (`sendFlood(pkt, transport_codes, delay, path_hash_size)` ~L208 / ~L665–680, `ROUTE_TYPE_TRANSPORT_FLOOD`), `src/helpers/TransportKeyStore.cpp` (`getAutoKeyFor`: a hashtag region's key is the first 16 bytes of SHA-256 of `#name`; `TransportKey::calcTransportCode`: HMAC over the payload type and payload, 0000 and FFFF reserved), `examples/companion_radio/MyMesh.cpp` (`sendFloodScoped` ~L489–523: code 0 from the scope, code 1 left 0; `DEFAULT_FLOOD_SCOPE_NAME` ~L925), `examples/simple_repeater/MyMesh.cpp` (`onRecvPacket`: an unscoped flood is forwarded only while the wildcard allows flood, a scoped one only on a region match; `allowPacketForward` and `flood.max.unscoped`; `sendFloodReply`: the reply goes in the request's scope); ours: `src/mesh/AgentMesh.cpp` (`dispatch` ~L295–304, `sendDiscovery` ~L380–397, `kFloodPathHashSize`), `src/settings/Keys.cpp`, `lib/agent-core/include/agent/LoginSequence.h` (the two tries), `JobRunner.h`, `docs/observer.md` and the release notes' console section; meshmaster.dk's `src/MeshMaster.Core/Configuration/dk.json` (the Danish standard: default scope `dk`, `flood.max.unscoped` 15).
**Size:** one session.
**Model:** claude-opus-5

## Goal

Thomas, 31 October: *are our exit nodes not using scopes at all? It might
become mandatory.* They are not. Every flood this firmware sends is a plain
`ROUTE_TYPE_FLOOD` with no transport code: a login or request to a repeater
with no known path (`dispatch`), and every path discovery (`sendDiscovery`).
Direct-routed traffic, which is most of what MeshMaster sends, is not affected,
because a repeater only checks the scope of a flood.

A stock repeater forwards an unscoped flood only while its wildcard region
allows flood, and only for `flood.max.unscoped` hops. The Danish standard sets
that to **15** today. The firmware's own comment in `sendFloodReply` names the
end state: *repeaters running `flood.max.unscoped=0`*, where an unscoped flood
dies at the first hop. On that day, an exit node that only floods unscoped can
no longer find a path to a new repeater.

A scoped flood is forwarded only by a repeater whose region map holds the scope
with flood allowed. Under the Danish standard that is `dk` on every repeater
that follows it, but a repeater with no region map drops a `dk` flood that it
would have forwarded unscoped. So: scope the floods `dk` by default, keep one
unscoped try while unscoped still works, and count which one answered, so that
the day the unscoped try stops earning its airtime is a number, not a guess.

## Deliverables

1. **`flood.scope`** in the settings table: a region name (`dk`, `dk-aarhus`;
   a leading `#` is accepted and dropped; the firmware's region-name characters,
   30 at most) or `*` for unscoped. Default **`dk`**. The key is the hashtag
   region's public key, computed once when the setting is read (`#` + name, as
   `getAutoKeyFor`). A private region (one with a secret key) is out of scope:
   the setting takes names, never keys.
2. **Scoped floods.** `dispatch` and `sendDiscovery` flood with
   `transport_codes[0]` = the scope's code over the packet and
   `transport_codes[1]` = 0, as the companion's `sendFloodScoped` does, when
   `flood.scope` is not `*`. A repeater's flood reply comes back in the same
   scope by itself (`sendFloodReply`).
3. **One unscoped try, while it is allowed.** Where a transaction floods twice
   within one attempt (the login's two tries, a discovery that is repeated), the
   **second** flood goes unscoped. A setting `flood.unscoped` (`retry` | `never`,
   default `retry`) turns that off for the day unscoped is gone. One rule, in
   one place: the mesh is told *scoped* or *unscoped* per send, and the job's
   log line says which (`login (guest) try 2: unscoped`).
4. **The figures.** `status` gets a line `flood: scope dk, answered scoped N /
   unscoped M / none K since boot`, counted per flooded transaction by which
   flood was answered. `get flood.scope` prints the name and the transport code
   of an example payload, so a bench can check the key against a repeater's
   `region` listing.
5. **MeshMaster hears it, when it can.** meshmaster.dk's task p4.20-151 adds an
   optional `flood` object to the heartbeat (`scope` — the name, `*`, or null —
   `unscopedRetry`, `answeredScoped`, `answeredUnscoped`, `unanswered`) and the
   fixture `AgentHeartbeatFlood.json`. First look: if that fixture is on
   meshmaster.dk's `master` (`gh api repos/twinoak/meshmaster.dk/contents/tests/fixtures/contracts`),
   sync the fixtures (`tools/sync-fixtures.ps1`) and send the object from
   `AgentLoop::buildHeartbeat` with the counts of deliverable 4. If it is not,
   do everything else and write an open item tagged `[1.9]` in this pile's
   FOLLOWUPS.md, *send `flood` in the heartbeat once p4.20-151 has merged*, so
   that it is not forgotten. Do not stop for it.
6. **Words:** a short *Flood scope* section in `README.md` and in the release
   notes' console section (`set
   flood.scope dk` is the default and needs typing only to change it), and a
   `docs/upstream.md` note that this follows `sendFloodScoped` at the pin.

## Acceptance criteria

- [ ] Native tests: the key of `dk` equals the first 16 bytes of SHA-256 of `#dk`; the transport code of a fixed packet matches MeshCore's `calcTransportCode` for it (computed in the test from the library, not pasted); `*` sends `ROUTE_TYPE_FLOOD` with no codes; a name with a character the firmware's region names forbid is refused by `set`.
- [ ] A flooded login sends try 1 scoped and try 2 unscoped under `retry`, both scoped under `never`; the log line names the scope of each try; the `status` counts move accordingly (a test with the fake mesh).
- [ ] Direct sends are unchanged byte for byte (a test).
- [ ] With `AgentHeartbeatFlood.json` on meshmaster.dk's `master`: fixtures synced, the heartbeat's `flood` matches it (`test_contract_fixtures`). Without it: the `[1.9]` item is in FOLLOWUPS.md and the report says so.
- [ ] `pio run -e heltec_v3 -e heltec_v4` and the native tests green, CI green.

## Out of scope

MeshMaster's side of the scope (meshmaster.dk task p4.20-151, which also does
the .NET agent's companion). Choosing a scope per job from the
target repeater's region map. Private regions.
