#include "TankoyomiChapterService.h"

#include "TankoyomiIdentity.h"
#include "TankoyomiScriptProvider.h"

#include <QNetworkAccessManager>
#include <QVariantMap>

#include <memory>

TankoyomiChapterService::TankoyomiChapterService(QNetworkAccessManager *nam, QObject *parent)
    : TankoyomiChapterService(nam, nullptr, parent)
{
}

TankoyomiChapterService::TankoyomiChapterService(
    QNetworkAccessManager *nam, TankoyomiConfigurationStore *configuration, QObject *parent)
    : QObject(parent),
      m_registry(TankoyomiProviderRegistry::fromResource())
{
    Q_UNUSED(nam);
    if (!m_registry.isValid()) return;
    m_configuration = configuration;
    if (!m_configuration)
        m_configuration = new TankoyomiConfigurationStore(m_registry, this);

    const QVariantList languageRows = m_registry.languages();
    for (const QVariant &languageValue : languageRows) {
        const QString language = languageValue.toMap().value(QStringLiteral("code")).toString();
        // The legacy m_registry.providersForLanguage(language) projection
        // remains manifest-enabled-only; construction needs the complete
        // validated inventory so a user can enable a manifest-disabled source.
        for (const TankoyomiProviderDescriptor &descriptor
             : m_registry.allProvidersForLanguage(language)) {
            auto *provider = new TankoyomiScriptProvider(
                descriptor.id, descriptor.language, descriptor.resourcePath,
                descriptor.allowedHosts, this);
            m_providers.insert(providerKey(descriptor.language, descriptor.id), provider);
        }
    }
}

QList<TankoyomiProviderDescriptor> TankoyomiChapterService::candidateProviders(
    const QString &language) const
{
    if (!m_configuration) return {};
    return m_configuration->providersForLanguage(language);
}
QString TankoyomiChapterService::providerKey(const QString &language,
                                             const QString &providerId) const
{
    return TankoyomiProviderRegistry::normalizeLanguage(language)
        + QLatin1Char(':') + providerId.trimmed();
}

TankoyomiScriptProvider *TankoyomiChapterService::providerFor(
    const QString &language, const QString &providerId) const
{
    return m_providers.value(providerKey(language, providerId), nullptr);
}

void TankoyomiChapterService::fetchCatalogue(const QString &requestId,
                                             const QString &title,
                                             const QString &language)
{
    if (!m_registry.isValid()) {
        emit catalogueFailed(requestId, m_registry.error());
        return;
    }
    const QString normalized = language.trimmed().isEmpty()
        ? m_configuration->defaultLanguage()
        : TankoyomiProviderRegistry::normalizeLanguage(language);
    const QList<TankoyomiProviderDescriptor> providers = candidateProviders(normalized);
    if (providers.isEmpty()) {
        emit catalogueFailed(
            requestId,
            QStringLiteral("No Tankoyomi chapter provider is configured for language '%1'.")
                .arg(normalized.isEmpty() ? language : normalized));
        return;
    }
    tryProviderChain(requestId, title, normalized, providers);
}

void TankoyomiChapterService::tryProviderChain(
    const QString &requestId,
    const QString &title,
    const QString &language,
    const QList<TankoyomiProviderDescriptor> &providers,
    int index)
{
    if (index < 0 || index >= providers.size()) {
        emit catalogueFailed(
            requestId,
            QStringLiteral("No Tankoyomi chapter provider is available for language '%1'.")
                .arg(language));
        return;
    }

    const TankoyomiProviderDescriptor descriptor = providers.at(index);
    TankoyomiScriptProvider *provider = providerFor(language, descriptor.id);
    if (!provider || !provider->isReady()) {
        if (index + 1 < providers.size()) {
            tryProviderChain(requestId, title, language, providers, index + 1);
            return;
        }
        emit catalogueFailed(
            requestId,
            provider ? provider->loadError()
                     : QStringLiteral("Tankoyomi provider '%1' is unavailable").arg(descriptor.id));
        return;
    }

    struct State {
        QVariantMap series;
        bool settled = false;
    };
    const auto state = std::make_shared<State>();
    QObject *scope = new QObject(this);
    const QString searchToken = requestId + QStringLiteral("|search|") + descriptor.id;
    const QString chaptersToken = requestId + QStringLiteral("|chapters|") + descriptor.id;

    auto failOrFallback = [this, scope, state, requestId, title, language, providers, index]
                          (const QString &message) {
        if (state->settled) return;
        state->settled = true;
        scope->deleteLater();
        if (index + 1 < providers.size())
            tryProviderChain(requestId, title, language, providers, index + 1);
        else
            emit catalogueFailed(requestId, message);
    };

    connect(provider, &TankoyomiScriptProvider::failed, scope,
            [searchToken, chaptersToken, failOrFallback](const QString &token,
                                                         const QString &message) {
        if (token == searchToken || token == chaptersToken)
            failOrFallback(message);
    });

    connect(provider, &TankoyomiScriptProvider::resolved, scope,
            [this, scope, state, provider, descriptor, requestId, title, language,
             searchToken, chaptersToken, failOrFallback](const QString &token,
                                                         const QVariant &value) {
        if (state->settled) return;
        if (token == searchToken) {
            const QVariantList results = value.toList();
            if (results.isEmpty()) {
                failOrFallback(QStringLiteral("Series not found on %1").arg(descriptor.name));
                return;
            }
            state->series = results.first().toMap();
            const QString wanted = title.trimmed();
            for (const QVariant &candidateValue : results) {
                const QVariantMap candidate = candidateValue.toMap();
                if (candidate.value(QStringLiteral("title")).toString().trimmed()
                        .compare(wanted, Qt::CaseInsensitive) == 0) {
                    state->series = candidate;
                    break;
                }
            }
            provider->getChapters(chaptersToken, state->series);
            return;
        }

        if (token != chaptersToken) return;
        const QVariantList rows = value.toList();
        if (rows.isEmpty()) {
            failOrFallback(QStringLiteral("No chapters found on %1").arg(descriptor.name));
            return;
        }

        const QString sourceSeriesId = QStringLiteral("tankoyomi:%1:%2:series")
                                           .arg(language, descriptor.id);
        QVariantList qualified;
        int rawOrder = 0;
        for (const QVariant &rowValue : rows) {
            const QVariantMap providerChapter = rowValue.toMap();
            const QString qualifiedId = TankoyomiIdentity::qualifyChapter(
                language, descriptor.id, providerChapter);
            if (qualifiedId.isEmpty()) continue;

            QVariantMap row = providerChapter;
            const QString rawId = providerChapter.value(QStringLiteral("id")).toString();
            QString locator = providerChapter.value(QStringLiteral("url")).toString();
            if (locator.isEmpty()) locator = rawId;
            QString label = providerChapter.value(QStringLiteral("title")).toString();
            if (label.isEmpty()) label = providerChapter.value(QStringLiteral("name")).toString();
            if (label.isEmpty()) label = providerChapter.value(QStringLiteral("label")).toString();
            if (label.isEmpty())
                label = QStringLiteral("Chapter %1")
                            .arg(providerChapter.value(QStringLiteral("number")).toString());

            row.insert(QStringLiteral("providerChapterId"), rawId);
            row.insert(QStringLiteral("providerLocator"), locator);
            row.insert(QStringLiteral("id"), qualifiedId);
            row.insert(QStringLiteral("seriesId"), sourceSeriesId);
            row.insert(QStringLiteral("source"), descriptor.id);
            row.insert(QStringLiteral("language"), language);
            row.insert(QStringLiteral("name"), label);
            row.insert(QStringLiteral("label"), label);
            row.insert(QStringLiteral("rawOrder"), rawOrder++);
            qualified.append(row);
        }

        if (qualified.isEmpty()) {
            failOrFallback(QStringLiteral("No usable chapters found on %1").arg(descriptor.name));
            return;
        }
        state->settled = true;
        scope->deleteLater();
        emit catalogueReady(requestId, sourceSeriesId, qualified);
    });

    provider->searchSeries(searchToken, title);
}

void TankoyomiChapterService::fetchPages(const QString &requestId,
                                         const QString &qualifiedChapterId)
{
    const auto parsed = TankoyomiIdentity::parseChapter(qualifiedChapterId);
    if (!parsed) {
        emit pagesFailed(requestId, QStringLiteral("Not a valid Tankoyomi chapter id"));
        return;
    }
    TankoyomiScriptProvider *provider = providerFor(parsed->language, parsed->providerId);
    if (!provider || !provider->isReady()) {
        emit pagesFailed(requestId,
                         provider ? provider->loadError()
                                  : QStringLiteral("Unknown Tankoyomi provider '%1' for '%2'")
                                        .arg(parsed->providerId, parsed->language));
        return;
    }

    QObject *scope = new QObject(this);
    const QString token = requestId + QStringLiteral("|pages|") + parsed->providerId;
    connect(provider, &TankoyomiScriptProvider::failed, scope,
            [this, scope, token, requestId](const QString &got, const QString &message) {
        if (got != token) return;
        scope->deleteLater();
        emit pagesFailed(requestId, message);
    });
    connect(provider, &TankoyomiScriptProvider::resolved, scope,
            [this, scope, token, requestId](const QString &got, const QVariant &value) {
        if (got != token) return;
        scope->deleteLater();
        emit pagesReady(requestId, value.toList());
    });

    provider->getPages(token, parsed->chapter);
}
