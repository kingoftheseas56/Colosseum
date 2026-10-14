# Five-world content migration inventory

2026-10-14; bounded live-source inspection of the `content-api-free` worktree.
Planning evidence only: R2 registration is pending; no deployment, native edits or release qualification occurred.
Initial consumer is the Colosseum application. No public/general-purpose service is implied.
The scoped 1.1.9 boundary excludes Feria and Vinyl and adds no MusicBrainz product UI.
This inventory supplements `2026-10-14-content-api-119-rollout.md` for the four SQL catalogues.

## Existing boundaries across all five worlds

| World | Existing content/state owners | Migration unit |
| --- | --- | --- |
| Tankoban | `MalCatalog`, `ComicsCatalog`, `TankobanCatalog` C++ owners | Bound search/detail/facet/discovery/volume queries; preserve semantic joins and mapping in the existing release plan |
| Biblio | `native/engine/BiblioCatalog.h`, `BiblioCatalogStore.h`, canonicalizer/providers/ranking/taxonomy | Query pages, facets, viewed book details and optional enrichment; preserve existing durable cache and local reader/library state |
| Theatre | `ImdbCatalog`, MAL anime catalogue | Selective semantic queries, not raw table replication; player/history remain local owners |
| Universes, fourth world | `qml/Universes.js`, `ExtensionsStore`, JSON universe payloads and standard page | Versioned universe index, individual section/detail units, canonical work references and URL asset descriptors |
| Vault, fifth world | `VaultLibrary`, `VaultIndex`, `VaultIdentity`, scanner/config and local files | Optional selected-title metadata/artwork enrichment only; no remote Vault catalogue or replacement local authority |

Tributes are separate stylized franchise experiences within the Universe experience, not a sixth world.
Content API reads may enrich a world; service unavailability cannot disable local library, files, playback or reading.
No SQL DB download, aggregate content pack download or GitHub REST fallback belongs in the migration.

## Biblio already has a durable content owner

`BiblioCatalog.h:3-25` describes keyless daily refresh, bounded retry and generation cancellation.
`BiblioCatalog.h:133-139` already exposes ready, refreshing, stale, offline, revision and last refresh/error.
`BiblioCatalog.h:167-209` owns refresh, Discover pages/AND filters, facets, Explore rows, mosaics and lazy enrichment.
`BiblioCatalogStore.h:3-17` owns validated atomic SQLite snapshot publication and last-good retention.
`BiblioCatalogStore.h:153-192` owns publication, paged reads, facets and active-snapshot availability.
Do not introduce a second Biblio catalogue store, truth owner or independently ranked cache.
Extend this owner for bounded server page/detail results; API response caching is not a full remote snapshot import.
Preserve canonical work/edition relationships and provider evidence through existing canonicalizer/ranking logic.
Migration must specify how partially cached pages/facets differ from a complete old local snapshot.
Daily refresh cannot silently become downloading every hosted book row.
Existing provider refresh is an optional enrichment route; independent curated content updates should be versioned server data.

## Universes: JSON exists, but it is only one content layer

`assets/universes/one-piece.json`, `dcau.json` and `star-wars.json` already contain structured universe documents.
Example `dcau.json:2-18` has universe id/title/logo/background, section ids/kinds and provider-pinned entries.
`qml/UniverseExtApi.js:18-23` maps three extension ids to those filename stems; Cosmere is absent from this map.
`UniverseExtApi.js:25-49` validates video/manga/comic/book entries and removes invalid/empty sections.
Its manual-entry exception is explicit; retain manual status rather than fabricate provider identities.
`UniverseExtApi.js:52-80` keeps a process-lifetime cache with no invalidation and uses an installed reader.
That permanent cache must change to version-aware refresh/last-good retention before independent page updates can work.
`qml/Main.qml:555` wires that reader to `Extensions.universePayload`.
`native/engine/ExtensionsStore.cpp:875-899` validates a bare stem, reads filesystem JSON and tries the executable-relative fallback.
This is not currently HTTP fetching, an R2 reader, or a QRC payload loader.
`native/app_resources.qrc:1-8` lists icon/trust/title-identity resources; it does not embed these universe JSON documents.
Keep `ExtensionsStore` as installation/capability owner; give content transport a bounded asynchronous native seam rather than downloading executable extension packs.

`qml/Universes.js:43` begins a large embedded curated universe roster/configuration array.
It contains banners, blurbs, canon work/provider pins, groups, hints and bespoke page settings, not just a tiny display index.
`Universes.js:1084-1093` resolves category/config by name; `:1180` constructs world roster.
`Universes.js:1258-1259` defines tribute order and membership; `:1280` starts shelf overlays.
Move roster metadata, standard content, overlays and provider pin sets into versioned declarative records.
Give records explicit stable IDs; display-name changes must not break a name-based route or lose progress links.
`qml/UniverseApi.js:26-54` performs Cinemeta/AniList transport in JavaScript.
`:137-194` progressively assembles live search-based results; this is distinct from the installed JSON standard page.
Migrate curated canon to server data and preserve verified provider pins; live search/enrichment must not replace canon membership with fuzzy matches.

## Tributes: separate data extraction from installed renderer code

`qml/UniversesWorld.qml:35-37,91-113` exposes separate tribute and standard-universe actions.
`qml/Main.qml:4524-4542` chooses shared Saga/Shelves templates or bespoke DCAU/Star Wars/One Piece/Cosmere renderers.
Its `plain` flag selects the standard payload-backed face independently of the bespoke tribute.
Keep that distinction in server manifests; new content does not grant permission to execute arbitrary remote QML/JS.

| Tribute | Declarative content currently tied to app files | Renderer/behavior remaining installed |
| --- | --- | --- |
| One Piece | `OnePieceEastBlueData.js:4` arcs; `OnePieceParadiseData.js`, `OnePieceNewWorldData.js`, `OnePieceFinalSeaData.js`; corresponding `*AtlasData.js` content/layout records; universe canon/config and badge manifests | `OnePieceUniversePage.qml`, four sea atlas QML components, arc pages/docks/cards, chart/geometry/focus/input behavior |
| DCAU | `DCAUUniverseData.js:3-12` hub labels, pinned video IDs, comic posts and portal refs; standard `dcau.json` sections | `DCAUUniversePage.qml`; data selection helpers at `DCAUUniverseData.js:19-57` remain code unless separately refactored |
| Star Wars | `GalaxyUniversePage.qml:41-57` destination labels/era-section joins/local art refs mixed with orbit/angle/speed/colors; standard `star-wars.json` | `GalaxyUniversePage.qml`, `StarWarsGalaxySystem.qml`, interaction/animation/layout behavior |
| Cosmere | `Universes.js` Cosmere starters/worlds/series plus provider queries; `CosmereApi.js:27-88` assembles atlas sections from that config | `CosmereUniversePage.qml`, resolver/mapping behavior; no existing Cosmere universe JSON in the three-file loader |

Extract editorial labels, blurbs, era/arc ranges, ordered work references and artwork descriptors first.
Do not blindly serialize whole JavaScript modules: some contain selectors, algorithms and progress matching as well as data.
Treat orbit/position/style values as optional validated renderer-supported parameters with safe bounds, not remotely executable layout instructions.
Assets already live under `assets/universes/` as PNG/JPEG/SVG, including One Piece banners/badges, DCAU portals/environments and Star Wars environments/marks.
Deliver individual asset URLs with immutable hash/version paths, MIME/dimensions and safe fallbacks; do not require an asset pack.
New editorial records/images fitting an installed template can publish without an app release.
A new renderer, unsupported entry kind, interaction model or native routing capability still requires an application update.

## Vault is local state, not a hosted dataset

`native/main.cpp:1138-1158` creates `VaultIndex` at local `index-v1.sqlite`, `VaultIdentity`, scanner and `VaultLibrary` façade.
`VaultLibrary.h:60` groups identity/index/thumb/poster state under a local root.
`:107-182` exposes local series/items/browse/search; `:214-245` owns identify/reshelve/hide/confirm operations.
`main.cpp:1169-1256` wires optional enrichment against selected local index rows.
Use hosted metadata/artwork only as replaceable evidence for selected titles; do not upload paths, roots, library inventories or canonical identity/progress by default.
Preserve explicit user identity decisions, ambiguous matches, local cache provenance and file admission ownership.

## Stable publication, caching and migration gates

Define separate IDs for universe, tribute, section, arc/era and work; attach provider IDs as references, not display titles.
Keep installed extension IDs and local progress keys compatible; aliases/tombstones handle renames and withdrawals without rewriting progress.
Serve a small version/capability index, then requested universe headers, sections, details and individual asset URLs.
Use version-consistent sections, bounded pagination and signed/validated publication metadata; atomically activate a tested revision.
Validate schema/renderer compatibility, work references, duplicate IDs, media routes and asset domains before publication.
Cache last-good requested units locally, with source/version/hash, TTL, byte limits and atomic write; coalesce/cancel obsolete requests.
Replace permanent JS cache entries on revision changes; failed/invalid refresh must retain last-good content and local progress.
Offline starts use bundled minimal seed/last-good requested content and honest missing-section states; never silently bulk-fetch the world.
Keep auth shared with production guest/account read leases; admin/publication/signing secrets never enter application packages.
R2 is pending: URL delivery and publication storage are design requirements, not confirmed deployed capabilities.
Require clean-profile and existing-cache tests, selective network-volume assertions, invalid revision rollback and provider-pinned route checks.
API-only enablement remains blocked by production guest issuance, semantic parity, full required source readiness and installed runtime evidence.
For 1.1.9 use the established non-Feria/non-Vinyl candidate boundary; this inventory alone authorizes no new tribute redesign or release claim.
