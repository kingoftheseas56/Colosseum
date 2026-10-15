# Arc 62 Vinyl - BA2-D-B2 Runtime Playlist-Entry Attribution Handoff

> Continuation status (2026-10-06): BA2-D-B2 and BA2-D-B3 are complete and verified. Wave 2 is sealed locally. The body below is retained as historical pre-B2/B3 context; current authority is `BA2D1-MPV-EVENT-IDENTITY-EVIDENCE.md`.

Date: 2026-10-06
From: BA2-D-B1 Raw mpv API Qualification
State: BA2-D-A + BA2-D-B1 complete and verified; BA2-D-B2 not started
Production Colosseum: read-only

## Entry point

Continue Arc 62 Vinyl backend with **BA2-D-B2 Runtime Playlist-Entry Attribution**.

Read completely:

- `BA2D-MPV-ADOPTION-CONTRACT.md`;
- `BA2D1-MPV-EVENT-IDENTITY-EVIDENCE.md`;
- `HANDOFF-BA2DB-RAW-MPV-CALLBACK-SEEK.md`;
- `ba2d-mpv-adoption-prototype/CMakeLists.txt`;
- `ba2d-mpv-adoption-prototype/mpv_api_probe.cpp`.

BA2-A/B/C, BA2-D-A, and BA2-D-B1 are frozen unless this checkpoint proves a concrete incompatibility.

Do not modify production Colosseum in this checkpoint.

## Verified foundation

BA2-D-A already verifies the tokenized abstract player bridge.

BA2-D-B1 independently proves that the installed concrete API surface exposes:

- `MpvController::mpv() -> mpv_handle*`;
- `mpv_create_weak_client`;
- `mpv_request_event`;
- `mpv_wait_event`;
- `mpv_set_wakeup_callback`;
- `mpv_destroy`;
- raw START_FILE / FILE_LOADED / END_FILE / SEEK / PLAYBACK_RESTART events;
- START_FILE and END_FILE playlist-entry identity;
- raw END_FILE error evidence.

Verification for the current build:

- direct `vinyl_ba2d_mpv_api_probe.exe`: exit 0;
- CTest at 14:52 IST: 2/2 PASS.

The configured build tree:

`ba2d-mpv-adoption-prototype/build-ba2d-vs2/`

is intentionally retained as non-authoritative scratch so this checkpoint can avoid another expensive full configure when possible.

## Critical negative control

A raw-libmpv runtime probe already issued two successful:

`loadfile ... replace`

commands back-to-back.

Only:

`START_FILE entry=2`

was observed.

The first issued load was replaced before it generated START_FILE.

Therefore **FIFO mapping from issued Vinyl load tokens to later START_FILE events is invalid**.

Do not reintroduce it.

## BA2-D-B2 objective

Prove the smallest runtime mechanism that associates each successful concrete load command with the exact mpv playlist-entry identity that owns later raw events.

The target relation is:

`PlaybackWorkToken <-> mpv playlist_entry_id`

This checkpoint owns load/observation attribution only.

Exact seek-request completion is reserved for BA2-D-B3.

## First proof to attempt

Using the smallest local probe against the installed runtime, test whether immediately after a successful synchronous:

`loadfile <resource> replace`

the same core exposes the newly assigned current entry ID through documented playlist properties such as:

`playlist/0/id`

or an equivalent documented property.

The proof must cover at least:

1. load A -> capture concrete entry ID A;
2. load B immediately replaces A -> capture concrete entry ID B;
3. A and B use distinct entry IDs;
4. same resource loaded twice still yields distinct load identities;
5. raw START_FILE / END_FILE identity can be matched to the captured IDs;
6. a load that is replaced before START_FILE does not cause token B to be mislabeled as token A.

Do not infer identity from URL equality.

## Raw event observation

Prefer a separate weak libmpv client on the same core if it can observe the needed raw events without interfering with MpvQt's normal client.

The weak-client path is now compile-qualified, but runtime behavior still requires proof.

If a weak client does not receive the required events, record that result and choose the next smallest evidence-backed mechanism.

## If immediate property retrieval fails

Do not fall back to FIFO.

Investigate one bounded alternative:

- an async command/reply handshake tied to the same load operation;
- a documented playlist property/event sequence that yields the new entry ID;
- a minimal native player extension that returns or records the concrete entry ID when issuing load.

If none can be proven inside this checkpoint, persist the exact gap and stop.

## Observation ownership

Once a START_FILE establishes the currently started playlist-entry ID, later observations such as:

- FILE_LOADED;
- pause;
- position;
- duration

may be routed through the token bound to that concrete entry only if the runtime proof demonstrates the association remains valid.

Do not stamp observations with merely "the current Vinyl token."

EOF/failure should use the raw END_FILE playlist-entry ID when available.

## Header loads

BA2-D-B2 may inspect how the same physical load identity mechanism behaves for header-bearing loads.

It must not silently collapse duplicate BA2-B headers into QVariantMap semantics.

If duplicate-header conversion remains unresolved, record it as a separate concrete adoption issue rather than weakening BA2-B input truth.

## Acceptance

BA2-D-B2 is complete only when a deterministic local runtime proof establishes or rejects a concrete load identity mechanism.

A successful proof should show:

1. issued load can be associated with a concrete mpv entry ID without FIFO;
2. replacement-before-START_FILE is handled correctly;
3. same-resource reloads remain distinct;
4. START_FILE/END_FILE preserve historical token ownership;
5. stale old-entry callbacks can be forwarded with the old token and rejected by BA2-A;
6. current entry observations can be routed without consulting BA1, Recording MBID, provider ID, or URL identity;
7. production Colosseum remains unchanged.

## Hard boundary

BA2-D-B2 is a guardrail-created sub-slice.

Finish its runtime attribution proof, persist the result, then stop.

**Do not start BA2-D-B3 exact seek-completion attribution in the same turn.**

BA2-D-B3 requires another later explicit `continue`.

## Recommended prompt

```text
@Brotherhood Desktop Commander
@Preflight Architect

Continue Arc 62 Vinyl backend with BA2-D-B2 Runtime Playlist-Entry Attribution.

First read completely:
arcs/62-vinyl/backend/HANDOFF-BA2DB2-PLAYLIST-ENTRY-ATTRIBUTION.md

Use the existing verified BA2-D-A / BA2-D-B1 state. Do not use FIFO load-token attribution.

First prove or reject the exact loadfile -> mpv playlist-entry identity mapping with the smallest bounded runtime probe.

Work only in Preflight-Architect.
Production Colosseum remains read-only.
```
