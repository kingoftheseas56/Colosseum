#pragma once
#include <QObject>
#include <QHash>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class RatingsReviewsProviderReadProjection final : public QObject {
    Q_OBJECT
public:
    using UrlOpener = std::function<bool(const QUrl &)>;
    explicit RatingsReviewsProviderReadProjection(QObject *parent = nullptr);
    Q_INVOKABLE QVariantMap presentation(
        const QString &world, const QString &kind, const QString &mediaId,
        const QVariantMap &readIds, quint64 profileGeneration,
        quint64 routeGeneration);
    Q_INVOKABLE bool reviewSourceAvailable(const QString &providerId) const;
    Q_INVOKABLE bool openReviewSource(const QString &providerId);
    Q_INVOKABLE QVariantList normalizeProviderOrder(const QVariantList &savedOrder) const;
    Q_INVOKABLE QVariantMap moveVisibleProvider(
        const QVariantList &savedOrder, const QVariantList &visibleOrder,
        const QString &providerId, int direction) const;
    Q_INVOKABLE bool acceptsResult(
        const QString &world, const QString &kind, const QString &mediaId,
        quint64 profileGeneration, quint64 routeGeneration) const;
    static QStringList canonicalProviderIds();
    static bool isSafeSourceUrl(const QUrl &url);
    void setUrlOpenerForTests(UrlOpener opener);
private:
    QVariantMap emptyPresentation(
        const QString &world, const QString &kind, const QString &mediaId,
        quint64 profileGeneration, quint64 routeGeneration) const;
    QVariantMap fixturePresentation(
        const QString &variant, quint64 profileGeneration,
        quint64 routeGeneration);
    static QString displayName(const QString &providerId);
    static QVariantList stringsToVariants(const QStringList &values);
    QHash<QString, QUrl> m_reviewSources;
    UrlOpener m_urlOpener;
    QString m_world;
    QString m_kind;
    QString m_mediaId;
    quint64 m_profileGeneration = 0;
    quint64 m_routeGeneration = 0;
};
