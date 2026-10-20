# M01 bounded implementation receipt

Implemented Probe and LegacyProbe independently against locked Stremio 4.21.1 bundle SHA256 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f. Source modules 305 (legacy), 432 (normalization/cache), 861 (ffprobe process). An initially supplied different oracle path was detected before production translation; all final fixtures and comparisons use the locked bundle.

## Implemented
- HLSv2 exact ffprobe argument vector; track-specific metadata, strict index grouping, missing/null duration, JavaScript numeric-prefix and Number-to-string handling.
- Three successful URL results in newest-completion-first cache. Hits do not refresh ordering; failures do not cache; no invented replay on eviction.
- Local process output accumulation independent of ProcessPort's 64KiB diagnostic retention; 120000ms default timeout, cancellation, teardown, cache delivery, error strings.
- Separate remote ffprobe bridge result seam. UUID request correlation, duplicate result suppression, source 500ms close delay, remote destroy/timeout no-op. This is NOT the ffmpeg remote listening-socket driver.
- Legacy ffmpeg -i stderr parser, sparse stream positions, omitted duration, millisecond units, language omission, original regex quirks. Exact 20000ms timeout and 504.
- Additive ProcessCallbacks.onOutputClosed appended after existing fields; QProcess readChannelFinished drains both output pipes with lifetime/relookup guards. ProcessDriver forwards it for local children.
- Legacy missing-file result remains successful {streams:[]} when stderr EOF precedes exit, matching actual source execution. It does not reuse HLSv2 nonzero-exit rules.

## Tests and results
- Test-first initial stub: red.log, exit 1, expected missing normalization.
- Remote seam stub: remote-red.log, expected failure before implementation.
- Numeric boundary regression: numeric-red.log, exit 1, proved 1e-6/1e20 stringification mismatch before correction.
- Multiple-root JSON regression: multiple-red.log, exit 1 before correction. Source once(data) retains first complete root; first-value correction passes full source/native lifecycle checks.
- Fourteen exact factory normalization fixtures, including video/audio/subtitle, multiple audio, invalid/missing numeric metadata, negative/zero values, sparse legacy tracks, real MP4/MKV samples: PASS.
- Exact M861 argument-vector snapshot: PASS.
- Child process/cache/eviction/failure/malformed/timeout/cancel/destroy: PASS.
- Dedicated bridge seam/no-op destroy/delayed close: PASS (mock transport boundary, not external bridge qualification).
- Real source lifecycle via full bundle with real child_process: source-lifecycle.json. Same cache ordering and error strings, including malformed JSON, exit 7, SIGKILL timeout, legacy missing-file success.
- Owner deletion inside output EOF callback: PASS.
- Legacy missing executable and 20000ms timeout: PASS (legacy-timeout.log).
- Actual ffprobe and ffmpeg on tiny.mp4 and tiny.mkv: exact equality against oracle-normalized results, PASS (green-mp4.log and green-mkv.log).
- Existing M00-01, M00-02, M00-03 regression: PASS (m00-regression.log).
- Diff whitespace check: PASS.

## Reproduction
From source root, with CMake and Qt selected for the host:
1. Integrate cmake/packets/M01.cmake after M00, call server1_register_m01_packet(). Include tests/cmake/M01.cmake and call server1_register_m01_tests().
2. Build server1_m01_probe_tests; run ctest -R '^M0[01]-' --output-on-failure.
3. node native/colosseum_server_v1/tests/run_probe_oracle.cjs "$LOCKED_BUNDLE" artifacts/server1/M01/fixtures.json artifacts/server1/M01/media
4. node native/colosseum_server_v1/tests/run_probe_lifecycle.cjs "$LOCKED_BUNDLE"
5. server1_m01_probe_tests native/colosseum_server_v1/tests/fixtures/probe.json "$PWD/native/colosseum_server_v1/tests/probe_process_fixture.py" "$PWD/artifacts/server1/M01/media/tiny.mp4"
6. Repeat 5 for tiny.mkv; use --legacy-timeout as last argument for the 20-second boundary test.

Corpus generation used ffmpeg 7.1.5 (binary SHA256/version in binary-identity.txt):
ffmpeg -v error -f lavfi -i color=c=blue:s=160x120:r=25 -f lavfi -i sine=frequency=440:sample_rate=44100 -t 0.4 -c:v mpeg4 -c:a aac -y tiny.mp4
ffmpeg -v error -i tiny.mp4 -c copy -y tiny.mkv

The actual isolated build used /tmp/m01-driver and /tmp/m01-build, CMake /tmp/server-build-tools/cmake/data/bin/cmake, SDK ../deps/Qt/6.11.1/gcc_64. Build output is in build.log.

## Qualification boundaries
- No external remote ffprobe bridge exists in the host yet; only its explicit transport seam and source timing are qualified.
- Windows process/EOF timing and real binaries have not been exercised in this Linux worker.
- Registered source tests and actual isolated tests are distinguished from project-wide integration CTest, which the integration owner runs.
- No route integration, commit, push, production service or global parity claim is made by this packet.
- The generic lab.py M01 comparator is not wired here; evidence uses exact locked factory/full-bundle differential scripts and native tests, not fabricated lab acceptance.
