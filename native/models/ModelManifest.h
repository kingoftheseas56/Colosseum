// native/models/ModelManifest.h
#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace models {

enum class ManifestError { None, ManifestMissing, ManifestInvalid, FileMissing, ChecksumFailed };

// Stable wire codes shared by every offline-ML feature's failure surface.
QString toCode(ManifestError error);

// Generic bundled-model descriptor (guided comics detector, alignment speech
// models). Core identity + integrity live here; domain-specific fields (tensor
// shapes, classes, thresholds, languages) ride along untouched in `extra`.
class ModelManifest {
public:
    int schema = 0;
    QString modelId;
    QString modelVersion;
    QString file;   // model filename relative to the manifest's directory
    QString sha256; // lowercase hex digest of the model file
    QString license;
    QJsonObject extra; // the full manifest object, for domain readers
    QString dir;       // directory containing manifest.json (set by load)

    QString filePath() const;

    static std::optional<ModelManifest> load(const QString &manifestPath,
                                             ManifestError *error = nullptr);
    ManifestError validateChecksum() const; // streams the file through SHA-256
};

} // namespace models
