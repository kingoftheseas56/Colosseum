# Colosseum Watch Party relay

Slices of `docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md`.

**Slice 1** proved a production `colosseum.exe` can complete a real `wss://` handshake — with
genuine certificate verification, no client TLS exceptions — against a Cloudflare Workers relay
running locally. The Worker checks the `X-Colosseum-Watch-Party-Protocol` upgrade header (426 if
missing/wrong); the Durable Object (`RoomDO`) accepts the socket, logs the upgrade headers, and
replies to any text frame with one protocol-v3 `error` envelope (`code:
"protocol_version_mismatch"`, the frozen wire code the desktop client already maps to its
`protocolVersionMismatch` error category — see `native/watchparty/WatchPartyRoomServiceClient.cpp`),
then closes 1000.

**Slice 2** adds `src/protocol.ts`: the full protocol-v3 wire schema — every envelope/message
type, strictly validated (exact-keys, required/optional fields, typed enums, the 64 KiB message
ceiling, source-descriptor validation) — transcribed from
`native/watchparty/WatchPartyProtocol.{h,cpp}` and `WatchPartyTypes.{h,cpp}` (the client is FROZEN
for this plan and is the conformance oracle: the relay must speak exactly what the client parses,
never the other way around). `RoomDO`'s Slice 1 echo path now builds and serializes its reply
through this module instead of a hand-rolled object literal, with behavior otherwise unchanged. No
room/session/lifecycle logic exists yet — that is Slice 3.

## Layout

- `src/index.ts` — Worker `fetch`: routes WebSocket upgrades, enforces the protocol header at the
  edge (426 before ever reaching a DO), forwards to `RoomDO`.
- `src/protocol.ts` — protocol v3 core: types, `parseMessage()` (strict envelope + per-type
  payload validation), `serializeMessage()`, and typed builders for every server → client message.
- `src/room-do.ts` — the scaffold DO: accept, log headers, echo one `error` frame (via
  `protocol.ts`), close.
- `test/protocol.test.ts` — vitest conformance suite (see "Running the relay test gate" below).
- `test/fixtures/valid/*.json` — one fixture per protocol message type (both directions),
  transcribed from the contract's own examples.
- `test/fixtures/invalid/*.json` — targeted invalid cases: unknown envelope/payload key, missing
  required key, wrong field type, bad protocol version, bad source kind, wrong-direction type. The
  oversize (64 KiB ceiling) case is generated in the test itself from a valid fixture, not a static
  file (a literal 64 KiB+ JSON fixture isn't a useful diff to carry in the repo).
- `test/probe-handshake.mjs` — node/`ws` probe: asserts upgrade+frame with header `3`, and refusal
  (426) with header `2` or no header.
- `test/dev-ca.pem` — the CA cert this recipe imports (see below). Committed so the recipe is
  reproducible without re-running `openssl s_client` against a live `wrangler dev`.

## Running the relay test gate

```
cd server/watchparty-relay
npm test        # vitest run — 38 conformance cases, in-process via @cloudflare/vitest-pool-workers
                 # (runs INSIDE workerd/Miniflare, no live network, deterministic)
```

This is a NEW deterministic gate the plan defines, separate from ctest's `-L unit` gate; it does
not replace or touch that gate. `npm test` must stay green before every commit touching `src/`.

## Running locally

```
cd server/watchparty-relay
npm install
npm run dev:https        # wrangler dev --local-protocol https, serves wss://localhost:8787
```

## The TLS recipe (the actual unknown this slice killed)

**Recipe (b1) won — no custom TLS terminator needed.** `wrangler dev --local-protocol https`
serves a *fixed, self-signed* certificate (issuer `CN=localhost, O=Cloudflare, OU=Workers`,
`emailAddress=wrangler@cloudflare.com`) bundled with the `workerd` binary. Verified
2026-08-20: killed and restarted `wrangler dev` and re-extracted the cert — identical SHA-256
fingerprint both times (`05:77:DE:...:FF:5F`), so it is safe to import once and reuse across
sessions, not regenerated per-run.

### Extract the cert (already done; `test/dev-ca.pem` is committed)

```
# with wrangler dev --local-protocol https running:
openssl s_client -connect 127.0.0.1:8787 -servername localhost </dev/null 2>/dev/null \
  | openssl x509 -outform PEM > test/dev-ca.pem
```

### Import into the Windows CURRENT USER trust store (Qt/Schannel side)

Our Qt build uses the Schannel TLS backend (`qschannelbackend.dll`, verified 2026-08-20), which
honors the Windows certificate store — so importing the relay's dev CA there lets `colosseum.exe`
verify it with **zero client code changes**.

`certutil -user -addstore Root <pem>` hangs waiting on an interactive install-confirmation
surface in this environment and was abandoned. Use PowerShell's `Import-Certificate` instead —
it is silent and CLI-safe:

```powershell
Import-Certificate -FilePath "server\watchparty-relay\test\dev-ca.pem" `
  -CertStoreLocation Cert:\CurrentUser\Root
```

Verify:

```powershell
Get-ChildItem Cert:\CurrentUser\Root | Where-Object { $_.Subject -match "wrangler" }
```

Removal (cleanup):

```powershell
Get-ChildItem Cert:\CurrentUser\Root | Where-Object { $_.Subject -match "wrangler" } |
  Remove-Item
```

**Safety:** current-user store only, never LocalMachine. The cert's subject/issuer
(`Cloudflare/Workers/wrangler@cloudflare.com`) makes it unambiguous to find and remove later; it
is `wrangler`'s own bundled local-dev cert, not something this repo mints, so there is nothing
Colosseum-specific to name — the recipe above is how any brother finds and strips it.

### The Node probe side note

Node does **not** read the Windows certificate store. The probe (`test/probe-handshake.mjs`)
needs the CA handed to it explicitly:

```
NODE_EXTRA_CA_CERTS=test/dev-ca.pem RELAY_URL=wss://localhost:8787 npm run probe
```

This only matters for the node probe. The **Qt client** verifies via the Windows store import
above and needs no environment variable.

## Slice 1 evidence (2026-08-20)

- Node probe: `PROBE_OK` on all three cases (`with-header-3`, `wrong-header-2`, `no-header`),
  `PROBE_OK overall=pass`.
- Relay log line proving the real app's connection (not just the probe) carried the protocol
  header:
  `watchparty-relay RoomDO upgrade headers: {...,"x-colosseum-watch-party-protocol":"3"}`
- Isolated `lanista session run` against a real `native/build-msvc/colosseum.exe`, relay live:
  `watchPartyJoinPhase` `idle` → `error`, `watchPartyJoinErrorCategory` `""` →
  `protocolVersionMismatch` — proof the TLS+WSS handshake completed, the upgrade succeeded, the
  relay's typed error frame round-tripped, and the client parsed it into its real error surface.
  Session artifacts: `artifacts/lanista-sessions/20260820-140027-27917da0/`.
- Fail-closed control (relay stopped, same scenario): no hang, no crash;
  `watchPartyJoinPhase` reached `connecting` (client's documented reconnect/backoff state) instead
  of ever completing a join. Session artifacts:
  `artifacts/lanista-sessions/20260820-140305-5e1441f6/`.

## Slice 2 evidence (2026-08-20)

- `npm test`: 38/38 conformance cases green (21 valid-fixture round-trips across every client→server
  and server→client message type, 1 dual-direction shape-mismatch check, 1 client-only-type-sent-
  as-server-message check, 9 targeted invalid-class rejections, 1 oversize-ceiling rejection, 2
  coverage-completeness checks, 1 baseline-sanity control).
- Negative control (a) — extra key on a valid fixture: added an unexpected top-level key to
  `test/fixtures/valid/leaveRoom.json`; rerun went to 36/38 (exactly the `leaveRoom` round-trip
  case and the baseline-sanity control that reuses the same fixture went red, nothing else moved);
  reverted, rerun back to 38/38.
- Negative control (b) — mis-cased serializer error code: changed the `protocol_version_mismatch`
  literal emitted on a version mismatch to `Protocol_Version_Mismatch` (cast through `as ErrorCode`
  to keep the deliberate mutation compiling); rerun went to 37/38 (exactly
  `envelope-bad-version.json rejects protocol_version_mismatch` went red, asserting the received
  code string, not just that it threw); reverted, rerun back to 38/38.
- `node test/probe-handshake.mjs` against a live `wrangler dev --local-protocol https`:
  `PROBE_OK` on all three cases (`with-header-3`, `wrong-header-2`, `no-header`), `overall=pass` —
  Slice 1's transport did not regress after wiring `RoomDO`'s reply through `protocol.ts`. Relay
  log confirmed the reply is now built via `buildErrorMessage()`/`serializeMessage()`.
- `wrangler dev` process (PIDs 3820, 12680 — the wrangler CLI + its workerd child) killed by exact
  PID after the probe run; command-line-verified no `wrangler` process remained.

## Slice 3 — room lifecycle (2026-08-20)

`RoomDO` now owns real room state: `createRoom` (signed-in only), `joinRoom` (signed-in + guest,
capacity 12), `reconnectRoom` (token validate + rotate), `leaveRoom`, `endRoom` (host-only, full
ephemeral erasure). A new `src/auth.ts` seam (`validateBearer`) resolves the `Authorization: Bearer`
header at WebSocket-upgrade time; fail-closed by default (`RELAY_DEV_AUTH` unset/not `"1"` refuses
every signed-in attempt with `unauthenticated`, matching the plan's "Standing constraints"). Every
session-scoped command binds to the connection's own participant — a client-asserted `senderId` that
disagrees with that binding is refused `not_authorized`, never trusted on its own.

**Architecture discrepancy, recorded and resolved:** the plan's header describes "Durable Object per
room". The frozen client (`WatchPartyUiController::configureServiceUrl`) only ever opens one static
service URL and sends `createRoom`/`joinRoom` *after* the socket is already connected — it never
learns a room-specific URL before connecting. There is therefore no signal `src/index.ts` could route
on to pick a per-room DO before the room exists. This slice keeps Slice 1/2's single fixed DO instance
(`env.ROOMS.idFromName("slice1-spike-room")`, `index.ts` unchanged) and has that one instance hold a
room *registry* (`Map<roomId, Room>`). Every room is still exclusively Durable-Object-owned, ephemeral,
in-memory state — only the one-DO-per-room shard granularity is deferred to a future deployment slice
(would need either a client change, frozen for this plan, or a lobby-hop redirect scheme).

**roomId format:** `WP-XXXX-XXXX` (8 random chars from a 32-symbol alphabet with ambiguous characters
`0/O/1/I` excluded, drawn via `crypto.getRandomValues`). Oracle: the client treats `roomId` as an
opaque string with no format validation beyond `WatchPartyUiController::trimmedRoomId`'s 128-character
single-line cap (`native/watchparty/WatchPartyUiController.cpp`) — any short human-typeable token
satisfies it. The `WP-XXXX-XXXX` shape mirrors the plan's own illustrative join-sheet placeholder
while staying inside a text field meant for a person to read aloud or type.

**Host-leave rule (oracle-derived, not invented):** `WatchPartyRoomController::leave()`
(`native/watchparty/WatchPartyRoomController.cpp`) refuses `HostMustEndRoom` when the host tries to
leave while other participants remain. This relay ports that exact rule (`not_authorized` refusal) —
without it, a host leaving a non-empty room would violate `roomSnapshot`'s "exactly one host" invariant
with no grace/transfer machinery yet to repair it (that's Slice 4). A lone host leaving still destroys
the room via the normal "everyone left" ephemeral-erasure path.

**Slice 4 message types** (`timelineCommand`, `setControlMode`, `participantState`,
`removeParticipant`, `chat`, `reaction`) are syntactically accepted by `protocol.ts` but not yet
processed here; they get a typed `invalid_message` refusal (the contract's closed error-code
vocabulary has no dedicated "not implemented yet" code) rather than a silent drop.

### Slice 3 evidence (2026-08-20)

- `npm test`: 56/56 green (38 protocol-conformance cases from Slice 2, unchanged, + 18 new
  `test/room-lifecycle.test.ts` cases covering create/join/reconnect/leave/end, capacity, identity
  binding, and the Slice-4 typed-refusal path).
- Flip-one-guard drill (a) — capacity: changed the 12-participant cap check from `>=` to `>` in
  `handleJoinRoom`; rerun went to 55/56, exactly `13th join attempt (capacity 12) is refused
  room_full, typed` red (plus expected client-side `uncaught exception` noise from the test's own
  `parseMessage` self-check rejecting the resulting 13-row snapshot as non-conformant — not a second
  silently-broken case); reverted, rerun back to 56/56.
- Flip-one-guard drill (b) — reconnect token rotation: commented out the `reconnectToken =
  generateReconnectToken()` line in `handleReconnectRoom`; rerun went to 54/56, exactly the two
  token-rotation cases red (`valid token restores the same participant identity and rotates the
  token` and `a reused, rotated-away reconnect token is refused unauthenticated` — both guarded by
  the same line, both went red, nothing else moved); reverted, rerun back to 56/56.
- `node test/probe-handshake.mjs` against a live `wrangler dev --local-protocol https`: `PROBE_OK`
  on all three cases. The "with-header-3" case was updated for Slice 3's real per-type dispatch: it
  used to send an arbitrary bogus `type: "probe"` frame and rely on Slice 1/2's blanket
  echo-scaffold (which replied `protocol_version_mismatch` to literally any frame, since no room
  logic existed yet); that blanket behavior is gone by design now that real dispatch exists — an
  unrecognized type correctly gets a non-terminal `invalid_message` reply and the socket stays open.
  The probe now sends a `version: 2` frame instead, which is still genuinely supposed to be terminal
  per the contract ("protocol/version mismatch is terminal for that connection") — `PROBE_OK
  case=with-header-3 upgrade=101 closeCode=1000 errorCode=protocol_version_mismatch`.
- `wrangler dev` process (cmd.exe host PID 9444, its two `node.exe` children, two `workerd.exe`
  children) killed by exact PID after the probe run; command-line-verified no `wrangler`/`workerd`
  process remained.

## Slice 4 — authority: timeline, control mode, presence, moderation, host grace, chat (2026-08-20)

`RoomDO` now implements every message type Slice 3 stubbed with the "not yet handled" typed
refusal: `timelineCommand` (authority per `controlMode` — host-only in Host Control, any current
participant in Shared Control; accepted commands mutate the authoritative timeline and emit
`timelineState` with the revision incremented by exactly 1, never reissued or regressed),
`setControlMode` (host-only, broadcasts a fresh `roomSnapshot`), `participantState` (bound to the
connection's own participant — no client-selected participant ID — broadcast as an authoritative
`participantState` event to every connected participant), `removeParticipant` (host-only, cannot
target the host; kicks by erasing the target's membership, sending it a terminal
`participant_removed`, closing its socket, then broadcasting the resulting `roomSnapshot` — no
ban/blacklist, so a fresh `joinRoom` afterward is evaluated under normal rules per the locked Arc-3
policy), `chat`/`reaction` (server-stamped identity/display-name/sequence, broadcast, never
persisted anywhere — not even Durable Object storage), and the per-connection rolling rate ceiling
(<=120 messages/10s, `rate_limited` typed refusal, checked before any parsing so a malformed-frame
flood still spends its own budget).

**Host disconnect grace and deterministic transfer:** a host socket dropping (without an explicit
`endRoom`) starts a grace window — `RELAY_HOST_GRACE_MS` (default 60000ms) — during which the room
survives and `roomSnapshot.hostReconnectDeadlineMs` reflects the deadline. A host `reconnectRoom`
within the window keeps ownership and clears the deadline. On expiry, the deposed host is erased
from the roster entirely (oracle: `WatchPartyRoomController::advanceTime()` — a disconnected
non-host participant keeps their slot for `reconnectRoom`, but an expired host does not), and the
earliest-joined connected signed-in participant becomes host (`hostChanged` then a fresh
`roomSnapshot`); guests are never eligible; with no eligible successor the room ends and is fully
erased (`roomEnded`).

**Durable Object alarm, one DO instance holding many rooms:** Slice 3's recorded architecture
discrepancy (this DO holds a *registry* of rooms, not one DO per room) means a single alarm clock
must serve every room's grace deadline. `scheduleGraceAlarm()` always sets the DO's one alarm to
the EARLIEST pending `hostReconnectDeadlineMs` across all rooms; `alarm()` re-checks every room
against the current clock (never fires unconditionally, even when forced early in tests) and
reschedules for whatever remains pending.

### Slice 4 evidence (2026-08-20)

- `npm test`: 73/73 green — 38 protocol-conformance (Slice 2) + 17 room-lifecycle (Slice 3, one
  stale placeholder assertion removed — see below) + 18 new `test/room-authority.test.ts` cases
  covering timeline authority/monotonic revision, control mode, participant-state fan-out,
  kick/fresh-rejoin, host grace/transfer/room-end, chat/reaction, and rate limiting.
- The Slice 3 placeholder test asserting `timelineCommand` got a blanket "not yet handled"
  `invalid_message` refusal is now factually wrong (that type is genuinely handled) and was
  replaced with a comment pointing at `test/room-authority.test.ts` — not silently dropped, its
  exact former assertion is what this slice implements.
- Flip-one-guard drill (a) — timeline authority: replaced `handleTimelineCommand`'s
  `participant.host || room.controlMode === "shared"` check with an unconditional `true`; rerun
  went to 72/73, exactly "a participant's timelineCommand is refused not_authorized under Host
  Control" red; reverted, rerun back to 73/73.
- Flip-one-guard drill (b) — guest-skip in host-grace transfer: removed the
  `candidate.identityKind !== "signedIn"` clause from `expireHostGrace`'s successor search; rerun
  went to 71/73 — both "grace expiry transfers to the earliest-joined connected signed-in
  participant, skipping guests" (a guest was picked instead of the signed-in second participant)
  and "grace expiry with only guests left ends and destroys the room" (a guest became host instead
  of the room ending) went red, plus the same benign client-side `parseMessage` self-check
  "uncaught exception" noise already documented for Slice 3's capacity drill (a guest-as-host
  snapshot violates `roomSnapshot`'s own "host must be signed in" invariant — not a second
  silently-broken case); reverted, rerun back to 73/73.
- Host-grace timers are tested with `vi.useFakeTimers()` + `vi.setSystemTime()` (so the DO's own
  `Date.now()` inside `alarm()` sees a deadline that has genuinely passed — the DO and the test run
  in the same isolate per `@cloudflare/vitest-pool-workers`) combined with
  `runDurableObjectAlarm(stub)` to force the alarm to fire immediately rather than waiting on the
  real Workers alarm scheduler. No real sleeps anywhere in `test/room-authority.test.ts`.
- `node test/probe-handshake.mjs` against a live `wrangler dev --local-protocol https`: `PROBE_OK`
  on all three cases, `overall=pass` — the transport/handshake layer is unaffected by Slice 4's
  application-layer additions.
- `wrangler dev` process tree (cmd.exe host, two `node.exe` wrangler processes, two `workerd.exe`
  children) killed by exact PID after the probe run; command-line-verified no
  `wrangler`/`workerd` process remained.
- Logging discipline: audited every `console.*` call added or touched this slice — there is exactly
  one (`sendTo`'s existing failure-path log, unchanged since Slice 1), and it logs only a connection
  id and the thrown error's string form, never a bearer, reconnect token, chat/reaction content, or
  source identity.

## Scope note

Slices 5+ (real-socket conformance probe, first real desktop client, two real clients, synced
playback, deploy) are runtime/acceptance work layered on top of the authority machinery this file
now fully implements.
