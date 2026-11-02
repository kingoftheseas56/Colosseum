# Optional media in source ZIP/TAR archives

Colosseum's official GitHub source downloads omit selected non-runtime media
payloads to keep the source archive small. No originals are deleted from Git:
a normal git clone contains every media file, signed fixture and test input.

The exact omitted files, byte lengths and SHA-256 checksums are recorded in
scripts/source_archive_media_manifest.json. Originals are addressable in
the public GitHub repository at the immutable sourceCommit listed there.

To restore media from an extracted GitHub source ZIP using the network:

    python scripts/restore_source_archive_media.py

For one use case, run --scope docs, --scope release, --scope capture
or --scope lanista. The capture scope restores the CI video and
capture illustrations. The lanista scope restores signed-chronicle
artwork. JSON and signature files and functional test scripts are
never omitted from archives.

To restore entirely offline, supply a complete local Git checkout or
previously downloaded original files stored under the same paths:

    python scripts/restore_source_archive_media.py --offline --source-dir /path/to/clone

To confirm every optional media payload is present and correct:

    python scripts/restore_source_archive_media.py --verify-only

The restorer verifies the expected length and SHA-256 before atomically
installing each file. It refuses to overwrite a present file with a
different hash. Offline mode never contacts GitHub.

Only documentation images, presentation art and Lanista visual/capture
media are archive-excluded. Application QML/C++, fonts, icons, Universe
art, wallpapers, Reader2 resources, test code, test manifests, release
manifests and signed metadata remain in both ZIP and normal checkout.

The Windows installer already discards documentation/tests/presentation
during runtime staging. Linux release CI uses a full Git checkout.
When running optional Lanista tests from the downloaded source ZIP,
restore their corresponding scope before executing those tests.
