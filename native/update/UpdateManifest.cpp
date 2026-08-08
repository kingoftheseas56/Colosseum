#include "update/UpdateManifest.h"

#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace Colosseum::Update {
namespace {

constexpr int kMaxText = 4096;
constexpr int kMaxHighlights = 8;
constexpr int kMaxArtwork = 16;

void fail(QString* error, const QString& message)
{
    if (error) *error = message;
}

bool exactKeys(const QJsonObject& object, const QSet<QString>& allowed, QString* error)
{
    for (const QString& key : object.keys()) {
        if (!allowed.contains(key)) {
            fail(error, QStringLiteral("unknown_manifest_key:") + key);
            return false;
        }
    }
    return true;
}

bool boundedText(const QJsonValue& value, QString* result, QString* error, const char* field)
{
    if (!value.isString() || value.toString().isEmpty() || value.toString().size() > kMaxText) {
        fail(error, QStringLiteral("invalid_text:") + QString::fromLatin1(field));
        return false;
    }
    *result = value.toString();
    return true;
}

bool sha256Field(const QJsonValue& value, QByteArray* result, QString* error, const char* field)
{
    const QString text = value.toString();
    static const QRegularExpression hexPattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    if (!value.isString() || !hexPattern.match(text).hasMatch()) {
        fail(error, QStringLiteral("invalid_sha256:") + QString::fromLatin1(field));
        return false;
    }
    *result = QByteArray::fromHex(text.toLatin1());
    return true;
}

std::optional<HighlightKind> highlightKind(const QString& kind)
{
    if (kind == QStringLiteral("feature")) return HighlightKind::Feature;
    if (kind == QStringLiteral("statistic")) return HighlightKind::Statistic;
    if (kind == QStringLiteral("beforeAfter")) return HighlightKind::BeforeAfter;
    if (kind == QStringLiteral("milestone")) return HighlightKind::Milestone;
    return std::nullopt;
}

bool safeAssetName(const QString& name)
{
    if (name.isEmpty() || name.size() > 128 || name.contains(u'/') || name.contains(u'\\')
        || name == QStringLiteral(".") || name == QStringLiteral(".."))
        return false;
    return true;
}

} // namespace

std::optional<Manifest> parseManifest(const QByteArray& verifiedUtf8, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(verifiedUtf8, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(error, QStringLiteral("invalid_json"));
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (!exactKeys(root, {QStringLiteral("schemaVersion"), QStringLiteral("version"),
                          QStringLiteral("tag"), QStringLiteral("eyebrow"),
                          QStringLiteral("title"), QStringLiteral("summary"),
                          QStringLiteral("installer"), QStringLiteral("minimumUpdaterVersion"),
                          QStringLiteral("notesUrl"), QStringLiteral("highlights"),
                          QStringLiteral("artwork")}, error))
        return std::nullopt;
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        fail(error, QStringLiteral("unsupported_schema"));
        return std::nullopt;
    }

    Manifest manifest;
    manifest.schemaVersion = 1;
    const auto version = Version::parseCanonical(root.value(QStringLiteral("version")).toString());
    const auto tagVersion = Version::parseTag(root.value(QStringLiteral("tag")).toString());
    if (!version || !tagVersion || version->compare(*tagVersion) != 0) {
        fail(error, QStringLiteral("version_tag_mismatch"));
        return std::nullopt;
    }
    manifest.version = *version;
    manifest.tag = root.value(QStringLiteral("tag")).toString();
    if (!boundedText(root.value(QStringLiteral("eyebrow")), &manifest.eyebrow, error, "eyebrow")
        || !boundedText(root.value(QStringLiteral("title")), &manifest.title, error, "title")
        || !boundedText(root.value(QStringLiteral("summary")), &manifest.summary, error, "summary"))
        return std::nullopt;

    const QUrl notesUrl(root.value(QStringLiteral("notesUrl")).toString());
    const QString expectedNotes = QStringLiteral(
        "https://github.com/kingoftheseas56/Colosseum/releases/tag/") + manifest.tag;
    if (!notesUrl.isValid() || notesUrl.scheme() != QStringLiteral("https")
        || notesUrl.toString() != expectedNotes) {
        fail(error, QStringLiteral("invalid_notes_url"));
        return std::nullopt;
    }
    manifest.notesUrl = notesUrl.toString();

    const QJsonObject installer = root.value(QStringLiteral("installer")).toObject();
    if (installer.isEmpty()
        || !exactKeys(installer, {QStringLiteral("asset"), QStringLiteral("size"),
                                  QStringLiteral("sha256")}, error)
        || !safeAssetName(installer.value(QStringLiteral("asset")).toString())
        || !boundedText(installer.value(QStringLiteral("asset")), &manifest.installerAsset,
                        error, "installer.asset")
        || !sha256Field(installer.value(QStringLiteral("sha256")), &manifest.installerSha256,
                        error, "installer.sha256"))
        return std::nullopt;
    const QJsonValue size = installer.value(QStringLiteral("size"));
    if (!size.isDouble() || size.toDouble() <= 0 || size.toDouble() != std::floor(size.toDouble())) {
        fail(error, QStringLiteral("invalid_installer_size"));
        return std::nullopt;
    }
    manifest.installerSize = static_cast<qint64>(size.toDouble());

    const auto minimum = Version::parseCanonical(
        root.value(QStringLiteral("minimumUpdaterVersion")).toString());
    if (!minimum) {
        fail(error, QStringLiteral("invalid_minimum_updater_version"));
        return std::nullopt;
    }
    manifest.minimumUpdaterVersion = *minimum;

    const QJsonArray highlights = root.value(QStringLiteral("highlights")).toArray();
    if (highlights.size() > kMaxHighlights) {
        fail(error, QStringLiteral("too_many_highlights"));
        return std::nullopt;
    }
    const QSet<QString> highlightKeys = {
        QStringLiteral("kind"), QStringLiteral("section"), QStringLiteral("title"),
        QStringLiteral("body"), QStringLiteral("value"), QStringLiteral("context"),
        QStringLiteral("beforeCaption"), QStringLiteral("afterCaption"),
        QStringLiteral("artworkAssets")};
    for (const QJsonValue& value : highlights) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty() || !exactKeys(object, highlightKeys, error))
            return std::nullopt;
        const auto kind = highlightKind(object.value(QStringLiteral("kind")).toString());
        Highlight highlight;
        if (!kind || !boundedText(object.value(QStringLiteral("section")), &highlight.section,
                                  error, "highlight.section")
            || !boundedText(object.value(QStringLiteral("title")), &highlight.title,
                            error, "highlight.title")
            || !boundedText(object.value(QStringLiteral("body")), &highlight.body,
                            error, "highlight.body"))
            return std::nullopt;
        highlight.kind = *kind;
        for (const QString& optional : {QStringLiteral("value"), QStringLiteral("context"),
                                        QStringLiteral("beforeCaption"), QStringLiteral("afterCaption")}) {
            if (object.contains(optional) && !object.value(optional).isString()) {
                fail(error, QStringLiteral("invalid_highlight_text"));
                return std::nullopt;
            }
        }
        highlight.value = object.value(QStringLiteral("value")).toString();
        highlight.context = object.value(QStringLiteral("context")).toString();
        highlight.beforeCaption = object.value(QStringLiteral("beforeCaption")).toString();
        highlight.afterCaption = object.value(QStringLiteral("afterCaption")).toString();
        const QJsonArray assets = object.value(QStringLiteral("artworkAssets")).toArray();
        for (const QJsonValue& asset : assets) {
            const QString name = asset.toString();
            if (!safeAssetName(name)) {
                fail(error, QStringLiteral("unsafe_highlight_artwork"));
                return std::nullopt;
            }
            highlight.artworkAssets.append(name);
        }
        manifest.highlights.append(highlight);
    }

    const QJsonArray artwork = root.value(QStringLiteral("artwork")).toArray();
    if (artwork.size() > kMaxArtwork) {
        fail(error, QStringLiteral("too_many_artwork_assets"));
        return std::nullopt;
    }
    QSet<QString> artworkNames;
    for (const QJsonValue& value : artwork) {
        const QJsonObject object = value.toObject();
        if (!exactKeys(object, {QStringLiteral("asset"), QStringLiteral("sha256")}, error))
            return std::nullopt;
        Artwork item;
        item.assetName = object.value(QStringLiteral("asset")).toString();
        if (!safeAssetName(item.assetName) || artworkNames.contains(item.assetName)
            || !sha256Field(object.value(QStringLiteral("sha256")), &item.sha256, error,
                            "artwork.sha256")) {
            fail(error, QStringLiteral("invalid_artwork_asset"));
            return std::nullopt;
        }
        artworkNames.insert(item.assetName);
        manifest.artwork.append(item);
    }

    return manifest;
}

} // namespace Colosseum::Update
