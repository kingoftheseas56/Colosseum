#pragma once

#include "TankoyomiProviderRegistry.h"
#include "TankoyomiConfigurationStore.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>

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

    void fetchCatalogue(const QString &requestId, const QString &title, const QString &language);
    void fetchPages(const QString &requestId, const QString &qualifiedChapterId);
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
    QString providerKey(const QString &language, const QString &providerId) const;
    TankoyomiScriptProvider *providerFor(const QString &language,
                                         const QString &providerId) const;
    void tryProviderChain(const QString &requestId,
                          const QString &title,
                          const QString &language,
                          const QList<TankoyomiProviderDescriptor> &providers,
                          int index = 0);

    TankoyomiProviderRegistry m_registry;
    TankoyomiConfigurationStore *m_configuration = nullptr;
    QHash<QString, TankoyomiScriptProvider *> m_providers;
};
