# Collections sharing 1.3.0: agreed experience and completion design

Status: planning deliverable, 2026-11-04. Hemanth approved the recipient flow in this conversation and requested a complete plan first. This document specifies how to complete that flow; it does not authorize product implementation, deployment, migration or release. The durable design and plan live in `docs/plans/`; this repository reserves `docs/superpowers/` for ignored local scratch.

## Outcome and scope

A creator explicitly publishes a collection and sends a link under `https://getcolosseum.world/c/<share-id>`. A recipient opens the collection in Colosseum where supported, or sees a useful collection preview in the browser. Browser title actions lead to the website's downloads surface. In Colosseum, the recipient explicitly saves an ordinary shared collection as an independent editable collection, or keeps a fixed gift edition.

The browser shows collection contents and personal presentation. It does not grow movie/series pages, song/album pages, readers, players, provider searches or media delivery. Match the native collection's visual character with the existing website background/materials, typography and restrained title layout. QML remains the application UI.

## Starting evidence

- Current inspected master: `8a9b7ad73ca91b01c638a7e263f64e788799c3e1`. The primary checkout has unrelated changes; preserve them.
- Preserved sharing candidate: `058b2984457f99fa63f6c22f2f6d484878865700`, branch `codex/collections-sharing-1.3.0-paused-20261030`. It already implements service publications, passphrases, revocation, anonymous viewing, gift retention, independent gift copies and focused tests. It is outside master.
- The candidate still has the rejected three-tab sharing popup. Its compact replacement is only a proposal. See the pinned candidate's `docs/archive/collections-sharing-1.3.0/{README,VERIFICATION,UI-REVISION-PROPOSAL}.md` and `server/account-service/COLLECTIONS_PUBLICATIONS.md`.
- Current desktop bootstrap exits when the instance lock is held; it does not forward an incoming collection link. A positional URL would also be mistaken for a QML override by `resolveStartupLayout()`.
- Master's named-collection format is schema 2; the candidate writes schema 3 and reads 2/3 with exact field validation. Ordinary import needs a compatible format extension.
- Fresh earlier assessment: eight Node test results passed and the sharing QML diff had zero lint findings. These do not establish a new native build or full database pass. The archive records unresolved full database-package migration-context timeouts.
- Read-only provider inspection on 2026-11-04: `colosseum-site` is static Cloudflare Pages on production branch `main`; apex and `www` are attached. The existing Render account service is Docker Free, auto-deploys `master`, and its latest listed live deployment is source `69e31a79b9a7e70017ce1fca5ed48fe0982b9176`. Production database contents/schema were not queried. Recheck all mutable deployment facts before execution.

## Product contract

### Sending

1. Share opens a compact popup. Opening it never publishes anything.
2. First use: **Create link**, then a selectable link and **Copy link**. Reopening reuses the active ordinary link connected to this exact collection and owner; Copy never creates another publication.
3. Private edits remain private until **Publish changes** succeeds. Show a concise explanation at that action.
4. **Link settings**, **Make a gift**, receiving and link management are separate reachable views, without an always-visible tab console.
5. Publishing/managing hosted links uses existing Colosseum sign-in. Drafting, receiving and saving into an unlocked local profile remain account-free.
6. A protected update requires a passphrase or an explicit **Remove protection** choice. An empty hidden field must never silently remove protection. Preserve the existing replacement API semantics.

### Receiving in the browser

1. The public URL loads the collection preview. Protected content stays hidden until a successful passphrase request. Invalid, revoked, unsupported and unavailable links have specific recovery states.
2. Show ordered titles, public notes, description and permitted gift presentation. Never fetch the creator's local files, private artwork or arbitrary provider payloads. Existing embedded custom covers remain bounded and sanitized.
3. Title cards are real accessible links to `/downloads?collection=<share-id>`, labelled so the installation destination is expected. Use **Get Colosseum to open these titles** near the list. No pretend play buttons or browser media detail pages.
4. Keep **Open in Colosseum**, **Get Colosseum** and **Copy collection link** available. Android can use a package-scoped intent link for the explicit Open action when appropriate; the fallback returns to the preview, not a redirect loop.
5. `/downloads` displays the existing Downloads panel. A validated `collection` query value supplies **Return to your shared collection** and a copyable canonical link. No arbitrary return URL is accepted.
6. After installation, reopen the original share link. Seamless restoration across an installer is not promised or implemented through tracking, cookies or fingerprinting.
7. Keep data requests noncacheable and passphrases out of URLs, storage, logs, analytics, clipboard and app-launch URIs. Do not embed protected collection contents in social metadata or cached HTML.

### Receiving and saving in Colosseum

1. Every accepted app link opens a receiving preview, never auto-saves, plays media, adds Library membership, starts downloads or changes progress.
2. Route cold starts and links arriving while Colosseum is already running to the same owner. Wait for the real QML destination and an unlocked, healthy active profile. Do not create a second app/profile owner or bypass account/shared-PC locks.
3. **Save collection** imports an ordinary publication into an independent editable NamedCollectionsStore collection. Preserve the published name, description, title identities/editions, order, public notes and permitted presentation. Resolve name collisions with the existing unique-name policy.
4. Saving the same `(share-id, revision, snapshot hash)` again opens the existing saved collection without modifying it. The same share/revision with a different snapshot is rejected. A later published revision can be explicitly saved as another independent collection; it never overwrites the recipient's earlier edits.
5. **Keep Gift** retains the immutable gift edition and offline presentation. **Make My Own Version** keeps its existing independent-copy behavior, including clearing the gift's personal decorations/messages/notes from the editable version.
6. Revocation stops future hosted access; it cannot recall already saved ordinary copies or gifts. Unavailable exact references stay visible and unavailable; do not substitute another title, edition or provider assertion.
7. In the native receiving preview, an available title may open its existing native detail route without requiring Save first. Use the same validated shared-member conversion and existing `openReferenceRequested`/`openCollectionEntry` path; no temporary collection is persisted. Closed gifts expose their contents only after Open.

## Link and activation contract

| Purpose | Address |
| --- | --- |
| Canonical public link | `https://getcolosseum.world/c/<43-character-share-id>` |
| Accepted alias | `https://www.getcolosseum.world/c/<share-id>` |
| Registered app protocol | `colosseum://collection/<share-id>` |
| Legacy compatibility | Configured account-service origin plus `/collections/s/<share-id>` |
| Browser title/download action | `/downloads?collection=<share-id>` |

The share ID remains the existing 256-bit base64url identifier: exactly 43 ASCII letters, digits, `_` or `-`. Do not use a collection name or local collection UUID as the hosted identifier. Accept an optional single trailing slash for public/legacy URLs and normalize it away. Reject credentials in URLs, query/fragment additions to share/app links, encoded separators/IDs, extra path segments, unexpected ports and lookalike hosts. Local test origins require explicit test configuration; a public link never selects its own API origin.

The human-facing domain and account-service transport origin are separate. Native authenticated API traffic continues to use AccountServiceEndpoint and existing transport. Parsing an incoming website link extracts a validated ID; it does not send credentials or fetch arbitrary URLs.

### Automatic opening: actual platform contract

- **Android:** verified HTTPS App Links for `/c/` on apex and `www`, using the actual release application's package ID and signing-certificate SHA-256 fingerprints. Serve valid `/.well-known/assetlinks.json` independently on both hosts without redirects. Qt 6.11 documents incoming URL handlers; register early and test both initial and subsequent intents before adding a Java bridge.
- **Windows:** register the custom protocol through the existing per-user NSIS installer, including update/rollback/uninstall ownership. The current installer is unpackaged; Windows' verified HTTPS app association requires package identity. Retain the current installer for 1.3.0 rather than silently adding MSIX/sparse packaging. Use the website's protocol handoff where the browser permits it, with a visible Open action when confirmation or a gesture is required.
- **Linux:** register the custom protocol through a hidden `NoDisplay=true` entry emitted by the existing desktop packager (`Exec=colosseum --collection-url %u`), preserving the normal visible app-menu entry as `Exec=colosseum`, and forward the URI to the single running application. Respect desktop/browser handler choices.
- **macOS:** add the custom scheme to the existing app bundle and route incoming events. Verified HTTPS association additionally requires the actual signing/associated-domain setup and qualification. Do not invent a signing identity or claim this verified from bundle-generation tests.
- Browser launch attempts are bounded to one per navigation, never repeated after Back/return from the app. Do not use timers, hidden iframes, or elapsed time as proof of installation or successful launch. No automatic download redirect after a failed/blocked launch. On a browser where an unattended attempt produces an unknown-handler error, retain the explicit Open action and readable preview.
- Universal prompt-free launching is not a release promise. Every advertised platform/browser must record whether it opens directly, asks for confirmation, or needs the visible action. A browser restriction cannot be resolved by pretending launch success.

## Architecture and approaches

**Chosen:** keep the existing account service/PostgreSQL and native store/controller. Keep the website static: Pages serves a staged copy of the existing versioned HTML/CSS/JS viewer for `/c/*`, and that viewer calls only the account service's anonymous collection endpoints. Add exact-origin CORS only to those public endpoints. Native publishing/authentication stays unchanged.

This avoids introducing another backend, public media catalogue, new database, proxy authentication protocol or billable infrastructure. The website can render its shell while a sleeping origin wakes. CORS is limited to `https://getcolosseum.world` and `https://www.getcolosseum.world`, methods GET/POST/OPTIONS and the needed Content-Type header, with credentials omitted; localhost/preview origins require explicit nonproduction configuration. No wildcard origins or credentialed CORS.

**Considered:** proxy page, assets and API through Pages Functions. It preserves relative API URLs but adds invocation/runtime work and another trusted proxy hop for rate limiting. It is unnecessary for this bounded preview. **Rejected:** recreating application media pages or changing installers to obtain universal HTTPS association as part of this release.

The viewer remains authored once under `server/account-service/internal/httpserver/collectionviewer/`. A small repository-owned staging tool copies those assets into the independently maintained website, injects only validated public API configuration, and records source hashes. The site integration changes existing `_redirects`, `_headers` and Downloads routing without replacing the current website or existing release/preview-guard Workers. Use same-origin 200 rewrites for `/c/*` and `/downloads`; verify real Pages behavior in a preview deployment.

## Native persistence and activation decisions

- Extend NamedCollectionsStore to write local schema 4 and read 2/3/4. Keep schema-3 gift fields unchanged; add `savedShareId`, `savedRevision` and `savedSnapshotHash` for ordinary imports. Default all three to empty/zero on other collections. Read old files without rewriting until a successful mutation, and keep the existing atomic generation/locking behavior. Older clients reject schema 4 safely; document that application downgrade limitation.
- Store provenance in the same atomic collection transaction, not a second receipt store. Adoption preserves divergent local edits and validates provenance; repeated Save must not erase either side of an adoption conflict. Existing CollectionSyncAdapter synchronizes Library state; this feature does not add automatic named-collection cloud synchronization.
- Add a small URL parser and a narrow bootstrap activation owner. Keep InstanceLock authoritative. A per-user local IPC channel forwards only a bounded validated collection ID to the lock-owning process; do not expose Lanista or arbitrary native commands. Tag-specific test processes remain isolated.
- Treat `--collection-url <url>` as an exclusive external activation invocation. Reject additional positional, QML-override, updater or duplicate activation arguments instead of letting a malicious protocol argument select a local QML file. Ordinary launches without this flag retain their existing argument behavior.
- Hold at most the latest pending external link until the destination/profile is ready. A profile transition invalidates in-flight received content; present the link anew after unlock instead of carrying a save into another profile. Reject saves while the store is changing or unhealthy.
- Clear typed passphrases on close/profile changes, clear sensitive received state on teardown, preserve source/owner checks and bounded retries. Keep gift/local-store/projection quotas unless separately changed.

## Completion and release gates

The feature is complete only after the combined source passes native/controller/store/QML/browser/service checks, real installed cold/warm activation, ordinary Save/edit/restart/offline/retry/adoption, gift regression, security/failure cases, desktop/narrow visual review, and production-path verification after separately approved rollout. Fix or explain and resolve the recorded full database-package failure; a narrow passing selection is insufficient.

Production migration 0011 and exact schema gates require a coordinated compatible service rollout. Inspect current ingress/client-address handling: the existing code can bucket readers by the proxy peer. The separate trusted-proxy repair is evidence to review, not already merged/deployed work. Verify the actual trusted ingress chain before enabling public traffic; do not weaken limits or trust arbitrary forwarded headers.

Every platform actually shipped as sharing-capable must pass its installed-package matrix. Windows/Linux are the established stable lanes; Android must use the accepted development foundation and correct signing identity. Unavailable Android/macOS runtime evidence remains an explicit platform release gap, not a silently completed checkbox. No iOS app or new platform release is introduced here.

## Primary references checked for this plan

- [Qt 6.11 URL handlers](https://doc.qt.io/qt-6.11/qdesktopservices.html#setUrlHandler): incoming Android/Apple URL handling and handler lifetime.
- [Android App Link verification](https://developer.android.com/training/app-links/verify-applinks): host association and installed-app verification.
- [Chrome Android intents](https://developer.chrome.com/docs/android/intents): gesture restrictions and fallback behavior.
- [Windows web-to-app linking](https://learn.microsoft.com/en-us/windows/apps/develop/launch/web-to-app-linking): package identity requirement for HTTPS association.
- [Cloudflare Pages redirects](https://developers.cloudflare.com/pages/configuration/redirects/) and [headers](https://developers.cloudflare.com/pages/configuration/headers/): static routing and security headers.

These references constrain implementation; they do not verify Colosseum's final packages or live paths.
