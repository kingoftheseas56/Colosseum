# Books-under-Direct3D probe — RESULT: the assumption was FALSE

`native/main.cpp` picks ONE graphics backend per process: Direct3D11 when Player 2 boots
(`COLOSSEUM_PLAYER2=1`), OpenGL otherwise. The inherited belief was that QtWebEngine —
which IS the book reader (Reader 2 = `WebEngineView` + web channel over the vendored
reader) — requires OpenGL, so Player 2's D3D11 could never coexist with books.

**Nobody had ever tested it.** Asserted in `main.cpp`, unstated in Qt's docs, flagged
unverified in the brotherhood memory. Tested 2026-08-01 (Agent 4).

## How

`WebTest.qml` is run through `colosseum.exe` (which calls `QtWebEngineQuick::initialize()`),
identical file, both arms, via `run.bat`:

    tests\d3dbooks\run.bat gl      # OpenGL  — today's behaviour (control)
    tests\d3dbooks\run.bat d3d     # D3D11   — COLOSSEUM_PLAYER2=1

The page reports on ITSELF: it flips `document.title` to `PAINTED` only after its own
`requestAnimationFrame` has run five times — i.e. Chromium actually produced frames, not
merely parsed HTML. `onRenderProcessTerminated` catches a GPU-process death.

## Result

| Arm | Backend line from the log | Verdict |
|---|---|---|
| gl  | `Creating QRhi with backend OpenGL` | `CHROMIUM_COMPOSITED` |
| d3d | `Creating QRhi with backend D3D11`  | `CHROMIUM_COMPOSITED` |

No GPU errors, no render-process death, no silent fallback. The D3D11 arm even wrote its
own `qqpc_d3d11` pipeline cache.

**QtWebEngine composites fine under Direct3D 11 on this machine (Intel UHD 620).**

## What this does NOT establish

- Reader 2's FULL stack (web channel bridge, foliate pagination, themes, audio pairing)
  was not exercised — only the engine underneath it.
- It says nothing about mpv. Player 1's `MpvItem` renders through a GL-only path, so
  flipping the app to D3D11 still costs the CURRENT player. The books half of the
  objection is dead; the mpv half is not.
