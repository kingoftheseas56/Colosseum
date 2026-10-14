# Direct prompt for Dots — no-account 1.1.9 / Tankoban

Build and test the Colosseum 1.1.9 no-account candidate in your own Linux cloud
desktop. Keep Neon, Biblio, Feria and Vinyl out of feature work and test journeys.
Do not publish a release, activate an update feed or modify Cloudflare data,
secrets or billing. No GitHub REST dependency; Git transport is available.

Use `kingoftheseas56/Colosseum`, branch `codex/d1-119-no-account`, pinned at
`dfb4b8c90926ec0862a5612c3cc59faca1f17c51`. Verify the exact source pin before
building; do not substitute master or an older packaged executable. Use your
existing build/cache or connected alternate fork if useful, keeping the source
pin unchanged. Consult the repository Linux build instructions and record the
actual toolchain and dependencies.

Configure these explicit test-build values:

```text
COLOSSEUM_NO_ACCOUNT_EDITION=ON
COLOSSEUM_CONTENT_API_URL=https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev
```

Tankoban staging has been fully uploaded and independently verified: 10,000
series, 28,839 volume rows, both ready. Its source SHA256 is
`04d485c6a559fdc7b6d1d651a622befd77af8bf93b2c7bdbcf7ebbd7bdd7e5a1`.
Guest session request is `POST /v1/guest/session` with
`{"appId":"colosseum","apiVersion":1}`. Use its short-lived token only for
`GET /v1/tankoban/series/<malId>`; no admin token is needed or supplied. Keep
tokens out of returned logs. Other catalogue imports and API cutovers are not
complete; do not claim whole-product migration from Tankoban results.

Produce a matching native build and run the focused no-account edition/runtime,
shell gating, Tankoban catalogue and updater tests. Preserve the application
identity and local progress keys. Isolate any updater test-signing configuration;
do not change the candidate's production verification key. The existing `actual_worker_contract` Qt test
is a synthetic-fixture test with fixed expected values; run it against a matching
local instance of the actual Worker, not staging MAL 13, and label fixture
evidence separately. Obtain separate live staging evidence through the native
client and real application.

Launch the actual graphical application with an isolated profile and
`tankoban_catalog.db` absent. Prove native Qt requests reach the live guest and
Tankoban routes without relying on an old local Tankoban database. Exercise real
Tankoban UI interactions and capture redacted request logs and screenshots.
Check MAL 13 (113 volumes), MAL 1 (8), MAL 2 (zero), MAL 3 (22, including synthetic
entries), MAL 134233 (49, including newly uploaded data) and unknown 2147483646
(empty seriesInfo/volumes). Verify numeric ordering, exact strings and covers
where the UI supports them. Include the One Piece tribute consumer
`qml/OnePieceArcCatalogue.qml`; inspect its asynchronous remote integration and
report any stale local-readiness assumptions as a failure.

Verify no-account startup, unreachable sign-in/registration routes and no account
service traffic. Exercise representative local Tankoban progress, collections
and history using their real owners; restart the app and verify preservation.
Use isolated seeded returning-profile data for preservation checks, never real
user credentials or personal files.

After browsing, restart offline and report exactly what remains available.
Durable remote-catalogue caching is not previously qualified; an empty cache or
lost catalogue after restart is a FAIL to document, not a passed check. Exercise
unavailable service, expired/invalid lease, cancellation and late replies with
controlled fixtures where appropriate. Distinguish fixture checks from live
staging behavior. Record any legacy downloader activity rather than claiming it
has already been removed.

Keep live testing small: at most 20 semantic requests initially, spread to stay
below the 30-per-minute per-IP guest rate limit. Do not issue load tests or mass
catalogue scans. If further live requests are needed, first state their bounded
cost and reason. The upload has already passed full content verification.

Return PASS/FAIL/BLOCKED/NOT RUN per journey, source SHA, build flags, executable
SHA256, environment/toolchain, focused test outputs, real GUI screenshots,
redacted network evidence, clean-exit result and exact remaining defects. If you
make a narrow in-scope repair, keep its commit and rebuilt artifact separate
from the original pinned baseline evidence. Put one concise delivery record in
Dots-Colosseum pointing to the real branch/artifact/evidence. Linux evidence does
not establish Windows installer-upgrade preservation. Do not require a paid
Astra review or claim release readiness from upload or build success alone.
