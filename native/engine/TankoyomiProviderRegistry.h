#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <optional>

struct TankoyomiProviderDescriptor
{
    QString id;
    QString name;
    QString language;
    QString entry;
    QString resourcePath;
    QStringList allowedHosts;
    int priority = 999;
    // Inventory/default authority from manifest.json. Runtime enablement is an
    // overlay owned by TankoyomiConfigurationStore.
    bool manifestEnabled = true;
};

class TankoyomiProviderRegistry
{
public:
    explicit TankoyomiProviderRegistry(
        const QByteArray &manifestJson,
        QString resourceRoot = QStringLiteral(":/tankoyomi/"));

    static TankoyomiProviderRegistry fromResource(
        const QString &manifestPath = QStringLiteral(":/tankoyomi/manifest.json"));
    static QString normalizeLanguage(const QString &language);
    bool isValid() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }
    QString defaultLanguage() const { return m_defaultLanguage; }
    QString fallbackPolicy() const { return m_fallbackPolicy; }

    // Legacy runtime projection. This preserves the original meaning of this
    // API: only providers whose manifest default is enabled are returned.
    QList<TankoyomiProviderDescriptor> providersForLanguage(const QString &language) const;
    // Complete validated inventory projection for configuration and provider
    // construction. It includes manifest-default-disabled providers while
    // retaining manifest priority order and same-language resolution.
    QList<TankoyomiProviderDescriptor> allProvidersForLanguage(const QString &language) const;
    std::optional<TankoyomiProviderDescriptor> provider(const QString &language,
                                                        const QString &providerId) const;
    QVariantList languages() const;

private:
    struct LanguageDescriptor {
        QString code;
        QString label;
        QList<TankoyomiProviderDescriptor> providers;
    };

    void parse(const QByteArray &manifestJson, const QString &resourceRoot);
    static bool safeEntry(const QString &entry);
    static bool safeHost(const QString &host);

    QString m_defaultLanguage;
    QString m_fallbackPolicy;
    QList<LanguageDescriptor> m_languages;
    QString m_error;
};
