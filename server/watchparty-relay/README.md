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

## Scope note

This slice (2) adds the protocol core and conformance suite only. No `createRoom`/`joinRoom`/
lifecycle/authority room logic exists yet — that is Slices 3–4 of the plan. `RoomDO` still has no
per-room state; it remains the Slice 1 echo scaffold, now speaking through `protocol.ts`. Do not
build room features on top of this DO without reading `src/protocol.ts`'s exported builders and
validation first — they are the seam Slice 3 extends.
