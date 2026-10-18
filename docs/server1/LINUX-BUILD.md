# Native Linux Server 1 build

This is a development/component build, not full-server parity or release qualification.
The existing Windows dependency lock is unchanged. Linux dependencies are separately
identified; they are not represented as the accepted MSVC archive.

## Dependencies

- C++17 compiler, CMake >= 3.24, Qt Core/Network (tested 6.11.1)
- Boost headers and OpenSSL development libraries
- **Static libtorrent 2.0.11**, including its matching installed headers/CMake package

The adapter intentionally uses internal peer/torrent methods. The tested distro
shared package hides those symbols. Configure rejects a shared package and links
an executable probe for representative internal methods. A newer version or an
unrelated patched transport is not an interchangeable dependency.

Official source: https://github.com/arvidn/libtorrent/releases/download/v2.0.11/libtorrent-rasterbar-2.0.11.tar.gz

Downloaded archive SHA256:
`f0db58580f4f29ade6cc40fa4ba80e2c9a70c90265cd77332d3cdec37ecf1e6d`

Example, using caller-chosen absolute directories:

```sh
cmake -S "$LIBTORRENT_SOURCE" -B "$LIBTORRENT_BUILD" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -Dbuild_tests=OFF -Dbuild_examples=OFF \
  -DCMAKE_INSTALL_PREFIX="$LIBTORRENT_PREFIX"
cmake --build "$LIBTORRENT_BUILD" -j2
cmake --install "$LIBTORRENT_BUILD"
cmake -S native/colosseum_server_v1 -B build-server1-linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX;$LIBTORRENT_PREFIX" \
  -DLibtorrentRasterbar_DIR="$LIBTORRENT_PREFIX/lib/cmake/LibtorrentRasterbar"
cmake --build build-server1-linux -j2
ctest --test-dir build-server1-linux --output-on-failure --timeout 45
```

For a non-system Boost/OpenSSL installation, supply its prefix and corresponding
CMake package locations explicitly. Retain the generated cache/build logs to
identify actual inputs. The qualified local static build used Release,
encryption=ON, static_runtime=OFF, GCC 14.2.0 and Boost 1.83.0.

## Qualification boundaries

The twelve PowerShell wire drivers now select the platform executable name and
apply WindowStyle only on Windows. Install official PowerShell on PATH (tested
7.6.7). They retain their existing readiness and exact-wire assertions. R13/R14 use a disposable
self-referential symlink to provoke a real storage read exception; Windows needs
its own supported fixture qualification.

Restricted environments may allow TCP loopback but prohibit NETLINK. In the
tested cloud environment a libtorrent metadata seeder reports a session socket
open error (EPERM), while ordinary HTTP loopback tests pass. Preserve that
failure. The explicit SERVER1_TEST_FIXED_INTERFACE=1 test profile disables only
libtorrent's optional OS IP-change notifier. It does not change host security or
production defaults. Real wire tests passed in that bounded fixed-interface profile;
network-interface-change behavior is not thereby qualified.
The host remains a skeleton until the K12/H01/H02 composition is implemented.
