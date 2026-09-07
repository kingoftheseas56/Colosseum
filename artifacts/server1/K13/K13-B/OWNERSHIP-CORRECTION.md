# K13-B ownership correction

Original assumption: the K13-B owned-file list named the two implementation `.cpp` files, the convergence test, and the two packet manifests, but no public header. The initial implementation therefore treated test-local declarations as sufficient for the packet.

Live-source/compile review showed that K13-B produces `CachePolicyContract` and `FilesystemDiskPorts`, while downstream consumers need a legal production interface. The smallest authorized correction adds:

- `native/colosseum_server_v1/include/server1/settings/CachePolicy.h`
- `native/colosseum_server_v1/include/server1/platform/DiskSpace.h`

Both implementation files and `test_settings_cache.cpp` now include those headers; duplicate declarations were removed. `CachePolicy.h` includes and consumes K13-A's production `SettingsStore.h` interface. The POSIX atime repair remains within `CachePolicy.cpp` and the owned convergence test.

Evidence is the production-header compile in `green/FINAL-BUILD-OUTPUT.txt`, the focused CTest run in `green/FINAL-CTEST-OUTPUT.txt`, and the public-header references in the changed sources.

Affected workers/barrier: `K13-B` and `B-W2B`. K13-A source/header files were not edited. Shared files were not edited. Topology is unchanged: the correction adds only the two packet-owned public headers and does not alter packet ownership or the B-W2B merge endpoint.
