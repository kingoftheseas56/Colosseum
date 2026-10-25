# Colosseum 1.1.10 Linux source materials

This collector is pinned to the accepted 1.1.10 candidate from GitHub Actions run
`40192334970`, artifact `12599515396`, and application source commit
`422541a52bcd32be1cfeca7554d74186e6288cf1`. The package is
`Colosseum-1.1.10-linux-amd64.deb`, 335014660 bytes, SHA256
`bb6cca0f21c233cb8b8ff2c725828a6b6792245ebef73dc2b5dd090ced97f915`. It matches the local notices-package copy by byte count and SHA256.
The collector streams the package and retains only its identity, build identity,
notices, SBOMs and metadata. It does not rebuild or alter the installer.

Run the manual workflow on Ubuntu 24.04. It starts a fresh 1.1.10 collection;
no 1.1.9 proofs or source archives are reused. Ubuntu source acquisition uses
authenticated APT metadata, the original package provenance, exact binary
versions, exact file hashes and replayed RPATH transformations. The resulting
`ubuntu-source-index.json` records each exact source package/version, every
acquired file's SHA256 and size, and its APT acquisition URL.

The workflow records the accepted build workflow and an archive of the exact
application source commit. It also retains the original Ubuntu package
provenance, signed APT indices and keyring, Qt SPDX/CycloneDX SBOMs, and
collector revision. `SHA256-INDEX.json` hashes the retained evidence and
source material. CI artifacts have 14-day retention.

## Ubuntu package rebuild

For each entry in `ubuntu-source-index.json`, use the pinned `.dsc`, Debian
patches and original source files listed in that record. Verify the recorded
hashes, unpack with `dpkg-source -x <source.dsc>`, install the exact
`Build-Depends` from the matching authenticated Ubuntu snapshot, and follow
that source package's `debian/README*` or `debian/rules` build target. Do not
replace the recorded source version with a newer package version and treat it
as the same build.

## Application build and dynamic relinking

`recipes/accepted-build-workflow.yml` is the exact 1.1.10 build workflow from
the pinned application commit; `recipes/colosseum-422541a52bcd32be1cfeca7554d74186e6288cf1.tar.gz`
is the corresponding source tree. The workflow records the CMake options and
all build steps. It pins ECM 6.15.0 to SHA256
`f7cd022095a9e6bdbe5897720a24bfda81d211757b7c08b173061962bf2ee0b2`,
MpvQt 1.2.0 to SHA256
`8660ad79c0d60fed77f29b36e1742841466af5405de702c81a121e6eeb625ebb`,
and the rebuilt Qt TIFF plugin to qtimageformats commit
`77f35e5694885844c3eb3a737769b5ed290b8ccc` with
`FEATURE_system_tiff=OFF`. Qt SDK source manifests record the exact Qt
module archives and Git commits; the QtWebEngine manifest includes its
Chromium gitlink commits.

The app uses dynamically linked libraries. To relink after rebuilding a
dependency, use the accepted workflow's Qt 6.11.1 prefix, compiler, CMake
options and module versions, rebuild the app against that prefix, and preserve
the package's relative RPATHs (`$ORIGIN/.` for bundled libraries and
`$ORIGIN/../lib` for bundled executables). Re-run the package checks and
compare the resulting files before replacing any shipped runtime. These
materials do not prove that the Qt SDK binaries can be reproduced: the exact
Qt-prebuilt ICU 73.2 source/build provenance remains unresolved, and
QtWebEngine/Chromium closure remains subject to review.

The Qt module archives are acquired from Qt's release download area and their
source files are compared with the matching Git commit where available.
QtWebEngine's source archive and gitlink manifest are evidence for review;
they do not establish complete Chromium source/patch/configuration delivery.
The Unicode ICU 73.2 source archive is retained as a candidate only because
the Qt SBOM records `NOASSERTION` for its source location.

The source and review artifacts preserve upstream notices and report
unresolved correspondence or notice questions. They do not certify legal
compliance, make a public source offer, or close the public distribution gate.
Ubuntu `.dsc` packages include Debian patches and `debian/rules`; build or
relink claims require separate review of the exact package, source and outputs.
