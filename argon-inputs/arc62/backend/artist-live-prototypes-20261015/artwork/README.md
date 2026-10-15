# Arc 62 Artist Artwork Resolver, isolated native proof

Source read-only: Colosseum branch codex/vinyl-live-listening, pinned 860b96ae27a59c955f6aa9bf4642351ed8ce21e3. This independent C++17 component is **not wired to production**, does no I/O, does not embed photos, and has no Qt/QML or network dependency.

## Build and run on Laptop 1

Run compile-and-test.cmd from this folder. It initializes the **already installed** MSVC 2022 x64 environment and compiles two sources with /std:c++17 /W4 /WX, linking build/test_artist_artwork.exe and running 100% synthetic tests. All generated objects and the executable remain under artwork/build/.

Alternative with existing Qt CMake: C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build-cmake then cmake --build build-cmake --config Release and ctest --test-dir build-cmake -C Release --output-on-failure. Build roots must remain within this owned directory.

## Contract and rendering state

ArtistArtworkResolver::resolve(Request) consumes externally fetched provider candidates and cache attestations, never fetches media. It returns normalized HeroFrame values, rights/provenance metadata, device-cache suggestions when cache rights are separately verified, a fallback artist composition, diagnostic rejects, and a 0/1/2/3+ mode: **composition**, **static**, **crossfade**, **rotating**.

The local cache must supply verifiedBytesPresent=true **only after** checking local object existence and matching the pinned bytes SHA-256. A decoded saved manifest deliberately resets this flag to false. Cached entries additionally need unexpired rights evidence, cache permission, a safe opaque asset key, and a fresh expiry. Unsafe metadata degrades to typography/artist-portrait composition without blocking navigation. afterLoadFailure removes a bad image and recomputes the 0/1/2/3+ mode down to the composition fallback.

Only canonical artist-MBID-matched, actual clean photographs with suitable dimensions/crop and independently checked hero rights are eligible. Unknown, expired, unsupported, and restricted rights never become release-cleared by virtue of an API image URL. Photographs are deduped by normalized URL, exact SHA-256 (including verified local-cache bytes), local asset key, and near-match 64-bit perceptual hashes **when present**. For two images without comparable perceptual hashes, both must carry explicit humanReviewedAsDistinctInArtistSet attestation; otherwise only one is selected. This conservative gate prevents different URLs or byte hashes from being mistaken for proven distinct photographs. Source images still need human/editorial review: automated hash-distance cannot guarantee different photos under all crops or edits. No mandatory four frames, album-cover wallpaper, cosmetic clones, or fabricated artist photography.

An override is **only** an opaque, already verified local asset key, matched to the same artist; it is visible solely in LocalPreview, forces a static view, and explicitly marks releaseCleared=false. It must not flow to a server, shared cache, or release manifest.

The fallback always includes artist name, initials, and a deterministic identity-derived layout seed. A rights-cleared portrait (at least 400px square-class resolution) can be used as an element **inside** the composition but never stretched to a widescreen photograph. QML decides geometry, blur, readability, slideshow timing, and rendering.

## Adoption boundary

Adapt the native BG1 vinyl::ArtistMbid through its canonical string value to Request.artistMbid; resolve MusicBrainz redirects **before** selecting photos. Feed TheAudioDB/Fanart.tv/Commons candidates only after MBID-based provider matching and licensing inspection. Map results to **both** VinylArtistHero and full-page VinylBackdrop; currently VinylArtistHero.qml is fixture-backed and falls back to a release cover, while VinylBackdrop.qml uses album/Cold Ripple imagery. Production must remove artist album-cover-as-hero fallback when integrating this rule. No QML edited here.

