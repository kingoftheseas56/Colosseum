# Colosseum Watch Party relay — Slice 1 scaffold

Slice 1 of `docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md`. This slice proves ONE
thing: a production `colosseum.exe` can complete a real `wss://` handshake — with genuine
certificate verification, no client TLS exceptions — against a Cloudflare Workers relay running
locally. No room logic exists yet. The Worker checks the `X-Colosseum-Watch-Party-Protocol`
upgrade header (426 if missing/wrong); the Durable Object (`RoomDO`) accepts the socket, logs the
upgrade headers, and replies to any text frame with one protocol-v3 `error` envelope
(`code: "protocol_version_mismatch"`, the frozen wire code the desktop client already maps to its
`protocolVersionMismatch` error category — see `native/watchparty/WatchPartyTransport.cpp`), then
closes 1000.

## Layout

- `src/index.ts` — Worker `fetch`: routes WebSocket upgrades, enforces the protocol header at the
  edge (426 before ever reaching a DO), forwards to `RoomDO`.
- `src/room-do.ts` — the scaffold DO: accept, log headers, echo one `error` frame, close.
- `test/probe-handshake.mjs` — node/`ws` probe: asserts upgrade+frame with header `3`, and refusal
  (426) with header `2` or no header.
- `test/dev-ca.pem` — the CA cert this recipe imports (see below). Committed so the recipe is
  reproducible without re-running `openssl s_client` against a live `wrangler dev`.

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

## Scope note

This slice is a transport spike only. No `createRoom`/`joinRoom`/lifecycle/authority logic exists
— that is Slices 2–4 of the plan. Do not build room features on top of this DO without first
landing Slice 2's protocol/conformance layer.
