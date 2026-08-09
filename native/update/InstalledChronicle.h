#pragma once

#include "update/UpdateManifest.h"

#include <QString>

namespace Colosseum::Update {

// Result of a successful InstalledChronicle::load — the verified manifest plus
// the on-disk artwork root. Kept as a free struct so callers (UpdateService,
// the Qt Test) hold the result by value without depending on a loader instance.
struct LoadedChronicle
{
    Manifest manifest;
    QString artworkRoot;
};

// InstalledChronicle loads the bundled signed manifest that seeds the Update
// gallery with the installed release's chapters at rest. It reuses the exact
// trust path the updater uses for discovered releases: the manifest bytes are
// verified against the embedded Ed25519 public key (production key in shipping
// builds, test key under COLOSSEUM_UPDATE_TESTING) before parseManifest ever
// sees them. No unsigned manifest, no partial exposure.
//
// The loader is deliberately minimal: it verifies + parses, and exposes the
// resulting Manifest plus the on-disk directory where the bundled artwork
// files live (so UpdateService::highlightMap can SHA256-check them exactly as
// it checks downloaded artwork). Selection between installed and offered-newer
// manifests lives in UpdateService, not here.
//
// Trust contract:
//   - load() returns std::nullopt on signature failure, parse failure, or a
//     version/tag mismatch. It never returns a Manifest whose bytes were not
//     first verified by verifyEd25519Raw.
//   - On success, manifest is the verified chronicle; artworkRoot is the
//     directory containing the bundled artwork files (absolute path).
//   - The loader performs no network access and writes nothing.
class InstalledChronicle
{
public:
    // Load + verify the bundled chronicle at the given locations.
    // manifestPath / signaturePath: the signed manifest and its raw-or-hex
    //   Ed25519 signature (the same readVerifiedSignature convention the
    //   updater uses: 128 hex chars decode, otherwise raw bytes).
    // expectedVersion: the version the chronicle must carry (matched against
    //   manifest.version). A chronicle whose version differs from the compiled
    //   app identity is rejected — the gallery never shows a chronicle that
    //   does not belong to the installed release.
    // artworkRoot: the directory containing the bundled artwork files; copied
    //   verbatim into the result so callers reach the files without a second
    //   lookup. (qrc extraction to a cache path is the caller's job in Slice 3;
    //   for the test, this is a real on-disk fixture directory.)
    // error: optional error sink (signature failure, parse failure, mismatch).
    // Returns std::nullopt on any failure; never a partial/unverified result.
    static std::optional<LoadedChronicle> load(const QString& manifestPath,
                                               const QString& signaturePath,
                                               const QString& artworkRoot,
                                               const Version& expectedVersion,
                                               QString* error = nullptr);
};

} // namespace Colosseum::Update
