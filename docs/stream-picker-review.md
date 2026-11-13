# Arc 81 smart source picker: round-three review candidate

## Source and scope

- Repository `kingoftheseas56/Colosseum`, branch `dots/smart-picker-recovery`.
- Round-three start: `3b718a18d09cc5628ad08ac1bd158931d7e16ea1` (accepted round two).
- Original Colosseum basis: `1a813d66329c8075c37fe44a2bc0618e933239fc`.
- Harbor MIT source: `431c447977f8bc422d49ede47ad9b2681edb1649`.
- Tankoban 2 MIT source: `d6d804d6ce899c2c4aef37a2345e4d67de970acc`.
- Library, tests and evidence only. No UI, QML, playback-start, master merge, release or CI changes. Both upstream notices/credits remain intact.
- This report travels with the tested source commit; delivery records identify the exact published end tip. [Round-two report](https://github.com/kingoftheseas56/Colosseum/blob/3b718a18d09cc5628ad08ac1bd158931d7e16ea1/docs/stream-picker-review.md).

## Four review fixes

1. **Source quality within the band.** Disc (BluRay/BDRip/REMUX) and WEB-DL/WEBRip are premium; unknown/HDRip/other are intermediate; HDTV/DVD/SDTV are lower. The best available tier precedes score. A capture remains a fallback when no better in-band row survives the prior gates. Added boundary-safe BD1080p/[BD], Blu-Ray/Blu.Ray and bare WEB aliases plus SDTV parsing, with substring controls.
2. **AI/upscale/conversion handling.** Explicit AI, upscale/upscaled/upscaling, conv/conversion, Topaz and Gigapixel markers receive a clear -15 reason and primary deferral while non-upscaled alternatives survive. Full selected-file paths are checked, not only basenames. Plain regrade without AI, Conviction and A.I. Artificial Intelligence are negative controls. A separate `suspected-preHD-TV-upscale` -15 heuristic covers pre-2000 UHD WEBRip series with episode context (parsed year or supplied release date), excluding films, disc sources, modern shows and explicit remaster/restoration/film-scan metadata. It is suspicion, not proof of native resolution.
3. **Torrent health.** The numeric bonus becomes `min(24, 3 * log2(1 + seeders))`, replacing Harbor's coarse capped integer bonus. Public `ScoreOptions` tunables are `healthy_seeders=20`, `seeder_log_weight=3.0`, `seeder_bonus_cap=24.0`; negative weights/caps clamp to zero and nonfinite values use defaults. Within the selected source-quality pool, healthy rows precede below-floor/unknown counts when available. Otherwise low/unknown counts remain fallback; explicit zero-seed torrent safety is unchanged. Direct streams do not require a torrent swarm.
4. **French-led versus subtitle language.** VOSTFR/VOST/VF/VFF/VFQ/TRUEFRENCH/FRENCH mark French-led metadata. Unless French is preferred, those rows are deferred while a non-French-led alternative survives, even if they also say Dual Audio. Non-French preferred audio on such rows receives at most secondary +3. Multi Subs flag lines, adjacent subtitle-only flags and explicit subtitle metadata populate `subtitle_languages`, not `audio_languages`; subtitle-only English does not earn an audio bonus. Independent explicit audio names/flags remain audio evidence, and French preference is a positive control.

**Numeric ranking intentionally changes** because of log-scaled seeders, -15 upscale signals and corrected language/source parsing. All retained rows remain manually available and sorted by their new numeric score. Source, French and health primary diagnostics have zero delta. Size bands still do not themselves alter numeric ranking or per-tier bests.

### Explicit primary precedence

Eligibility → single episode → target resolution → DV compatibility → size band → non-French-led (unless French preferred) → non-upscaled → source tier → healthy floor → numeric score/ties.

Each preference acts only on survivors of the previous gate and falls back when none match. The health floor applies within the best remaining source tier, not globally: a highly seeded HDTV capture does **not** displace an in-band BluRay/WEB encode merely because that encode has fewer than 20 seeders. This preserves the requested Matrix source-quality safety invariant. Conversely, an oversized premium source does not displace an in-band capture. These crossed cases are tested.

Round-two resolution, selected-file, size, DV and language protections remain. [API, all constants and fallbacks](../native/stream/README.md).

## Verification

Final source: Debian 13, Qt 6.8.2, CMake 3.31.6. All builds used `-Wall -Wextra -Wpedantic -Werror`.

| Check | Result |
|---|---|
| GCC 14.2 focused build and CTest | Warning-clean; **5/5 suites passed** |
| Clang 19.1.7 focused build and CTest | Warning-clean; **5/5 suites passed** |
| GCC ASan + UBSan | **5/5 suites passed** |
| Component coverage | Input 19 assertions; parser 325; trust 184; scorer 54 named cases |
| Offline fixture replay | **20 responses / 1,996 rows / 21 metadata safety assertions passed** |
| Final source stability | No-op rebuilds and source hashes verified after final diagnostics |
| Diff/public-path checks | Passed |
| CI | **0 runs**; no main-repo or fork workflow dispatched |

Sanitizer environment: `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`. Leak detection remains **unqualified**, following round one's explicit LeakSanitizer unsupported-ptrace failure. ASan/UBSan passing is not a leak-check claim. Windows/MSVC, Android and full desktop builds remain untested; Clang is additional portability evidence, not MSVC qualification.

### Independently failing negative controls

Each mutation was compiled warnings-as-errors in an isolated source copy; production source was not temporarily reverted. Each failed from the intended regression, not setup/compilation. Unmutated scorer passed 54/54.

| Reverted behavior | Isolated mutation | Result |
|---|---|---|
| Source tier | Make `sourcePreference` return the same tier for all sources | **52/54**, source-tier choice and crossed source/health control fail |
| AI/upscale | Make `upscaleSignal` empty | **52/54**, explicit penalty/deferral and conservative old-TV inference fail |
| Seed health/weight | Disable health gate and restore Harbor integer bonus | **52/54**, exact seed formula and health-floor choice fail |
| French-led scoring/selection | Disable French primary deferral and its secondary-audio rule | **53/54**, French-led non-French-preference regression fails |
| Subtitle parser | Replace `parseLanguageTracks` with original undifferentiated audio-language parsing | Exit 1 at parser line 354: subtitle flags must not invent English audio |

New scorer cases were initially red 49/54, then green 54/54. A separate new-fixture-test/round-two-library control failed **exactly 3/21**: Godfather AI/conversion, Matrix HDTV and Demon Slayer VOSTFR. All 15 round-two safety assertions remained green in that baseline and are still green now.

## Round-two → round-three fixture comparison

**No human labels were supplied. All `humanPick` values remain null.** These are algorithm choices and metadata safety checks, not claims of a careful human's selection or working media playback. The same raw captures and provenance manifest are retained; no new network captures were made.

Round-two source: `3b718a18d09cc5628ad08ac1bd158931d7e16ea1`.
Preserved before-report SHA-256: `7d5ccaaf2743f9d4e5d7f7a72be5feb7c463c1872b42a767fc841602a567d582`.
After-report SHA-256: `00bfe14d6d85a256ebb358953e1ad61693f58c9fe71cf3af344d369a5317920c`.

Rows are zero-based. Size units are decimal GB. These are algorithm picks and metadata safety checks, **not human-reviewed labels**. All `humanPick` fields remain null. The reason-change column compares the *same old winning row* before and after, avoiding conflating a different winner with a scoring change.

| Title | Old pick | New pick | Old winning row after fixes | New selection reasons |
|---|---|---|---|---|
| Inception | row 48: `Inception.2010.1080p.MAX.WEB-DL.DDP5.1.H.264-TURG.mkv`; 3.67 GB | row 33: `Inception.2010.1080p.BluRay.DDP5.1.x265.10bit-GalaxyRG265.mkv`; 3.08 GB | score 52 → 63.2624; `seeders=16` +1 → +12.2624; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; streaming-size:preferred-known-friendly |
| The Matrix | row 66: `The.Matrix.1999.HDTV.Hybrid.OPEN-MATTE.1080p.x265.10Bit.HEVC.(Eng AAC 7.1 - Hin DDP 5.1).VITOENCODES.mkv`; 5.10 GB | row 39: `The.Matrix.1999.1080p.BluRay.DDP5.1.x265.10bit-GalaxyRG265.mkv`; 3.25 GB | score 52 → 63; `seeders=15` +1 → +12; `source-quality:capture-or-DVD` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; streaming-size:preferred-known-friendly |
| The Shawshank Redemption | row 26: `The Shawshank Redemption 1994 REPACK UHD BluRay 1080p DDP 5 1 DoVi HDR10 x265-SM737.mkv`; 8.51 GB | row 35: `The.Shawshank.Redemption.1994.bluray.sdr.1080p.av1.5.1.opus.subs-Dust.mkv`; 4.16 GB | score 57 → 66.9658; `seeders=9` absent → +9.96578; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; streaming-size:preferred-known-friendly |
| The Godfather | row 42: `The Godfather (1972) Al Pacino 1080p (AI Lab) ENG-ITA (moviesbyrizzo conv).mkv`; 4.49 GB | row 23: `The Godfather (1972) RM4K (1080p BluRay x265 10bit Tigole).mkv`; 10.28 GB | score 49 → 44.4221; `seeders=13` +1 → +11.4221; `source-quality:unknown-or-other` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0; `upscale-or-conversion` absent → -15 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; streaming-size:preferred-known-friendly |
| The Dark Knight | row 63: `The.Dark.Knight.2008.1080p.REGRADED.70mm.IMAX.BluRay.x265.10Bit.HEVC.(English AAC 5.1).VITOENCODES.mkv`; 5.99 GB | row 34: `The.Dark.Knight.2008.1080p.BluRay.DDP5.1.x265.10bit-GalaxyRG265.mkv`; 4.32 GB | score 51 → 60; `seeders=7` absent → +9; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; streaming-size:preferred-known-friendly |
| Interstellar | row 84: `Interstellar (2014) [1080p H264 Eng Ita EAC3 Sub Eng Ita].mkv`; 5.29 GB | row 54: `Interstellar.2014.1080p.BluRay.DDP5.1.x265.10bit-GalaxyRG265.mkv`; 4.82 GB | score 52 → 61.3783; `seeders=10` +1 → +10.3783; `source-quality:unknown-or-other` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; streaming-size:preferred-known-friendly |
| Parasite | row 18: `Parasite 2019 PROPER UHD BluRay 1080p DD Atmos 5 1 DoVi HDR10  x265-SM737.mkv`; 8.25 GB | row 23: `Parasite 2019 REPACK 1080p BluRay x265-YAWNTiC.mkv`; 8.46 GB | score 60 → 69.5098; `seeders=8` absent → +9.50978; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; streaming-size:preferred-known-friendly |
| Spirited Away | row 16: `Spirited Away (1080p BD Dual Audio ) [SHINIGAMI_KIRA].mkv`; 2.66 GB | row 16: `Spirited Away (1080p BD Dual Audio ) [SHINIGAMI_KIRA].mkv`; 2.66 GB | score 44 → 56.7207; `seeders=59` +5 → +17.7207; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=healthy-or-direct` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Dune | row 93: `Dune Part One (2021) [1080p AV1, 7.1 Opus]-dith3r.mkv`; 6.29 GB | row 47: `Dune.2021.1080p.WEBRip.x264.AAC5.1-[YTS.MX].mp4`; 2.95 GB | score 51 → 60.5098; `seeders=8` absent → +9.50978; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; streaming-size:preferred-known-friendly |
| Clayface | No pick | No pick | No earlier primary | no-eligible-primary: retained rows are unplayable, rough, known-zero-seed torrents, fraud-flagged, nonfinite-scored, or below the nonnegative-score floor |
| One Piece | row 21: `[Erai-raws] One Piece - 001 [1080p].mkv`; 0.77 GB | row 10: `[Lia] One Piece - 0001 [1080p].mkv`; 1.52 GB | score 51 → 45.3783; `language-unknown` absent → -3; `preferred-language` +12 → absent; `seeders=10` +1 → +10.3783; `source-quality:unknown-or-other` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Naruto | row 4: `Naruto 001 Remaster [V2] (BDRip 1080p x265 FLAC Multi) - Ryūjin 竜神.mkv`; 1.02 GB | row 4: `Naruto 001 Remaster [V2] (BDRip 1080p x265 FLAC Multi) - Ryūjin 竜神.mkv`; 1.02 GB | score 41 → 50.5098; `seeders=8` absent → +9.50978; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=0,source-tier=0; single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Attack on Titan | row 17: `Attack on Titan (2013) - S01E01 (1) - To You, in 2000 Years The Fall of Shiganshina, Part 1 (1080p BluRay x265 ImE).mkv`; 0.71 GB | row 17: `Attack on Titan (2013) - S01E01 (1) - To You, in 2000 Years The Fall of Shiganshina, Part 1 (1080p BluRay x265 ImE).mkv`; 0.71 GB | score 53 → 65.9316; `seeders=49` +4 → +16.9316; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=healthy-or-direct` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Death Note | row 1: `[Erai-raws] Death Note - 01 [1080p][Multiple Subtitle][BCE68CE7].mkv`; 0.64 GB | row 10: `Death.Note.s01e01.BDRip.1080p.HQ.Saturated.Color.Rus.Eng.Jap.SDIncorporation.mkv`; 2.52 GB | score 57 → 54.0196; `language-unknown` absent → -3; `preferred-language` +12 → absent; `seeders=161` +10 → +22.0196; `source-quality:unknown-or-other` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=healthy-or-direct` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Fullmetal Alchemist: Brotherhood | row 1: `[Erai-raws] Fullmetal Alchemist - Brotherhood - 01 [1080p NF WEB-DL AVC AAC][MultiSub][C99DA55C].mkv`; 0.93 GB | row 2: `Fullmetal Alchemist Brotherhood (2009) - S01E01 - (1) - Fullmetal Alchemist (1080p BluRay x265 ImE).mkv`; 0.40 GB | score 59 → 54.7997; `language-unknown` absent → -3; `preferred-language` +12 → absent; `seeders=96` +9 → +19.7997; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=healthy-or-direct` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; single-episode-preferred-over-pack; streaming-size:preferred-known-friendly |
| Demon Slayer: Kimetsu no Yaiba | row 13: `Demon Slayer Kimetsu no Yaiba S01E01 VOSTFR 1080p WEB H.264 AAC -Tsundere-Raws (CR).mkv`; 1.93 GB | row 4: `[DB]Kimetsu no Yaiba_-_01_(Dual Audio_10bit_BD1080p_x265).mkv`; 0.83 GB | score 50 → 46.5707; `french-led:deferred-when-non-french-alternative-in-band` absent → +0; `language-unknown` absent → -3; `preferred-language` +12 → absent; `seeders=22` +2 → +13.5707; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=healthy-or-direct` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Jujutsu Kaisen | row 27: `[Erai-raws] Jujutsu Kaisen - 01 [1080p][Multiple Subtitle][863939DC].mkv`; 1.46 GB | row 5: `Jujutsu Kaisen - S01E01 v2 (BD 1080p HEVC) [Vodes].mkv`; 2.70 GB | score 51 → 62; `seeders=15` +1 → +12; `source-quality:unknown-or-other` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=below-floor-or-unknown` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Cowboy Bebop | row 9: `Cowboy.Bebop.S01E01.1080p-Hi10p.BluRay.FLAC5.1.x264.[BD1B305A].mkv`; 2.44 GB | row 9: `Cowboy.Bebop.S01E01.1080p-Hi10p.BluRay.FLAC5.1.x264.[BD1B305A].mkv`; 2.44 GB | score 60 → 70.4605; `seeders=112` +10 → +20.4605; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=healthy-or-direct` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Frieren: Beyond Journey's End | row 20: `[Anitsu] Sousou no Frieren - S01E01 [BD 1080p x265 Opus] [DUAL][2C84B686].mkv`; 0.88 GB | row 20: `[Anitsu] Sousou no Frieren - S01E01 [BD 1080p x265 Opus] [DUAL][2C84B686].mkv`; 0.88 GB | score 55 → 67.0671; `seeders=64` +6 → +18.0671; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=healthy-or-direct` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |
| Steins; Gate | row 8: `Steins.Gate.S01E01.1080p.BluRay.10bit.x265.FLAC.Dual.Audio-QM.[0831ACC0].mkv`; 2.49 GB | row 8: `Steins.Gate.S01E01.1080p.BluRay.10bit.x265.FLAC.Dual.Audio-QM.[0831ACC0].mkv`; 2.49 GB | score 46 → 58.2647; `seeders=53` +5 → +17.2647; `source-quality:premium-disc-or-web` absent → +0; `streaming-size:preferred-known-friendly` +0 → absent; `torrent-health:floor=20,status=healthy-or-direct` absent → +0 | primary-resolution-policy:1080p (1080p > 720p > 4K > SD); quality-policy:french-led=0,upscale=0,healthy=1,source-tier=0; single-episode-preferred-over-pack; dual-audio-wins-score-ties; streaming-size:preferred-known-friendly |

13 of 20 primary row selections changed. No new network responses were captured for this replay.


The three reported regressions now select:
- **Godfather:** row 23, Tigole BluRay, 10.28 GB, 183 advertised seeders; no AI/conversion marker.
- **Matrix:** row 39, GalaxyRG265 BluRay, 3.25 GB, 274 advertised seeders; not HDTV.
- **Demon Slayer:** row 4, DB dual-audio BD1080p, 0.83 GB, 81 advertised seeders; not VOSTFR.

One Piece remains a 1080p selected episode file (now Lia row 10); Clayface still has no primary. All round-two assertions, including Parasite GB-language exclusion and film size caps, pass.

[Full replay with previous-winner current reasons](../tests/stream/fixtures/pick-results.json). [Capture provenance and reproduction](../tests/stream/fixtures/README.md).

## Known gaps

- No human-pick agreement, actual playback, default audio-track verification, DV profile probing or native-resolution verification. Provider labels, seed counts and size/runtime estimates are not authenticated.
- Generic marker heuristics can collide with title words such as AI/FRENCH/WEB; boundary and selected title controls reduce but do not eliminate semantic ambiguity. VOST describes subtitled original audio, not proof of a specific audio language; French-led deferral is a release-policy heuristic.
- Old-TV upscale inference is deliberately narrow and can still misclassify unlabelled legitimate restorations. It remains a fallback heuristic, not a hard trust rejection or content-authenticity claim.
- Source quality precedes swarm health as described above. A source-preferred low-seed fallback can still buffer. Average bitrate does not predict peak bitrate or actual torrent throughput.
- Positive second-provider ranking remains unqualified: Comet was Forbidden and MediaFusion returned empty responses during original collection.
- Retained Harbor behavior: fileIdx/anime can bypass trust episode comparisons; the 60-day cinema gate can reject genuine early digital releases; regional release dates can affect year penalties; inherited trust language/size/pack flags remain unused. `by_tier` is best numeric score, not a primary-safety guarantee.
- Human acceptance, full application, Windows/Android and master adoption remain pending. No release or deployment was performed.

## Reproduce

```sh
cmake -S native -B build-stream -DCOLOSSEUM_STREAM_PICKER_TESTS_ONLY=ON \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Werror"
cmake --build build-stream --parallel
ctest --test-dir build-stream --output-on-failure
git show 3b718a18d09cc5628ad08ac1bd158931d7e16ea1:tests/stream/fixtures/pick-results.json > before.json
build-stream/stream-tests/stream_fixture_test pick-results.json before.json
```

Clang: separate build directory and `-DCMAKE_CXX_COMPILER=clang++`. ASan/UBSan: separate build, add `-fsanitize=address,undefined -fno-omit-frame-pointer -g` to warning flags and run with the sanitizer environment above. All five suites are offline. `collect.py` is an optional explicit network refresh, not a test dependency.

**IMPLEMENTATION COMPLETE.** This is review-branch delivery, not human acceptance or master adoption.
