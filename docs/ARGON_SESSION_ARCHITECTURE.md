# Current Colosseum session model: source map for Argon

Source basis: canonical Colosseum master 536f1be5fceb23484092b58a424b4f79e4533e05. This is source analysis,
not a claim that a published binary or future OS image is runtime-qualified.

- native/SessionStore.h: single taskbar registry. Current source handles
  local media open/switch/close, savedState and Feria browser openBrowser
  sessions; browser IDs have a different lifecycle from local readers.
- qml/Main.qml: local media capture, teardown and restore on activeChanged.
  Comic/book Loaders are deactivated on switch. World pages remain warm.
- qml/Taskbar.qml: groups and opens session tiles.
- qml/PlayerPage.qml and qml/player2host/Player2Page.qml: warm media host;
  minimize pauses rather than destroying player. No proven timed eviction.
- qml/comicreader/ComicReaderSingleSurface.qml,
  ComicReaderDoubleSurface.qml, ComicReaderStripSurface.qml: explicit
  sourceSize decode caps, plus preview/HQ loading.
- qml/reader2/ReaderShell.qml and qml/reader2/Paper.qml: EPUB paper
  uses QtWebEngine and is unloaded with its reader Loader.
- qml/feria/FeriaSessionDeck.qml: retained QtWebEngine browser objects live
  in a shared deck outside the visible FeriaWorld Loader.
- qml/feria/FeriaQtWebEngine.qml: background freeze timer (30 seconds) has
  audible-page/callback/engine eligibility guards. Discarded state has an
  explicit exit-teardown path; do NOT confuse that with routine inactivity.

This is a HYBRID app-level session manager: loader-dehydrated readers,
warm video player, retained/frozen browser pages. It is not an OS process
scheduler and does not prove browser RAM release or renderer teardown.
SessionStore's in-memory records are not equivalent to durable crash restart.

Meaningful next test: measure private RAM, CPU/UI stalls, GPU allocations
and decoder lifetime across several visited Worlds, manga/EPUB, warm mpv and
Feria sessions. Do not promise zero crashes or complete VRAM release from
code inspection.
