# Colosseum Player F10 — Anime4K Upscaling Toggle (design)

**Date:** 2026-07-18 · **Owner:** Agent 4 (player lane) · **Status:** ratified by Hemanth ("do the anime4k one too") right after F9 acceptance; his eyes-on is the gate.

## What it is (plain sentence)
A three-way choice in the player's Picture popover — Off / Anime4K Fast / Anime4K Quality — that runs the video through Anime4K's sharpening-and-upscaling shaders, sticky across sessions.

## Why
Ratified from stremio-community-v5 intel (its headline player feature). Pure mpv property work: our engine already runs GPU shaders; the feature is the shader files plus one setting.

## Design
- **Shaders vendored** at `resources/shaders/anime4k/` — 8 `.glsl` files (~390 KB) + MIT LICENSE from the official bloc97/Anime4K v4.0.1 release. Mode A recipes from the pack's own instructions: Fast = Restore_CNN_M → Upscale_x2_M (+ AutoDownscale pre-passes, x2_S second pass); Quality = the VL nets, heavier on GPU.
- **C++ seam:** `MpvItem::setGlslShaders(QStringList)` joins with the platform list separator and sets mpv's `glsl-shaders`. Dedicated invocable on purpose — QML gets no generic mpv property poke (doctrine).
- **QML:** `upscaleModes` recipe table + `applyUpscale(index)` on PlayerPage; persisted as `playerSettings.upscaleMode` (player.ini); reapplied on player open (skipped when "off"). UI = an "Upscaling" section appended to the existing Video (fill-mode) popover, same row styling, gold on the active choice; the Picture button glows when upscaling is on, matching non-default fill behavior.
- Live-applies mid-playback (mpv hot-swaps shader chains); no reload.

## Approaches considered
User-editable shader dirs / arbitrary shader packs (v5's model) — rejected: outsources tuning to the user. HDR passthrough — out of scope. Hotkey — skipped (registry crowded; popover is two clicks).

## Testing
Both shader chains compile-tested in real mpv against a test video (zero shader errors). qmllint + full build + boot smoke. GPU cost and visual judgment = Hemanth's eyes; if Quality stutters on the machine, Fast is the fallback and we can retune recipes.
