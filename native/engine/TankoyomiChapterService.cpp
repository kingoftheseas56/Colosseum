#include "TankoyomiChapterService.h"

#include "TankoyomiIdentity.h"
#include "TankoyomiScriptProvider.h"

#include <QNetworkAccessManager>
#include <QVariantMap>
#include <QTimer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <memory>

namespace {
constexpr qint64 kManifestLimit = 1024 * 1024;
QUrl workerBase()
{
    QFile file(QStringLiteral(":/tankoyomi/manifest.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto transport = QJsonDocument::fromJson(file.readAll()).object()
                               .value(QStringLiteral("transport")).toObject();
    const QUrl base(transport.value(QStringLiteral("baseUrl")).toString());
    if (transport.value(QStringLiteral("type")) != QLatin1String("https-json")
        || !base.isValid() || base.scheme() != QLatin1String("https") || base.host().isEmpty()
        || !base.userInfo().isEmpty() || base.hasQuery() || base.hasFragment()
        || base.port(443) != 443 || (!base.path().isEmpty() && base.path() != QLatin1String("/"))) return {};
    return base;
}
QString manifestCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/tankoyomi/manifest-v1.json");
}
bool validRemoteManifest(const QByteArray &data)
{
    const auto root = QJsonDocument::fromJson(data).object();
    return data.size() <= kManifestLimit
        && root.value(QStringLiteral("id")) == QLatin1String("colosseum.well.tankoyomi")
        && root.value(QStringLiteral("contractVersion")).toInt() == 1
        && TankoyomiProviderRegistry(data).isValid();
}
TankoyomiProviderRegistry initialRegistry()
{
    if (!workerBase().isEmpty()) {
        QFile cached(manifestCachePath());
        if (cached.open(QIODevice::ReadOnly) && cached.size() <= kManifestLimit) {
            const QByteArray data = cached.readAll();
            if (validRemoteManifest(data)) return TankoyomiProviderRegistry(data);
        }
    }
    return TankoyomiProviderRegistry::fromResource();
}
QString searchTitleForQuery(const TankoyomiSeriesQuery &query)
{
    if (!query.discoveryTitle.trimmed().isEmpty()) return query.discoveryTitle.trimmed();
    if (!query.title.trimmed().isEmpty()) return query.title.trimmed();
    for (const QString &alias : query.aliases) {
        if (!alias.trimmed().isEmpty()) return alias.trimmed();
    }
    return {};
}
}


TankoyomiChapterService::TankoyomiChapterService(QNetworkAccessManager *nam, QObject *parent)
    : TankoyomiChapterService(nam, nullptr, parent)
{
}

TankoyomiChapterService::TankoyomiChapterService(
    QNetworkAccessManager *nam, TankoyomiConfigurationStore *configuration, QObject *parent)
    : TankoyomiChapterService(nam, configuration, 50000, parent)
{
}

TankoyomiChapterService::TankoyomiChapterService(
    QNetworkAccessManager *nam, TankoyomiConfigurationStore *configuration,
    int providerAttemptTimeoutMs, QObject *parent)
    : TankoyomiChapterService(nam, configuration, providerAttemptTimeoutMs,
                              MangaImageHostResolver::Lookup(), parent)
{
}

TankoyomiChapterService::TankoyomiChapterService(
    QNetworkAccessManager *nam, TankoyomiConfigurationStore *configuration,
    int providerAttemptTimeoutMs, MangaImageHostResolver::Lookup resolverLookup,
    QObject *parent)
    : TankoyomiChapterService(nam, configuration, providerAttemptTimeoutMs, std::move(resolverLookup),
                              initialRegistry(), workerBase(), parent)
{
}

TankoyomiChapterService::TankoyomiChapterService(
    QNetworkAccessManager *nam, TankoyomiConfigurationStore *configuration,
    int providerAttemptTimeoutMs, MangaImageHostResolver::Lookup resolverLookup,
    const TankoyomiProviderRegistry &registry, const QUrl &remoteBase, QObject *parent)
    : QObject(parent),
      m_registry(registry),
      m_nam(nam ? nam : new QNetworkAccessManager(this)),
      m_workerBase(remoteBase),
      m_providerAttemptTimeoutMs(qMax(1, providerAttemptTimeoutMs))
{
    if (!m_registry.isValid()) return;
    m_configuration = configuration;
    if (!m_configuration)
        m_configuration = new TankoyomiConfigurationStore(m_registry, this);

    addProviders(std::move(resolverLookup));
    if (!m_workerBase.isEmpty()) {
        qInfo() << "[tankoyomi] provider execution: Worker with a signed device addon for MangaSin" << m_workerBase.host();
        QTimer::singleShot(0, this, &TankoyomiChapterService::refreshRemoteRegistry);
    }
}

void TankoyomiChapterService::addProviders(MangaImageHostResolver::Lookup resolverLookup)
{
    const QVariantList languageRows = m_registry.languages();
    for (const QVariant &languageValue : languageRows) {
        const QString language = languageValue.toMap().value(QStringLiteral("code")).toString();
        // The legacy m_registry.providersForLanguage(language) projection
        // remains manifest-enabled-only; construction needs the complete
        // validated inventory so a user can enable a manifest-disabled source.
        for (const TankoyomiProviderDescriptor &descriptor
              : m_registry.allProvidersForLanguage(language)) {
            const QString key = providerKey(descriptor.language, descriptor.id);
            if (m_providers.contains(key)) continue;
            auto *provider = m_workerBase.isEmpty()
                ? new TankoyomiScriptProvider(descriptor.id, descriptor.language, descriptor.resourcePath,
                      descriptor.allowedHosts, m_nam, resolverLookup, this)
                : new TankoyomiScriptProvider(descriptor.id, descriptor.language, m_workerBase, m_nam, this);
            m_providers.insert(key, provider);
        }
    }
}

void TankoyomiChapterService::refreshRemoteRegistry()
{
    QNetworkRequest request(m_workerBase.resolved(QUrl(QStringLiteral("/manifest.json"))));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(10000);
    auto *reply = m_nam->get(request);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, reply, [reply, bytes] {
        bytes->append(reply->read(qMax<qint64>(0, kManifestLimit + 1 - bytes->size())));
        if (bytes->size() > kManifestLimit) reply->abort();
    });
    QTimer::singleShot(15000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes] {
        bytes->append(reply->read(qMax<qint64>(0, kManifestLimit + 1 - bytes->size())));
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200
            || !validRemoteManifest(*bytes)) {
            qWarning() << "[tankoyomi] remote inventory unavailable; retaining cached inventory";
            return;
        }
        m_registry = TankoyomiProviderRegistry(*bytes);
        addProviders();
        m_configuration->replaceRegistry(m_registry);
        const QString path = manifestCachePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile cache(path);
        if (cache.open(QIODevice::WriteOnly) && cache.write(*bytes) == bytes->size()) cache.commit();
        qInfo() << "[tankoyomi] Worker inventory refreshed" << m_workerBase.host();
    });
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
    const std::optional<QString> resolved = m_registry.resolveLanguage(language);
    return (resolved.has_value() ? resolved.value()
                                 : TankoyomiProviderRegistry::normalizeLanguage(language))
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
    TankoyomiSeriesQuery query;
    query.title = title;
    fetchCatalogue(requestId, query, language);
}

void TankoyomiChapterService::fetchCatalogue(const QString &requestId,
                                             const TankoyomiSeriesQuery &query,
                                             const QString &language)
{
    if (!m_registry.isValid()) {
        emit catalogueFailed(requestId, m_registry.error());
        return;
    }
    // Resolve the requested tag to the canonical installed language before any
    // provider key or chain is built; a regional request like pt-BR must land on
    // the stable installed code while unsupported or ambiguous tags fail here.
    // An empty request keeps its historical meaning: the configured default.
    const std::optional<QString> resolved = m_registry.resolveLanguage(
        language.trimmed().isEmpty() ? m_configuration->defaultLanguage() : language);
    if (!resolved.has_value()) {
        emit catalogueFailed(
            requestId,
            QStringLiteral("No Tankoyomi chapter provider is configured for language '%1'.")
                .arg(language.trimmed()));
        return;
    }
    const QString normalized = resolved.value();
    const QList<TankoyomiProviderDescriptor> providers = candidateProviders(normalized);
    if (providers.isEmpty()) {
        emit catalogueFailed(
            requestId,
            QStringLiteral("No Tankoyomi chapter provider is configured for language '%1'.")
                .arg(normalized.isEmpty() ? language : normalized));
        return;
    }
    if (searchTitleForQuery(query).isEmpty()) {
        emit catalogueFailed(requestId, QStringLiteral("No series title was provided."));
        return;
    }
    tryProviderChain(requestId, query, normalized, providers);
}

void TankoyomiChapterService::tryProviderChain(
    const QString &requestId,
    const TankoyomiSeriesQuery &query,
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
            tryProviderChain(requestId, query, language, providers, index + 1);
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

    auto failOrFallback = [this, scope, state, requestId, query, language, providers, index]
                          (const QString &message) {
        if (state->settled) return;
        state->settled = true;
        scope->deleteLater();
        if (index + 1 < providers.size())
            tryProviderChain(requestId, query, language, providers, index + 1);
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
            [this, scope, state, provider, descriptor, requestId, query, language,
             searchToken, chaptersToken, failOrFallback](const QString &token,
                                                         const QVariant &value) {
        if (state->settled) return;
        if (token == searchToken) {
            const QVariantList results = value.toList();
            if (results.isEmpty()) {
                failOrFallback(QStringLiteral("Series not found on %1").arg(descriptor.name));
                return;
            }
            const auto matched = TankoyomiSeriesMatcher::match(query, results, descriptor.titleDecorators);
            if (!matched.accepted) {
                failOrFallback(QStringLiteral("Series identity unavailable on %1 (%2)")
                                   .arg(descriptor.name, matched.reason));
                return;
            }
            state->series = matched.row;
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

    auto *attemptDeadline = new QTimer(scope);
    attemptDeadline->setSingleShot(true);
    attemptDeadline->setTimerType(Qt::PreciseTimer);
    connect(attemptDeadline, &QTimer::timeout, scope,
            [state, failOrFallback, descriptor, language]() {
        if (state->settled) return;
        const QString timeout = QStringLiteral("Tankoyomi provider '%1' (%2) attempt timeout")
                                    .arg(descriptor.id, language);
        qWarning().noquote() << timeout;
        failOrFallback(timeout);
    });
    attemptDeadline->start(m_providerAttemptTimeoutMs);
    provider->searchSeries(searchToken, searchTitleForQuery(query));
}

QString TankoyomiChapterService::pageAccessPolicyForChapter(const QString &qualifiedChapterId) const
{
    const auto parsed = TankoyomiIdentity::parseChapter(qualifiedChapterId);
    if (!parsed) return {};
    for (const auto &provider : m_registry.allProvidersForLanguage(parsed->language)) {
        if (provider.id == parsed->providerId) return provider.pageAccessPolicy;
    }
    return {};
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
