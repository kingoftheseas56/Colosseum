# Standalone Qt 6.11.1 download-source reproducer

This folder is independently buildable. It links only public Qt modules, with no
Colosseum source, owner, backend, data, private Qt header or external website.

The QML test is byte-identical to the two-case fixture already observed in the
shared harness. The small standalone HTTP/setup extraction is a separate
candidate and is not runtime-qualified until its own run is recorded.

## Build and run

Use an already installed official Qt 6.11.1 desktop package containing Quick,
QuickTest, Network and WebEngineQuick. No Qt runtime patch is required.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/gcc_64
cmake --build build --parallel
./build/feria_source_binding_reproducer
```

On headless Linux with the normal Qt/WebEngine runtime dependencies and Xvfb:

```sh
xvfb-run -a -s '-screen 0 1280x800x24' ./build/feria_source_binding_reproducer
```

Run as an ordinary non-root user. This folder does not install dependencies,
alter security settings, or turn off the Chromium sandbox.

## What it does

1. Load local document A in one retained view.
2. Click a same-origin download link; wait for the loopback server to receive it.
3. Withhold all attachment response headers/body until explicitly released.
4. Control: release while A is current. Provenance case: load B in the same view,
   then release.
5. Log the initiating fixture document and the current known view document when
   the native profile callback arrives; cancel the download.

Expected diagnostic: `FERIA_SOURCE_BINDING_PROVENANCE_GAP_OBSERVED`, showing A as
the initiator and B as the current retained view document. If navigation cancels
the operation, the test emits `INCONCLUSIVE` and skips rather than claiming proof.

The test never reads `request.view`/`page`, intentionally destroys a pending source,
or probes a null/stale native pointer. The observation proves current-view
application metadata is insufficient for originating-document attribution. It
does not establish what the unsafe source getter would return or reproduce a crash.
