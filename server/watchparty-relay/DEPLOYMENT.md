# Watch Party Relay — deployment

The relay is a Cloudflare Worker + Durable Object speaking the frozen protocol v3
(`SERVER-PROTOCOL-CONTRACT.md`, Preflight arc 03). The desktop client is shipped and
conformance-tested against it (73-case relay gate, 12-step live-socket probe, one- and
two-client acceptance runs — see README). Rooms are ephemeral: full state erasure on end,
no durable personal data, tokens and bearers never logged.

## Deploying the hosted default (Hemanth's account)

One-time: `npx wrangler login` (interactive, account owner present). Then, from this
directory:

```
npx wrangler deploy
```

The workers.dev URL it prints is the release endpoint. Configure the app with:

```
COLOSSEUM_WATCH_PARTY_URL=wss://<worker>.workers.dev
```

Unset, the app ships fail-closed: the Join sheet states the service is not configured and
nothing else changes — verified in acceptance (Slice 6 leg S, solo journey 33/33 with the
feature dormant).

## Deployed environment contract

- `RELAY_DEV_AUTH` — MUST be unset/0 in any deployed configuration. With no bearer
  validator configured, signed-in connects are refused (typed error) and guest join works.
  Verify after every deploy: a signed-in connect against the deployed endpoint must be
  refused (the Slice 5 probe's refusal matrix, pointed at the deployed URL).
- `RELAY_HOST_GRACE_MS` — host-disconnect grace window; default 60000.
- Rate ceiling ≤120 msgs/10s/connection and the 64 KiB frame cap are enforced in code.

## Boundaries (state these honestly wherever the endpoint is announced)

- **Public signed-in hosting is gated on the account service being publicly deployed**
  (it runs only on 127.0.0.1:8080 today) plus a bearer-introspection surface the relay's
  `src/auth.ts` seam can call. Until then a public relay serves guest joins into rooms
  created by tooling or future signed-in clients; hosted rooms from real accounts await
  the account lane.
- **v1 is single-shard:** the shipped client connects to one static URL before any room
  exists, so all rooms live in one registry DO (`ROOMS.idFromName`). Fine at
  private-party scale (rooms cap at 12); revisit per-room sharding only with evidence.

## Self-hosting (the Harbor-style option)

Any user with a free Cloudflare account can run their own relay: clone the repo,
`cd server/watchparty-relay && npm install && npx wrangler login && npx wrangler deploy`,
then point `COLOSSEUM_WATCH_PARTY_URL` at their own workers.dev URL. No central server is
required; media never passes through the relay — only sync frames.

## Verification status at arc close (2026-08-20)

- Relay: protocol conformance 73/73; live-socket journey + refusal matrix 12/12; two real
  clients proven for membership/chat-wire/kick/rejoin/grace/end (Runtime-validated).
- Synced playback end-to-end: the automated drive is committed
  (`test/accept-slice8-synced-playback.ps1`) and hard-assertion clean up to the sources
  sheet; final in-app playback sync was not machine-verified — by Hemanth's ruling
  (2026-08-20) it goes to field testing, with GitHub issues as the signal. The known
  residual risk tracks machine contention during acceptance, not relay or protocol logic.
- Local dev TLS: wrangler's bundled cert must be trusted per README for local runs; the
  arc-end cleanup removed it from the machine's user Root store. Deployed endpoints use
  real Cloudflare TLS — none of this applies.
