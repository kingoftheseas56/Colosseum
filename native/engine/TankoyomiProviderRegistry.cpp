#include "TankoyomiProviderRegistry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

TankoyomiProviderRegistry::TankoyomiProviderRegistry(const QByteArray &manifestJson,
                                                     QString resourceRoot)
{
    parse(manifestJson, resourceRoot);
}

TankoyomiProviderRegistry TankoyomiProviderRegistry::fromResource(const QString &manifestPath)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
        return TankoyomiProviderRegistry(QByteArray());

    QString root = manifestPath;
    const int slash = root.lastIndexOf(QLatin1Char('/'));
    root = slash >= 0 ? root.left(slash + 1) : QStringLiteral(":/tankoyomi/");
    return TankoyomiProviderRegistry(file.readAll(), root);
}

QString TankoyomiProviderRegistry::normalizeLanguage(const QString &language)
{
    QString normalized = language.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
    const int dash = normalized.indexOf(QLatin1Char('-'));
    if (dash >= 0) normalized.truncate(dash);
    return normalized;
}
bool TankoyomiProviderRegistry::safeEntry(const QString &entry)
{
    if (entry.isEmpty() || entry.startsWith(QLatin1Char('/')) || entry.startsWith(QLatin1Char('\\')))
        return false;
    if (entry.contains(QStringLiteral("..")) || entry.contains(QLatin1Char(':'))
        || entry.contains(QLatin1Char('\\')))
        return false;
    return entry.endsWith(QStringLiteral(".js"), Qt::CaseInsensitive);
}

bool TankoyomiProviderRegistry::safeHost(const QString &host)
{
    const QString normalized = host.trimmed().toLower();
    if (normalized.isEmpty() || normalized.contains(QStringLiteral("://"))
        || normalized.contains(QLatin1Char('/')) || normalized.contains(QLatin1Char(':'))
        || normalized.startsWith(QLatin1Char('.')) || normalized.endsWith(QLatin1Char('.')))
        return false;
    for (const QChar c : normalized) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-')))
            return false;
    }
    return true;
}

void TankoyomiProviderRegistry::parse(const QByteArray &manifestJson, const QString &resourceRoot)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_error = QStringLiteral("Tankoyomi manifest is invalid JSON");
        return;
    }
    const QJsonObject root = document.object();
    m_defaultLanguage = normalizeLanguage(root.value(QStringLiteral("defaultLanguage")).toString());
    m_fallbackPolicy = root.value(QStringLiteral("fallbackPolicy")).toString();
    if (m_defaultLanguage.isEmpty()) {
        m_error = QStringLiteral("Tankoyomi manifest has no default language");
        return;
    }
    if (m_fallbackPolicy != QLatin1String("same-language-only")) {
        m_error = QStringLiteral("Tankoyomi manifest fallback policy must be same-language-only");
        return;
    }

    QString resourcePrefix = resourceRoot;
    if (!resourcePrefix.endsWith(QLatin1Char('/'))) resourcePrefix += QLatin1Char('/');
    QSet<QString> languageCodes;
    const QJsonArray languages = root.value(QStringLiteral("languages")).toArray();
    if (languages.isEmpty()) {
        m_error = QStringLiteral("Tankoyomi manifest has no languages");
        return;
    }

    for (const QJsonValue &languageValue : languages) {
        const QJsonObject languageObject = languageValue.toObject();
        LanguageDescriptor language;
        language.code = normalizeLanguage(languageObject.value(QStringLiteral("code")).toString());
        language.label = languageObject.value(QStringLiteral("label")).toString().trimmed();
        if (language.code.isEmpty() || language.label.isEmpty() || languageCodes.contains(language.code)) {
            m_error = QStringLiteral("Tankoyomi manifest contains an invalid or duplicate language");
            return;
        }
        languageCodes.insert(language.code);
        QSet<QString> providerIds;
        const QJsonArray providers = languageObject.value(QStringLiteral("providers")).toArray();
        for (const QJsonValue &providerValue : providers) {
            const QJsonObject providerObject = providerValue.toObject();
            if (providerObject.value(QStringLiteral("enabled")).toBool(true) == false) continue;

            TankoyomiProviderDescriptor provider;
            provider.id = providerObject.value(QStringLiteral("id")).toString().trimmed();
            provider.name = providerObject.value(QStringLiteral("name")).toString().trimmed();
            provider.language = language.code;
            provider.entry = providerObject.value(QStringLiteral("entry")).toString().trimmed();
            provider.priority = providerObject.value(QStringLiteral("priority")).toInt(999);
            if (provider.id.isEmpty() || provider.name.isEmpty() || !safeEntry(provider.entry)
                || providerIds.contains(provider.id)) {
                m_error = QStringLiteral("Tankoyomi manifest contains an invalid or duplicate provider");
                return;
            }
            providerIds.insert(provider.id);

            const QJsonArray hosts = providerObject.value(QStringLiteral("allowedHosts")).toArray();
            for (const QJsonValue &hostValue : hosts) {
                const QString host = hostValue.toString().trimmed().toLower();
                if (!safeHost(host)) {
                    m_error = QStringLiteral("Tankoyomi provider '%1' has an invalid host allowlist")
                                  .arg(provider.id);
                    return;
                }
                if (!provider.allowedHosts.contains(host)) provider.allowedHosts.append(host);
            }
            if (provider.allowedHosts.isEmpty()) {
                m_error = QStringLiteral("Tankoyomi provider '%1' has no allowed hosts").arg(provider.id);
                return;
            }
            provider.resourcePath = resourcePrefix + provider.entry;
            language.providers.append(provider);
        }
        std::stable_sort(language.providers.begin(), language.providers.end(),
                         [](const TankoyomiProviderDescriptor &a,
                            const TankoyomiProviderDescriptor &b) {
            return a.priority < b.priority;
        });
        m_languages.append(language);
    }

    if (!languageCodes.contains(m_defaultLanguage)) {
        m_error = QStringLiteral("Tankoyomi default language is not installed");
        return;
    }
}

QList<TankoyomiProviderDescriptor>
TankoyomiProviderRegistry::providersForLanguage(const QString &requested) const
{
    if (!isValid()) return {};
    const QString language = requested.trimmed().isEmpty()
        ? m_defaultLanguage : normalizeLanguage(requested);
    for (const LanguageDescriptor &entry : m_languages) {
        if (entry.code == language) return entry.providers;
    }
    return {};
}
std::optional<TankoyomiProviderDescriptor>
TankoyomiProviderRegistry::provider(const QString &language, const QString &providerId) const
{
    const QList<TankoyomiProviderDescriptor> providers = providersForLanguage(language);
    for (const TankoyomiProviderDescriptor &entry : providers) {
        if (entry.id == providerId) return entry;
    }
    return std::nullopt;
}

QVariantList TankoyomiProviderRegistry::languages() const
{
    QVariantList out;
    if (!isValid()) return out;
    for (const LanguageDescriptor &language : m_languages) {
        if (language.providers.isEmpty()) continue;
        out.append(QVariantMap{
            {QStringLiteral("code"), language.code},
            {QStringLiteral("label"), language.label},
            {QStringLiteral("providerCount"), language.providers.size()}
        });
    }
    return out;
}