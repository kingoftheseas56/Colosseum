# Vinyl performance investigation baseline

This checkpoint preserves the current Vinyl presentation and playback corrections for a separate performance investigation. It is not a release or whole-product acceptance record.

Included behavior:
- Approved song, album, artist, cassette and radio widgets, search presentation and recommendation feedback controls.
- Exposure recording persists without rebuilding the current Home composition.
- Remote audio defaults to YouTube; the current song can explicitly request a torrent while its existing audio keeps playing. Cancellation/no match is nonfatal; torrent startup/seek failure retains YouTube fallback.
- Fresh catalogue metadata uses the existing cache. Signed playback URLs remain uncached. Fresh artist results do not claim offline state; stale fallback retains that indicator.
- Album track rows show heart and add-to-playlist. A verified backdrop source-geometry ordering defect is corrected without changing the blur aesthetic.

Evidence available from the originating integration run:
- Application target built and launched against the real QML tree with the existing QA profile. The QML fingerprint was `3e1205d8169443c63c2d9c13643610273ae0a9dd038033a678eef5a0d5c5cb51`.
- Catalogue cache snapshot: 52 passed, one opt-in live-provider skip. Final narrow stale-status correction: 12 focused cases passed; this was not followed by another whole-suite run.
- Explicit torrent behavior: 13 native cases passed. Video handoff: 15 passed. Song actions: 18 passed. Remote fallback and progressive-seek regressions passed in the preceding focused runs.
- Backdrop geometry regression and four independent D3D11 render scenarios passed. The complete missing-until-fullscreen-exit user symptom remains a manual acceptance gap.
- Broader album prototype suite: 14 passed, two cover-fit failures at smaller desktop sizes, one optional screenshot skip. Do not call the full QML suite green.

Remaining performance concern:
An anonymous fresh catalogue helper call took about 7.2 seconds. Exact YouTube resolution probes took about 16.4 and 19.6 seconds. The instrumented run included about 12.5 seconds importing yt_dlp and 5 seconds extracting a stream. Builds were running, so these are diagnostic observations, not isolated app benchmarks or proof of a memory/disk/network cause. Audio first-sound, video first-frame, stalls and seek recovery still require real playback measurements without competing heavy work.

The old five-chunk Home goal is cleared. Current order: YouTube audio/music-video performance; then Song screen auto-annotations and understandable HUD icons without hover descriptions; catalogue/Discover later.

The interrupted empty-mix regression draft is deliberately excluded from this checkpoint and preserved in the originating working tree. No empty-mix fix or passing test is claimed. The performance worker must preserve existing Home, onboarding, identities, stores, queue, local playback and the optional torrent behavior.
