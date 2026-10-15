#pragma once

#include <QObject>
#include <QVariantMap>

class ColosseumTitleIdentityRegistry;
class ProfileStoreRuntime;
class RatingsReviewsDelivery;
class RatingsReviewsProviderReadProjection;

class RatingsReviewsController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint64 profileGeneration READ profileGeneration NOTIFY profileGenerationChanged)
    Q_PROPERTY(bool routeActive READ routeActive NOTIFY routeActiveChanged)

public:
    explicit RatingsReviewsController(
        ProfileStoreRuntime *runtime,
        ColosseumTitleIdentityRegistry *identityRegistry,
        RatingsReviewsProviderReadProjection *providerRead,
        QObject *parent = nullptr,
        RatingsReviewsDelivery *delivery = nullptr);

    quint64 profileGeneration() const;
    bool routeActive() const;

    Q_INVOKABLE QVariantMap open(
        const QVariantMap &context,
        quint64 routeGeneration);
    Q_INVOKABLE void close(quint64 routeGeneration);
    Q_INVOKABLE QVariantMap canonicalProjection() const;
    Q_INVOKABLE QVariantMap providerPresentation() const;
    Q_INVOKABLE QVariantMap deliveryPresentation() const;

    Q_INVOKABLE QVariantMap saveLocal(
        const QVariant &rating,
        const QVariant &review,
        bool spoiler,
        quint64 routeGeneration,
        quint64 profileGeneration);
    Q_INVOKABLE QVariantMap clearRating(
        quint64 routeGeneration,
        quint64 profileGeneration);
    Q_INVOKABLE QVariantMap deleteReview(
        quint64 routeGeneration,
        quint64 profileGeneration);
    Q_INVOKABLE QVariantMap publish(
        const QVariantList &destinations,
        quint64 routeGeneration,
        quint64 profileGeneration);
    Q_INVOKABLE QVariantMap retryDelivery(
        const QString &operationId,
        quint64 routeGeneration,
        quint64 profileGeneration);
    Q_INVOKABLE QVariantMap reconcileDelivery(
        const QString &operationId,
        quint64 routeGeneration,
        quint64 profileGeneration);

    Q_INVOKABLE QVariantMap moveProvider(
        const QString &providerId,
        int direction,
        const QVariantList &visibleOrder,
        quint64 routeGeneration,
        quint64 profileGeneration);
    Q_INVOKABLE bool reviewSourceAvailable(const QString &providerId) const;
    Q_INVOKABLE bool openReviewSource(const QString &providerId);

signals:
    void profileGenerationChanged();
    void routeActiveChanged();
    void routeInvalidated();
    void canonicalChanged();

private:
    bool matches(
        quint64 routeGeneration,
        quint64 profileGeneration) const;
    QVariantMap staleResult(const QString &code) const;
    QVariantMap committedResult(
        bool ok,
        const QString &errorCode,
        bool changed = false,
        quint64 revision = 0,
        const QString &recordKey = QString()) const;
    void invalidateRoute();

    ProfileStoreRuntime *m_runtime = nullptr;
    ColosseumTitleIdentityRegistry *m_identityRegistry = nullptr;
    RatingsReviewsProviderReadProjection *m_providerRead = nullptr;
    RatingsReviewsDelivery *m_delivery = nullptr;
    quint64 m_profileGeneration = 1;
    quint64 m_routeGeneration = 0;
    bool m_routeActive = false;
    QString m_world;
    QString m_kind;
    QString m_mediaId;
    QVariantMap m_routeContext;
};
