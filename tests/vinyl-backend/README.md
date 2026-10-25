# Vinyl backend receiving (M0R)

This standalone build qualifies the adopted Arc 62 implementations before the
M0F app integration. It does not register a second live player, queue, database,
or QML service. The existing app continues to use its current owners.

Run from a C++ developer environment with CMake, Ninja, Qt 6.11 and a matching
libmpv development package and the installed MpvQt public headers. Keep
generated files outside the source checkout:

```sh
cmake -S tests/vinyl-backend -B /path/to/backend-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/qt \
  -DLIBMPV_PREFIX=/path/to/libmpv -DMPVQT_PREFIX=/path/to/mpvqt
cmake --build /path/to/backend-build --parallel 2
ctest --test-dir /path/to/backend-build --show-only=json-v1
ctest --test-dir /path/to/backend-build --output-on-failure --no-tests=error
```

All domain implementations compile once into `colosseum_vinyl_backend`.
`FakeVinylCatalogueClient` and `FakeArtistSource` compile into a separate
test-support archive. Tests retain their original component names and paths
for provenance; the old independent build graphs are replaced by this one.

The required real tests are `vinyl_w4_h_real_cue_journey`,
`vinyl_ba2db2_playlist_identity_probe`, `vinyl_ba2db3_seek_completion_probe`
and `vinyl_w4a_bounded_mpv_probe`. There are also the 36 other W4 checks and six
Artist component/composition checks, plus the installed host API probe
`vinyl_ba2d_mpv_api_probe`. Confirm all 47 tests are registered and none are
disabled before accepting a run.

To check the receiving set automatically, save `--show-only=json-v1` output to
a UTF-8 JSON file and run `python tests/vinyl-backend/verify_inventory.py FILE`.
The guard's six unit cases run with
`python -m unittest discover -s tests/vinyl-backend -p test_receiving_inventory.py`.

Configuration fails if the default real media is absent. To use equivalent
explicit fixtures, pass `VINYL_REAL_MEDIA`, `VINYL_IDENTITY_VIDEO` and
`VINYL_IDENTITY_AUDIO` as absolute file paths. Real libmpv probes deliberately
use null audio/video output: their passes establish engine behavior, not audible
device output or an actual-app listening journey.

Original reviewed hashes and findings are recorded in
`docs/reviews/2026-10-20-vinyl-backend-coverage.json` and its companion review.
The M0R receiving receipt records the repaired source hashes and fresh results.
