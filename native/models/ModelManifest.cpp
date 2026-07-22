// native/models/ModelManifest.cpp
#include "models/ModelManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace models {

QString toCode(ManifestError error)
{
    switch (error) {
    case ManifestError::None:
        return QString();
    case ManifestError::ManifestMissing:
        return QStringLiteral("manifest_missing");
    case ManifestError::ManifestInvalid:
        return QStringLiteral("manifest_invalid");
    case ManifestError::FileMissing:
        return QStringLiteral("model_missing");
    case ManifestError::ChecksumFailed:
        return QStringLiteral("model_checksum_failed");
    }
    return QString();
}

QString ModelManifest::filePath() const
{
    return QDir(dir).filePath(file);
}

std::optional<ModelManifest> ModelManifest::load(const QString &manifestPath,
                                                 ManifestError *error)
{
    auto fail = [&](ManifestError code) -> std::optional<ModelManifest> {
        if (error)
            *error = code;
        return std::nullopt;
    };

    QFile f(manifestPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return fail(ManifestError::ManifestMissing);

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(ManifestError::ManifestInvalid);

    const QJsonObject obj = doc.object();
    ModelManifest manifest;
    manifest.schema = obj.value(QStringLiteral("schema")).toInt();
    manifest.modelId = obj.value(QStringLiteral("modelId")).toString();
    manifest.modelVersion = obj.value(QStringLiteral("modelVersion")).toString();
    manifest.file = obj.value(QStringLiteral("file")).toString();
    manifest.sha256 = obj.value(QStringLiteral("sha256")).toString().toLower();
    manifest.license = obj.value(QStringLiteral("license")).toString();
    manifest.extra = obj;
    manifest.dir = QFileInfo(manifestPath).absolutePath();

    if (manifest.schema < 1 || manifest.modelId.isEmpty() || manifest.file.isEmpty()
        || manifest.sha256.size() != 64)
        return fail(ManifestError::ManifestInvalid);

    if (error)
        *error = ManifestError::None;
    return manifest;
}

ManifestError ModelManifest::validateChecksum() const
{
    QFile f(filePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return ManifestError::FileMissing;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return ManifestError::ChecksumFailed;
    return hash.result().toHex() == sha256.toUtf8() ? ManifestError::None
                                                    : ManifestError::ChecksumFailed;
}

} // namespace models
