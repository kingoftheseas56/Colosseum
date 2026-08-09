#include "update/InstalledChronicle.h"

#include "update/UpdateTrust.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace Colosseum::Update {
namespace {

// Mirrors UpdateService::readVerifiedSignature: accept either 128 hex chars
// (decode to 64 raw bytes) or raw 64-byte bytes. Keeps the bundled signature
// format identical to the updater's on-disk signature convention.
QByteArray decodeSignature(const QByteArray& bytes)
{
    const QByteArray text = bytes.trimmed();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{128}$"));
    if (hex.match(QString::fromLatin1(text)).hasMatch())
        return QByteArray::fromHex(text);
    return bytes;
}

void fail(QString* error, const QString& message)
{
    if (error) *error = message;
}

QByteArray readFile(const QString& path, const char* what, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("installed_chronicle_") + QString::fromLatin1(what)
                       + QStringLiteral("_open_failed"));
        return {};
    }
    return file.readAll();
}

} // namespace

std::optional<LoadedChronicle> InstalledChronicle::load(const QString& manifestPath,
                                                        const QString& signaturePath,
                                                        const QString& artworkRoot,
                                                        const Version& expectedVersion,
                                                        QString* error)
{
    const QByteArray manifestBytes = readFile(manifestPath, "manifest", error);
    if (manifestBytes.isEmpty()) {
        fail(error, error ? *error : QStringLiteral("installed_chronicle_manifest_empty"));
        return std::nullopt;
    }
    const QByteArray signatureRaw = readFile(signaturePath, "signature", error);
    if (signatureRaw.isEmpty()) {
        fail(error, error ? *error : QStringLiteral("installed_chronicle_signature_empty"));
        return std::nullopt;
    }
    const QByteArray signature = decodeSignature(signatureRaw);

    // Trust gate: verify the manifest bytes against the embedded public key
    // BEFORE parsing. This is the same verifyEd25519Raw path the updater uses
    // for discovered releases — no second trust lane.
    if (!verifyEd25519Raw(manifestBytes, signature, embeddedUpdatePublicKey(), error)) {
        fail(error, QStringLiteral("installed_chronicle_signature_failed"));
        return std::nullopt;
    }

    // Only verified bytes reach the parser.
    const auto parsed = parseManifest(manifestBytes, error);
    if (!parsed) {
        fail(error, QStringLiteral("installed_chronicle_parse_failed"));
        return std::nullopt;
    }

    // The chronicle must belong to the installed release. A bundle whose
    // version differs from the compiled app identity is rejected — the gallery
    // never shows a chronicle for a different release than the one installed.
    if (parsed->version.compare(expectedVersion) != 0) {
        fail(error, QStringLiteral("installed_chronicle_version_mismatch"));
        return std::nullopt;
    }

    LoadedChronicle result;
    result.manifest = *parsed;
    result.artworkRoot = QDir::cleanPath(artworkRoot);
    return result;
}

} // namespace Colosseum::Update
