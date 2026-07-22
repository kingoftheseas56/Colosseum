# Player 2 — Lab Parity Ledger

Parity is a ledger, not a feeling. A row is complete only when the behavior exists in the standalone
harness, has automated evidence where practical, AND passes an eyes-on comparison against the current
player. **No row is complete until both evidence columns are populated.** Status is restricted to
`NOT RUN`, `FAIL`, `PASS`, or `ACCEPTED EXCEPTION (<Hemanth's written reference>)`.

- **Production evidence** — the current-player behavior/file this row matches (`qml/PlayerPage.qml`
  and friends; read-only, never modified).
- **Harness test** — the automated/deterministic check, if any.
- **Eyes-on result** — Hemanth's side-by-side verdict.
- **Lab status** — the standalone-harness closure. **Integrated status** — reopened after
  `ColosseumPlayer2HostServices` is connected (Task 17); starts `NOT RUN` for every host-backed row.

Seeded from the design parity table (`2026-07-21-colosseum-player2-isolated-development-design.md`).
Task 13 builds the core playback chrome; source/episode/skip/window/capture/live rows belong to
Tasks 14–15 and stay `NOT RUN` here.

## Core playback chrome (Task 13)

| Behavior | Production evidence | Harness test | Eyes-on result | Lab status | Integrated status | Notes |
|---|---|---|---|---|---|---|
| Play / pause (button + Space + click) | PlayerPage transportRow hero + `togglePlayPause` | harness loads shell, gate green | NOT RUN | NOT RUN | NOT RUN | TransportBar hero → `session.play/pause` |
| Exact seek (scrub) | seekBar `seekTo`→`mpv.seekExact` | shell loads | NOT RUN | NOT RUN | NOT RUN | SeekBar release → `session.seekExact`; no position-timer |
| Relative seek (skip ±10, ← / →) | `seekStep`→`mpv.seekStep` | shell loads | NOT RUN | NOT RUN | NOT RUN | → `session.seekRelative` |
| Seek bar (progress, chapters, handle, hover time) | seekBar fills + chapter ticks + hover card | shell loads | NOT RUN | NOT RUN | NOT RUN | gold progress, chapter ticks from `session.chapters` |
| Buffer/cache fill on seek bar | seekBar cache fill (`mpv.cacheTime`) | — | NOT RUN | NOT RUN | NOT RUN | `bufferedFraction` not yet surfaced by the session (gap) |
| Volume + mute (slider, M, wheel) | VolumeControl + `M` + wheel ±5% | shell loads | NOT RUN | NOT RUN | NOT RUN | → `session.setVolume/setMuted` |
| Elapsed / duration / remaining toggle | seekRow time labels | shell loads | NOT RUN | NOT RUN | NOT RUN | click duration → remaining |
| State line (buffering / paused / seek) | stateRow `stateLineText` | shell loads | NOT RUN | NOT RUN | NOT RUN | ends-at wall clock deferred to slice 2 |
| Auto-hide + cursor hide | `hideTimer` 1800/4500ms | shell loads | NOT RUN | NOT RUN | NOT RUN | pins visible while paused/buffering; menu-pin in slice 2 |
| Fullscreen request | fullscreen RoundButton + F | shell loads | NOT RUN | NOT RUN | NOT RUN | emits `fullscreenRequested()` for the host |
| Prev / next episode | transportRow prev/next | — | NOT RUN | NOT RUN | NOT RUN | slice 2 (needs host adjacency) |
| Frame step (`,` / `.`) | `mpv.frameStep/frameBackStep` | — | NOT RUN | NOT RUN | NOT RUN | slice 2 (session has `frameStep`) |
| Speed + sleep timer | SpeedMenuButton | — | NOT RUN | NOT RUN | NOT RUN | slice 2 (session speed not yet exposed — gap) |
| Audio track menu + delay | AudioMenu + SYNC row | — | NOT RUN | NOT RUN | NOT RUN | slice 2 (TrackMenu) |
| Subtitle menu + delay + style | SubtitleMenu + SubStyleBar | — | NOT RUN | NOT RUN | NOT RUN | slice 2 (TrackMenu + SubtitleLayer) |
| Subtitle rendering | mpv/libass on the surface | — | NOT RUN | NOT RUN | NOT RUN | Player 2 paints cues in QML (SubtitleLayer) from `session.subtitleCue` |
| Fit / fill / aspect / zoom | FillMenuButton | — | NOT RUN | NOT RUN | NOT RUN | slice 2 (session `videoAspect/panscan/videoZoom`) |
| Loudness normalization (Smooth/Light/Full) | overflow loudness cycle | — | NOT RUN | NOT RUN | NOT RUN | slice 2 (session `normalizationMode`) |
| Chapters (labels, current, switch) | seek ticks + chapter transient | shell loads | NOT RUN | NOT RUN | NOT RUN | ticks shown; label/menu slice 2 |
| Stats overlay (`D`) | statsOverlay | — | NOT RUN | NOT RUN | NOT RUN | slice 2 (StatsOverlay ← `diagnosticsSnapshot`) |
| Loading / buffering / error screen | PlayerLoadingScreen | — | NOT RUN | NOT RUN | NOT RUN | slice 2 |
| Hotkeys / shortcut sheet | PlayerHotkeys.js + ShortcutsSheet | — | NOT RUN | NOT RUN | NOT RUN | slice 1 wires Space/←/→/M/F; full registry + sheet slice 2 |
| Context menu (right-click) | overflowPanel | — | NOT RUN | NOT RUN | NOT RUN | slice 2 |
| Pause card | pauseCard | — | NOT RUN | NOT RUN | NOT RUN | slice 2 |
| Compact folds (tight/snug/tiny) | width-driven folds | — | NOT RUN | NOT RUN | NOT RUN | slice 2 |

## Deferred to Tasks 14–15 (source/episode/skip/window/capture/live)

Source recovery drawer, episode browser / Up Next, skip segments, window/PiP/minimize, screenshot/GIF,
live/DVR — all `NOT RUN`; owned by Tasks 14–15.

## Numeric gates (Task 16)

A/V p95, 2h soak, 100 seeks, 50 open/close, memory, ABBA efficiency ≥25%, hardware matrix,
normalization measurements — all `NOT RUN`; owned by Task 16.
