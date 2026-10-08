# Alpha composition control (no media decoding)

Build this directory independently with Qt Quick/WebEngineQuick/Test. Pass `-DCMAKE_CXX_FLAGS=-fPIC` on GCC/Clang for the tested newer-runtime comparison. Run `alpha_probe /absolute/evidence/directory` on a real OpenGL desktop; run `python3 verify.py /absolute/evidence/directory` with Pillow installed.

`ALPHA_START_TRANSPARENT=1` enables the deliberately transparent first-paint case. It passed 82/82 pixel/input assertions on the tested Qt 6.8.2 and Qt 6.11.1 runtimes. The executable was compiled against Qt 6.8.2. Native Qt Quick checkerboard, HTML captions/menu, native pointer events, resize, scroll and zoom are exercised. This is not video playback evidence.

Default mode starts with an opaque body then removes its background. It exposes a retained-pixel failure on the tested Qt 6.8.2 setup: 78/83 checks pass, five fail. The separate `--negative-as-transparent` verifier option intentionally rejects the real opaque screenshot. Do not run that option against the transparent-first case and claim a negative control.

Exact runtime records, screenshots and failure evidence are indexed in the task's Dots-Colosseum receiving entry. No Qt or Chromium source was built or changed.
