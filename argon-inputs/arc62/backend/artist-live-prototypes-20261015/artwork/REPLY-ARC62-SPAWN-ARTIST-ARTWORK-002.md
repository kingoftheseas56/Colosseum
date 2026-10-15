TASK: arc62-spawn-artist-artwork-002
STATUS: DONE

# Scope and frozen-source receipt
- Owner: artist-specific hero/background artwork resolution only. All authored code, tests, instructions, objects, executables and proof logs reside in this exact artwork-owned directory.
- Source: C:\Users\Suprabha\.codex\worktrees\vinyl-live-listening\Colosseum; branch codex/vinyl-live-listening; observed HEAD before and after = 860b96ae27a59c955f6aa9bf4642351ed8ce21e3 (matches frozen SHA).
- Existing modified artist docs/QML/fixtures and untracked ArtistCard/FullCategory were present initially and remain present, unchanged by this worker. No git modifications, commits, provider logins, downloads, release writes, system configuration or cross-owner edits.
- Read-only inputs: JOB-02-ARTIST-ARTWORK.md; Arc62 research/ARTIST-ARTWORK-SOURCE-STRATEGY-2026-10-14.md including MF DOOM and The D.O.C. addenda; Arc62 catalogue/ARTWORK-STRATEGY.md; BG1 ArtistMbid contracts; Arc65 warehouse baseline; VinylBackdrop.qml, VinylArtistHero.qml, VinylArtistPage.qml, VinylArtistData.qml and live world.

# Actual files produced (all under this owner directory)
- ArtistArtworkResolver.h: provider-independent request/candidate, rights, local override, cache, selected-frame and fallback value contracts.
- ArtistArtworkResolver.cpp: full C++17 implementation, deterministic grading/selection/dedupe, fallback downgrade, and versioned fail-closed metadata codec.
- test_artist_artwork.cpp: executable deterministic synthetic cases and negative controls, no copyrighted image bytes.
- CMakeLists.txt: standalone C++17 test target, CTest and strict warnings.
- compile-and-test.cmd: installed MSVC 2022 x64 direct build/test runner.
- README.md: exact build commands, contract, licensing caveats, BG1 and Arc65 adoption boundaries.
- build/resolver.obj, build/test.obj, build/test_artist_artwork.exe, build/msvc-test.log: locally produced direct-MSVC proof.
- build-cmake/: isolated generated CMake/MSBuild/CTest artifacts including Release/test_artist_artwork.exe and CTest records.

# Observable execution
1. Executed compile-and-test.cmd on Laptop 1 through Brotherhood Desktop Commander with existing MSVC. Compiler flags: /std:c++17 /EHsc /W4 /WX. Exit 0. Last log line: RESULT 38 PASS / 0 FAIL.
2. Executed C:\Qt\Tools\CMake_64\bin\cmake.exe with -G "Visual Studio 17 2022" -A x64, built Release using CMake/MSBuild. Successful executable in build-cmake/Release.
3. Executed C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build-cmake -C Release --output-on-failure. 1/1 CTest PASS, 0 failed, exit 0.
4. Rechecked exact Git SHA, branch and pre-existing dirty-file set after builds; source remained read-only. File enumeration and SHA-256 checks completed for the six authored top-level files. No other worker's directory was used.

# Implemented behavior
- Requires strict MusicBrainz artist-MBID equality, provider/source attribution, verified canonical artist match, actual clean photo with safe crop, HTTPS source, valid photo dimensions and purpose, and individually documented rights status. UNKNOWN/RESTRICTED/expired/no-license-evidence -> NOT release-cleared, even if a provider supplied an image URL.
- Hero candidates require minimum 1280x720 and aspect 1.40..2.50. Portraits can participate in fallback composition only, minimum 400x400 and aspect 0.55..1.80. Logos, video stills, album covers, placeholders, heavily branded and unsuitable/cropped photos do not become wide hero photos.
- Deterministic ordering and capped selected list (default 8, maximum 12). 0 -> artist-specific typographic composition; 1 -> static; 2 -> crossfade; 3+ -> rotating. Artist fallback always carries name/monogram/MBID-derived layout seed and optional rights-cleared portrait. No stock or album-cover hero fallback.
- Rejects duplicates by normalized origin URL, matching SHA-256, identical cache reference, and near-identical supplied perceptual hash. Where comparable perceptual signatures are absent, two photos both need explicit human-reviewed-distinct attestation; different URLs or content hashes alone never establish distinctness. The resolver does not fabricate frames to reach four.
- Pure metadata-only cache codec (V2) preserves provenance, rights scope/evidence/attribution, expiry, dimensions, origin and fingerprints. On reload, local byte-presence trust is reset to false; caller must verify bytes and cache hash before offline use. Cache entries with expired rights/TTL, mismatched hashes, missing confirmation or unsafe opaque keys fail closed. Verified cache can satisfy an offline request without remote access. Cache suggestions require separately verified local-cache permission.
- Optional user-provided asset is accepted only for the same MBID in LocalPreview, with verified local asset key/size, and is always marked localOnly and releaseCleared=false. Release scope ignores overrides. afterLoadFailure removes failed frames and recomputes rotation -> crossfade -> static -> artist-specific fallback without blocking artist metadata.

# Deterministic negative controls covered
- 0/1/2/3/4 image modes, capped count and shuffled-input stable order;
- wrong MBID/name-only match, placeholder, branded/album image, too-small or extreme ratio;
- same normalized URL, SHA-256 duplicate, near perceptual duplicate, unreviewed false distinctness, same cache key/bytes;
- unknown, restricted, undocumented, expired rights, permission to render without permission to cache;
- failed/expired cache integrity, inconsistent origin/bytes SHA, stale cache, corrupt manifests, restart requiring fresh byte attestation, true offline cached fallback;
- valid local-only override, denied release override, wrong artist/traversal key, and three consecutive image-failure downgrades.

# Adopter contract / mismatches
- Map BA0/BG1 ArtistMbid into request.artistMbid as the canonical UUID, resolving explicit BG1 artist redirects before provider lookup. Never use artist names, aliases or MF DOOM side identities as canonical equivalence. Provider transport/credential handling stays outside this resolver; TheAudioDB, Fanart.tv, Wikimedia Commons candidates require independent per-file legal and identity qualification.
- To adopt natively, add an artist artwork data seam above the current fixture-driven VinylArtistData. Feed selected images and fallback data to both VinylArtistHero and VinylBackdrop; keep QML responsible for blur, contrast, crop and transitions. Existing VinylArtistHero uses a release cover when no portrait; existing VinylBackdrop uses release imagery/Cold Ripple, neither is the approved artist-specific fallback behavior. Those QML files were intentionally not modified.
- Arc65 image-cache owner should persist metadata atomically, check content bytes/size/MIME/hashes, manage cache eviction and failures, validate redirected origins/SSRF protections, and return post-validation cache attestations. The resolver performs none of those network/disk tasks. API availability alone never grants commercial reuse or redistribution permission.
- The 2026-10-15 addenda report MF DOOM four distinct provider images and The D.O.C. two clean wide provider images. This isolated proof does NOT assert their rights clearance or execute their live data: every test image, hash, author and licensing claim is synthetic.

# Explicit unverified boundaries
No live TheAudioDB/Fanart/Commons calls, pixel-level similarity inspection, actual photo licensing approval, Arc65 cache integration, Qt/QML rendering, timing/GPU blur, full-app build, runtime artist pages, nor production wiring was attempted. Perceptual hashes and editorial review indicators are required externally to qualify distinctness. Successful local C++17 unit tests do not establish provider coverage, commercial permissions or end-to-end runtime acceptance.

Result: isolated native artwork proof COMPLETE and locally executed; production adoption remains a separate integration/rights/cache workstream.
