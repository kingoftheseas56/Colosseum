# Stremio Sync qualification

This record maps the approved product contract to executable evidence. It distinguishes deterministic Colosseum verification from checks that require an external account or deployed service.

| # | Contract behavior | Evidence | Verdict |
|---|---|---|---|
| 1 | Local and Neon profiles connect without password entry | `stremio_sync` browser-callback, identity-validation and vault cases; `account_attachment_runtime::stremioLegacyAccountlessProfileCanConnect`; official browser sign-in and first import on the daily profile | Pass |
| 2 | Profile-isolated accounts and secrets | Tagged/profile vault isolation, late-reply fences, account-device relay and shared-PC suites | Pass |
| 3 | Deletion-free first merge of library/progress/watch/addons | Library codec/reconcile cases, first addon merge/readback, committed visible result, panel result test | Pass |
| 4 | Subsequent two-way changes settle without echoes | Durable intent/receipt tests, provider rebase/readback tests, `stremioRuntimeRelaysAcrossAccountDevicesWithoutEcho`; empty watched-set and exhausted-intent recovery hardening; live queue drain and provider readback | Pass |
| 5 | Newest real activity wins | Native stale/equal/newer activity cases and Go Theatre progress/watch semantic-merge cases | Pass |
| 6 | Passive removals do not cross-delete; explicit dual removal retries | Durable membership/removal cases and `tst_theatre_removal_dialog.qml` | Pass |
| 7 | Labelled Stremio History without Stats inflation | History native/Go preservation cases, crash replay without activity fact, account-activity QML case | Pass |
| 8 | Provider failure cannot break playback or local persistence | Bounded retry/restart cases, owner persistence suites and existing playback/progress unit gates | Pass at deterministic layers; live provider-outage playback witness pending |
| 9 | New device reconnects and receives no secret/addon URL through Neon | Marker-only reconnect cases, payload firewall, addon/profile owner cases | Pass |
| 10 | Disconnect/switch removes the old credential and keeps merged data | Disconnect/switch generation-fence cases and panel confirmation case | Pass |
| 11 | Official icon, Theatre-only placement and accessible keyboard behavior | Asset provenance, top-bar QML case and tagged Theatre panel journey | Pass |
| 12 | One complete sync with no chooser/toggles/deferred provider UI | Panel QML contract and source-scope review | Pass |

## Security and negative controls

The qualification set explicitly exercises echo suppression, late replies from the wrong profile incarnation, secret/path rejection in both native and Go policy, and imported History without synthetic Activity/Stats facts. The tagged runtime projection and captured Theatre-panel session contain no credential, callback URL or configured addon URL.

## External boundaries

The deterministic fixture, native, QML and account-service tests use no personal account or private library. `TEST_DATABASE_URL` was unavailable on the qualification host, so disposable-Postgres integration tests remained skipped; the changed Go policy and merge tests ran in the full account-service suite. Official Stremio browser login and first import were witnessed on the designated daily profile. The credential stayed in Windows Credential Manager and was never printed into test or runtime evidence.

This branch is source-built and locally qualified. It is not a deployed release.

## Final run summary

- Native build: all 115 executable targets for the 147-test unit graph compiled.
- Native unit gate: 137/147 passed; all affected Stremio/account/sync/History/extension targets passed, with ten unrelated baseline or host-fixture failures recorded in the test ledger.
- Qt Quick aggregate: 657 passed, 13 failed, 3 skipped; all Stremio cases passed.
- Account service: `go test ./...` passed; disposable-database coverage was unavailable because `TEST_DATABASE_URL` was not configured.
- Runtime: fixture projection 4/4 in `20260920-173932-f06e4001`; accountless Theatre panel 12/12 in `20260920-174008-25a92f16`.
- Security: final runtime artifacts contain no credential, callback URL, configured addon URL or fixture secret.

## Outbound recovery hardening (2026-09-20)

The live account exposed three exhausted `series_watched` intents whose remote rows correctly encoded an empty watched set as `""`. `StremioCodec` now accepts that provider value, explicit **Sync now** revives exhausted intents once, and passive/background runs retain the bounded retry ceiling. A visible sync can no longer report success while its durable outbox is pending or exhausted; its success summary is published only after the acknowledged removals are durably committed.

The real provider then exposed three additional valid default shapes: an empty series watched set on inbound import, zero offset/duration with an empty or retained video id, and an empty `lastWatched` value for undated watched state. These now import as absent progress/activity while nonzero partial playback remains rejected. Root-id series progress is accepted because Stremio emits it for real series rows. Per-item failure logging contains only bounded media ids and errors, never credentials or addon URLs.

The Stremio Qt Test target passed 75/75, including both completion orderings between the inbound/addon pass and durable outbox drain. The account-attachment runtime target, focused core-sync target (44/44), and isolated Stremio panel QML target (6/6) passed. The earlier full registered unit gate ended 140/147; all Stremio cases passed and the seven remaining failures are unrelated existing host/static/timing gates recorded in the main test ledger.

The rebuilt daily app first remained `syncFailed` with three pending intents during passive startup, proving that background retry did not silently reset the ceiling. One human **Sync now** action drained all three intents. A credential-safe provider readback returned all three rows with nonempty watched fields. After the real default-shape repairs, a fresh automatic sync reached `synced` with `pendingCount=0` and the summary `Sync complete · 85 library items · 19 addons`; a later passive run remained green at `completedRun=2`. The credential remained in Windows Credential Manager and was never printed.
