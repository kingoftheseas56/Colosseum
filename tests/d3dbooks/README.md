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

---

# Follow-up: can an in-app grab SEE the reader under D3D11? YES — pixel-identical

Run: `tests\d3dbooks\rungrab.bat gl` and `... d3d` (probe: `GrabTest.qml`).

The "uncapturable headless" memory rules out grabbing Colosseum from OUTSIDE the process
(GDI `CopyFromScreen`, `PrintWindow`, computer-use MCP). `QQuickItem::grabToImage()` is a
different mechanism — it re-renders through Qt's own scene graph, in-process. Two things
were unproven, and the second was the real risk:

1. does `grabToImage()` work on a D3D11 boot?
2. does it capture **WebEngine** content? Chromium renders OUT OF PROCESS, so its pixels
   could plausibly never appear in the scene graph at all.

**Both hold.** Each arm reported `GRAB_OK` and wrote a PNG containing Chromium's actual
pixels (red page, white "GRAB ME", blue block) — not a blank or black frame.

    grab-gl.png   (OpenGL boot)
    grab-d3d.png  (D3D11 boot, COLOSSEUM_PLAYER2=1)

**The two files are BYTE-IDENTICAL.** Chromium composites the same picture on both backends.

## Why this matters beyond books

This is the capability the lanista dev-control bridge
(`Brotherhood/docs/superpowers/plans/2026-08-01-colosseum-lanista-bridge.md`) is built on:
its golden-image work (Task 8) assumes `grabToImage()` can see the app. For WebEngine
surfaces on either backend, it can. Verified before the goldens were written, not after.

**API gotcha for whoever builds on this:** in the QML callback, `result.image.width` reads
back `undefined` — `QQuickItemGrabResult.image` is not introspectable that way from QML.
Use `result.saveToFile(path)` (returns bool) and take dimensions from the source item.
