# Exact Linux candidate source acquisition

The default run resumes pinned collection39258916821 by downloading review
artifact12190145167 and source artifact12189870457 on CI. Before reuse, every
file is verified against that collection's complete SHA256 index. The accepted
Debian input is independently streamed again. The final receipts distinguish
reused proofs (including all207 SDK files) from the two repaired source URL
index entries. APT's URL query runs in an empty temporary directory because it
otherwise suppresses URIs for already cached files. Current Ubuntu archives precede the historical
snapshot; downloads use bounded retries for transient network errors. The exact
Qt WebEngine extension repository comes from the accepted pinned aqt installer
layout. Existing command logs are preserved and new receipts append after them.
For a fresh collection, manually dispatch with full_collection=true; this also
works after the optional reuse artifacts expire, provided the accepted package
input is still accessible. Reuse is never treated as a new independent build.

This collector prepares review artifacts for the accepted 1.1.9 Debian package;
it does not rebuild the application, publish a release or promise a source offer.
The package size and SHA256 are pinned in inventory.py. All payload files are
hashed through a stream; only notices, license texts and metadata are extracted.

Run the named branch workflow on Ubuntu 24.04. Its input is accepted artifact
12182359609 of run 39244501311. The workflow retains the exact application's
source archive, accepted build workflow and collector revision. It enables
Ubuntu deb-src with normal signed APT authentication and retains InRelease,
Packages/Sources indices, source configuration, archive keyring and command logs.

For each actual Ubuntu-origin bundled dependency, distro.py looks up the exact
origin path in dpkg's database. This is only an owner candidate: verification
then requires the binary package at the version recorded in the accepted
package's provenance, authenticated Packages metadata, matching archive checksum,
the exact file inside that archive, matching pre-relocation SHA256, and replayed
RPATH transformation matching the shipped bytes. For ffmpeg/ffprobe, whose
pre-relocation hashes were not retained, the exact final transformed bytes are
required. Runner drift never substitutes a newer package or source version.

Only verified binary metadata supplies source package/version. Source identity
must also agree with the original build provenance. apt-get source downloads
the exact version, and all source files must match authenticated Sources SHA256
and size. The source index retains actual APT acquisition URIs. An unavailable
old version, absent owner, notice, hash or source metadata stays unresolved.
Host package lists and SONAME-name guesses are never sufficient evidence.
The accepted runner image supplied AppArmor .8, superseded before the package
build. CI adds official Ubuntu snapshot 20261001T000000Z noble-updates/main
metadata to acquire that exact version; historical validity checking is disabled
only for that archived repository, with archive signature verification retained.
The signed historical indices and configuration are part of review evidence.

Qt is separate from Ubuntu: its own original SPDX/CycloneDX documents are
retained. Upstream collection acquires Qt release module archives, verifies
official download.qt.io SHA256 metadata and retains source notices. Module Git
snapshots use full commits from retained SBOMs where present, otherwise the
upstream v6.11.1 tag with its resolved commit recorded. Archive files are compared
with Git blob identities; missing/different paths and gitlinks remain visible.
sdk.py additionally verifies original Qt installer archive checksums, locates
exact file members, checks retained pre-relocation hashes where present and
replays RPATH edits against shipped SHA256. The original installer archives are
temporary CI inputs, not claimed to be source. Archive checksums and the exact
Qt Updates.xml metadata remain in review evidence. SBOM path attribution by
itself is explicitly weaker than this file verification. ICU73.2
has NOASSERTION source location in the retained SDK SBOM and remains unresolved
until exact SDK source/build provenance is supplied. The Unicode73.2 source
archive is acquired against official Unicode SHA512 metadata as a candidate,
with this correspondence limitation recorded. QtWebEngine source archives
contain Chromium, but transitive source closure and SDK configuration must be
reviewed against the retained Chromium SBOMs and recorded gitlinks.

The custom TIFF plugin has its own exact qtimageformats commit snapshot
77f35e5694885844c3eb3a737769b5ed290b8ccc, separate from the SDK module. This includes
the bundled libtiff source, attribution and COPYRIGHT. The accepted workflow
records FEATURE_system_tiff=OFF and the complete CMake invocation. MpvQt1.2.0 and
ECM6.15.0 archives must match accepted build SHA256 pins; their notices are
retained unchanged. The workflow records all original build options and linkage.
Reproducing the application uses that accepted workflow/source with the acquired
exact dependency sources, not a claim that current APT versions reproduce it.
Ubuntu source packages retain their Debian patches and debian/rules; inspect each
.dsc and use dpkg-source -x followed by the package's documented build procedure.
No reproduction/build or license-specific relinking qualification is claimed.

Outputs: review artifact with inventory, unchanged notices, metadata, logs,
build recipes and unresolved cases; source artifact with actual acquired source
archives and SHA256-INDEX.json. Verify each downloaded artifact/file checksum
before receiving. CI artifacts expire after 14 days. Public distribution needs
durable approved source hosting and notice/source-delivery acceptance. No
license classification defaults to permissive, no blanket licensing verdict is
generated, and successful CI alone never closes the source-distribution gate.
