# Tankoyomi signed device addons — 2026-10-25

**Status: device execution and signed delivery deployed; MangaSin reading verified in the production Windows app; LectorXD live reading remains blocked.** No 1.1.9 v2 release, installer, master merge or update-feed publication is included.

## Execution and trust boundary

- Only exact `es/mangasin` and `es/lectorxd` registrations use device execution. All other Tankoyomi providers retain their Worker execution path. Provider order, user enablement and disabled defaults retain their existing configuration owner.
- The app downloads an Ed25519-signed envelope from fixed Worker routes. A separate addon public key is embedded in native code. The private key is encrypted outside the repository, with its passphrase protected by Windows DPAPI for the signing user; no private signing material is deployed.
- The signature covers `Colosseum Tankoyomi addon v1\n` followed by the exact decoded payload bytes. The payload binds addon ID, provider ID, language, revision, host permissions, script bytes and SHA-256. Native validation rejects identity changes, broadened host permissions, invalid encoding and script-digest mismatches before execution.
- Verified envelopes are cached atomically and their signatures are checked again on load. Refresh failures retain a valid cached envelope. Revision rollback and conflicting scripts at the same cached revision are rejected. The cache cannot establish freshness after all local history is removed; signatures do not prove a source is reachable.
- Provider scripts use the existing QJSEngine runtime and a restricted network/crypto bridge. This is not an OS-level sandbox. Requests use an isolated source network manager, public-HTTPS/DNS/redirect checks, a 40-request operation ceiling, 8 MiB reply and 16 MiB aggregate limits, a 45-second operation deadline and interruptible five-second script slices. The unchanged Worker transport retains its two-MiB reply limit.
- Source scripts remain remotely updatable. The app contains the generic runner and trust root, while downloads, images, canonical identities, settings and readers retain their existing native owners. Provider scripts are not added to production application resources.

## Published delivery

- Existing service: `colosseum-tankoyomi`; manifest revision `0.2.2`, contract `1`.
- Current deployment version: `b47e8712-567e-4063-94a2-cc69cd7c430e`.
- `/healthz`, `/manifest.json`, `/addons/v1/es/mangasin.json` and `/addons/v1/es/lectorxd.json` returned HTTP 200. Public MangaSin revision 2 and LectorXD revision 1 envelopes verified against the native public key; raw script SHA-256 values and local source bytes matched.
- MangaSin revision 2 script SHA-256: `71019e8502fc93081d304bfb2847919b77ffe8e9bebaa4bc9167aeae35707d7a`.
- LectorXD script SHA-256: `aa7a53a4ee4c81252e57e1c212fefff6ee2ad1cf1b1cb53cdef5b274d5a790d3`.
- No other Worker or hosting service was deployed or changed.

## Verification

- Worker suite after publication of MangaSin revision 2: 20 tests passed, including pinned production signatures, script/source identity, offline signing, fixed routes and prior transport contracts. Wrangler dry bundle passed (147.59 KiB, gzip 37.32 KiB).
- Native store harness passed signature-tampering, cross-provider/language replay, host permissions, digest/encoding/size, verified-cache fallback and revision-rollback cases.
- Native runtime harness exercised the actual MangaSin script with deterministic local signed/network fixtures, including chapter decryption and image URL parsing. It also covered LectorXD's explicit unsupported search, Astro chapter and reader parsing, hard request-budget failure even when script errors are caught, and interruption of an infinite script.
- The production Windows `colosseum_runtime_ready` target built successfully. Five focused CTest cases passed: addon store, device runtime, Worker transport, legacy script transport and service configuration. Store/runtime tests passed again against the final MangaSin source. Both Spanish parser contracts, the JavaScript runtime contract and extension inventory contract passed. No full-suite, Linux or Android runtime claim is made.
- During receiving verification, explicit C++ ownership was added to the provider's JavaScript wrapper after a stack-owned LectorXD fixture exposed engine-driven destruction. Invalid Worker URLs retain their previous asynchronous failure behavior. No failed or interrupted build is treated as app proof.

## Actual app results

Both checks loaded the real `Main.qml` and native services using the rebuilt production executable, with separate temporary app-data profiles and the public Worker endpoint. Qt's offscreen platform rendered the actual UI; saved captures were visually inspected. Normal account and provider preferences were not used or modified.

- Executable SHA-256: `655cf563cb22743e9a75a08e1feebdae726249ca293db69352d2fc8860af1901`.
- **MangaSin: successful live reading.** The app verified and cached signed revision 2, discovered 837 One Piece chapters, selected ordinary chapter 1196, downloaded its 16 images (about 6.0 MB) through the native downloader and opened the real reader with `ready=true`. The captured spread displayed readable Spanish dialogue. Driver exit: 0.
- This was also a real addon-update test: revision 1 loaded and found chapters, but image requests failed because the page results omitted the canonical chapter Referer. Two otherwise identical bounded GETs changed from HTTP 403 to HTTP 200 when that header was added. Revision 2 adds `referer` to provider page rows, allowing the existing native transport to send it. The same app binary then completed reading after fetching the signed update; no image/downloader transport code was changed.
- Earlier chapter 1 attempts found historical Blogspot image URLs returning HTTP 404. Those failed results are retained; one recent successful chapter does not establish availability of every historical image.
- **LectorXD: delivery/error handling verified; live reading unqualified.** The app verified and cached signed revision 1, visibly displayed `LectorXD title search is unsupported until a verified search route is available`, and produced zero chapters with no provider/language fallback. Driver exit: 20 (blocked, not a successful reader check). Separate ordinary direct GETs to its home, catalogue, known series and reader URLs returned HTTP 403. Fixture parsing success does not establish live access or title discovery.
- Unrelated existing startup diagnostics (including `universeHallLayer` and optional cover failures) remained visible in the logs and were not changed.

## Release gate

LectorXD needs a verified title-discovery protocol, usable ordinary device access and a complete actual-app reader check. Its signed script can be corrected through the Worker once these are established. Both disabled manifest defaults and existing user preference overlays remain unchanged. MangaSin has a positive Windows app result; Linux/Android runtime and release packaging remain separate checks. Keep the draft open and hold the 1.1.9 v2 release while required qualification remains incomplete.
