# M00 authenticated-source evidence repair

This is packet proof, not B-W2B acceptance or Server 1.0 playback proof.

The replacement probe executes the actual M433/M434/M436/M667/M855/M861 factories from the SHA-verified bundle. M564 is identity-checked, not initialized. Real temporary files and owned Node children exercise local conversion. Deterministic bridge/HTTP ports and an explicit 500 ms virtual timer exercise remote conversion. M861 fixed argv, close events and binary remote output are observed separately.

All seven frozen hashes and line ranges are correct. Hash input is exact factory text, not enclosing complete lines. Byte offsets and LF-only line numbers in SOURCE-EXECUTION.json make this reproducible. The prior claim that M667/M861 annotations are wrong is superseded.

The 11-line differential is a bounded projection, not universal API equivalence. The historical output=callbacks-complete label compares eight stdout and five stderr bytes; the real source sends debug stderr to its file sink, not a native callback. The one-cancel projection does not claim the source exposes native finish-once/no-ready callbacks. Those remain independently passing native ownership assertions. Remote ffprobe differs from remote ffmpeg; downstream wrappers must not substitute one for the other. Observed forced-mode precedence and destroy behavior are recorded rather than normalized away.

With the frozen MSVC/Qt environment loaded:

```text
cmake -S artifacts/server1/M00/M00-A/repair-20260912 -B <fresh-build> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
cmake --build <fresh-build> --clean-first --parallel 2
ctest --test-dir <fresh-build> --output-on-failure --repeat until-fail:3 -j1
python artifacts/server1/M00/M00-A/scenarios/verify_m00_oracle.py --oracle <locked-server.js> --candidate <fresh-build>/server1_m00_process_tests.exe --output <new-proof-directory>
```

The verifier rejects a one-byte oracle mutation, checks all seven factory identities/ranges, and compares eleven outputs in order. Commands and unmodified stdout/stderr are retained. Raw compiler logs retain carriage returns rather than being normalized for diff-check. Native production, tests and manifests are unchanged.
