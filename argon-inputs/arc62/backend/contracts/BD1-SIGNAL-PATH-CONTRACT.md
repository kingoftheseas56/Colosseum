# Vinyl Backend BD1 - Signal Path Contract

Status: VERIFIED locally against Qt 6.11.1; native harness and CTest green; production Colosseum adoption not performed
Date: 2026-10-06
Owner: Arc 62 Vinyl

## Purpose

BD1 defines a presentation-independent value model for truthful audio-path evidence.

The model exists to answer a narrow question without manufacturing certainty:

source facts -> requested processing -> observed transformations -> selected output -> observed output

Those are separate states. A later UI may render them however it wants, but it must not collapse them into a single "Hi-Res", "Lossless", "Exclusive", or "Bit-perfect" badge that claims more than the evidence proves.

BD1 consumes BA0 identity. It does not change BA0.

## Governing doctrine

1. Recording MBID remains canonical playable-song identity.
2. Source/provider identity remains mapping and provenance, never canonical identity.
3. Source format is not actual output format.
4. Requested policy is not evidence that processing occurred.
5. Selected backend/device is not evidence of negotiated output.
6. Exclusive-mode request is not exclusive-mode confirmation.
7. No observed resampling or reformatting is not proof of bit-perfect playback.
8. Unknown facts stay unknown. BD1 never invents bit depth, sample rate, bitrate, channel count, device format, or proof strength.
9. Platform-specific mechanisms remain below the contract. Windows, Linux, macOS, or future platform adapters may populate the same model differently.
10. QML is not part of BD1.

## Basis

Primary product and donor basis:
- backend README and frozen BA0 Domain Contract;
- Strawberry donor assessment, especially the distinction between source facts, requested processing, conversion/resampling, selected backend/device, and actual negotiated output;
- Nuclear donor assessment for provider/source provenance separation;
- Desktop Listening Room direction for source transparency without granting presentation authority over truth.

Reference prototype:
backend/bd1-signal-path-prototype/

## Evidence facts

Any value that can be absent or uncertain is represented as an EvidenceFact<T>.

It carries two independent pieces:
- knowledge state: known or unknown;
- provenance: origin kind, source label, evidence strength, and optional BA0 ProviderItemRef.

Evidence strengths are:
- unknown;
- reported;
- requested;
- inferred;
- observed;
- confirmed.

A stronger-looking word is never inferred merely because the value is convenient. In particular, reported and inferred values do not become observed output evidence.

Provenance kinds are:
- canonical metadata;
- source provider;
- user policy;
- player runtime;
- output backend;
- device runtime;
- unknown.

## Source audio facts

SourceAudioFacts is anchored to a BA0 RecordingMbid and may retain an optional BA0 ProviderItemRef for the selected source.

It models:
- codec;
- container;
- channels;
- sample rate in Hz;
- bit depth when known;
- bitrate in kbps when meaningful.

Bitrate is intentionally optional. For sources where bitrate is unavailable, misleading, variable, or otherwise not meaningful, it remains unknown.

Source facts never populate observed output.

## Requested processing

RequestedProcessing captures policy intent separately from source and runtime evidence.

It includes:
- ReplayGain mode: off, track, album;
- normalization mode: off, ReplayGain, loudness;
- requested sample-rate behavior: device default, follow source, fixed rate;
- optional fixed requested sample rate;
- software-volume intent: unspecified, unity, adjusted;
- optional software gain in dB;
- exclusive-mode request.

A request does not add a TransformationStage by itself.

For example, requesting ReplayGain does not prove that gain was actually applied. BD2 or a later runtime adapter must add observed evidence.

## Transformation stages

TransformationStage records a processing or conversion stage only when the caller has evidence for that stage.

Kinds include:
- ReplayGain;
- loudness normalization;
- software volume;
- resampling;
- sample-format conversion;
- channel mixing;
- other explicit transformation.

Each stage retains provenance and may carry input/output sample-rate evidence.

BD1 treats an observed or confirmed transformation stage as transformed. Merely requested or inferred stages do not receive that promotion.

No timestamps are synthesized by BD1.

## Selected output

SelectedOutput keeps backend and device identifiers as opaque strings.

BD1 does not parse those identifiers or attach Windows-only, ALSA-only, CoreAudio-only, WASAPI-only, ASIO-only, or mpv-only semantics to their spelling.

The model also carries a separate exclusive-confirmation fact. That fact must have confirmed evidence strength before the verdict can say exclusive_confirmed.

## Observed output

ObservedOutputFacts is separate from both source facts and requested output policy.

It can contain:
- channels;
- sample rate in Hz;
- bit depth;
- sample format.

All can remain unknown.

BD1 never fills this structure from source facts.

## Truthful verdict vocabulary

SignalPathVerdict has three independent axes.

Conversion:
- unknown;
- transformed;
- no_observed_conversion.

Exclusive:
- unknown;
- exclusive_requested;
- exclusive_confirmed.

Bit-perfect:
- bit_perfect_not_proven.

BD1 deliberately has no positive bit-perfect state.

### Conversion derivation

transformed is returned when:
- an observed or confirmed TransformationStage exists; or
- source/output sample rates differ and the output fact is observed or confirmed; or
- channel counts differ and the output fact is observed or confirmed; or
- bit depths differ and the output fact is observed or confirmed.

A merely reported or inferred output mismatch stays unknown.

no_observed_conversion is returned only when all three comparable fields are known, each output-side fact is observed or confirmed, and the values match:
- sample rate;
- channels;
- bit depth;
and no observed transformation stage exists.

Otherwise conversion remains unknown.

This makes incomplete evidence fail conservative rather than optimistic.

### Exclusive derivation

exclusive_confirmed requires:
- a known true exclusive-confirmation fact; and
- confirmed evidence strength.

If that proof is absent but exclusive mode was requested, the verdict is exclusive_requested.

A request, backend selection, device selection, or observed hint cannot be promoted to confirmation.

### Bit-perfect derivation

The only BD1 bit-perfect verdict is bit_perfect_not_proven.

Matching source/output formats, no observed conversion, unity software volume, and confirmed exclusive mode may all become useful evidence later. They still do not establish end-to-end bit identity by themselves.

## Explicit non-goals

BD1 does not:
- control mpv;
- enumerate devices;
- switch devices;
- detect backend capabilities;
- observe live output;
- implement ReplayGain or loudness processing;
- decide source eligibility;
- rank source candidates;
- create UI badges;
- register QML types;
- persist settings;
- assert platform-specific behavior;
- prove bit-perfect playback.

BD2 owns mpv/device observation and will populate this model from real runtime evidence.

## Prototype files

backend/bd1-signal-path-prototype/ contains:
- VinylSignalPathTypes.h;
- VinylSignalPathTypes.cpp;
- test_signal_path.cpp;
- CMakeLists.txt.

The prototype depends on BA0's existing VinylDomainTypes and otherwise targets Qt Core only.

## Required deterministic coverage

The native harness encodes these acceptance cases:
1. 24/96 FLAC source with unknown actual output remains unknown.
2. 24/96 source observed at 48 kHz is transformed.
3. requested exclusive mode does not equal confirmed exclusive mode.
4. no observed conversion does not equal proven bit-perfect.
5. unknown bit depth stays unknown and prevents a false no-conversion claim.
6. ReplayGain/requested processing remains separate from source facts and observed transformation evidence.
7. Recording MBID plus provider/fact provenance survive independently.
8. the model is usable as native Qt Core value types with no QML dependency.

## Verification evidence

Local qualification completed on 2026-10-06.

Toolchain evidence:
- Qt package: `C:\Qt\6.11.1\msvc2022_64`;
- CMake cache resolved `Qt6_DIR` to `C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6`;
- CMake cache resolved `Qt6Core_DIR` to `C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6Core`;
- compiler: MSVC 19.44.35227.0 via `cl.exe` from Visual Studio 2022 Community;
- generator: Ninja from `C:\Qt\Tools\Ninja\ninja.exe`.

Build evidence:
- CMake configure completed successfully;
- all six Ninja build steps completed successfully;
- `vinyl_ba0_domain.lib`, `vinyl_signal_path.lib`, and `vinyl_signal_path_harness.exe` linked successfully.

Execution evidence:
- direct native harness exit code: 0;
- CTest: 1/1 tests passed, 0 failed;
- the harness exercises all eight required BD1 acceptance cases plus the negative control that reported-only output evidence cannot manufacture a transformed verdict.

Static scope evidence:
- CMake requests Qt 6.11 Core only and links no QML/Qt Quick module;
- the implementation never populates observed output from source facts;
- source/output comparisons require observed or confirmed output provenance;
- no platform-specific backend semantics are encoded in the value model;
- no positive bit-perfect verdict exists;
- BA0, the shared backend README, MEMORY, other lanes, and production Colosseum were not edited by BD1.

The first failed qualification attempt was an orchestration-only PATH mistake: a same-line `%PATH%` expansion replaced the Visual Studio compiler PATH after `VsDevCmd.bat`. The repaired invocation removed PATH rewriting and passed Ninja by absolute path. That failure did not expose a BD1 source defect.
