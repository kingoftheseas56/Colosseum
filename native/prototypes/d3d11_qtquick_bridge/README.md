# D3D11VA-to-Qt Quick bridge prototype

This isolated Windows prototype answers one question: can Colosseum keep HEVC decoder frames on
the GPU, convert them to a Qt-compatible texture, and composite ordinary QML over them without
mpvqt's OpenGL framebuffer path? It is experiment code, not a replacement player.

The proven path is:

```text
FFmpeg D3D11VA P010/NV12 decoder surface
  -> ID3D11VideoProcessor conversion pass
  -> three-slot shared RGBA8 ring + two shared fences
  -> Qt D3D11 device
  -> QSGD3D11Texture::fromNative
  -> QQuickItem + QML overlay
```

This is not zero-copy. It has no GPU-to-CPU frame transfer, but it performs one GPU-to-GPU video
processor pass from the decoder surface into an RGBA8 presentation texture. Qt 6.11's public
native texture wrapper rendered RGBA8 correctly on this machine; the initially planned BGRA8
surface rendered black because the API has no format argument.

## Prerequisites

- Windows 11 and a D3D11.4-capable GPU/driver
- Visual Studio 2022 Community with the x64 C++ toolchain
- Qt 6.11.1 at `C:\Qt\6.11.1\msvc2022_64`
- CMake and Ninja under `C:\Qt\Tools`
- FFmpeg shared development build at `C:\tools\ffmpeg-master-latest-win64-gpl-shared`

The default FFmpeg root is a CMake cache variable and can be overridden with
`-DFFMPEG_ROOT=C:/another/location`.

## Configure and build

Open an x64 Visual Studio developer shell, or run `VsDevCmd.bat` first. The Qt installation on the
test machine also requires its local license-check bypass environment variable.

```powershell
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 -DCMAKE_BUILD_TYPE=Release
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build
```

Deploy Qt and FFmpeg beside the executable. The complete FFmpeg DLL set is copied because the
three directly linked libraries have shared-runtime dependencies.

```powershell
& 'C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe' --release --qmldir qml `
  build\d3d11_qtquick_bridge.exe
Get-ChildItem 'C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\*.dll' |
  Copy-Item -Destination build -Force
```

## Tests and runtime gates

```powershell
& .\tests\prototype_contract_test.ps1
& .\build\slot_ring_test.exe

& .\build\d3d11_qtquick_bridge.exe --source synthetic --duration 60 `
  --report "$env:TEMP\colosseum-d3d11-gate-a.json"

& .\build\d3d11_qtquick_bridge.exe --source hevc `
  --file "$env:USERPROFILE\Downloads\Colosseum\The Wire - S4E13 - Final Grades - 20260720_211141.mp4" `
  --duration 300 --report "$env:TEMP\colosseum-d3d11-gate-b-300s.json"
```

Press `F11` to exercise fullscreen composition. Gate B decodes video only; silence is expected.
The process exits rather than falling back if D3D11VA, shared fences, the adapter match, or the
video processor is unavailable.

## Counter meanings

- `decoded`: hardware frames returned by FFmpeg.
- `converted`: frames successfully processed from P010/NV12 into a shared RGBA8 slot.
- `published` / `generated`: converted or synthetic slots published after a producer-fence signal.
- `presented`: new slots selected by the Qt render item.
- `repeated`: item updates that retained the already displayed texture because no newer slot existed.
- `late`: decoded frames whose target PTS was missed by more than one source-frame duration.
- `dropped`: decoded frames skipped because all three slots were still owned by producer/consumer.
- `producerStarved`: producer slot-acquisition failures; equivalent to `dropped` for HEVC.
- `deviceErrors`: failed D3D calls or invalid ring transitions.
- `cpuTransfers`: deliberately fixed at zero; any future CPU frame path invalidates this experiment.

`presented` may trail `published` by a few frames at shutdown because the producer and Qt render
thread stop at different points. `repeated` counts scene-graph updates, not monitor refresh cycles.

## Boundaries

The prototype has no audio clock, subtitles, seek/flush protocol, streaming, HDR output,
deinterlacing policy, device-loss recovery, adaptive cadence, or production settings. It uses a
GPL FFmpeg build, and Kodi was consulted only as GPL architectural reference; production licensing
must be reviewed before reuse. Do not merge these sources into `colosseum.exe` as if the bridge
were a complete player.
