# Colosseum Player F9 — Seek-Bar Thumbnail Previews (design)

**Date:** 2026-07-18 · **Owner:** Agent 4 (player lane) · **Status:** ratified by Hemanth ("start with the seek thumbnails"), design calls made per recorded taste rules; his eyes-on is the acceptance gate.

## What it is (plain sentence)
Hover the seek bar → the existing timestamp tooltip grows a small picture of that moment in the video.

## Why
Ratified 2026-07-18 from stremio-community-v5 intel (its ThumbFast integration). Ours rides on machinery we already ship: ffmpeg (gif capture already finds it), the player's current source URL, and the existing hover tooltip at the seek bar.

## Approaches considered
1. **ThumbFast-style secondary libmpv instance** — how mpv scripts do it; renders into a shared buffer. Rejected: designed for mpv's OSD, wrong shape for our Qt Quick chrome; heavy.
2. **Background pre-generation of a full thumbnail strip** — smooth once built. Rejected: hammers the torrent stream with random access while it's trying to play; wasted work for positions never hovered.
3. **On-demand single-frame ffmpeg extraction with cache (CHOSEN)** — one short-lived `ffmpeg -ss <t> -i <src> -frames:v 1` per hovered bucket, async, LRU-cached, latest-wins. Smallest thing that works; graceful on both local files (fast) and torrent-HTTP (range-seek, ~0.3–1s).

## Design
**C++ (`native/player/seekthumbnailer.{h,cpp}`, QML type `SeekThumbnailer`)** — QML paints, C++ decides: process transport, caching, threading live here.
- `request(source: url, timeSec: real)` → bucket = floor(t/5s)·5s. Cache hit → emit immediately. Same-bucket job already running → ignore. Different bucket running → kill it, start the new one (latest-wins; no queue).
- ffmpeg: `-hide_banner -loglevel error -ss <bucket> -i <src> -frames:v 1 -vf scale=320:-2 -f mjpeg pipe:1`, stdout captured; 10s kill-timer guards HTTP stalls. `-ss` before `-i` = fast keyframe seek, and range-seek over StremioService HTTP.
- Result → base64 `data:image/jpeg;base64,…` string, cached (QCache, 128 entries ≈ 10min of unique hovers) → `thumbReady(bucketSec, imageUrl)`.
- `reset()` clears cache + kills any job; called on source change.
- ffmpeg discovery: `MpvItem::findFfmpeg()` made static and reused (exe dir → tools/ → PATH). No members involved.

**QML (PlayerPage.qml, the existing hover tooltip at the seek bar)**
- Tooltip card widens to hold an `Image` (16:9, 200×112) above the timestamp when a thumb for the hovered bucket is ready; timestamp-only until then (today's look = the loading state — no spinners, per taste).
- Hover motion requests per bucket; `thumbReady` sets the image only if it still matches the hovered bucket.
- Same visibility rule as today's tooltip (`hovered && !seeking`); no new surfaces, no menus. Grayscale glass, matches the existing card (black 0.86, white hairline).

## Out of scope
Pre-generated strips, thumbnails while dragging (tooltip already hides while seeking — unchanged), per-episode/grid thumbnails, disk persistence of thumbs.

## Testing
qmllint + full MSVC build + boot smoke (lazy player page: lint covers QML syntax). Extraction is process-plumbing — meaningful verification is eyes-on: hover a downloaded file (fast thumbs) and a torrent stream (slower, appears once fetched). Failure mode is silent fallback to today's timestamp-only tooltip.
