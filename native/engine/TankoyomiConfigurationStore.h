#pragma once

#include "TankoyomiProviderRegistry.h"

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>

#include <memory>
#include <optional>

// User-owned overlay over TankoyomiProviderRegistry's immutable manifest
// inventory. The registry remains responsible for validating identities,
// language membership, and manifest defaults; this class only persists the
// user's default language, enabled flags, and provider order.
class TankoyomiConfigurationStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString defaultLanguage READ defaultLanguage NOTIFY defaultLanguageChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool liveReading READ liveReading NOTIFY liveReadingChanged)

public:
    explicit TankoyomiConfigurationStore(const TankoyomiProviderRegistry &registry,
                                         QObject *parent = nullptr);
    TankoyomiConfigurationStore(const TankoyomiProviderRegistry &registry,
                                const QString &iniPath,
                                QObject *parent = nullptr);

    QString defaultLanguage() const { return m_defaultLanguage; }
    int revision() const { return m_revision; }
    bool liveReading() const { return m_liveReading; }
    Q_INVOKABLE bool setLiveReading(bool enabled);
    // Replace remote inventory while retaining the user's persisted overlay.
    bool replaceRegistry(const TankoyomiProviderRegistry &registry);

    // QML-facing projections. Provider rows include every valid manifest
    // provider, including providers whose manifest default is disabled.
    Q_INVOKABLE QVariantList languages() const;
    Q_INVOKABLE QVariantList providers(const QString &language) const;

    // Runtime ladder: only currently enabled providers, in configured order.
    QList<TankoyomiProviderDescriptor> providersForLanguage(const QString &language) const;

    Q_INVOKABLE bool setDefaultLanguage(const QString &language);
    Q_INVOKABLE bool setProviderEnabled(const QString &language,
                                        const QString &providerId,
                                        bool enabled);
    Q_INVOKABLE bool moveProvider(const QString &language,
                                  const QString &providerId,
                                  int newIndex);
    Q_INVOKABLE bool moveProviderUp(const QString &language, const QString &providerId);
    Q_INVOKABLE bool moveProviderDown(const QString &language, const QString &providerId);
    Q_INVOKABLE bool resetProviderOrder(const QString &language);

signals:
    void defaultLanguageChanged();
    void liveReadingChanged();
    void providersChanged(const QString &language);
    void changed();
    void configurationChanged();

private:
    struct LanguageState {
        QStringList order;
        QHash<QString, bool> enabled;
    };

    void initializeDefaults();
    void load();
    bool persist() const;
    void bump(const QString &language, bool defaultChanged);
    QString canonicalLanguage(const QString &language) const;
    const LanguageState *stateFor(const QString &language) const;
    LanguageState *stateFor(const QString &language);
    QList<TankoyomiProviderDescriptor> manifestProviders(const QString &language) const;
    std::optional<TankoyomiProviderDescriptor> manifestProvider(const QString &language,
                                                                const QString &providerId) const;

    TankoyomiProviderRegistry m_registry;
    std::unique_ptr<QSettings> m_settings;
    QString m_defaultLanguage;
    bool m_liveReading = false;
    QHash<QString, LanguageState> m_states;
    int m_revision = 0;
};
