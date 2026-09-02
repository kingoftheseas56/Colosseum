#include "TankoyomiIdentity.h"

#include <QJsonDocument>
#include <QJsonObject>

QString TankoyomiIdentity::normalizeLanguage(const QString &language)
{
    QString normalized = language.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
    const int dash = normalized.indexOf(QLatin1Char('-'));
    if (dash >= 0) normalized.truncate(dash);
    return normalized;
}

bool TankoyomiIdentity::safeProviderId(const QString &providerId)
{
    if (providerId.isEmpty()) return false;
    for (const QChar c : providerId) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('-')
              || c == QLatin1Char('_') || c == QLatin1Char('.')))
            return false;
    }
    return true;
}
QString TankoyomiIdentity::qualifyChapter(const QString &language,
                                           const QString &providerId,
                                           const QVariantMap &chapter)
{
    const QString normalized = normalizeLanguage(language);
    const QString provider = providerId.trimmed();
    if (normalized.isEmpty() || !safeProviderId(provider) || chapter.isEmpty())
        return {};

    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(chapter))
                                .toJson(QJsonDocument::Compact);
    if (json.isEmpty()) return {};
    const QByteArray payload = json.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals);
    return QStringLiteral("tankoyomi:%1:%2:chapter:v1:%3")
        .arg(normalized, provider, QString::fromLatin1(payload));
}

bool TankoyomiIdentity::isQualifiedChapter(const QString &value)
{
    return value.startsWith(QStringLiteral("tankoyomi:"));
}
std::optional<TankoyomiQualifiedChapter>
TankoyomiIdentity::parseChapter(const QString &qualifiedId)
{
    const QStringList parts = qualifiedId.split(QLatin1Char(':'));
    if (parts.size() != 6 || parts.at(0) != QLatin1String("tankoyomi")
        || parts.at(3) != QLatin1String("chapter") || parts.at(4) != QLatin1String("v1"))
        return std::nullopt;

    const QString language = normalizeLanguage(parts.at(1));
    const QString providerId = parts.at(2).trimmed();
    const QByteArray encoded = parts.at(5).toLatin1();
    if (language.isEmpty() || !safeProviderId(providerId) || encoded.isEmpty())
        return std::nullopt;

    const QByteArray json = QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding);
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QVariantMap chapter = document.object().toVariantMap();
    if (chapter.isEmpty()) return std::nullopt;
    return TankoyomiQualifiedChapter{language, providerId, chapter};
}
