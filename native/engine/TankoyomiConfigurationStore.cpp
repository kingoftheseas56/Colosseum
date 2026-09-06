#include "TankoyomiConfigurationStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace {
constexpr auto kConfigurationKey = "tankoyomi/configuration";
}

TankoyomiConfigurationStore::TankoyomiConfigurationStore(
    const TankoyomiProviderRegistry &registry, QObject *parent)
    : QObject(parent),
      m_registry(registry),
      m_settings(std::make_unique<QSettings>())
{
    initializeDefaults();
    load();
}

TankoyomiConfigurationStore::TankoyomiConfigurationStore(
    const TankoyomiProviderRegistry &registry, const QString &iniPath, QObject *parent)
    : QObject(parent),
      m_registry(registry),
      m_settings(std::make_unique<QSettings>(iniPath, QSettings::IniFormat))
{
    initializeDefaults();
    load();
}

void TankoyomiConfigurationStore::initializeDefaults()
{
    m_defaultLanguage = m_registry.defaultLanguage();
    m_states.clear();
    for (const QVariant &languageValue : m_registry.languages()) {
        const QVariantMap row = languageValue.toMap();
        const QString language = row.value(QStringLiteral("code")).toString();
        LanguageState state;
        for (const TankoyomiProviderDescriptor &provider : manifestProviders(language)) {
            state.order.append(provider.id);
            state.enabled.insert(provider.id, provider.manifestEnabled);
        }
        m_states.insert(language, state);
    }
}

QString TankoyomiConfigurationStore::canonicalLanguage(const QString &language) const
{
    const QString normalized = language.trimmed().isEmpty()
        ? m_defaultLanguage
        : TankoyomiProviderRegistry::normalizeLanguage(language);
    return m_states.contains(normalized) ? normalized : QString();
}

const TankoyomiConfigurationStore::LanguageState *TankoyomiConfigurationStore::stateFor(
    const QString &language) const
{
    const QString canonical = canonicalLanguage(language);
    if (canonical.isEmpty()) return nullptr;
    const auto state = m_states.constFind(canonical);
    return state == m_states.constEnd() ? nullptr : &state.value();
}

TankoyomiConfigurationStore::LanguageState *TankoyomiConfigurationStore::stateFor(
    const QString &language)
{
    const QString canonical = canonicalLanguage(language);
    if (canonical.isEmpty()) return nullptr;
    return &m_states[canonical];
}

QList<TankoyomiProviderDescriptor> TankoyomiConfigurationStore::manifestProviders(
    const QString &language) const
{
    if (language.trimmed().isEmpty()) return {};
    return m_registry.allProvidersForLanguage(language);
}

std::optional<TankoyomiProviderDescriptor> TankoyomiConfigurationStore::manifestProvider(
    const QString &language, const QString &providerId) const
{
    const auto providers = manifestProviders(language);
    for (const auto &provider : providers) {
        if (provider.id == providerId.trimmed()) return provider;
    }
    return std::nullopt;
}

void TankoyomiConfigurationStore::load()
{
    if (!m_settings) return;
    const QByteArray encoded = m_settings->value(QString::fromLatin1(kConfigurationKey)).toByteArray();
    if (encoded.isEmpty()) return;

    const QJsonDocument document = QJsonDocument::fromJson(encoded);
    if (!document.isObject()) return;
    const QJsonObject root = document.object();

    const QString storedDefault = TankoyomiProviderRegistry::normalizeLanguage(
        root.value(QStringLiteral("defaultLanguage")).toString());
    if (m_states.contains(storedDefault) && !manifestProviders(storedDefault).isEmpty())
        m_defaultLanguage = storedDefault;

    const QJsonObject languages = root.value(QStringLiteral("languages")).toObject();
    for (auto languageIt = languages.constBegin(); languageIt != languages.constEnd(); ++languageIt) {
        // Persisted object names are explicit language identities. They must
        // never use canonicalLanguage()'s empty-means-current-default behavior;
        // empty, whitespace, and unsupported keys are corrupt/stale data.
        const QString rawLanguage = languageIt.key().trimmed();
        if (rawLanguage.isEmpty()) continue;
        const QString language = TankoyomiProviderRegistry::normalizeLanguage(rawLanguage);
        if (language.isEmpty() || !languageIt.value().isObject()) continue;
        if (!m_states.contains(language)) continue;
        LanguageState *state = stateFor(language);
        if (!state) continue;
        const QJsonObject stored = languageIt.value().toObject();

        const QJsonObject enabled = stored.value(QStringLiteral("enabled")).toObject();
        for (const TankoyomiProviderDescriptor &provider : manifestProviders(language)) {
            const auto value = enabled.value(provider.id);
            if (value.isBool()) state->enabled.insert(provider.id, value.toBool());
        }

        const QJsonArray order = stored.value(QStringLiteral("order")).toArray();
        QStringList normalizedOrder;
        QSet<QString> seen;
        for (const QJsonValue &value : order) {
            const QString id = value.toString().trimmed();
            if (id.isEmpty() || seen.contains(id) || !manifestProvider(language, id).has_value()) continue;
            normalizedOrder.append(id);
            seen.insert(id);
        }
        for (const TankoyomiProviderDescriptor &provider : manifestProviders(language)) {
            if (!seen.contains(provider.id)) normalizedOrder.append(provider.id);
        }
        if (!normalizedOrder.isEmpty() || manifestProviders(language).isEmpty())
            state->order = normalizedOrder;
    }
}

bool TankoyomiConfigurationStore::persist() const
{
    if (!m_settings) return false;
    QJsonObject root;
    root.insert(QStringLiteral("defaultLanguage"), m_defaultLanguage);
    QJsonObject languages;
    for (auto languageIt = m_states.constBegin(); languageIt != m_states.constEnd(); ++languageIt) {
        QJsonObject state;
        QJsonObject enabled;
        for (auto enabledIt = languageIt.value().enabled.constBegin();
             enabledIt != languageIt.value().enabled.constEnd(); ++enabledIt) {
            enabled.insert(enabledIt.key(), enabledIt.value());
        }
        QJsonArray order;
        for (const QString &id : languageIt.value().order) order.append(id);
        state.insert(QStringLiteral("enabled"), enabled);
        state.insert(QStringLiteral("order"), order);
        languages.insert(languageIt.key(), state);
    }
    root.insert(QStringLiteral("languages"), languages);

    m_settings->setValue(QString::fromLatin1(kConfigurationKey),
                         QJsonDocument(root).toJson(QJsonDocument::Compact));
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}

void TankoyomiConfigurationStore::bump(const QString &language, bool defaultChanged)
{
    ++m_revision;
    if (defaultChanged) emit defaultLanguageChanged();
    if (!language.isEmpty()) emit providersChanged(language);
    emit changed();
    emit configurationChanged();
}

QVariantList TankoyomiConfigurationStore::languages() const
{
    QVariantList out;
    for (const QVariant &languageValue : m_registry.languages()) {
        const QVariantMap manifest = languageValue.toMap();
        const QString language = manifest.value(QStringLiteral("code")).toString();
        const LanguageState *state = stateFor(language);
        int enabledCount = 0;
        if (state) {
            for (const QString &id : state->order) {
                if (state->enabled.value(id, false)) ++enabledCount;
            }
        }
        QVariantMap row = manifest;
        row.insert(QStringLiteral("enabledProviderCount"), enabledCount);
        row.insert(QStringLiteral("default"), language == m_defaultLanguage);
        out.append(row);
    }
    return out;
}

QVariantList TankoyomiConfigurationStore::providers(const QString &language) const
{
    QVariantList out;
    const QString canonical = canonicalLanguage(language);
    if (canonical.isEmpty()) return out;
    const LanguageState *state = stateFor(canonical);
    if (!state) return out;

    QHash<QString, TankoyomiProviderDescriptor> descriptors;
    for (const TankoyomiProviderDescriptor &provider : manifestProviders(canonical))
        descriptors.insert(provider.id, provider);

    int rank = 0;
    for (const QString &id : state->order) {
        const auto provider = descriptors.constFind(id);
        if (provider == descriptors.constEnd()) continue;
        const TankoyomiProviderDescriptor &descriptor = provider.value();
        out.append(QVariantMap{
            {QStringLiteral("id"), descriptor.id},
            {QStringLiteral("name"), descriptor.name},
            {QStringLiteral("language"), descriptor.language},
            {QStringLiteral("allowedHosts"), descriptor.allowedHosts},
            {QStringLiteral("enabled"), state->enabled.value(descriptor.id, descriptor.manifestEnabled)},
            {QStringLiteral("manifestEnabled"), descriptor.manifestEnabled},
            {QStringLiteral("rank"), rank++},
            {QStringLiteral("priority"), descriptor.priority}
        });
    }
    return out;
}

QList<TankoyomiProviderDescriptor> TankoyomiConfigurationStore::providersForLanguage(
    const QString &language) const
{
    const QString canonical = canonicalLanguage(language);
    if (canonical.isEmpty()) return {};
    const LanguageState *state = stateFor(canonical);
    if (!state) return {};

    QHash<QString, TankoyomiProviderDescriptor> descriptors;
    for (const TankoyomiProviderDescriptor &provider : manifestProviders(canonical))
        descriptors.insert(provider.id, provider);

    QList<TankoyomiProviderDescriptor> out;
    for (const QString &id : state->order) {
        const auto provider = descriptors.constFind(id);
        if (provider == descriptors.constEnd() || !state->enabled.value(id, provider->manifestEnabled))
            continue;
        out.append(provider.value());
    }
    return out;
}

bool TankoyomiConfigurationStore::setDefaultLanguage(const QString &language)
{
    const QString canonical = TankoyomiProviderRegistry::normalizeLanguage(language);
    if (!m_states.contains(canonical) || manifestProviders(canonical).isEmpty()) return false;
    if (canonical == m_defaultLanguage) return true;
    const QString previous = m_defaultLanguage;
    m_defaultLanguage = canonical;
    if (!persist()) {
        m_defaultLanguage = previous;
        return false;
    }
    bump(QString(), true);
    return true;
}

bool TankoyomiConfigurationStore::setProviderEnabled(const QString &language,
                                                     const QString &providerId,
                                                     bool enabled)
{
    const QString canonical = canonicalLanguage(language);
    LanguageState *state = stateFor(canonical);
    if (!state || !manifestProvider(canonical, providerId).has_value()) return false;
    const QString id = providerId.trimmed();
    const bool previous = state->enabled.value(id, false);
    if (previous == enabled) return true;
    state->enabled.insert(id, enabled);
    if (!persist()) {
        state->enabled.insert(id, previous);
        return false;
    }
    bump(canonical, false);
    return true;
}

bool TankoyomiConfigurationStore::moveProvider(const QString &language,
                                               const QString &providerId,
                                               int newIndex)
{
    const QString canonical = canonicalLanguage(language);
    LanguageState *state = stateFor(canonical);
    if (!state || !manifestProvider(canonical, providerId).has_value()) return false;
    const QString id = providerId.trimmed();
    const int oldIndex = state->order.indexOf(id);
    if (oldIndex < 0 || newIndex < 0 || newIndex >= state->order.size()) return false;
    if (oldIndex == newIndex) return true;
    state->order.move(oldIndex, newIndex);
    if (!persist()) {
        state->order.move(newIndex, oldIndex);
        return false;
    }
    bump(canonical, false);
    return true;
}

bool TankoyomiConfigurationStore::moveProviderUp(const QString &language, const QString &providerId)
{
    const QString canonical = canonicalLanguage(language);
    const LanguageState *state = stateFor(canonical);
    if (!state) return false;
    const int index = state->order.indexOf(providerId.trimmed());
    return index > 0 && moveProvider(canonical, providerId, index - 1);
}

bool TankoyomiConfigurationStore::moveProviderDown(const QString &language, const QString &providerId)
{
    const QString canonical = canonicalLanguage(language);
    const LanguageState *state = stateFor(canonical);
    if (!state) return false;
    const int index = state->order.indexOf(providerId.trimmed());
    return index >= 0 && index + 1 < state->order.size()
        && moveProvider(canonical, providerId, index + 1);
}

bool TankoyomiConfigurationStore::resetProviderOrder(const QString &language)
{
    const QString canonical = canonicalLanguage(language);
    if (canonical.isEmpty()) return false;
    LanguageState *state = stateFor(canonical);
    if (!state) return false;
    const QStringList previous = state->order;
    state->order.clear();
    for (const TankoyomiProviderDescriptor &provider : manifestProviders(canonical))
        state->order.append(provider.id);
    if (state->order == previous) return true;
    if (!persist()) {
        state->order = previous;
        return false;
    }
    bump(canonical, false);
    return true;
}
