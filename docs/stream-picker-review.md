# Arc 81 smart source picker: review candidate

## Source and scope

- Basis: `kingoftheseas56/Colosseum` master `1a813d66329c8075c37fe44a2bc0618e933239fc`.
- Review branch: `dots/smart-picker-recovery`.
- Harbor: `431c447977f8bc422d49ede47ad9b2681edb1649`.
- Tankoban 2: `d6d804d6ce899c2c4aef37a2345e4d67de970acc`.
- Pure Qt Core C++ library only. No QML, screen, playback-start, master, release or CI changes.
- Earlier unpushed implementation was unavailable after environment recovery. This candidate was reconstructed from the exact source pins; no earlier test claims are reused.

## Verification

- Debian 13, GCC 14.2, Qt 6.8.2, CMake 3.31.6.
- Warning-clean focused CMake build: `-Wall -Wextra -Wpedantic -Werror`.
- CTest: **5/5** suites passed (input, parser, trust, scorer, real fixtures).
- Component coverage: 19 input assertions, 241 parser assertions, 184 trust assertions, 37 scorer cases.
- AddressSanitizer + UndefinedBehaviorSanitizer: **5/5**, with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
- LeakSanitizer itself failed in this sandbox with its explicit ptrace unsupported error. Leak detection is **not qualified**; disabling it is not represented as a leak check.
- 20 actual Torrentio responses, 1,996 metadata rows: 10 movies and 10 anime episodes including One Piece and recent-film Clayface. Replay checks row conservation, deterministic picks, score ordering, rejection reasons and safe no-pick for the captured CAM-only Clayface pool.
- No main-repo or fork CI run started. Full desktop application, Windows, Android and actual playback remain untested.

## Adaptation and review findings

All 12 upstream parser scenarios are represented, excluding the removed cache assertion. The 22 scoring scenarios are represented as 15 preserved behaviors and 7 adaptations for the explicitly removed cache/Easynews/order behavior. Harbor trust behavior is covered; its Rust serde-only null option decoding does not apply to native structs. Tankoban single/dual/identity/quality-target safeguards are tested; its incompatible numeric 0–100 score formula and 1080p-only no-fallback rule are not copied.

Removed debrid cache boosts/tiers, Easynews boosts, direct-URL bonus, WebView2 penalties and HTML5 multi-audio penalties. Retained corpus statistics, language/group/codec/audio scoring, seeders, size and fresh-release protections. Both MIT notices are preserved.

Cross-review reproduced and fixed three gaps with regression tests: underscore CAM tags evading detection; mild movie-sized parser warnings blocking legitimate anime; bracketed single-digit episode ranges such as `[1-130 >700]` escaping batch detection. Primary selection is single-first for episode requests, then good 1080p → 720p → 4K → SD. The full retained list still sorts by score.

See [library API and inherited limits](../native/stream/README.md). In particular, an explicit fileIdx/anime bypasses upstream episode comparisons; early genuine digital releases can be rejected by the retained fresh-cinema rule; regional metadata dates can affect year penalties. These are not claims of authenticated identity or working playback.

## Fixture comparisons

**Human choices have not been supplied.** Every human-pick column is pending. Library picks below are outputs for review, not self-labelled correct answers. Public metadata claims about language and quality were not verified by playing media.

The second-provider attempts are recorded: Comet returned Forbidden and MediaFusion returned empty lists for Inception, Matrix and One Piece. They do not establish positive multi-provider ranking coverage.

| Title | Rows / kept | Library pick (zero-based row) | Human pick | Top scoring reasons |
|---|---:|---|---|---|
| Inception | 99 / 90 | #29: Начало.2010.UHD.BDRip.1080p.mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=161 (+10) |
| The Matrix | 130 / 118 | #41: The.Matrix.1999.REMASTERED.1080p.BluRay.REMUX.AVC.DTS-HD.MA.TrueHD.7.1.Atmos-FGT.mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=135 (+10) |
| The Shawshank Redemption | 95 / 76 | #29: The.Shawshank.Redemption.1994.1080p.BluRay.x265.AAC.5.1.mp4 | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=169 (+10) |
| The Godfather | 101 / 79 | #24: The.Godfather.1972.1080p.BluRay.REMUX.AVC.DTS-HD.MA.TrueHD.5.1-FGT.mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=103 (+10) |
| The Dark Knight | 96 / 84 | #35: The Dark Knight (2008) 1080p PROPER Bluray 10-bit x265 HEVC TrueHD AC3 5.1 [XannyFamily].mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=103 (+10) |
| Interstellar | 145 / 130 | #45: Interstellar.2014.PROPER.IMAX.1080p.UHD.BluRay.x265.HDR.DV.DD+5.1.Dual.YG.mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=504 (+10) |
| Parasite | 58 / 49 | #17: Parasite 2019 UHD BluRay 1080p DD Atmos 5 1 DoVi HDR10 x265-SM737.mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); DV+HDR10 (+6) |
| Spirited Away | 77 / 57 | #16: Spirited Away (1080p BD Dual Audio ) [SHINIGAMI_KIRA].mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=59 (+5) |
| Dune | 142 / 133 | #50: Dune.2021.1080p.BluRay.REMUX.AVC.DTS-HD.MA.TrueHD.7.1.Atmos-FGT.mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=215 (+10) |
| Clayface | 14 / 12 | No pick | Pending | no-eligible-primary: retained rows are unplayable, rough, known-zero-seed torrents, fraud-flagged, nonfinite-scored, or below the nonnegative-score floor |
| One Piece | 500 / 491 | #1: [Feibanyama] One Piece EP0001 [IQIYI WebRip 2160p HEVC OPUS Multi-Audio Multi-Subs].mkv | Pending | 4K (+25); preferred-language (+12); addon-priority-0 (+12); trusted-addon (+4) |
| Naruto | 50 / 29 | #4: Naruto 001 Remaster [V2] (BDRip 1080p x265 FLAC Multi) - Ryūjin 竜神.mkv | Pending | 1080p (+20); addon-priority-0 (+12); multi-language (+4); trusted-addon (+4) |
| Attack on Titan | 81 / 41 | #10: Attack.on.Titan.S01E01.REPACK.1080p.BluRay.x265.Opus.2.0-ZeroBuild.mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=69 (+6) |
| Death Note | 53 / 46 | #4: [Ryuuga] - Death Note  - 01 - Rebirth [1080p].mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=62 (+6) |
| Fullmetal Alchemist: Brotherhood | 54 / 39 | #2: Fullmetal Alchemist Brotherhood (2009) - S01E01 - (1) - Fullmetal Alchemist (1080p BluRay x265 ImE).mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=86 (+8) |
| Demon Slayer: Kimetsu no Yaiba | 50 / 35 | #4: [DB]Kimetsu no Yaiba_-_01_(Dual Audio_10bit_BD1080p_x265).mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=81 (+8) |
| Jujutsu Kaisen | 60 / 54 | #5: Jujutsu Kaisen - S01E01 v2 (BD 1080p HEVC) [Vodes].mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=220 (+10) |
| Cowboy Bebop | 50 / 46 | #9: Cowboy.Bebop.S01E01.1080p-Hi10p.BluRay.FLAC5.1.x264.[BD1B305A].mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=112 (+10) |
| Frieren: Beyond Journey's End | 91 / 46 | #8: [Beatrice-Raws] Sousou no Frieren - 01 [BDRip 1080p x265 Dolby TrueHD].mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=203 (+10) |
| Steins; Gate | 50 / 44 | #3: [MTBB] Steins;Gate - 01 (BD 1080p) [D97A3DA3].mkv | Pending | 1080p (+20); preferred-language (+12); addon-priority-0 (+12); seeders=122 (+10) |

The One Piece pool selects an eligible 4K single ahead of the available 1080p packs. This is the explicit single-before-pack policy, not a change to the 1080p target within the eligible single pool.

Full outputs and all rejection reasons: [`pick-results.json`](../tests/stream/fixtures/pick-results.json). Source responses, hashes, metadata and collection procedure: [fixture README](../tests/stream/fixtures/README.md).

## Reproduce

```sh
cmake -S native -B build-stream -DCOLOSSEUM_STREAM_PICKER_TESTS_ONLY=ON
cmake --build build-stream --parallel
ctest --test-dir build-stream --output-on-failure
build-stream/stream-tests/stream_fixture_test pick-results.json
```

No network is used by these tests. `collect.py` is an explicit optional network refresh tool, never a CTest dependency.

**BLOCKED: genuine human-reviewed fixture picks and their comparison assertions are missing.** The implementation and automated evidence are delivered for review; this is not master adoption or full acceptance.
