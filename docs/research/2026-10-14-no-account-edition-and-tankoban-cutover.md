# No-account edition + Tankoban D1 wiring — implementation and coverage

Date: 2026-10-14. Branch: `codex/d1-119-no-account` (base `3640e96e`).
Scope: the 1.1.9 no-account edition switch and the D1 semantic endpoint wiring
into this candidate. No merge of master or the server branch; no Neon, Biblio,
Feria or Vinyl change; no packaging, feed or release action.

## 1. Edition switch (smallest existing-style mechanism)

`COLOSSEUM_NO_ACCOUNT_EDITION` CMake option (default OFF) →
`COLOSSEUM_NO_ACCOUNT_EDITION=1` compile definition on the `colosseum` target,
mirroring the existing option/define pattern (UPDATE_TESTING,
RATINGS_REVIEWS_TESTING). Standard builds compile byte-identical behavior:
every change is behind the define or reads a new CONSTANT property that
returns true when it is absent from a controller (harness back-compat).

### Mechanics (edition ON)

- **No account-service traffic is constructible.**
  `AccountServiceEndpoint::configuredUrl()` returns an empty URL — env
  override, compile-time override and the production fallback
  (`https://colosseum-account-service.onrender.com`) are all suppressed.
  `AccountHttpTransport` fails every request on an empty base, so no
  authentication, refresh or sync call can leave the process.
- **Remembered sessions are never restored.**
  `AccountRuntime::prepareForQml()` calls `controller.continueWithoutAccount()`
  instead of `restoreRememberedSession()`; pending account deletion is not
  resumed (no UI exists to drive it; it stays pending for the account
  edition). Remembered vault credentials are left untouched.
- **Startup settles straight into the local-only profile** — the exact
  existing "Continue without account" path: onboarding completes durably
  (`account/onboardingCompleted`, `account/localOnlyChosen`), the LocalOnly
  profile prepares, and every personal store binds through
  ProfileStoreRuntime unchanged.
- **Every account entry surface is gated in QML** by the controller's new
  CONSTANT `accountFeaturesAvailable` property (false in edition builds):
  the TopBar account medallion (the only opener of AccountFlyout →
  AccountCenter, i.e. sign-in/registration/sync controls) and
  AccountOnboardingHost (the onboarding/sign-in/recovery cover). Stub
  controllers without the property keep legacy behavior for harnesses.
- **Edition identity for update targeting:** update requests now carry
  `Colosseum/<version> no-account` (`UpdateUserAgent.h`), and a
  `ColosseumEdition` context property ("no-account"/"standard") is published
  at startup for QML and diagnostics.
- **Identity preserved:** application name/org/version and all profile paths
  are untouched — an upgrade from 1.1.8 (local, local-only or signed-in)
  reuses the same `Roaming/Brotherhood/Colosseum` tree. A previously
  signed-in user boots into the local-only profile; their account profile
  directory and vault credentials remain on disk for the later account
  edition.
- **Kept intentionally:** AccountRuntime and ProfileStoreRuntime (sole owners
  of Progress/Collection/SearchHistory/AudioPairing/preferences/history/
  activity/tracker stores), Stremio sync (third-party, user-connected,
  accountless-capable) and the watch-party relay (separate service,
  user-triggered).

### Files (candidate)

- `native/CMakeLists.txt` — option, edition define, edition default of
  `COLOSSEUM_CONTENT_API_URL` to the staging endpoint (explicit cache value,
  including empty, always wins).
- `native/account/AccountServiceEndpoint.cpp` — edition suppression.
- `native/account/AccountController.h` — `accountFeaturesAvailable` property.
- `native/account/AccountRuntime.cpp` — `prepareForQml` edition branch.
- `native/update/UpdateUserAgent.h` — edition UA suffix.
- `native/main.cpp` — `ColosseumEdition` context property.
- `qml/TopBar.qml`, `qml/account/AccountOnboardingHost.qml` — entry gates.
- `tests/auto/no_account_edition/tst_no_account_edition.cpp` +
  `tests/CMakeLists.txt` registration (edition builds only).
- `tests/no_account_shell_gating_harness.qml` +
  `tests/test_no_account_shell_gating.ps1` — real-QML gating harness.

## 2. D1 semantic endpoint wiring (Tankoban lane only)

The candidate now compiles the staging semantic endpoint in by default
(edition builds): `COLOSSEUM_CONTENT_API_URL` seeds to
`https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev`
(HTTPS-origin validated by the existing block). Client behavior is unchanged
from the recorded integration: local SQLite authority, remote enrichment of
explicit-MAL-ID misses only, 32-entry cache, 5-minute revalidation,
source-hash invalidation, honest offline/unavailable states. If the endpoint
answers 503 (guest gate not yet deployed), remote stays inert — the wiring is
safe-but-inert until qualification completes.

Qualification state (honest, as of this commit):

- D1 staging: `tankoban__series` ready (10,000 rows);
  `tankoban__volumes` 25,504/28,839 rows imported 2026-10-14 (59K-write day
  budget reached; resume completes it — see the server branch note
  `2026-10-14-d1-volumes-import-and-guest-gate.md`).
- Guest gate: config + rate-limit binding committed on
  `codex/content-api-free`; deploy BLOCKED on expired wrangler auth (exact
  blocker + runbook recorded there). The currently deployed worker predates
  the guest routes entirely.
- Native-to-Worker live hook (`actual_worker_contract`) remains the
  unexecuted step until the deploy + volumes completion land; it was not
  bypassed or simulated.

## 3. Catalogue consumer inventory — why the downloader stays

Every catalogue consumer and the endpoint each needs before the legacy
GitHub downloader (`CatalogVaultClient` → `api.github.com/repos/
kingoftheseas56/Colosseum-Data` releases) can be disabled:

| Consumer | Data | Needs for cutover | Status |
|---|---|---|---|
| MangaSeries / TankobanSeriesView | Tankoban+MAL | series+volumes semantic (live once qualified); MAL identity rows still local | partial (enrichment only) |
| OnePieceArcCatalogue | Tankoban | async volumes adoption or local DB | not covered |
| MangaCatalogPage | MAL | filtered/ordered catalog endpoint | not covered |
| GenrePage / TheatreGenrePage | MAL | genre entries + counts | not covered |
| Theatre suite (TheatreApi/World/Series) | MAL anime + IMDb | anime catalog; titleCatalog/titleFacts | not covered |
| TankobanDiscoverApi facets | MAL + Comics | facet discovery (value+count, AND-filter) | not covered |
| SearchSurface / WorldSearch | Comics | ranked all-word search | not covered |
| VaultIdentifyDialog / VaultIdentifier | Comics+MAL+IMDb | cross-catalogue match/search | not covered |
| ContinueTile (home row) | IMDb | titlePresentation | not covered |
| DCAU / ComicSeries / releases | Comics | series detail, downloads+mirrors, curated shelves | not covered |

D1 side: 10/24 tables imported; the remaining 14 (MAL/Comics/IMDb ~1.1M
rows, ~2.76M estimated writes) need ≈46 more publisher-days at the 60K/day
self-imposed budget. **Decision: the downloader stays enabled in this
candidate.** Claiming a complete cutover now would be false; the endpoint
matrix above is the required coverage definition. Release-time GitHub REST
removal is re-evaluated per lane as each endpoint set is implemented,
imported and qualified.

## 6. Review-findings resolution (2026-10-14, second pass, post-dd2d8363)

**F3 — premature endpoint activation (resolved).** The edition switch no
longer seeds `COLOSSEUM_CONTENT_API_URL`; a fresh edition configure now
leaves it empty (verified: `COLOSSEUM_CONTENT_API_URL:STRING=` in a clean
`-DCOLOSSEUM_NO_ACCOUNT_EDITION=ON` cache). The staging endpoint ships only
via explicit qualification overrides, and a release build compiles it in only
after data readiness, deployed API contracts and native-to-Worker tests pass.
Explicit empty overrides stay empty.

**F5 — ordered update identity (implemented and proven).** The signed
manifest gains an optional `sequence` field (positive 32-bit integer;
zero/negative/fractional/string forms rejected by the parser). A manifest
carrying a sequence orders strictly by it, so the later approved account
1.1.9 (sequence 2) is offered to the earlier no-account 1.1.9 (sequence 1)
at an equal displayed version, while equal/older sequences stay `UpToDate`
(no repeat offers). Legacy unsequenced manifests keep version-only order —
safe because only 1.1.9+ clients read the first-party feed (1.1.8 updater
remains on its dead GitHub API path; first-party feed clients are all new
parsers). The installed side comes from the bundled signed chronicle seed
(`loadInstalledChronicle` reads its sequence), so both ends of the
comparison are signed artifacts. The seen/failed latches key by release
identity (`seq:N` when sequenced), so a corrected re-publication of the same
displayed version is offered again instead of staying suppressed. Release
process: author `sequence` in the presentation JSON (generator validates);
every published release gets a strictly higher sequence regardless of
displayed version. Proof: `update_service_harness` edition-ordering
scenarios (offer/equal/older/legacy/failure-latch/re-publication),
`update_manifest_trust_harness` sequence parse cases, tooling test
`test_presentation_sequence_orders_editions_in_signed_manifest` (11/11
pass).

**F4 — real runtime startup proof (implemented and proven).**
`tst_no_account_runtime` (edition builds only) drives the REAL
AccountRuntime/ProfileStoreRuntime/FirstAccountProfileCoordinator through
`prepareForQml()` with real profile ownership under per-case tagged
test-mode AppData. Six substantive cases (init/cleanup excluded), all green:
fresh startup settles a local profile with every personal store bound and a
loopback listener observing ZERO account-service requests; a populated
local profile restart preserves durable progress state; remembered
credentials are NOT restored (no traffic) yet remain stored for the account
edition; a pending account deletion stays pending without resume; legacy
and account profile trees are preserved untouched; a REAL coordinator
failure (corrupt profile-adoption journal) surfaces `mode "error"` +
`local_profile_prepare_failed` instead of being concealed, and recovers on
retry. The QML side was hardened to match: the onboarding host stays
visible in `mode "error"` under the edition, and AccountWelcome hides its
sign-in/create entries (leaving Continue-local as the retry surface) —
covered case-by-case in the extended `no_account_shell_gating` harness.
Known honest boundary: this is runtime + component proof, not a packaged
full-app launch; installer preservation remains separately blocked.

**F6 — cutover status correction (recorded; work continues).** The Tankoban
lane remains the only qualified lane. Volumes: 25,504/28,839 rows imported;
the remaining 3,335 rows (~7,720 estimated writes including chunk overhead)
are blocked by the 2026-10-14 budget day (58,978 reserved) and resume on
2026-10-15 UTC with the recorded one-command publisher resume. Budget
accounting correction: the publisher ledger counts RESERVED ESTIMATED
writes, not actual D1 rows written; actual account-wide usage is observable
only through Cloudflare analytics (blocked with the rest of wrangler auth),
so no free-plan headroom is claimed from the ledger. The MAL/Comics/IMDb
endpoint sets, their imports (~2.7M estimated writes ≈ 45-60 more publisher
days at the conservative 45-60K/day), native consumer integration, and
downloader disablement remain open in that order; the consumer matrix above
is the per-lane checklist. The downloader stays enabled; a fresh profile
still depends on it for every non-Tankoban lane.

**F1/F2 — credential rotation and worker configuration** live on the server
branch (`codex/content-api-free`); see
`docs/research/2026-10-14-d1-volumes-import-and-guest-gate.md` there for the
rotation tool, its derivation proof, the fixed rate-limit namespace, the
actual-config dry-run validation, and the exact wrangler-auth blocker that
keeps rotation/deploy unexecuted (and the incident unresolved).

## 4. Verification actually run (2026-10-14)

Isolated focused builds (MSVC 2022 / Qt 6.11.1 / Ninja), sources = this
candidate only:

- Edition build (`-DCOLOSSEUM_NO_ACCOUNT_EDITION=ON`): target
  `tst_no_account_edition` built clean; run: **6 passed, 0 failed** (87 ms) —
  endpoint suppressed even with `COLOSSEUM_ACCOUNT_SERVICE_URL` env canary;
  `accountFeaturesAvailable` false via property system; startup settlement
  lands `localOnly`, durably completes onboarding, prepares the profile, and
  the counting transport records **zero account-service sends**; settlement
  is idempotent.
- Standard build (edition OFF): `tst_account_service_endpoint` **3 passed**;
  `tst_tankoban_catalog` **16 passed, 1 skipped** (same live-worker skip as
  the recorded baseline); `tst_account_onboarding` **20 passed** — the
  property addition disturbs no onboarding contract.
- QML (real components, offscreen): `tests/test_no_account_shell_gating.ps1`
  → `no-account shell gating: OK` — TopBar hides the account medallion and
  AccountOnboardingHost never shows under the edition gate; stub controllers
  keep both surfaces (back-compat).
- Hosted: see §2 and the server-branch receipts (healthz, series pagination
  rows live, volumes honest 503, worker tests 11/11 with guest config).

Not verified (honest gaps): full-application edition runtime (no app build or
launch in this slice — the three-line `prepareForQml` branch is compile- and
unit-verified only); installer preservation (unchanged from prior blocker,
not retested, no bypass attempted); the live native↔Worker hook (blocked as
above).

## 5. Rollback

Edition OFF is the default: every changed surface is inert in standard
builds. To roll back the candidate: revert this commit (the wiring, gates,
tests and doc are one commit). Hosted rollback is recorded on the server
branch (guest lane returns to fail-closed without the secret/vars; imported
rows are additive and inert until `ready=1`).
