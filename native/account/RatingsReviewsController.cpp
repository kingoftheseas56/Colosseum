#include "RatingsReviewsController.h"

#include "ProfilePreferencesStore.h"
#include "ProfileStoreRuntime.h"
#include "RatingsReviewsDelivery.h"
#include "RatingsReviewsStore.h"
#include "../engine/ColosseumTitleIdentityRegistry.h"
#include "../engine/RatingsReviewsProviderReadProjection.h"

#include <QStringList>

namespace {
RatingsReviewsStore::Identity identityOf(
    const QString &world,
    const QString &kind,
    const QString &mediaId) {
    return {world, kind, mediaId};
}

QVariantList stringListVariants(const QStringList &values) {
    QVariantList out;
    for (const QString &value : values)
        out.append(value);
    return out;
}

QStringList variantStrings(const QVariantList &values) {
    QStringList out;
    for (const QVariant &value : values)
        out.append(value.toString());
    return out;
}
}

RatingsReviewsController::RatingsReviewsController(
    ProfileStoreRuntime *runtime,
    ColosseumTitleIdentityRegistry *identityRegistry,
    RatingsReviewsProviderReadProjection *providerRead,
    QObject *parent,
    RatingsReviewsDelivery *delivery)
    : QObject(parent),
      m_runtime(runtime),
      m_identityRegistry(identityRegistry),
      m_providerRead(providerRead),
      m_delivery(delivery) {
    setObjectName(QStringLiteral("ratingsReviewsController"));
    Q_ASSERT(m_runtime);
    Q_ASSERT(m_identityRegistry);
    Q_ASSERT(m_providerRead);

    connect(m_runtime, &ProfileStoreRuntime::storesAboutToChange,
            this, [this]() {
        ++m_profileGeneration;
        emit profileGenerationChanged();
        invalidateRoute();
    });
    connect(m_runtime, &ProfileStoreRuntime::storesChanged,
            this, [this]() {
        emit canonicalChanged();
    });
}

quint64 RatingsReviewsController::profileGeneration() const {
    return m_profileGeneration;
}

bool RatingsReviewsController::routeActive() const {
    return m_routeActive;
}

QVariantMap RatingsReviewsController::staleResult(const QString &code) const {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("errorCode"), code},
            {QStringLiteral("providerOperationCount"), 0}};
}

QVariantMap RatingsReviewsController::committedResult(
    bool ok,
    const QString &errorCode,
    bool changed,
    quint64 revision,
    const QString &recordKey) const {
    QVariantMap result{
        {QStringLiteral("ok"), ok},
        {QStringLiteral("errorCode"), errorCode},
        {QStringLiteral("changed"), changed},
        {QStringLiteral("revision"), QVariant::fromValue(revision)},
        {QStringLiteral("recordKey"), recordKey},
        // Saving is canonical-only. It cannot manufacture provider work.
        {QStringLiteral("providerOperationCount"), 0}
    };
    if (ok) {
        result.insert(QStringLiteral("canonical"), canonicalProjection());
        result.insert(QStringLiteral("delivery"), deliveryPresentation());
    }
    return result;
}

void RatingsReviewsController::invalidateRoute() {
    if (!m_routeActive)
        return;
    m_routeActive = false;
    m_routeContext.clear();
    emit routeActiveChanged();
    emit routeInvalidated();
}

bool RatingsReviewsController::matches(
    quint64 routeGeneration,
    quint64 profileGeneration) const {
    return m_routeActive
        && routeGeneration == m_routeGeneration
        && profileGeneration == m_profileGeneration;
}

QVariantMap RatingsReviewsController::open(
    const QVariantMap &context,
    quint64 routeGeneration) {
    const QVariantMap identity = context.value(QStringLiteral("identity")).toMap();
    const QString world = identity.value(QStringLiteral("world")).toString();
    const QString kind = identity.value(QStringLiteral("kind")).toString();
    const QString mediaId = identity.value(QStringLiteral("mediaId")).toString();

    if (!m_identityRegistry)
        return staleResult(QStringLiteral("identity_unavailable"));

    const QVariantMap resolvedIdentity =
        m_identityRegistry->resolve(world, kind, mediaId, {});
    if (!resolvedIdentity.value(QStringLiteral("available")).toBool()
        || resolvedIdentity.value(QStringLiteral("world")).toString() != world
        || resolvedIdentity.value(QStringLiteral("kind")).toString() != kind
        || resolvedIdentity.value(QStringLiteral("mediaId")).toString() != mediaId) {
        return staleResult(QStringLiteral("identity_unavailable"));
    }

    QString keyError;
    const QString key = RatingsReviewsStore::recordKeyForIdentity(
        identityOf(world, kind, mediaId), &keyError);
    if (key.isEmpty())
        return staleResult(QStringLiteral("identity_unavailable"));

    RatingsReviewsStore *store =
        m_runtime ? m_runtime->ratingsReviewsStore() : nullptr;
    if (!store)
        return staleResult(QStringLiteral("profile_unavailable"));

    m_world = world;
    m_kind = kind;
    m_mediaId = mediaId;
    m_routeContext = context;
    m_routeGeneration = routeGeneration;
    if (!m_routeActive) {
        m_routeActive = true;
        emit routeActiveChanged();
    }

    return {{QStringLiteral("ok"), true},
            {QStringLiteral("errorCode"), QString()},
            {QStringLiteral("profileGeneration"),
             QVariant::fromValue(m_profileGeneration)},
            {QStringLiteral("routeGeneration"),
             QVariant::fromValue(m_routeGeneration)},
            {QStringLiteral("canonical"), canonicalProjection()},
            {QStringLiteral("providers"), providerPresentation()},
            {QStringLiteral("delivery"), deliveryPresentation()},
            {QStringLiteral("providerOperationCount"), 0}};
}

void RatingsReviewsController::close(quint64 routeGeneration) {
    if (!m_routeActive || routeGeneration != m_routeGeneration)
        return;
    m_routeActive = false;
    m_routeContext.clear();
    emit routeActiveChanged();
}

QVariantMap RatingsReviewsController::canonicalProjection() const {
    RatingsReviewsStore *store =
        m_runtime ? m_runtime->ratingsReviewsStore() : nullptr;
    QVariantMap out{
        {QStringLiteral("hasRating"), false},
        {QStringLiteral("rating"), QVariant()},
        {QStringLiteral("hasReview"), false},
        {QStringLiteral("review"), QString()},
        {QStringLiteral("spoiler"), false},
        {QStringLiteral("revision"),
         QVariant::fromValue(store ? store->revision() : 0)}
    };
    if (!store || !m_routeActive)
        return out;

    const auto record =
        store->record(identityOf(m_world, m_kind, m_mediaId));
    if (!record)
        return out;

    if (record->rating) {
        out.insert(QStringLiteral("hasRating"), true);
        out.insert(QStringLiteral("rating"), *record->rating);
    }
    if (record->review) {
        out.insert(QStringLiteral("hasReview"), true);
        out.insert(QStringLiteral("review"), *record->review);
        out.insert(QStringLiteral("spoiler"), record->spoiler);
    }
    return out;
}

QVariantMap RatingsReviewsController::providerPresentation() const {
    if (!m_routeActive || !m_providerRead)
        return {};

    QVariantMap presentation = m_providerRead->presentation(
        m_world,
        m_kind,
        m_mediaId,
        m_routeContext.value(QStringLiteral("readIds")).toMap(),
        m_profileGeneration,
        m_routeGeneration);

    ProfilePreferencesStore *preferences =
        m_runtime ? m_runtime->preferencesStore() : nullptr;
    const QStringList saved = preferences
        ? preferences->ratingsReviewsProviderOrder() : QStringList{};
    const QVariantList normalized =
        m_providerRead->normalizeProviderOrder(stringListVariants(saved));
    presentation.insert(QStringLiteral("savedOrder"), normalized);

    const QVariantList aggregates =
        presentation.value(QStringLiteral("aggregates")).toList();
    QHash<QString, QVariantMap> aggregateByProvider;
    for (const QVariant &rowValue : aggregates) {
        const QVariantMap row = rowValue.toMap();
        aggregateByProvider.insert(
            row.value(QStringLiteral("providerId")).toString(), row);
    }

    QVariantList orderedAggregates;
    QVariantList visible;
    for (const QVariant &providerValue : normalized) {
        const QString providerId = providerValue.toString();
        const auto it = aggregateByProvider.constFind(providerId);
        if (it == aggregateByProvider.cend())
            continue;
        orderedAggregates.append(it.value());
        const QString state =
            it.value().value(QStringLiteral("state")).toString();
        if (state == QLatin1String("ready") || state == QLatin1String("error"))
            visible.append(providerId);
    }
    presentation.insert(QStringLiteral("aggregates"), orderedAggregates);
    presentation.insert(QStringLiteral("visibleOrder"), visible);
    return presentation;
}

QVariantMap RatingsReviewsController::deliveryPresentation() const {
    QVariantMap presentation{
        {QStringLiteral("available"), false},
        {QStringLiteral("providers"), QVariantList{}},
        {QStringLiteral("operations"), QVariantList{}}};
    if (!m_routeActive || !m_delivery)
        return presentation;

    QString keyError;
    const QString key = RatingsReviewsStore::recordKeyForIdentity(
        identityOf(m_world, m_kind, m_mediaId), &keyError);
    if (key.isEmpty())
        return presentation;

    const QVariantList providers = m_delivery->publishDestinations(key);
    presentation.insert(QStringLiteral("available"), !providers.isEmpty());
    presentation.insert(QStringLiteral("providers"), providers);
    presentation.insert(
        QStringLiteral("operations"), m_delivery->deliveryProjection(key));
    return presentation;
}

QVariantMap RatingsReviewsController::saveLocal(
    const QVariant &rating,
    const QVariant &review,
    bool spoiler,
    quint64 routeGeneration,
    quint64 profileGeneration) {
    if (!matches(routeGeneration, profileGeneration))
        return staleResult(QStringLiteral("stale_generation"));

    RatingsReviewsStore *store = m_runtime->ratingsReviewsStore();
    if (!store)
        return staleResult(QStringLiteral("profile_unavailable"));

    std::optional<double> canonicalRating;
    if (rating.isValid() && !rating.isNull()) {
        bool ok = false;
        const double value = rating.toDouble(&ok);
        if (!ok)
            return staleResult(QStringLiteral("invalid_rating"));
        canonicalRating = value;
    }

    std::optional<QString> canonicalReview;
    if (review.isValid() && !review.isNull())
        canonicalReview = review.toString();

    RatingsReviewsStore::CommitResult commit;
    QString error;
    const bool ok = store->saveLocal(
        identityOf(m_world, m_kind, m_mediaId),
        canonicalRating,
        canonicalReview,
        canonicalReview ? spoiler : false,
        &commit,
        &error);
    if (!ok)
        return committedResult(false, error);

    emit canonicalChanged();
    return committedResult(
        true, QString(), commit.changed, commit.revision, commit.recordKey);
}

QVariantMap RatingsReviewsController::clearRating(
    quint64 routeGeneration,
    quint64 profileGeneration) {
    if (!matches(routeGeneration, profileGeneration))
        return staleResult(QStringLiteral("stale_generation"));
    RatingsReviewsStore *store = m_runtime->ratingsReviewsStore();
    if (!store)
        return staleResult(QStringLiteral("profile_unavailable"));

    RatingsReviewsStore::CommitResult commit;
    QString error;
    const bool ok = store->clearRating(
        identityOf(m_world, m_kind, m_mediaId), &commit, &error);
    if (!ok)
        return committedResult(false, error);
    emit canonicalChanged();
    return committedResult(
        true, QString(), commit.changed, commit.revision, commit.recordKey);
}

QVariantMap RatingsReviewsController::deleteReview(
    quint64 routeGeneration,
    quint64 profileGeneration) {
    if (!matches(routeGeneration, profileGeneration))
        return staleResult(QStringLiteral("stale_generation"));
    RatingsReviewsStore *store = m_runtime->ratingsReviewsStore();
    if (!store)
        return staleResult(QStringLiteral("profile_unavailable"));

    RatingsReviewsStore::CommitResult commit;
    QString error;
    const bool ok = store->deleteReview(
        identityOf(m_world, m_kind, m_mediaId), &commit, &error);
    if (!ok)
        return committedResult(false, error);
    emit canonicalChanged();
    return committedResult(
        true, QString(), commit.changed, commit.revision, commit.recordKey);
}

QVariantMap RatingsReviewsController::publish(
    const QVariantList &destinations,
    quint64 routeGeneration,
    quint64 profileGeneration) {
    if (!matches(routeGeneration, profileGeneration))
        return staleResult(QStringLiteral("stale_generation"));
    RatingsReviewsStore *store = m_runtime->ratingsReviewsStore();
    if (!store || !m_delivery || !m_delivery->active())
        return staleResult(QStringLiteral("delivery_unavailable"));

    QString keyError;
    const QString key = RatingsReviewsStore::recordKeyForIdentity(
        identityOf(m_world, m_kind, m_mediaId), &keyError);
    const auto record = key.isEmpty() ? std::nullopt : store->recordByKey(key);
    if (!record)
        return staleResult(QStringLiteral("canonical_record_unavailable"));

    QList<RatingsReviewsPublishDestination> parsed;
    for (const QVariant &value : destinations) {
        const QVariantMap row = value.toMap();
        const QString providerId = row.value(QStringLiteral("providerId")).toString();
        if (providerId.isEmpty())
            continue;
        RatingsReviewsPublishDestination destination;
        destination.providerId = providerId;
        destination.ratingSelected = row.value(
            QStringLiteral("ratingSelected")).toBool();
        destination.reviewSelected = row.value(
            QStringLiteral("reviewSelected")).toBool();
        if (row.contains(QStringLiteral("shortenedReview"))
            && !row.value(QStringLiteral("shortenedReview")).isNull()) {
            destination.shortenedReview = row.value(
                QStringLiteral("shortenedReview")).toString();
        }
        parsed.append(destination);
    }
    if (parsed.isEmpty())
        return staleResult(QStringLiteral("no_destinations_selected"));

    const RatingsReviewsPublishResult published = m_delivery->publishCommitted({
        key,
        store->revision(),
        ratingsReviewsCanonicalPayloadDigestV1(*record),
        parsed});
    QVariantList providers;
    int operationCount = 0;
    bool accepted = false;
    QString firstReason;
    for (const RatingsReviewsPublishProviderResult &provider : published.providers) {
        QVariantList operationIds;
        for (const QString &operationId : provider.operationIds)
            operationIds.append(operationId);
        operationCount += operationIds.size();
        accepted = accepted || provider.accepted;
        if (firstReason.isEmpty() && !provider.reason.isEmpty())
            firstReason = provider.reason;
        providers.append(QVariantMap{
            {QStringLiteral("providerId"), provider.providerId},
            {QStringLiteral("accepted"), provider.accepted},
            {QStringLiteral("reason"), provider.reason},
            {QStringLiteral("operationIds"), operationIds}});
    }
    return {
        {QStringLiteral("ok"), accepted},
        {QStringLiteral("errorCode"), accepted ? QString() : firstReason},
        {QStringLiteral("providers"), providers},
        {QStringLiteral("providerOperationCount"), operationCount},
        {QStringLiteral("delivery"), deliveryPresentation()}};
}

QVariantMap RatingsReviewsController::retryDelivery(
    const QString &operationId,
    quint64 routeGeneration,
    quint64 profileGeneration) {
    if (!matches(routeGeneration, profileGeneration))
        return staleResult(QStringLiteral("stale_generation"));
    QString error;
    const bool ok = m_delivery
        && m_delivery->retryOperation(operationId, &error);
    return {
        {QStringLiteral("ok"), ok},
        {QStringLiteral("errorCode"), error},
        {QStringLiteral("providerOperationCount"), 0},
        {QStringLiteral("delivery"), deliveryPresentation()}};
}

QVariantMap RatingsReviewsController::reconcileDelivery(
    const QString &operationId,
    quint64 routeGeneration,
    quint64 profileGeneration) {
    if (!matches(routeGeneration, profileGeneration))
        return staleResult(QStringLiteral("stale_generation"));
    QString error;
    const bool ok = m_delivery
        && m_delivery->reconcileOperation(operationId, &error);
    return {
        {QStringLiteral("ok"), ok},
        {QStringLiteral("errorCode"), error},
        {QStringLiteral("providerOperationCount"), 0},
        {QStringLiteral("delivery"), deliveryPresentation()}};
}

QVariantMap RatingsReviewsController::moveProvider(
    const QString &providerId,
    int direction,
    const QVariantList &visibleOrder,
    quint64 routeGeneration,
    quint64 profileGeneration) {
    if (!matches(routeGeneration, profileGeneration))
        return staleResult(QStringLiteral("stale_generation"));
    ProfilePreferencesStore *preferences = m_runtime->preferencesStore();
    if (!preferences)
        return staleResult(QStringLiteral("profile_unavailable"));

    const QVariantList saved = stringListVariants(
        preferences->ratingsReviewsProviderOrder());
    const QVariantMap moved = m_providerRead->moveVisibleProvider(
        saved, visibleOrder, providerId, direction);
    if (!moved.value(QStringLiteral("changed")).toBool())
        return moved;

    const QStringList newOrder =
        variantStrings(moved.value(QStringLiteral("savedOrder")).toList());
    if (!preferences->setRatingsReviewsProviderOrder(newOrder))
        return staleResult(QStringLiteral("provider_order_save_failed"));

    QVariantMap committed = moved;
    committed.insert(
        QStringLiteral("savedOrder"),
        stringListVariants(preferences->ratingsReviewsProviderOrder()));
    committed.insert(QStringLiteral("ok"), true);
    committed.insert(QStringLiteral("providerOperationCount"), 0);
    return committed;
}

bool RatingsReviewsController::reviewSourceAvailable(const QString &providerId) const {
    return m_providerRead && m_providerRead->reviewSourceAvailable(providerId);
}

bool RatingsReviewsController::openReviewSource(const QString &providerId) {
    return m_providerRead && m_providerRead->openReviewSource(providerId);
}
