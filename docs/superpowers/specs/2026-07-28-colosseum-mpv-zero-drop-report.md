# Colosseum mpv Zero-Drop Report

## Build identity

- Git HEAD: `96d9c64e5e13bc0223b8ab7f12b8e0ff0d5ab17e`
- Executable: `C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\.worktrees\player2-chrome-port\native\build-msvc\colosseum.exe`
- Executable timestamp: `2026-07-28T22:41:12.4833331+05:30`
- GPU: Intel UHD Graphics 620
- Driver: `31.0.101.2135`

## Controlled inputs

- Media: `C:\Users\Suprabha\Downloads\Colosseum\Tenet - 20260726_184029.mp4`
- Backend: `mpv (player 1)`
- Hardware decode: `d3d11va-copy`
- Video synchronization: `display-resample`
- Interpolation: enabled
- Warm-up: 30 seconds
- Measurement window: 300 seconds
- Required runs: two consecutive passes, stopping fail-closed after the first failed pass

## Run 1

- Process exit: `0`
- Decoder drops: `0 -> 0` (delta `0`)
- Output drops: `38 -> 507` (delta `469`)
- Playback position: `30.030 -> 330.038` (progress `300.008` seconds)
- A/V sync: `0.000013500000000138401 -> 0.00001359999975245052`
- Average output-drop rate: approximately `1.56` per second
- Classification: failed the zero-additional-output-drop gate

The probe captured interval endpoints, not per-second samples. It therefore proves the total delta
but does not independently classify the drops as steady or clustered.

## Run 2

Not run. The gate stopped fail-closed after Run 1 disproved the zero-drop objective.

## A/V synchronization and hardware decode

A/V synchronization remained stable at the recorded endpoints and playback advanced for the full
measurement interval. The player remained on `d3d11va-copy`; the approved synchronization and
interpolation policy was active.

## Hemanth eyes-on verdict

Hemanth, 2026-07-28:

> "nope it was smooth play back for 4-8 seconds, one big stutter and then 4-8 seconds of smooth play
> and then a big stutter (usually 3-4 frames drop) and so on. It was really noticiable and
> distracting, as it was before."

The eyes-on pattern is periodic clustered hitching, not an invisible counter-only failure.

## Result

FAIL - direct-present design required

This result rejects the claim that `display-resample` plus interpolation can make the existing
Colosseum mpv/QtQuick integration zero-drop on the target machine. It does not yet distinguish
copy-back cost from QtQuick/full-application presentation cost; that distinction requires a
controlled isolation A/B before implementation begins.
