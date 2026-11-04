# Assembled Collections sharing qualification

These scripts use the real account-service/PostgreSQL, actual Colosseum application, real native stores/controllers, and actual browser. They are not API mocks or replacement UI.

## Cloud setup

Source an appropriate Qt/Go/native dependency environment. The delivered cloud run used `/workspace/shared/toolchains/env.sh`. `run_service_browser.sh` provisions a new `_test` database and runs the checked-in actual-service browser test. `run_service_native.sh` starts an independent database/service, seeds one synthetic account and gift through real HTTP endpoints, then runs the matching built application in `first`, `offline`, and `owner` phases. Every phase finishes only when its fixture directory receives a `finish` file. Use a fresh EVIDENCE directory; neither launcher overwrites an existing database.

The Linux sandbox disallows Unix sockets and isolates loopback between exec calls. PostgreSQL, service, app or browser therefore run in the same exec; no network restriction is relaxed. The normal Lanista socket cannot work here. For native acceptance, compile `COLOSSEUM_ENABLE_COLLECTIONS_TEST_FIXTURES=ON`; `InProcessLanistaFixture.h` routes bounded JSON requests into the **same real Lanista command dispatcher**, with its ordinary drive/write gates, replies and screenshots. This is explicitly an instrumented native application, not a socket-transport qualification.

The adapter is absent from shipping builds. It requires the existing exact synthetic marker/root/tag guard, explicit drive flag, safe run name, bounded input/commands/time, no symlink tree and no overwritten receipts. It exposes no QML evaluator, arbitrary native invocation or network listener. `file_client.py` is the observed command driver. Request/response JSON files are in `runtime-PHASE` beneath the synthetic app-data root named in `native-root.txt`.

The native synthetic build reuses the existing `AccountRuntimeCredentialOptions` + in-memory credential-store seam only inside the marked fixture. Actual AccountController and account HTTP authentication remain unchanged. **Linux OS keyring/credential persistence is NOT qualified** by these journeys. Credentials live only in process memory; receiving/offline persistence checks use a local profile, not a fabricated remembered login.

The offline phase targets a closed loopback receiving-service port. This verifies retained native state without its publication service; it does not establish a machine-wide network outage, live-provider resolution or playback.

## Evidence handling

Record source commit plus working diff identity, executable hashes, commands/replies and actual screenshots. `file_client.py` redacts typed password/passphrase fields in publishable command receipts. Do not commit `synthetic-seed-private.json`, raw runtime request files, database contents, service secrets or account session tokens. Screenshots must be inspected, not merely generated. Distinguish seed API actions, native UI actions, browser runtime, unit tests and unrun Windows/Android checks.
