# Arc 81 smart source picker: round-two review candidate

## Source and scope

- Repository: `kingoftheseas56/Colosseum`; review branch: `dots/smart-picker-recovery`.
- Round-two starting tip: `c27dcd395ee60e79093fdbd80dc47ee92be72708`.
- Original Colosseum basis: `1a813d66329c8075c37fe44a2bc0618e933239fc`.
- Harbor: `431c447977f8bc422d49ede47ad9b2681edb1649`.
- Tankoban 2: `d6d804d6ce899c2c4aef37a2345e4d67de970acc`.
- This document and its fixture report travel with the implementation commit. The delivery record records the exact published end tip.
- Pure Qt Core C++ library only. No QML, screen, playback-start, master, release or CI changes. Both upstream MIT notices and Tankoban credit remain intact.

## Five review fixes

1. **Language-code context.** Two-letter codes require a language-only bracket list, a labelled/flag language line, or adjacency to another language token. Numeric/size-unit neighbors are excluded, including nonbreaking spaces. `6.45 GB`, `1.2 GB` and ordinary ID/UK/IT/CA text do not invent languages. Flags and genuine tags still work; separate input fields cannot accidentally form an adjacent tag pair.
2. **Selected episode files.** A batch torrent is treated as a single for primary selection only with a finite nonnegative integral `fileIdx` and a selected filename independently identifying exactly the requested episode. Missing selection, ranges, wrong episode/season and ambiguous filenames remain packs. The original parsed pack flags stay intact; a zero-delta reason explains the effective classification. `evaluate` propagates expected episode/season into scoring context.
3. **Primary-only streaming band.** Within the chosen resolution and DV-compatible pool, known-friendly rows precede unknown-size rows, then oversized fallback. Numeric ranking and per-tier bests are unchanged by this policy. Every row gains a zero-delta `streaming-size` diagnostic. Decimal caps: 1080p films 15 GB/episodes 3 GB; 720p 6/1.5 GB; 4K 30/6 GB; SD 3/1 GB. With positive runtime, both the cap and estimated average bitrate must pass (1080p 20 Mbps,720p 8,4K 40,SD 5). Without runtime, only size applies. These are tunable constants, not measured throughput.
4. **Preferred primary versus secondary audio.** Primary/only preferred audio receives +12; another leading dub receives `preferred-language-secondary` +3. Explicit default/dub tags, ordered language tags/flags, non-Latin-leading metadata and Rutracker conventions inform this heuristic. English-first/Japanese-first controls, grouped `[EN/RU]`, labelled language lists and preferred Japanese under a Japanese title prevent over-broad downgrade. `multi-language` remains +4 when no concrete preferred language matches.
5. **DV capability.** `ScoreOptions::dolby_vision_supported` defaults false. DV-only receives no HDR bonus and is fallback-only within the target resolution; this compatibility preference takes precedence over size. DV+HDR10 and DV Hybrid metadata receive HDR10 fallback +5. With support enabled, Harbor's DV +6 scoring/eligibility returns. Hybrid without DV does not invent DV.

See [API and tunable constants](../native/stream/README.md).

## Verification

All runs used the final source, offline fixtures and Qt 6.8.2 on Debian 13:

| Build | Result |
|---|---|
| GCC 14.2, `-Wall -Wextra -Wpedantic -Werror` | Warning-clean; **5/5 CTest suites passed** |
| Clang 19.1.7, same warnings-as-errors | Warning-clean; **5/5 CTest suites passed** |
| GCC AddressSanitizer + UndefinedBehaviorSanitizer | **5/5 passed**, `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1` |
| Component assertions | Input 19; parser 278; trust 184; scorer 49 named cases |
| Real-response replay | 20 responses / 1,996 rows; **15 metadata safety assertions passed** |
| Diff/public-path checks | Passed; no source outside the library/tests/docs changed |
| CI | **0 runs**; no workflow dispatched |

LeakSanitizer is **not qualified**: round one reproduced its explicit unsupported-ptrace failure in this sandbox, so leak detection remained disabled. Sanitizer success is not a leak-check claim. Full application builds, Windows/MSVC, Android and actual playback remain untested. Clang helps portability but does not qualify MSVC.

### Negative controls: each fix was reverted independently

The final tests were compiled against isolated source copies with only the named behavior disabled. Each mutant compiled warning-clean and exited 1 from an assertion, not from compilation or setup failure. Production files were never temporarily reverted.

| Fix reverted | Isolated mutation | Observed failing regression |
|---|---|---|
|1 ISO context | Bypass numeric/context checks in `parseLanguages` while retaining two-letter iteration | Parser line 307: ordinary `6.45 GB` must have no language |
|2 selected file | Make `batch` ignore `selectedEpisodeFile` | **46/49** scorer cases; selected episode, true-pack control, `evaluate` propagation fail |
|3 streaming band | Bypass the `streamingBand(...).preference != bandPreference` selection guard | **47/49**; ranked-score preservation/primary choice and unknown/oversized fallback cases fail |
|4 secondary audio | Bypass `preferredAudioSecondary`, restoring unconditional preferred +12 | **46/49**; secondary dub, unknown-order Rutracker and contextual language ordering fail |
|5 DV capability | Restore unconditional Harbor HDR points and disable `unsupportedDvOnly` | **47/49**; unsupported DV scoring and DV-before-size priority fail |
|5 Hybrid parse | Disable only the DV+Hybrid normalization | Parser line 340: DV.Hybrid must parse as DvHdr10 |

The unmutated scorer passed **49/49** before mutation. Additional review regressions were red 46/49 then green 49/49 after refinement. A separate new-fixture-test/round-one-library control failed **7 of 15** safety checks: Parasite false English and bonus, One Piece 4K, Inception multidub, and Matrix/Godfather/Dune size. Clayface remained no-pick. This baseline proves the new safety assertions detect the reported regressions rather than merely exercising code.

## Fixture comparisons

**Human choices have not been supplied.** Every `humanPick` remains null; no agent choice is labelled as human. These metadata assertions are not human-pick agreement or media playback validation.

The same raw responses and manifest are retained. Comet previously returned Forbidden and MediaFusion empty lists; positive second-provider coverage remains unqualified. No new network captures were made.

Round-one source: `c27dcd395ee60e79093fdbd80dc47ee92be72708`.
Preserved before-report SHA-256: `0aa922eb32460d861922d80c30a3b622e4df41d4462b54fb805a5788683b45d4`.
After-report SHA-256: `7d5ccaaf2743f9d4e5d7f7a72be5feb7c463c1872b42a767fc841602a567d582`.

Rows are zero-based. Size units are decimal GB. These are algorithm picks and metadata safety checks, **not human-reviewed labels**. All `humanPick` fields remain null. The reason-change column compares the *same old winning row* before and after, avoiding conflating a different winner with a scoring change.

| Title | Old pick | New pick | Old winning row after fixes | New selection reasons |
|---|---|---|---|---|
| Inception | row 29: `Начало.2010.UHD.BDRip.1080p.mkv` | row 48: `Inception.2010.1080p.MAX.WEB-DL.DDP5.1.H.264-TURG.mkv`; 3.67 GB | score 64 → 54; `DV` +6 → absent; `HDR10-fallback` absent → +5; `preferred-language` +12 → absent; `preferred-language-secondary` absent → +3; `streaming-size:outside-band (size=26.04GB limit=15GB bitrate=23.46Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); streaming-size:preferred-known-friendly |
| The Matrix | row 41: `The.Matrix.1999.REMASTERED.1080p.BluRay.REMUX.AVC.DTS-HD.MA.TrueHD.7.1.Atmos-FGT.mkv` | row 66: `The.Matrix.1999.HDTV.Hybrid.OPEN-MATTE.1080p.x265.10Bit.HEVC.(Eng AAC 7.1 - Hin DDP 5.1).VITOENCODES.mkv`; 5.10 GB | score 66 → 51; `language-unknown` absent → -3; `preferred-language` +12 → absent; `streaming-size:outside-band (size=36.69GB limit=15GB bitrate=35.97Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); streaming-size:preferred-known-friendly |
| The Shawshank Redemption | row 29: `The.Shawshank.Redemption.1994.1080p.BluRay.x265.AAC.5.1.mp4` | row 26: `The Shawshank Redemption 1994 REPACK UHD BluRay 1080p DDP 5 1 DoVi HDR10 x265-SM737.mkv`; 8.51 GB | score 61 → 46; `language-unknown` absent → -3; `preferred-language` +12 → absent; `streaming-size:within-band (size=2.38GB limit=15GB bitrate=2.24Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); streaming-size:preferred-known-friendly |
| The Godfather | row 24: `The.Godfather.1972.1080p.BluRay.REMUX.AVC.DTS-HD.MA.TrueHD.5.1-FGT.mkv` | row 42: `The Godfather (1972) Al Pacino 1080p (AI Lab) ENG-ITA (moviesbyrizzo conv).mkv`; 4.49 GB | score 65 → 50; `language-unknown` absent → -3; `preferred-language` +12 → absent; `streaming-size:outside-band (size=49.05GB limit=15GB bitrate=37.37Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); streaming-size:preferred-known-friendly |
| The Dark Knight | row 35: `The Dark Knight (2008) 1080p PROPER Bluray 10-bit x265 HEVC TrueHD AC3 5.1 [XannyFamily].mkv` | row 63: `The.Dark.Knight.2008.1080p.REGRADED.70mm.IMAX.BluRay.x265.10Bit.HEVC.(English AAC 5.1).VITOENCODES.mkv`; 5.99 GB | score 64 → 49; `language-unknown` absent → -3; `preferred-language` +12 → absent; `streaming-size:within-band (size=9.01GB limit=15GB bitrate=7.90Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); streaming-size:preferred-known-friendly |
| Interstellar | row 45: `Interstellar.2014.PROPER.IMAX.1080p.UHD.BluRay.x265.HDR.DV.DD+5.1.Dual.YG.mkv` | row 84: `Interstellar (2014) [1080p H264 Eng Ita EAC3 Sub Eng Ita].mkv`; 5.29 GB | score 69 → 37; `DV` +6 → absent; `dolby-vision-unsupported:primary-deferred-within-resolution` absent → +0; `language-mismatch` absent → -14; `preferred-language` +12 → absent; `streaming-size:within-band (size=4.52GB limit=15GB bitrate=3.57Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); streaming-size:preferred-known-friendly |
| Parasite | row 17: `Parasite 2019 UHD BluRay 1080p DD Atmos 5 1 DoVi HDR10 x265-SM737.mkv` | row 18: `Parasite 2019 PROPER UHD BluRay 1080p DD Atmos 5 1 DoVi HDR10  x265-SM737.mkv`; 8.25 GB | score 61 → 45; `DV+HDR10` +6 → absent; `HDR10-fallback` absent → +5; `language-unknown` absent → -3; `preferred-language` +12 → absent; `streaming-size:within-band (size=6.93GB limit=15GB bitrate=6.94Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); streaming-size:preferred-known-friendly |
| Spirited Away | row 16: `Spirited Away (1080p BD Dual Audio ) [SHINIGAMI_KIRA].mkv` | row 16: `Spirited Away (1080p BD Dual Audio ) [SHINIGAMI_KIRA].mkv`; 2.66 GB | score 53 → 44; `preferred-language` +12 → absent; `preferred-language-secondary` absent → +3; `streaming-size:within-band (size=2.66GB limit=15GB bitrate=2.84Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Dune | row 50: `Dune.2021.1080p.BluRay.REMUX.AVC.DTS-HD.MA.TrueHD.7.1.Atmos-FGT.mkv` | row 93: `Dune Part One (2021) [1080p AV1, 7.1 Opus]-dith3r.mkv`; 6.29 GB | score 66 → 51; `language-unknown` absent → -3; `preferred-language` +12 → absent; `streaming-size:outside-band (size=39.96GB limit=15GB bitrate=34.38Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); streaming-size:preferred-known-friendly |
| Clayface | No pick | No pick | No earlier primary | no-eligible-primary: retained rows are unplayable, rough, known-zero-seed torrents, fraud-flagged, nonfinite-scored, or below the nonnegative-score floor |
| One Piece | row 1: `[Feibanyama] One Piece EP0001 [IQIYI WebRip 2160p HEVC OPUS Multi-Audio Multi-Subs].mkv` | row 21: `[Erai-raws] One Piece - 001 [1080p].mkv`; 0.77 GB | score 52 → 43; `preferred-language` +12 → absent; `preferred-language-secondary` absent → +3; `streaming-size:within-band (size=1.60GB limit=6GB bitrate=8.53Mbps limit=40Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Naruto | row 4: `Naruto 001 Remaster [V2] (BDRip 1080p x265 FLAC Multi) - Ryūjin 竜神.mkv` | row 4: `Naruto 001 Remaster [V2] (BDRip 1080p x265 FLAC Multi) - Ryūjin 竜神.mkv`; 1.02 GB | score 41 → 41; `streaming-size:within-band (size=1.02GB limit=3GB bitrate=5.65Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Attack on Titan | row 10: `Attack.on.Titan.S01E01.REPACK.1080p.BluRay.x265.Opus.2.0-ZeroBuild.mkv` | row 17: `Attack on Titan (2013) - S01E01 (1) - To You, in 2000 Years The Fall of Shiganshina, Part 1 (1080p BluRay x265 ImE).mkv`; 0.71 GB | score 56 → 48; `multi-language` absent → +4; `preferred-language` +12 → absent; `streaming-size:outside-band (size=4.11GB limit=3GB bitrate=21.93Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Death Note | row 4: `[Ryuuga] - Death Note  - 01 - Rebirth [1080p].mkv` | row 1: `[Erai-raws] Death Note - 01 [1080p][Multiple Subtitle][BCE68CE7].mkv`; 0.64 GB | score 54 → 39; `language-unknown` absent → -3; `preferred-language` +12 → absent; `streaming-size:within-band (size=1.40GB limit=3GB bitrate=7.75Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Fullmetal Alchemist: Brotherhood | row 2: `Fullmetal Alchemist Brotherhood (2009) - S01E01 - (1) - Fullmetal Alchemist (1080p BluRay x265 ImE).mkv` | row 1: `[Erai-raws] Fullmetal Alchemist - Brotherhood - 01 [1080p NF WEB-DL AVC AAC][MultiSub][C99DA55C].mkv`; 0.93 GB | score 54 → 54; `streaming-size:within-band (size=0.40GB limit=3GB runtime=unknown; size-cap-only)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Demon Slayer: Kimetsu no Yaiba | row 4: `[DB]Kimetsu no Yaiba_-_01_(Dual Audio_10bit_BD1080p_x265).mkv` | row 13: `Demon Slayer Kimetsu no Yaiba S01E01 VOSTFR 1080p WEB H.264 AAC -Tsundere-Raws (CR).mkv`; 1.93 GB | score 57 → 48; `preferred-language` +12 → absent; `preferred-language-secondary` absent → +3; `streaming-size:within-band (size=0.83GB limit=3GB bitrate=4.25Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Jujutsu Kaisen | row 5: `Jujutsu Kaisen - S01E01 v2 (BD 1080p HEVC) [Vodes].mkv` | row 27: `[Erai-raws] Jujutsu Kaisen - 01 [1080p][Multiple Subtitle][863939DC].mkv`; 1.46 GB | score 59 → 50; `preferred-language` +12 → absent; `preferred-language-secondary` absent → +3; `streaming-size:within-band (size=2.70GB limit=3GB bitrate=14.97Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Cowboy Bebop | row 9: `Cowboy.Bebop.S01E01.1080p-Hi10p.BluRay.FLAC5.1.x264.[BD1B305A].mkv` | row 9: `Cowboy.Bebop.S01E01.1080p-Hi10p.BluRay.FLAC5.1.x264.[BD1B305A].mkv`; 2.44 GB | score 60 → 60; `streaming-size:within-band (size=2.44GB limit=3GB bitrate=13.54Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Frieren: Beyond Journey's End | row 8: `[Beatrice-Raws] Sousou no Frieren - 01 [BDRip 1080p x265 Dolby TrueHD].mkv` | row 20: `[Anitsu] Sousou no Frieren - S01E01 [BD 1080p x265 Opus] [DUAL][2C84B686].mkv`; 0.88 GB | score 63 → 54; `preferred-language` +12 → absent; `preferred-language-secondary` absent → +3; `streaming-size:within-band (size=2.91GB limit=3GB bitrate=15.52Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Steins; Gate | row 3: `[MTBB] Steins;Gate - 01 (BD 1080p) [D97A3DA3].mkv` | row 8: `Steins.Gate.S01E01.1080p.BluRay.10bit.x265.FLAC.Dual.Audio-QM.[0831ACC0].mkv`; 2.49 GB | score 60 → 45; `language-unknown` absent → -3; `preferred-language` +12 → absent; `streaming-size:within-band (size=2.19GB limit=3GB bitrate=11.68Mbps limit=20Mbps)` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |

16 of 20 primary row selections changed. No new network responses were captured for this replay.


### Reading the changed selections

The One Piece winner is now row 21, `[Erai-raws] One Piece - 001 [1080p].mkv`, a selected episode file inside a batch torrent. Inception changes from the 24 GB Rutracker multidub to a 3.67 GB WEB-DL. Matrix 5.10 GB, Godfather 4.49 GB and Dune 6.29 GB satisfy their primary band. Parasite row17 has no inferred English or preferred-language bonus. Clayface still has no primary.

The table reports algorithm choices, not endorsements of release labels such as AI/upscale, track order or provider metadata. Full outputs, same-old-row diagnostics and rejection reasons: [`pick-results.json`](../tests/stream/fixtures/pick-results.json). Provenance and collection procedure: [fixture README](../tests/stream/fixtures/README.md).

## Retained behavior and known gaps

Round one ported Harbor parser/trust/corpus/scoring and Tankoban single/dual/identity/target safeguards. Debrid cache tiers/bonuses, Easynews, direct-URL bonuses, WebView2 and HTML5 multi-audio penalties remain removed. All 12 upstream parser scenarios excluding cache are represented; 22 scoring scenarios are represented as 15 preserved and 7 adapted behaviors. Rust serde-only null-option decoding does not apply to native structs.

- Harbor still bypasses episode/season trust checks for explicit fileIdx and anime. The new selected-file classification does not authenticate the torrent or change that inherited trust behavior.
- The 60-day fresh-cinema gate can reject genuine early digital releases. Regional release dates can affect year penalties.
- Language and DV/HDR metadata are heuristics; subtitle tags can resemble audio tags, and no actual track/profile probing or automatic player audio selection was added.
- Average bitrate does not establish peak bitrate, swarm health, start time or buffering. Missing size/runtime and no-friendly-band cases use documented fallbacks.
- `by_tier` remains highest numeric score, not a primary-safety guarantee. Size preference does not rewrite it.
- Trust's inherited `allow_season_packs`, `allow_size_outliers` and language fields are not read by Harbor trust logic; language affects scoring. `respect_addon_order` does not override score ranking.
- Human selections, live playback, positive Comet/MediaFusion ranking, full desktop build and Windows/Android receiving tests remain open. No master adoption.

## Reproduce

```sh
cmake -S native -B build-stream -DCOLOSSEUM_STREAM_PICKER_TESTS_ONLY=ON \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Werror"
cmake --build build-stream --parallel
ctest --test-dir build-stream --output-on-failure
# Optional same-old-row diagnostics; extract the published round-one report:
git show c27dcd395ee60e79093fdbd80dc47ee92be72708:tests/stream/fixtures/pick-results.json > before.json
build-stream/stream-tests/stream_fixture_test pick-results.json before.json
```

For Clang use a separate build directory and `-DCMAKE_CXX_COMPILER=clang++`. For ASan/UBSan use a separate build with `-fsanitize=address,undefined -fno-omit-frame-pointer -g` added to the warning flags, then the sanitizer environment above. The five suites are offline; `collect.py` is an explicit optional network refresh, never a CTest dependency.

**IMPLEMENTATION COMPLETE.** Human testing was explicitly deferred until the code is written. This is review-branch delivery, not product acceptance or master adoption.
