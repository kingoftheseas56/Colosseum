# Tankoyomi signed device addons — 2026-10-25

**Status: MangaSin is the only new active Spanish source; LectorXD is parked by the user's later October 25 decision.** MangaSin reading was verified in the production Windows app. No 1.1.9 v2 release, installer, master merge or update-feed publication is included.

## Execution and trust boundary

- Only the exact `es/mangasin` registration uses production device execution. All other registered Tankoyomi providers retain their Worker execution path. LectorXD is not registered or published. Provider order, user enablement and MangaSin's disabled default retain their existing configuration owner.
- The app downloads an Ed25519-signed envelope from fixed Worker routes. A separate addon public key is embedded in native code. The private key is encrypted outside the repository, with its passphrase protected by Windows DPAPI for the signing user; no private signing material is deployed.
- The signature covers `Colosseum Tankoyomi addon v1\n` followed by the exact decoded payload bytes. The payload binds addon ID, provider ID, language, revision, host permissions, script bytes and SHA-256. Native validation rejects identity changes, broadened host permissions, invalid encoding and script-digest mismatches before execution.
- Verified envelopes are cached atomically and their signatures are checked again on load. Refresh failures retain a valid cached envelope. Revision rollback and conflicting scripts at the same cached revision are rejected. The cache cannot establish freshness after all local history is removed; signatures do not prove a source is reachable.
- Provider scripts use the existing QJSEngine runtime and a restricted network/crypto bridge. This is not an OS-level sandbox. Requests use an isolated source network manager, public-HTTPS/DNS/redirect checks, a 40-request operation ceiling, 8 MiB reply and 16 MiB aggregate limits, a 45-second operation deadline and interruptible five-second script slices. The unchanged Worker transport retains its two-MiB reply limit.
- Source scripts remain remotely updatable. The app contains the generic runner and trust root, while downloads, images, canonical identities, settings and readers retain their existing native owners. Provider scripts are not added to production application resources.

## Initial dual-source delivery (historical)

- Existing service: `colosseum-tankoyomi`; manifest revision `0.2.2`, contract `1`.
- Initial deployment version: `b47e8712-567e-4063-94a2-cc69cd7c430e`; superseded by the MangaSin-only follow-up below.
- `/healthz`, `/manifest.json`, `/addons/v1/es/mangasin.json` and `/addons/v1/es/lectorxd.json` returned HTTP 200. Public MangaSin revision 2 and LectorXD revision 1 envelopes verified against the native public key; raw script SHA-256 values and local source bytes matched.
- MangaSin revision 2 script SHA-256: `71019e8502fc93081d304bfb2847919b77ffe8e9bebaa4bc9167aeae35707d7a`.
- LectorXD script SHA-256: `aa7a53a4ee4c81252e57e1c212fefff6ee2ad1cf1b1cb53cdef5b274d5a790d3`.
- No other Worker or hosting service was deployed or changed.

## Initial dual-source verification (historical)

- Worker suite after publication of MangaSin revision 2: 20 tests passed, including pinned production signatures, script/source identity, offline signing, fixed routes and prior transport contracts. Wrangler dry bundle passed (147.59 KiB, gzip 37.32 KiB).
- Native store harness passed signature-tampering, cross-provider/language replay, host permissions, digest/encoding/size, verified-cache fallback and revision-rollback cases.
- Native runtime harness exercised the actual MangaSin script with deterministic local signed/network fixtures, including chapter decryption and image URL parsing. It also covered LectorXD's explicit unsupported search, Astro chapter and reader parsing, hard request-budget failure even when script errors are caught, and interruption of an infinite script.
- The production Windows `colosseum_runtime_ready` target built successfully. Five focused CTest cases passed: addon store, device runtime, Worker transport, legacy script transport and service configuration. Store/runtime tests passed again against the final MangaSin source. Both Spanish parser contracts, the JavaScript runtime contract and extension inventory contract passed. No full-suite, Linux or Android runtime claim is made.
- During receiving verification, explicit C++ ownership was added to the provider's JavaScript wrapper after a stack-owned LectorXD fixture exposed engine-driven destruction. Invalid Worker URLs retain their previous asynchronous failure behavior. No failed or interrupted build is treated as app proof.

## Initial dual-source app results (historical)

Both checks loaded the real `Main.qml` and native services using the rebuilt production executable, with separate temporary app-data profiles and the public Worker endpoint. Qt's offscreen platform rendered the actual UI; saved captures were visually inspected. Normal account and provider preferences were not used or modified.

- Executable SHA-256: `655cf563cb22743e9a75a08e1feebdae726249ca293db69352d2fc8860af1901`.
- **MangaSin: successful live reading.** The app verified and cached signed revision 2, discovered 837 One Piece chapters, selected ordinary chapter 1196, downloaded its 16 images (about 6.0 MB) through the native downloader and opened the real reader with `ready=true`. The captured spread displayed readable Spanish dialogue. Driver exit: 0.
- This was also a real addon-update test: revision 1 loaded and found chapters, but image requests failed because the page results omitted the canonical chapter Referer. Two otherwise identical bounded GETs changed from HTTP 403 to HTTP 200 when that header was added. Revision 2 adds `referer` to provider page rows, allowing the existing native transport to send it. The same app binary then completed reading after fetching the signed update; no image/downloader transport code was changed.
- Earlier chapter 1 attempts found historical Blogspot image URLs returning HTTP 404. Those failed results are retained; one recent successful chapter does not establish availability of every historical image.
- **LectorXD: delivery/error handling verified; live reading unqualified.** The app verified and cached signed revision 1, visibly displayed `LectorXD title search is unsupported until a verified search route is available`, and produced zero chapters with no provider/language fallback. Driver exit: 20 (blocked, not a successful reader check). Separate ordinary direct GETs to its home, catalogue, known series and reader URLs returned HTTP 403. Fixture parsing success does not establish live access or title discovery.
- Unrelated existing startup diagnostics (including `universeHallLayer` and optional cover failures) remained visible in the logs and were not changed.

## MangaSin-only follow-up

The user confirmed LectorXD was also blocked in their normal browser and directed: "leave lectorxd, we add only mangasins". LectorXD is removed from manifest 0.2.3, the production provider map, production device registration and signed delivery routes. Its parser source, signed snapshot and deterministic fixtures remain parked in the repository. No source setting is erased or migrated; existing preference overlays for other sources retain their owner. MangaSin's signed revision 2 and script bytes are unchanged.

Before that decision, fresh catalogue/home GETs returned Cloudflare-branded HTTP 403 blocks; known series and reader GETs returned HTTP 403 with `cf-mitigated: challenge`. Adding Referer did not resolve catalogue access. Current primary adapter/site evidence did not establish a title-query protocol. [Cloudflare documents several possible causes of a branded 403](https://developers.cloudflare.com/support/troubleshooting/http-status-codes/4xx-client-error/error-403/); these responses do not establish which specific rule was responsible. Investigation stopped when the user parked the source.

The browser tool failed initialization, the browser render tool produced no output, and automatic approval review rejected a direct browser launch with "blocked by policy" and no additional reason. That denied launch was not rerouted. Ordinary HTTP investigation and the user's normal-browser observation are separate evidence.

Final verification:

- Only `colosseum-tankoyomi` was redeployed: manifest `0.2.3`, deployment `57a0bbdb-6a9a-4d06-a52d-10a3101cc976`; dry bundle 104.79 KiB (gzip 24.26 KiB). Health, manifest and the MangaSin addon route returned HTTP 200. MangaSin revision 2 verified against the native pin with the same SHA-256/source bytes as before. LectorXD's addon route and metadata search route returned HTTP 404.
- Production provider-map/inventory tests cover LectorXD's exclusion and zero upstream calls even if an obsolete signed envelope is injected. Preserved LectorXD parser/API fixtures are explicitly injected tests, not production registrations. All 21 Worker tests, extension inventory contract, MangaSin parser contract, JavaScript runtime contract and diff checks passed.
- The production Windows runtime-ready app rebuilt successfully. All five focused native CTest cases passed: addon store, device runtime, Worker transport, legacy script transport and service configuration.
- Final executable SHA-256: `9118e24b77a86760a9edfbaa0449304992099fee35e0c010afbc664f298ad3eb`. A fresh isolated profile using the actual Main/native services and public endpoint reported only ZonaTMO, NiAdd and MangaSin in the Spanish inventory. MangaSin remained disabled before explicit test-profile opt-in; LectorXD was absent.
- That app verified signed MangaSin revision 2, loaded 837 One Piece chapters, downloaded chapter 1196's 16 pages (about 6.0 MB) through the native downloader and opened the real reader with `ready=true`. The rendered capture was inspected and showed Spanish dialogue. Driver exit: 0. No production QML changes, user settings changes, master merge or release publication were made.

## Release gate

LectorXD is excluded from this release scope and is no longer a prerequisite. MangaSin remains an explicit opt-in, with a positive Windows app result. Linux/Android runtime and installer/release packaging remain separate checks; this checkpoint does not publish the release or merge master.
