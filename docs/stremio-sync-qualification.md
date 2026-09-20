# Stremio Sync qualification

This record maps the approved product contract to executable evidence. It distinguishes deterministic Colosseum verification from checks that require an external account or deployed service.

| # | Contract behavior | Evidence | Verdict |
|---|---|---|---|
| 1 | Local and Neon profiles connect without password entry | `stremio_sync` browser-callback, identity-validation and vault cases; `account_attachment_runtime::stremioLegacyAccountlessProfileCanConnect` | Automated path passes; official-account witness pending |
| 2 | Profile-isolated accounts and secrets | Tagged/profile vault isolation, late-reply fences, account-device relay and shared-PC suites | Pass |
| 3 | Deletion-free first merge of library/progress/watch/addons | Library codec/reconcile cases, first addon merge/readback, committed visible result, panel result test | Pass |
| 4 | Subsequent two-way changes settle without echoes | Durable intent/receipt tests, provider rebase/readback tests, `stremioRuntimeRelaysAcrossAccountDevicesWithoutEcho` | Pass |
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

The deterministic fixture, native, QML and account-service tests use no personal account or private library. `TEST_DATABASE_URL` was unavailable on the qualification host, so disposable-Postgres integration tests remained skipped; the changed Go policy and merge tests ran in the full account-service suite. Official Stremio browser login also remains unverified until a designated test account is supplied and its user completes the external sign-in. Neither gap authorizes use of a personal account.

This branch is source-built and locally qualified. It is not a deployed release.

## Final run summary

- Native build: all 115 executable targets for the 147-test unit graph compiled.
- Native unit gate: 137/147 passed; all affected Stremio/account/sync/History/extension targets passed, with ten unrelated baseline or host-fixture failures recorded in the test ledger.
- Qt Quick aggregate: 657 passed, 13 failed, 3 skipped; all Stremio cases passed.
- Account service: `go test ./...` passed; disposable-database coverage was unavailable because `TEST_DATABASE_URL` was not configured.
- Runtime: fixture projection 4/4 in `20260920-173932-f06e4001`; accountless Theatre panel 12/12 in `20260920-174008-25a92f16`.
- Security: final runtime artifacts contain no credential, callback URL, configured addon URL or fixture secret.
