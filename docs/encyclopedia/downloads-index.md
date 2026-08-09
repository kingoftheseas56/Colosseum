# Colosseum Code Encyclopedia -- Generated Source Index

> **GENERATED FILE -- DO NOT EDIT.** Edit source comments, then run the generator.
> Acceptance state: `docs/encyclopedia/downloads-state.json`

## Summary

- Total files: **6**
- Documented: **3**
- Undocumented: **3**
- Drifted: **0**

<a id="file-native-engine-audiobookdownloader-cpp"></a>
## `native/engine/AudiobookDownloader.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `fabb27a938d133fc52ce403376c031e6bc43849f`
- Current blob: `fabb27a938d133fc52ce403376c031e6bc43849f`
- Source: [`native/engine/AudiobookDownloader.cpp`](../../native/engine/AudiobookDownloader.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-audiobookdownloader-h"></a>
## `native/engine/AudiobookDownloader.h`

- Status: **CURRENT**
- Accepted blob: `eadaaca5797b2a6bb713b7d523b51b353cebb3c2`
- Current blob: `eadaaca5797b2a6bb713b7d523b51b353cebb3c2`
- Source: [`native/engine/AudiobookDownloader.h`](../../native/engine/AudiobookDownloader.h)

```text
// AudiobookDownloader.h
//
// The audiobook half of the download-fed backbone. An audiobook is a torrent of
// N audio files (a single .m4b, or a set of .mp3 chapters). This ports
// BookDownloader's proven HTTP-stream-to-disk machinery but sources bytes from
// the Stremio engine's localhost HTTP instead of LibGen, and keys everything by
// `pairKey` (the title+author pairing identity) so a book page can flip to "Listen".
//
// Transport (proven live 2026-07-12 — see docs plan Task 0):
//   1. m_stream->prefetch(infoHash, 0) starts/adopts the Stremio engine AND
//      registers the torrent, then emits fetchReady(url, infoHash, 0). The url
//      (http://127.0.0.1:<port>/<infoHash>/0) gives us the engine base.
//   2. POST <base>/<infoHash>/create → JSON { files:[{path,name,length,offset}] }.
//      fileIdx = the index into that files[] array. Filter to audio extensions,
//      natural-sort, and each file streams from <base>/<infoHash>/<fileIdx>
//      COMPLETE (plain GET, no Range → whole file; proven, no buffer cap).
//   3. Stream each file to <appdata>/audiobooks/<pairKeyHash>/<NN - name>.<ext>
//      via chunked readyRead → .part → atomic rename (BookDownloader lineage).
//
// On-disk: <appdata>/audiobooks/<pairKeyHash>/ + <appdata>/audiobooks/index.json.
//
// Threading: pure QNetworkAccessManager + QObject lambdas on the main thread.
```

<a id="file-native-engine-downloadfileops-h"></a>
## `native/engine/DownloadFileOps.h`

- Status: **UNDOCUMENTED**
- Accepted blob: `a800c26171645a39b8a6b09195159893c9dd961f`
- Current blob: `a800c26171645a39b8a6b09195159893c9dd961f`
- Source: [`native/engine/DownloadFileOps.h`](../../native/engine/DownloadFileOps.h)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-localdownloads-cpp"></a>
## `native/engine/LocalDownloads.cpp`

- Status: **UNDOCUMENTED**
- Accepted blob: `73df1093a1137d760ee03db8681b61d565520c4f`
- Current blob: `73df1093a1137d760ee03db8681b61d565520c4f`
- Source: [`native/engine/LocalDownloads.cpp`](../../native/engine/LocalDownloads.cpp)

_No explanatory comment was harvested after the allowed file preamble._

<a id="file-native-engine-localdownloads-h"></a>
## `native/engine/LocalDownloads.h`

- Status: **CURRENT**
- Accepted blob: `9d5e7e62773c2d22d7f9920aa83ca055f77b0e1c`
- Current blob: `9d5e7e62773c2d22d7f9920aa83ca055f77b0e1c`
- Source: [`native/engine/LocalDownloads.h`](../../native/engine/LocalDownloads.h)

```text
// LocalDownloads — the unified read-model behind the Downloads page.
// Normalizes all five download backbones (MangaDownloader, BookDownloader,
// ComicDownloader, player DownloadStore, and MangaTankobanService's volume
// lane) into one world → series → item shape so QML only renders. It owns NO
// files and NO network: every action routes to the owning backend. Progress
// (resume) is deliberately NOT consulted here — downloads answer "what exists
// locally", not "where do I resume".
// Design: chatgpt_requests/20260629-171426-…-plan-review/response.md (ratified),
// layout ratified 2026-07-04 (agents/colosseum-downloads-mock.html).
// 2026-07-16: Tankoban volume mode composed in — the page predated volume mode
// and silently omitted every volume it ingested (Hemanth eyes-on).
```

<a id="file-qml-downloadspage-qml"></a>
## `qml/DownloadsPage.qml`

- Status: **CURRENT**
- Accepted blob: `fdc2ff94352996e25018d12212895c85d0a020bf`
- Current blob: `fdc2ff94352996e25018d12212895c85d0a020bf`
- Source: [`qml/DownloadsPage.qml`](../../qml/DownloadsPage.qml)

```text
// DownloadsPage — everything the house holds locally, in one full page.
// Ratified design: agents/colosseum-downloads-mock.html (2026-07-04, "go with it").
// Structure IS the information: "Now arriving" (live jobs, cross-world) answers a
// different question than the vault shelves (settled files, world → series → item),
// so they are separate surfaces in that order. Data = LocalDownloads (read-model);
// every action routes back to the owning backend. No sample data — empty lanes
// say so honestly and route to their world.
```
