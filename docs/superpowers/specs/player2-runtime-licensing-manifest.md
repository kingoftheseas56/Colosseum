# Player 2 — Runtime Licensing Manifest (Task 16)

> Recorded 2026-07-25 from the actual DLLs the lab links/loads (`ffmpeg -version` on the shipped
> binaries, not from memory). Kodi remains an architecture reference only — no Kodi implementation is
> copied. Qt is licensed separately at the app level (unchanged by Player 2).

## FFmpeg runtime (the only third-party media dependency Player 2 adds)

- **Build:** `N-123581-ga077da895b-20260321` (BtbN auto-build, `win64-gpl-shared`), gcc 15.2.0
  cross-compiled, unmodified upstream binaries. Origin: github.com/BtbN/FFmpeg-Builds nightlies.
- **Shipped DLLs (7):** `avcodec-62`, `avdevice-62`, `avfilter-11`, `avformat-62`, `avutil-60`,
  `swresample-6`, `swscale-9` (+ the bundle's `LICENSE.txt` must ship beside them).
- **Modification status:** UNMODIFIED. Player 2 uses the public C API only (demux, decode, swresample,
  avfilter graphs: `atempo`, `loudnorm`/`dynaudnorm`, `aformat`).

### ⚠ Licensing finding (decision needed at Task 17 packaging)
The lab build is the **GPL** variant (`--enable-gpl --enable-version3`, includes libx264/x265/xvid,
libdvdread, etc.). Shipping these exact DLLs places the combined distribution under **GPLv3**.
Colosseum's repo is public, so GPL compliance is *possible* — but Player 2 only needs decode/demux/
filter features that are all available in the **LGPL** variant (`win64-lgpl-shared`): it never encodes
H.264/H.265. **Recommendation:** swap the packaged DLLs to BtbN's `win64-lgpl-shared` build at Task 17
(a drop-in: same API/ABI majors), keeping GPL only if Hemanth explicitly prefers it. Either way the
required notices = FFmpeg's LICENSE/COPYING files shipped with the DLLs + a credits mention.

## Optional media dependencies
- **libass:** NOT linked by Player 2 (subtitles render via our own text layer + PGS bitmap path).
  The FFmpeg bundle has it compiled in; unused by our API surface.
- **miniz** (GunzipReply, A0's Jikan work) and other app-side libs: outside Player 2's scope.

## What Player 2 itself adds
All Player 2 native code (`native/player2/**`) is first-party Colosseum code. The D3D11 zero-copy
pipeline uses the Windows SDK only. WASAPI audio uses the Windows SDK only. No other third-party code
is vendored into the player.
