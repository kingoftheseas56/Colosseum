# Third-party notices

Nuvio Engine itself is GPL-3.0-or-later. The complete project license is in `LICENSE` and is packaged with each release artifact. The notices below cover separately licensed inputs.

## libtorrent

Nuvio Engine uses libtorrent as its BitTorrent protocol implementation.

- Project: https://github.com/arvidn/libtorrent
- Pinned release: 2.0.12 stable
- Pinned commit: `740a0b9aeabe00e762cc0efe4a0f27593db2550b`
- License: BSD 3-Clause

The source is fetched only when the optional protocol backend build is enabled. No libtorrent source or binary is committed to this repository. Android, Apple, Linux, and Windows packages reproduce libtorrent's combined license and bundled third-party notices, plus the license for its exact `try_signal` submodule.

Nuvio carries source patches for bounds checking of macOS/BSD routing-table messages and for loading the application-supplied TLS trust bundle during session startup. These patches remain covered by libtorrent's BSD 3-Clause license.

## Boost

Libtorrent uses Boost headers from the pinned Boost 1.86.0 source archive.

- Project: https://www.boost.org/
- Archive SHA-256: `1bed88e40401b2cb7a1f76d4bab499e352fa4d0c5f31c0dbae64e24d34d7513b`
- License: Boost Software License 1.0

Android, Apple, Linux, and Windows distributions package the complete Boost Software License.

## OpenSSL

Android, Apple, Linux, and Windows release binaries statically link a source-built OpenSSL TLS provider.

- Project: https://www.openssl.org/
- Pinned release: `3.5.7` LTS
- Archive SHA-256: `a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8`
- License: Apache License 2.0

No OpenSSL source or binary is committed to this repository. Platform builds verify the source archive checksum and build private static libraries for each architecture. Android, Linux, and Windows hide those symbols in their shared libraries; Apple merges the provider into each XCFramework archive. Every distribution includes the complete OpenSSL license.

## Text::Template

OpenSSL's source-generation step requires the Perl `Text::Template` module. It is downloaded as a checksummed build-only input and is not linked or packaged into Nuvio Engine.

- Project: https://metacpan.org/dist/Text-Template
- Pinned release: `1.61`
- Archive SHA-256: `a295ea7d1ef241ae2640c1f7864b628f8e6f99ec14fb1da781b2f5f2168dcf09`
- License: the same terms as Perl 5 (GNU General Public License or Artistic License)

## llvm-mingw

Windows packages are cross-compiled with llvm-mingw and statically include its LLVM C++ runtime components.

- Project: https://github.com/mstorsjo/llvm-mingw
- Pinned release: `20260407` with LLVM 22.1.3 and UCRT
- Cross-toolchain archive SHA-256: `a7d68599e828d6bb61e2397bbef43e7a69a5b9c38ee7eec642e05d16b17f3f36`
- License: the consolidated terms reproduced from the release's `LICENSE.TXT`

Every Windows distribution packages that complete consolidated license. No llvm-mingw source or standalone runtime DLL is committed or distributed by this repository.
