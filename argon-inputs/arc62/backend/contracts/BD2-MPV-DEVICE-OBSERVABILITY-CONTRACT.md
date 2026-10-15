# Vinyl Backend BD2 - mpv / Device Observability Contract

Status: VERIFIED locally against Qt 6.11.1; native harness and CTest green; production Colosseum adoption not performed
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BD2 defines how runtime audio evidence from mpv and a future platform device adapter populates
the frozen BD1 Signal Path model without manufacturing certainty.

BD1 owns the evidence model and truthful verdict vocabulary.
BD2 owns observation normalization and evidence mapping.

BD2 does not change BA0, BD1, production Colosseum, or QML.

Critical law:

> Absence of observed conversion is not proof of bit-perfect output.

## Entry gate

BD1 is VERIFIED locally against Qt 6.11.1.
Its native harness and CTest are green.

BD2 may therefore build on the frozen BD1 contract.

## Basis inspected

Arc 62 basis:

- backend/README.md
- backend/BA0-DOMAIN-CONTRACT.md
- backend/BD1-SIGNAL-PATH-CONTRACT.md
- donor-reviews/strawberry/DONOR-ASSESSMENT.md

Read-only production Colosseum basis:

- native/player/mpvitem.h
- native/player/mpvitem.cpp
- native/player/mpvproperties.h
- desktop CI dependency pins

Current production dependency evidence identifies:

- MpvQt 1.2.0
- Windows libmpv build 20260814-git-7b8915bc1d

The current MpvItem already observes track-list and exposes stats reads including audio-codec
and audio-params/channel-count. It does not currently expose a Vinyl-owned observation contract
for audio-out-params, current-ao, actual device identity, or confirmed exclusive mode.

BD2 does not modify MpvItem. It defines the future adapter seam.

## Governing laws

1. RecordingMbid remains canonical playable-song identity.
2. Runtime properties are evidence, never canonical identity.
3. Source/container facts are distinct from decoder-output facts.
4. Decoder-output facts are distinct from audio-API-output facts.
5. Audio-API-output facts are distinct from physical-device facts.
6. Requested device or exclusive policy is not an observed result.
7. Selected backend is not proof of selected physical device.
8. No observed conversion is not proof that no conversion happened outside observable boundaries.
9. Sample-format spelling never manufactures source or device bit depth.
10. Missing or unusable runtime facts stay unknown.
11. QML has no authority over BD2 truth.
12. Platform adapters may strengthen evidence only for facts they can actually observe.

## Evidence planes

BD2 keeps five evidence planes separate:

1. source/container evidence;
2. decoder-output evidence;
3. mpv audio-API-boundary output evidence;
4. platform/device-runtime evidence;
5. requested policy/configuration.

No plane is silently promoted into another.

## mpv source and track mapping

### current-tracks/audio/codec

Maps to BD1 SourceAudioFacts.codec when that field lacks stronger evidence.

Evidence:

- provenance kind: PlayerRuntime
- strength: Reported
- source label: mpv:current-tracks/audio/codec

It describes the selected source track codec.
It does not change RecordingMbid identity.

### file-format

Maps to BD1 SourceAudioFacts.container when that field lacks stronger evidence.

Evidence:

- provenance kind: PlayerRuntime
- strength: Reported
- source label: mpv:file-format

The value remains an opaque mpv format description.
BD2 does not parse it into stronger container truth than mpv reports.

### current-tracks/audio/demux-samplerate

Retained by BD2 as a reported demux hint.

Evidence:

- provenance kind: PlayerRuntime
- strength: Reported
- source label: mpv:current-tracks/audio/demux-samplerate

The mpv manual explicitly describes the container-indicated samplerate as not always accurate.

Therefore it does not override decoder-observed sample-rate evidence.

### current-tracks/audio/demux-channel-count

Retained by BD2 as a reported demux hint.

Evidence:

- provenance kind: PlayerRuntime
- strength: Reported
- source label: mpv:current-tracks/audio/demux-channel-count

The container-indicated channel count is also not always accurate.

Therefore it does not override decoder-observed channel evidence.

### current-tracks/audio/demux-bitrate

May populate BD1 SourceAudioFacts.bitrateKbps when that field is otherwise unknown.

Evidence:

- provenance kind: PlayerRuntime
- strength: Reported
- source label: mpv:current-tracks/audio/demux-bitrate

The value is normalized from bits per second to kilobits per second.
It remains reported source-side evidence, not negotiated-output evidence.

## Decoder-output mapping

mpv documents audio-params as audio format output by the audio decoder.

### audio-params/format

Retained by BD2 as decoded sample-format evidence.

Evidence:

- provenance kind: PlayerRuntime
- strength: Observed
- source label: mpv:audio-params/format

It does not populate BD1 source bit depth.

### audio-params/samplerate

Maps to BD1 SourceAudioFacts.sampleRateHz as the observed source-side PCM rate at decoder exit.

Evidence:

- provenance kind: PlayerRuntime
- strength: Observed
- source label: mpv:audio-params/samplerate

It may replace weaker reported or inferred source evidence.
It does not overwrite existing observed or confirmed source evidence.

### audio-params/channel-count

Maps to BD1 SourceAudioFacts.channels as observed decoder-output channels.

Evidence:

- provenance kind: PlayerRuntime
- strength: Observed
- source label: mpv:audio-params/channel-count

It may replace weaker reported or inferred source evidence.
It does not overwrite existing observed or confirmed source evidence.

## mpv audio-output boundary

mpv documents audio-out-params as the format of data written to the audio API.

That makes it strong evidence about mpv's own output boundary.
It is not automatically proof of the physical device's final hardware format.

A system mixer, operating-system audio stack, driver, or device may still transform the stream.

### audio-out-params/format

Maps to BD1 ObservedOutputFacts.sampleFormat when no stronger device-runtime format exists.

Evidence:

- provenance kind: OutputBackend
- strength: Observed
- source label: mpv:audio-out-params/format

### audio-out-params/samplerate

Maps to BD1 ObservedOutputFacts.sampleRateHz when no stronger device-runtime rate exists.

Evidence:

- provenance kind: OutputBackend
- strength: Observed
- source label: mpv:audio-out-params/samplerate

### audio-out-params/channel-count

Maps to BD1 ObservedOutputFacts.channels when no stronger device-runtime channel fact exists.

Evidence:

- provenance kind: OutputBackend
- strength: Observed
- source label: mpv:audio-out-params/channel-count

## Selected audio backend

### current-ao

Maps to BD1 SelectedOutput.backendId.

Evidence:

- provenance kind: OutputBackend
- strength: Observed
- source label: mpv:current-ao

It identifies the active mpv audio-output driver.

It does not prove the selected physical device or hardware format.

## Selected output device

### audio-device

The mpv audio-device property is retained only as the configured device selector.

Evidence:

- provenance kind: PlayerRuntime
- strength: Requested
- source label: mpv:audio-device

It does not populate BD1 SelectedOutput.deviceId.

This distinction is mandatory because mpv explicitly documents that audio-device does not tell
a client which audio device is actually in use. The value auto is likewise a selector, not an
actual endpoint identity.

Actual device identity remains unknown until a platform/device adapter can report the active
runtime endpoint.

When such evidence exists, BD2 maps it to BD1 SelectedOutput.deviceId with:

- provenance kind: DeviceRuntime
- strength: Observed

## Requested versus confirmed exclusive mode

### options/audio-exclusive

BD2 retains the mpv exclusive option as request/configuration evidence.

Evidence:

- provenance kind: PlayerRuntime
- strength: Requested
- source label: mpv:options/audio-exclusive

It never populates BD1 SelectedOutput.exclusiveConfirmed.

mpv documents that exclusive mode works only with supported output paths and can be ignored by
other outputs. A true option value therefore proves only configuration intent.

The caller-owned BD1 RequestedProcessing.exclusiveModeRequested remains separate.
BD2 never rewrites that requested policy from an mpv option echo.

BD1 SelectedOutput.exclusiveConfirmed may be populated only when a platform/device adapter can
positively confirm the active runtime mode.

Confirmed exclusivity uses:

- provenance kind: DeviceRuntime
- strength: Confirmed

## Device-runtime adapter seam

The prototype models future device evidence without implementing an OS-specific adapter.

A future adapter may provide:

- actual device identifier;
- device-side sample format;
- device-side sample rate;
- device-side channel count;
- device-side bit depth;
- confirmed exclusive-mode state.

When present, device-runtime output facts outrank mpv audio-out-params when populating BD1
ObservedOutputFacts because they are closer to the final device boundary.

If a platform cannot expose one of these facts, the fact remains unknown.

BD2 deliberately does not promise that every platform can expose every field.

## Conversion evidence

BD2 records an observed transformation only when two observed boundaries disagree.

Decoder to mpv audio API:

- decoder sample rate vs mpv output sample rate -> Resampling
- decoder sample format vs mpv output sample format -> SampleFormatConversion
- decoder channel count vs mpv output channel count -> ChannelMix

mpv audio API to device runtime:

- output sample rate mismatch -> Resampling
- output sample format mismatch -> SampleFormatConversion
- output channel-count mismatch -> ChannelMix

Equal values add no transformation stage.

Missing values add no transformation stage.

No transformation stage means only that BD2 did not observe a conversion at the boundaries it
could see. It is not evidence that no conversion occurred elsewhere.

## Bit depth

BD2 does not derive bit depth from mpv sample-format spelling.

Names such as s24, s32, float, packed forms, or unpacked forms do not by themselves prove:

- the original encoded source bit depth;
- the effective device bit depth;
- end-to-end bit identity.

Rules:

- source bit depth remains whatever stronger source evidence established;
- mpv observations alone leave source bit depth unknown when it was unknown;
- output bit depth is populated only when a device-runtime adapter directly reports it.

## Evidence precedence

BD2 applies the following precedence rules:

1. reported mpv source metadata fills only unknown source fields;
2. decoder-observed sample rate and channels may replace weaker reported or inferred source facts;
3. decoder-observed facts do not overwrite already observed or confirmed source evidence;
4. device-runtime output facts outrank mpv audio-API output facts;
5. requested configuration never becomes observed output;
6. mpv request/configuration never becomes device confirmation.

This keeps downstream BD1 verdicts evidence-calibrated instead of optimistic.

## Unknown and invalid values

Blank strings, zero or negative sample rates, zero or negative channel counts, zero or negative
bit depths, and other unusable runtime values remain unknown.

BD2 never synthesizes convenient defaults such as:

- 44.1 kHz;
- stereo;
- 16-bit;
- WASAPI;
- default device;
- exclusive mode;
- lossless;
- bit-perfect.

## Prototype

Reference prototype:

backend/bd2-mpv-observability-prototype/

Files:

- VinylMpvObservability.h
- VinylMpvObservability.cpp
- test_mpv_observability.cpp
- CMakeLists.txt

The prototype is presentation-independent and Qt Core only.

It deliberately does not link libmpv. Deterministic fake runtime observations exercise the
mapping policy without mutating or depending on production MpvItem.

A future production adapter can translate live MpvQt/libmpv property observations into the same
normalized input structure.

## Required deterministic coverage

The native harness verifies:

1. source codec and container come only from source-side runtime evidence;
2. decoder-observed source rate and channels outrank weaker demux hints;
3. sample-format spelling never manufactures bit depth;
4. mpv output rate, format, and channels remain output-backend observations;
5. current-ao can identify the observed backend;
6. audio-device remains request/configuration evidence rather than actual-device proof;
7. exclusive request/configuration does not become exclusive confirmation;
8. missing output and device facts remain unknown;
9. observed decoder-to-audio-API differences create transformation stages;
10. device-runtime evidence can populate actual device and output facts when supplied;
11. no observed conversion still yields BD1 bit_perfect_not_proven;
12. weaker runtime evidence cannot overwrite confirmed source evidence;
13. invalid or non-positive runtime values remain unknown.

## Explicit non-goals

BD2 does not:

- modify production MpvItem;
- add mpv properties to the production stats card;
- enumerate devices;
- switch devices;
- implement WASAPI, CoreAudio, PipeWire, ALSA, or AudioUnit adapters;
- prove physical-device format when only mpv audio-API facts exist;
- infer source or output bit depth from sample-format names;
- confirm exclusive mode from an mpv option;
- prove bit-perfect playback;
- change BA0 or BD1;
- add QML.

## Future production adapter need

The remaining production adoption work is an adapter beneath Vinyl that:

1. observes the documented mpv properties on the live player;
2. normalizes them into BD2 runtime observations;
3. obtains actual device identity and device/exclusive facts from platform APIs where available;
4. refreshes evidence on file load, audio-track change, backend/device change, and audio
   reconfiguration;
5. invalidates stale facts back to unknown when the underlying runtime stops exposing them.

That adapter is later production-adoption work.

BD2 itself remains mock-agnostic, presentation-independent, and conservative about evidence.

## Verification evidence

Local qualification completed on 2026-10-06.

Toolchain evidence:

- Qt package: C:\Qt\6.11.1\msvc2022_64
- compiler: MSVC 19.44.35227.0 via Visual Studio 2022 Community
- generator: Ninja at C:\Qt\Tools\Ninja\ninja.exe
- CMake: C:\Qt\Tools\CMake_64\bin\cmake.exe

Build evidence:

- CMake configure completed successfully against Qt 6.11.1;
- the initial Ninja build completed all 8 steps successfully;
- vinyl_ba0_domain.lib, vinyl_signal_path.lib, vinyl_mpv_observability.lib, and
  vinyl_mpv_observability_harness.exe linked successfully;
- the final incremental rebuild after improving the harness receipt completed 2/2 steps.

Execution evidence:

- direct native harness exit code: 0;
- direct harness receipt: VINYL_BD2_MPV_OBSERVABILITY_OK checks=53;
- CTest: 1/1 tests passed, 0 failed.

The harness proves the critical negative controls:

- inaccurate demux hints do not outrank decoder-observed source facts;
- sample-format names do not manufacture bit depth;
- configured audio-device does not become actual device identity;
- mpv exclusive configuration does not become confirmed exclusivity;
- missing output/device facts remain unknown;
- requested policy is not rewritten from runtime option echoes;
- no observed conversion plus confirmed exclusivity still leaves bit-perfect not proven;
- weaker runtime evidence does not overwrite confirmed source evidence.

Qualification notes:

- the first environment-discovery probe found that the BDC process had no CMake on PATH;
- an initial cmd invocation also failed before configuration because cmd /s mangled the quoted
  Visual Studio batch path;
- neither failure compiled or exercised BD2 source;
- qualification then used the verified explicit Qt, Ninja, CMake, and Visual Studio paths and
  completed successfully.

Production Colosseum remained read-only throughout BD2.
