# Repaired INT-W2 candidate for independent review

Mechanical gate: PASS. B-W2B acceptance: PENDING. W3: CLOSED.

The candidate reuses clean assembly 801a4b4f and serially applies the approved H00/K13 repairs plus the new M00 evidence and blob-hash repair. REPLAY.json records exact input and assembled SHAs. The verified native source is 260835ff2b52b11a169cf97493b9dbe49c7eb285. Commits after that source contain only this gate evidence and candidate STATE.json, not production changes.

Fresh configuration/build completed 29/29 steps under the frozen C++17/MSVC/Qt lane. All 20 externally locked dependency identities matched. Exactly 16 tests passed once and then three consecutive times. The additional combined consumer includes all seven public headers, links all five packet libraries, and executes actual symbols, including the repaired 413 parser path and a real ephemeral listener. The complete gate also passed a second fresh build through the reproducer below.

K01 72/72, K13-A 11/11, K13-B 9/9 and M00 11/11 source/native lines compare equal in order. K13-A's package identity is embedded 4.21.0, not CDN selector 4.21.1. H00's real native loopback wire is compared against its pinned source-derived transport profile; this is not presented as full live-oracle HTTP parity. Native stream/disconnect and queue bounds were rerun.

M00 uses actual verified bundle factories, checks all seven frozen hashes/ranges, and rejects a one-byte oracle mutation before execution. Its README separates observable byte/cancellation projections from source-only ffprobe/ffmpeg API differences. The earlier allegation that M667/M861 authority ranges or digests are wrong is refuted. No frozen authority or topology was changed.

## Reproduce on the locked Windows machine

Use a new scratch directory that does not yet exist. The script loads the observed VS environment, never cleans an existing build directory, and never writes acceptance state:

```text
python artifacts/server1/INT-W2/repair-20260912/reproduce_gate.py --work <new-private-directory> --oracle <authenticated-server.js>
```

The checked-in REPRODUCTION.json records the installed tool paths used for this Windows lane. COMMANDS.json records the underlying commands. The script relocates only checkout and scratch paths, verifies the oracle before running, preserves raw outputs, checks the exact inventory and repeat count, executes the combined consumer, and compares the integrated traces. This is mechanical reproduction, not reviewer independence.

RECEIPT.json, REPLAY.json, SCOPE-AUDIT.json, DEPENDENCY-HASH-CHECK.json, BINARY-IDENTITIES.json and HASHES.json are the review entry points. Raw stdout/stderr retain their captured bytes. No raw logs were normalized to make whitespace checking green. One initial K13-B invocation omitted its root argument and ran the default cases; it is explicitly discarded, not counted as differential proof.

## Boundaries

The official feature remains at e5660e27, with the three unrelated deletions in docs/build/linux.md, macos.md and windows.md preserved. No official checkout, master, Runtime, ServerComposition, RootRoutes, application entry/CMake/TorrentEngine or CI mutation occurred. The only native difference from 801a4b4f is the previously approved H00 owning-string line. No branch was pushed.

Codex must independently inspect the M00 repair and the assembled tree and decide B-W2B. This producer does not accept its own integration gate. Full server runtime, playback and platform qualification remain downstream.
## Integration hygiene repair

The candidate now passes `python scripts/check_public_paths.py` with zero tracked findings. The cleanup sanitized both the repaired W2 evidence and inherited historical Server 1.0 evidence that predated this assembly; the guard itself was not weakened. `reproduce_gate.py` now executes the public-path guard before the mechanical gate and aborts if it fails. The post-sanitization fresh reproduction passed the full 16-test/48-repeat, combined-link, H00 413, and differential gate. B-W2B remains pending independent acceptance and W3 remains closed.
