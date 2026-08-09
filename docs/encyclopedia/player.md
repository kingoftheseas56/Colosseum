# Player (Theatre playback) — subsystem guide

> **Hand-written. Keep it true.** If you change how playback, recovery, resume, or the player
> chrome works, update this file in the same commit. The per-file index beside it
> ([`player-index.md`](player-index.md)) is generated — never edit that one.
>
> Verified against `master` (Player 1 / mpvqt path). Drafted via the encyclopedia arc,
> ground-truthed and adopted by Agent 0.

## 1. What this subsystem is for

Play the thing the user picked — a torrent stream, a downloaded file, or a direct/arriving URL —
through one mpv-backed fullscreen page, with recovery, resume, and Continue Watching kept honest.
(The hosted web-player surface — VidKing — was removed 2026-08-07, `4053fdc`; Theatre now offers
only sources it can verify: torrents + direct HTTP.)

## 2. The flow

```
any door (TheatreSeries, Continue tile, Downloads, Universe, SourcesSheet)
  → Main.qml openPlayer() / openMovieSession()            (Main.qml:1127, 1334)
  → playerLayer.item.play*                                (Main.qml:2536 — PlayerPage.qml)
      playTorrent     → StreamServer.play(infoHash,fileIdx) → stremio-runtime (TB2's
                        server.js) → http://127.0.0.1:<port>/<infoHash>/<fileIdx>
                        → streamReady(url) → mpv.loadFile(url)      (PlayerPage.qml:1179, 1240)
      playLocalFile   → mpv.loadFile(localPath)                      (PlayerPage.qml:2058)
      playRemoteUrl   → mpv.loadFile(url)    (play-while-downloading) (PlayerPage.qml:2123)
  → MpvItem (mpvqt → libmpv)  →  OpenGL RHI  →  pixels on screen
```

**The boot RHI choice gates everything.** mpvqt renders through OpenGL, so the process-wide
graphics backend is picked in `native/main.cpp` **before the app object exists** (main.cpp:436–476).
Player 1 is the default boot; `COLOSSEUM_PLAYER2=1` (on a Player-2 build) boots the experimental
D3D11 engine instead, and the two can never coexist in one process (see Traps).

**Player 2 is out of the main build (Hemanth's ruling, 2026-08-09).** "Largely discontinued — it
has no reason to exist inside the main build; player 1 is our main and only player unless we hit a
problem we can't solve with player 1." `COLOSSEUM_PLAYER2_IN_APP` now defaults OFF
(native/CMakeLists.txt): stock builds don't link `player2_core`, don't define `COLOSSEUM_PLAYER2`,
don't copy the FFmpeg-62 runtime beside the exe, and report `Player2Available=false` (the shell
then routes PlayerPage.qml — the designed seam). The lab (`native/player2/` + its CTest suite)
stays in the tree, opt-in via the same flag; flipping it ON restores the old wiring intact.

**The state machine lives in QML.** PlayerPage.qml owns starting/errored/fileReady, the recovery
watch, stream candidates + retry/switch, the resume overlay, skip segments, Up-Next, and the
loading face. `MpvItem` stays a thin property/command surface over libmpv.

## 3. The files that matter

| File | Role |
|---|---|
| `qml/PlayerPage.qml` | the player chrome AND the whole playback state machine — 5,866 lines; the only QML that talks to mpv |
| `native/player/mpvitem.{h,cpp}` | the one mpv surface (`MpvItem`, QML type `Colosseum.Player`); cached observed props, load/seek/tracks, frame grab, GIF |
| `native/player/streamserver.{h,cpp}` | `Stream` — torrent → localhost HTTP via TB2's `stremio-runtime` child process |
| `native/player/downloadstore.{h,cpp}` | `Download` — video downloads + downloaded-videos library; feeds play-while-arriving |
| `native/ProgressStore.h` | `Progress` — the Continue Watching / resume backbone (shared with manga/biblio) |
| `native/SessionStore.h` | `Sessions` — the shell's open-sessions model the player's restore state rides on |
| `native/player/seekthumbnailer.{h,cpp}` | F9 hover-thumbnails — one ffmpeg per hovered 5s bucket, LRU `data:` URLs |
| `native/player/windowmodestore.{h,cpp}`, `powerstore.{h,cpp}`, `windowstatepolicy.{h,cpp}` | shell window (fullscreen / F11-windowed / PiP) + display-sleep inhibit during playback |
| `qml/PlayerLoadingScreen.qml` | Stremio-style startup loader (logo + backdrop + indeterminate bar) |
| `qml/PlayerHotkeys.js`, `qml/PlayerTrackPrefs.js`, `qml/Subtitles.js` | pure-JS logic: hotkey registry, per-show track prefs, online-subtitle fetch |
| `native/main.cpp` | boot RHI pick (L436–476) + player type registration (L543–550) |

## 4. Where state lives

- **Continue Watching / resume — `Progress`** (ProgressStore.h). Persisted to QSettings key
  `continue/entries` (org `Brotherhood`, app `Colosseum`), keyed `"<kind>\x1f<id>"`. Video ids are
  the *identity* (`tt…`, `tt…:s:e`, else `infoHash:fileIdx` / `local:<path>`), so a resume point is
  **cross-source**: any torrent of the same episode resumes the same position. Series entries group
  per series-root; `forget()` drops the whole group; a movie past 90% is finished and drops off
  Continue (ProgressStore.h:359–385). Writes are **off-thread** (5s silent tick + lifecycle writes)
  since the 2026-07-29 stutter fix.
- **Player prefs — `player.ini`** (Settings block, PlayerPage.qml:37–59): seek step, skip segments,
  loudness mode, subtitle/audio language policy, and `trackPrefsJson` — per-show track prefs,
  allowlisted fields, capped at 200 shows (PlayerTrackPrefs.js).
- **Sessions — in-memory only** (SessionStore.h): which surfaces are open + each one's saved-state
  blob. Not persisted.
- **Window/geometry — WindowModeStore** QSettings (fullscreen vs F11-windowed, PiP, saved geometry).
- **Downloads — `<appdata>/videos/index.json` + `videos/queue.json`** (DownloadStore).
- **Captures / GIFs — `<Pictures>/Colosseum/`** (mpvitem.cpp:774–808).

## 5. Traps

1. **The two players are a boot choice, and Player 1 is the daily driver.** Qt picks the RHI once
   per process, before the app exists (main.cpp:436–476). `COLOSSEUM_PLAYER2=1` on a Player-2 build
   boots D3D11; everything else is OpenGL/mpv. The Task 18 default-flip was walked back the same
   evening (main.cpp:446–462 — that comment block itself contradicts; see report). Never "route a
   playback to Player 2" at runtime: the process cannot render it unless it booted D3D11, and
   `Player2Available` reports the real boot (main.cpp:1067–1073; the 2026-07-25 scar).
2. **A busy GUI thread holds ready video frames hostage** — the known Qt trap, verified here:
   Qt Quick's threaded render loop cannot present until the GUI thread hands the frame over, and
   mpv's dropped-frame counter is blind to it (GuiStallProbe.h:1–23; frame_pacing.sh measured
   median 26ms / p99 223ms GUI gaps while mpv dropped nothing). Stutter work starts with
   `COLOSSEUM_GUI_STALL_PROBE=40` and chasing GUI-thread work, not mpv knobs.
3. **Progress writes were a proven stutter source.** The 5s playback tick MUST use
   `Progress.recordSilent` — `record()` bumps `changed()` and re-renders every Continue tile
   (2026-07-29 A/B: +100 dropped frames/60s with `changed()` firing vs +3 with it suppressed).
   Disk writes are off-thread on purpose; never add a GUI-thread `QSettings::sync()` to the hot
   path (ProgressStore.h:10–27, 176–178).
4. **`mpv.position` et al. must stay cached-read.** Getters answer from observed values; a blocking
   `getProperty` on the GUI thread takes the mpv core lock per binding evaluation — PlayerPage has
   30+ `mpv.position` reads (mpvitem.cpp:124–142, 527–543; comment says 27, grep says 34, the
   point is the same).
5. **Loudness "full" stutters weak hardware** (2026-07-20 audit): EBU R128 loudnorm upsamples to
   192kHz at ~65x audio CPU. Default is off; it is a live global `af` option — change modes via
   `setAudioNormalization`, never per-file filters (mpvitem.cpp:336–351).
6. **mpv error codes are knowingly inert.** Vendored mpvqt forwards only the coarse end-file reason,
   so `mapEndFileErrorCode` always returns "unknown" — the substring branches can never match today.
   Intentional and harmless: recovery works off PlayerPage's reason ladder. Don't extend the mapper
   expecting real codes until mpvqt forwards `prop->error` (mpvitem.cpp:240–274).
7. **`audio-stream-silence=yes` must not go.** Windows reclaims idle WASAPI streams (paused +
   minimized is the normal parked state), and the session then resumes into dead audio until
   restart. Feeding silence keeps the AO alive (mpvitem.cpp:67–71).
8. **EOF can be a lie for arriving streams.** If mpv hits the end of the `.part` while the download
   is still running (position < duration − 5), that is not the end of the film — hand over to the
   live stream (PlayerPage.qml:3073–3080). Related: downloads use `StreamServer.prefetch`
   (`fetchReady`) precisely because PlayerPage loads **every** `streamReady` it hears
   (streamserver.h:44–47).
9. **Local playback is not isolated from online lookup.** `playLocalFile` still calls
   `fetchSubtitles()` and records Progress keyed off `mediaId` (which becomes `local:<path>` when
   there is no tt id) (PlayerPage.qml:2058–2101). Expect network + Continue behavior from local
   files; and `Progress.forget` removes the whole series group, not one episode.
10. **mpv property-name traps.** `frame-drop-count` is the VO (output) drop count,
    `decoder-frame-drop-count` the decoder's; `vo-drop-frame-count` does not exist — asking returns
    an ErrorReturn that renders as "NaN" in QML. Keep the allowlist as-is (mpvitem.cpp:355–367).
11. **Seek thumbs and GIFs share one ffmpeg** (`MpvItem::findFfmpeg`; exe dir → `tools/` → PATH).
    One GIF encode at a time; abort during encoding is a no-op; teardown kills the encode so no
    truncated GIF poses as a saved clip (mpvitem.cpp:82–95, 414–520).
12. **The hosted-player (VidKing) graceful-degrade branch is still live.** VidKing was removed
    end-to-end (`4053fdc`, 2026-08-07), but `Main.qml` keeps a `hostedPlayerId` branch so an old
    Continue Watching entry stamped before removal routes to the Theatre detail page for a real
    source pick (never crashes, never bypasses the extension switch). Seeing `hostedPlayerId` in
    Main.qml is not a leftover to delete — it is the deliberate retirement ramp. Do not mistake it
    for the feature coming back; the progress parity gate ASSERTS Main carries no hosted playback
    machinery, exactly to trip a partial future revival.

## 6. How to test it

- **Unit (ctest):** `ctest -L unit` runs `colosseum.progress_store_harness` (record /
  recordSilent / forget-group / watchedMark), `colosseum.window_state_policy_harness`, and the
  QtTest `colosseum.qttest.window_state_policy` (tests/CMakeLists.txt:28–79). No mpv needed.
- **Runtime bridge:** `tests/lanista_harness.cpp` boots a real engine over a deterministic scene;
  the player exposes `objectName: "player"` with `playbackStarted` / `playbackPosition` proxies so a
  scenario asserts "playback actually advanced", not "the page opened" (PlayerPage.qml:24–35).
- **Text-contract parity gates:** the `tests/test_player_*_p0.ps1` suite (e.g.
  `test_player_p0_parity.ps1`) grep-asserts that PlayerPage/Main/SourcesSheet still carry the
  signals and state the doors rely on. They prove wiring exists, never that playback works.
- **What it cannot cover:** real rendering (needs an OpenGL boot + GPU), stutter (diagnose with
  `tests/frame_pacing.sh` + `COLOSSEUM_GUI_STALL_PROBE`, not the harnesses), chrome feel, and live
  torrents — those stay eyes-on with the running app.

## Keeping this page honest

```bash
# refresh the index after editing any source comment
python scripts/code_encyclopedia.py --paths docs/encyclopedia/player.paths \
  --output docs/encyclopedia/player-index.md --state docs/encyclopedia/player-state.json

# gate: fails if a file changed since its description was accepted
python scripts/code_encyclopedia.py ... --check

# after reviewing a changed comment, ratify it
python scripts/code_encyclopedia.py ... --accept <path>
```
