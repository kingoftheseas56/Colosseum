#pragma once

#include "TankoyomiProviderRegistry.h"
#include "TankoyomiConfigurationStore.h"
#include "TankoyomiSeriesMatcher.h"
#include "MangaImageHostResolver.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QUrl>
#include <QPointer>

class QNetworkAccessManager;
class TankoyomiScriptProvider;

class TankoyomiChapterService final : public QObject
{
    Q_OBJECT
public:
    explicit TankoyomiChapterService(QNetworkAccessManager *nam, QObject *parent = nullptr);
    TankoyomiChapterService(QNetworkAccessManager *nam,
                            TankoyomiConfigurationStore *configuration,
                            QObject *parent = nullptr);
    TankoyomiChapterService(QNetworkAccessManager *nam,
                            TankoyomiConfigurationStore *configuration,
                            int providerAttemptTimeoutMs,
                            QObject *parent = nullptr);
    TankoyomiChapterService(QNetworkAccessManager *nam,
                            TankoyomiConfigurationStore *configuration,
                            int providerAttemptTimeoutMs,
                            MangaImageHostResolver::Lookup resolverLookup,
                            QObject *parent = nullptr);

    void fetchCatalogue(const QString &requestId, const QString &title, const QString &language);
    // Explicit backend/inventory injection for embedders and deterministic transport checks.
    TankoyomiChapterService(QNetworkAccessManager *nam, TankoyomiConfigurationStore *configuration,
                            int providerAttemptTimeoutMs, MangaImageHostResolver::Lookup resolverLookup,
                            const TankoyomiProviderRegistry &registry, const QUrl &remoteBase,
                            QObject *parent = nullptr);
    void fetchCatalogue(const QString &requestId, const TankoyomiSeriesQuery &query, const QString &language);
    void fetchPages(const QString &requestId, const QString &qualifiedChapterId);
    QString pageAccessPolicyForChapter(const QString &qualifiedChapterId) const;
    QVariantList languages() const { return m_configuration ? m_configuration->languages() : QVariantList{}; }
    TankoyomiConfigurationStore *configuration() const { return m_configuration; }
    QList<TankoyomiProviderDescriptor> candidateProviders(const QString &language) const;

signals:
    void catalogueReady(const QString &requestId, const QString &sourceSeriesId,
                        const QVariantList &chapters);
    void catalogueFailed(const QString &requestId, const QString &message);
    void pagesReady(const QString &requestId, const QVariantList &pages);
    void pagesFailed(const QString &requestId, const QString &message);
private:
    void addProviders(MangaImageHostResolver::Lookup resolverLookup = {});
    void refreshRemoteRegistry();
    QString providerKey(const QString &language, const QString &providerId) const;
    TankoyomiScriptProvider *providerFor(const QString &language,
                                         const QString &providerId) const;
    void tryProviderChain(const QString &requestId,
                          const TankoyomiSeriesQuery &query,
                          const QString &language,
                          const QList<TankoyomiProviderDescriptor> &providers,
                          int index = 0);

    TankoyomiProviderRegistry m_registry;
    TankoyomiConfigurationStore *m_configuration = nullptr;
    QHash<QString, TankoyomiScriptProvider *> m_providers;
    QPointer<QNetworkAccessManager> m_nam;
    QUrl m_workerBase;
    int m_providerAttemptTimeoutMs = 50000;
};
