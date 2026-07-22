// tests/model_manifest_harness.cpp
#include "models/ModelManifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    require(f.open(QIODevice::WriteOnly), "fixture file writable");
    f.write(bytes);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");

    const QByteArray modelBytes = QByteArrayLiteral("fake model weights, deterministic");
    const QString modelPath = dir.filePath(QStringLiteral("tiny.onnx"));
    writeFile(modelPath, modelBytes);
    const QByteArray sha = QCryptographicHash::hash(modelBytes, QCryptographicHash::Sha256).toHex();

    const QString manifestPath = dir.filePath(QStringLiteral("manifest.json"));
    writeFile(manifestPath, QByteArray("{\n"
                                       "  \"schema\": 1,\n"
                                       "  \"modelId\": \"tiny-test\",\n"
                                       "  \"modelVersion\": \"abc1234\",\n"
                                       "  \"file\": \"tiny.onnx\",\n"
                                       "  \"sha256\": \"" + sha + "\",\n"
                                       "  \"license\": \"MIT\",\n"
                                       "  \"classes\": {\"0\": \"panel\", \"1\": \"text\"}\n"
                                       "}\n"));

    models::ManifestError error = models::ManifestError::None;
    auto manifest = models::ModelManifest::load(manifestPath, &error);
    require(manifest.has_value(), "valid manifest loads");
    require(error == models::ManifestError::None, "no error on valid load");
    require(manifest->modelId == QStringLiteral("tiny-test"), "modelId parsed");
    require(manifest->filePath() == modelPath, "filePath resolves beside manifest");
    require(manifest->extra.contains(QStringLiteral("classes")), "domain fields ride in extra");
    require(manifest->validateChecksum() == models::ManifestError::None, "checksum passes");

    // One flipped byte must fail closed.
    writeFile(modelPath, QByteArray("Fake model weights, deterministic"));
    require(manifest->validateChecksum() == models::ManifestError::ChecksumFailed,
            "flipped byte detected");

    QFile::remove(modelPath);
    require(manifest->validateChecksum() == models::ManifestError::FileMissing,
            "missing model file detected");

    models::ManifestError badError = models::ManifestError::None;
    require(!models::ModelManifest::load(dir.filePath(QStringLiteral("ghost.json")), &badError)
                .has_value(),
            "missing manifest rejected");
    require(badError == models::ManifestError::ManifestMissing, "missing manifest code");

    const QString brokenPath = dir.filePath(QStringLiteral("broken.json"));
    writeFile(brokenPath, QByteArrayLiteral("{ not json"));
    require(!models::ModelManifest::load(brokenPath, &badError).has_value(),
            "broken manifest rejected");
    require(badError == models::ManifestError::ManifestInvalid, "broken manifest code");

    require(models::toCode(models::ManifestError::FileMissing) == QStringLiteral("model_missing"),
            "stable code model_missing");
    require(models::toCode(models::ManifestError::ChecksumFailed)
                == QStringLiteral("model_checksum_failed"),
            "stable code model_checksum_failed");

    std::cout << "MODEL_MANIFEST_OK\n";
    return 0;
}
