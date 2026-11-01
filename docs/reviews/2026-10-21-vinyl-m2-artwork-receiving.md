# M2 artist artwork follow-up

Status: development-app artwork receiving accepted. Base `53cc416680b26145d63a8b021149eaebff166911` on `codex/vinyl-production-flow`; main integration and release remain separate.

## Implementation

- Add the missing TheAudioDB artist-ID artwork adapter. The original backend contained a pure artwork resolver, service ports and fixture payloads, not a live photo-fetch implementation. Commons is not claimed implemented by this slice.
- Read only the four artist fanart fields after exact canonical-ID matching. Do not populate artist photos from album covers, clearart, banners, or arbitrary result URLs. Metadata reads are bounded and errors remain optional to catalogue/local listening.
- Download through the QML engine's existing `CachingNam` and image disk cache. Bound media reads/decode geometry; derive actual dimensions, SHA256 and a perceptual difference fingerprint before calling the unchanged `ArtistArtworkResolver`. Keep its geometry, canonical identity, expiry and duplicate admission rules. Do not label arbitrary records as human reviewed.
- Project admitted frames into the existing hero and synchronized blurred skin. Reuse the existing 12-second automatic rotation. Hemanth subsequently removed the exposed image-selection/local-override UI entirely; internal pin/auto diagnostics are not product controls. Empty/thin sets remain empty/static; render-failed frames are excluded, and a failed pinned frame returns to auto. No-photo live backdrops use the normal composed/wallpaper fallback rather than enlarging an album cover into an artist photo.
- Artist and profile departure cancel artwork requests, abort image replies and advance the generation fence. Public metadata cache restore still re-decodes cached image bytes; serialized availability does not bypass admission.
- Retain source/attribution and development-use scope. TheAudioDB's [current API terms](https://www.theaudiodb.com/docs_terms_of_use.php) permit development projects and restrict free-key app-store publication. No CC grant or individual photographer is invented when absent; `distributionCleared=false` preserves the separate M8 distribution gate.

## Receiving review

Fresh read-only Luna xhigh review found two Important issues at the shared image-manager boundary: it overwrote explicit manual redirects and cache-only intent. The narrow fix preserves explicit request policies while retaining existing defaults when absent. A real `CachingNam` fixture regression verifies a cache miss sends zero requests and a redirect to an unlisted HTTPS host is not followed. The artwork consumer additionally rejects changed final URLs or redirect-target attributes.

Review also identified a nonblocking renderer-error coverage gap. The hero now drops failed frames, and a direct missing-image QML regression passed. An initial test expected the label `rotating`; it was corrected to the existing UI's `multi-rotate` enum, without changing product behavior.

## Build and automated checks

- Windows Qt 6.11.1/MSVC, app `C:\b\vinyl-production-build\colosseum.exe`.
- EXE SHA256 `524f7f68758374c4693ce8c0c772e542e96b38113294cc62a10eaac3e578f0ec`.
- QML tree SHA256 `750f0956bf8f2a1c3000f2201ec2bbe37b159c65ea4b0b7a170d73deea0d39d6`.
- Qualification and fixture flags OFF. Existing private interpreter/runtime is unchanged; app-relative helper is staged by the normal post-link step.
- Four new helper contracts failed before the adapter existed, then passed. Existing helper suite: 13 passed, one optional live-smoke skipped; catalogue helper contracts: six passed.
- Native catalogue/artwork: 13 cases passed, including four/one/zero photos, duplicate images and invalid dimensions; the prior catalogue, profile and unavailable-MusicBrainz checks remain.
- Focused QML artwork mode/pin/auto/missing-image check: three cases including setup/cleanup passed.

All ten focused CTest targets passed before the final UI changes (42 world cases, 22 shared image-manager cases, 13 native catalogue/artwork cases). After the removals and search redesign, all 44 world cases passed again, including assertions that the removed button/dialog do not exist, desktop/narrow search layout, filtering, empty results, arrow/Enter/Escape navigation, focus return and route activation without queue admission. A popup test now waits for deferred layout before checking its scroll extent. A new single-release regression initially exposed duplicate fabricated Track results; remote placeholder rows are now excluded from track search. Failed launcher/layout/search receipts are retained. Evidence root: `D:\Colosseum-Vinyl-M2-Artwork-20261021`; runtime payload, photos, logs, profiles and screenshots remain local.

## Album and Song scope

The ordinary `VinylLiveWorld` already instantiates the existing Album and Song QML. This follow-up verifies Artist → real Album → track Details/Song → Back. Album metadata/tracklists/version selection and queue actions are connected. Song metadata, local playback and navigation are connected; live remote audio, lyrics/annotations/translations and video qualification remain the explicit M3/M6 work. A browser/source-backed Song view is not evidence of remote audio decoding.

## Actual app receiving and final UI corrections

- `vinyl-m1-bf4c11524e5b40969b9fe3e255609cd3`: tagged Illmatic import, partial reimport, Library/Album/Song/Artist navigation, FLAC output, cold restore/resume and isolated profile all passed in three phases with the online helper disabled.
- `vinyl-artwork-3e6e557366454c569bed8b947e853230`: live and helper-disabled cached restarts both admitted four distinct actual Eminem photos. The ordinary 12-second timer changed the synchronized hero image; internal pin/auto, real Album, Song Details and return to Artist passed. Both test phases exited 0; a third phase left the actual Main.qml app interactive. These journeys used the same native EXE with the earlier QML tree `f5170693f7aedef98ebf10161c059c6f519aa2b8f98ccd4ddf86226d15cb208a`.
- Hemanth then requested complete removal of the image-selection option and the Singles section's prototype layout description. The button, dialog, local-image entry, explanatory copy and opening hooks were deleted. Automatic rotation remains. The removed sentence was not artist biography or catalogue content.
- Hemanth subsequently requested an overhaul of artist search. `VinylArtistSearchPopup.qml` now uses the existing Glass, Theme, FocusRing, KeyboardAction and rounded artwork renderer. It has a custom search field, cover rows, compact close control and concise result states. The stock title/footer/Close bar and instructional copy are gone. Existing Album/Single/Song/Video/Live routes remain the activation owners.
- The final QML tree is recorded above. The owned earlier review app was identified by PID/path/start ticks and closed normally; the updated app was reopened for Hemanth using the same disposable test profile with all online services available. `final-open-app.json` records exact process identity, readiness and final QML hash. Its live four-photo assertion also verifies that the deleted controls do not exist. The app remains interactive after the driver stops.
- Final review directory: `final-review-383bfc0aaade46c29e1d94bba2acd156`, PID 23340 at readiness. `review-search.png` was visually inspected: actual album covers, rounded panel/rows, custom input and close control render in the ordinary app with 70 real catalogue results. Readiness reports responding, and the driver has stopped. Startup also reports an unrelated existing `universeHallLayer` reference warning outside this slice; this is not a claim of app-wide warning-free qualification.

Native/local receiving and the initial live/cached journeys precede the final presentation-only removals. Final world QML and final review-app evidence cover those removals; no claim is made that the earlier screenshots show the final header.
