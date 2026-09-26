#pragma once

#include "RatingsReviewsDeliveryTypes.h"
#include "RatingsReviewsStore.h"

#include <QObject>
#include <QVariantList>

#include <functional>
#include <memory>
#include <optional>

class RatingsReviewsDeliveryOutbox;
class RatingsReviewsDeliveryReceiptStore;
class RatingsReviewsProviderMappingStore;
class ProfilePreferencesStore;

struct RatingsReviewsPublishDestination final {
    QString providerId;
    bool ratingSelected = false;
    bool reviewSelected = false;
    std::optional<QString> shortenedReview;
};

struct RatingsReviewsPublishIntent final {
    QString canonicalKey;
    quint64 committedStoreRevision = 0;
    QString canonicalPayloadDigest;
    QList<RatingsReviewsPublishDestination> destinations;
};

struct RatingsReviewsPublishProviderResult final {
    QString providerId;
    bool accepted = false;
    QString reason;
    QList<QString> operationIds;
};

struct RatingsReviewsPublishResult final {
    QList<RatingsReviewsPublishProviderResult> providers;
};

struct RatingsReviewsPrivatePaths final {
    QString mappingsPath;
    QString outboxPath;
    QString receiptsPath;
};

struct RatingsReviewsDeliveryActivation final {
    QString profileId;
    RatingsReviewsPrivatePaths privatePaths;
    RatingsReviewsStore *canonicalStore = nullptr;
};

struct RatingsReviewsPrivateProfileBinding final {
    QString profileId;
    QString preferencesIniPath;
    RatingsReviewsPrivatePaths privatePaths;
};

struct RatingsReviewsPrivateAdoptionCallbacks final {
    std::function<bool(
        const RatingsReviewsPrivateProfileBinding &source,
        const RatingsReviewsPrivateProfileBinding &destination,
        RatingsReviewsStore *destinationCanonical,
        QString *error)> handoff;
};

struct RatingsReviewsDeliveryProviderCapability final {
    bool connected = false;
    quint64 connectionGeneration = 0;
    bool ratingCapable = false;
    bool reviewCapable = false;
    bool spoilerMetadata = false;
    QString conversionMapDigest;
    QJsonValue translatedRating;
    int reviewTextLimit = 0;
};

class RatingsReviewsDeliveryAdapter
{
public:
    enum class SendOutcome {
        Success,
        KnownProviderFailure,
        NeedsAttention,
        TransientBeforeSend,
        UnknownAfterDispatch,
    };
    enum class ReconcileOutcome {
        MatchesIntendedState,
        ProvesAbsent,
        NewerOrDifferentState,
        Indeterminate,
    };
    struct Result {
        SendOutcome outcome = SendOutcome::UnknownAfterDispatch;
        QString safeProviderStatus;
        QString safeErrorClass;
        QString safeRemoteVersion;
        std::optional<qint64> safeRemoteTimestampMs;
    };

    virtual ~RatingsReviewsDeliveryAdapter() = default;
    virtual Result send(const RatingsReviewsDeliveryOperation &operation) = 0;
    virtual ReconcileOutcome reconcile(const RatingsReviewsDeliveryOperation &operation) = 0;
};

QString ratingsReviewsCanonicalPayloadDigestV1(
    const RatingsReviewsStore::Record &record);

class RatingsReviewsDelivery final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY stateChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY stateChanged)
    Q_PROPERTY(int unknownCount READ unknownCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList providerRows READ providerRows NOTIFY stateChanged)
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    Q_PROPERTY(QString fixturePhase READ fixturePhase NOTIFY stateChanged)
    Q_PROPERTY(int fixtureARatingSendCount READ fixtureARatingSendCount NOTIFY stateChanged)
    Q_PROPERTY(int fixtureAReviewSendCount READ fixtureAReviewSendCount NOTIFY stateChanged)
    Q_PROPERTY(int fixtureBSendCount READ fixtureBSendCount NOTIFY stateChanged)
    Q_PROPERTY(int fixtureBReconcileCount READ fixtureBReconcileCount NOTIFY stateChanged)
    Q_PROPERTY(int fixtureSpoilerBlockedSendCount READ fixtureSpoilerBlockedSendCount NOTIFY stateChanged)
#endif

public:
    using Clock = std::function<qint64()>;

    explicit RatingsReviewsDelivery(Clock clock = {}, QObject *parent = nullptr);
    ~RatingsReviewsDelivery() override;

    bool activateProfile(const RatingsReviewsDeliveryActivation &activation,
                         QString *error = nullptr);
    void deactivateProfile();
    bool active() const;
    QString activeProfileId() const;
    quint64 activeProfileIncarnation() const;
    int pendingCount() const;
    int unknownCount() const;
    QVariantList providerRows() const;
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    QString fixturePhase() const;
    int fixtureARatingSendCount() const;
    int fixtureAReviewSendCount() const;
    int fixtureBSendCount() const;
    int fixtureBReconcileCount() const;
    int fixtureSpoilerBlockedSendCount() const;
#endif

    RatingsReviewsPublishResult publishCommitted(
        const RatingsReviewsPublishIntent &intent);
    bool retryOperation(const QString &operationId, QString *error = nullptr);
    bool reconcileOperation(const QString &operationId, QString *error = nullptr);
    QVariantList publishDestinations(const QString &canonicalKey);
    QVariantList deliveryProjection(const QString &canonicalKey) const;

    // This narrow injection seam is for deterministic test fixtures only.
    // Package 1 production registration deliberately supplies no adapters.
    void setFixtureProviderForTests(
        const QString &providerId,
        const RatingsReviewsDeliveryProviderCapability &capability,
        RatingsReviewsDeliveryAdapter *adapter);
    void setFixturePreferencesForTests(ProfilePreferencesStore *preferences);
    // The assembled-app fixture calls this only behind its exact tagged build
    // gate.  It cannot name a production provider or match a fuzzy title.
    void enableTaggedFixtureMappingForTests(
        const RatingsReviewsStore::Identity &identity);
    void clearFixtureProvidersForTests();

    static bool handoffPrivateState(
        const RatingsReviewsPrivateProfileBinding &source,
        const RatingsReviewsPrivateProfileBinding &destination,
        RatingsReviewsStore *destinationCanonical,
        QString *error = nullptr);

private:
    bool dispatchPending(const QString &operationId, QString *error = nullptr);
    bool markTerminal(
        RatingsReviewsDeliveryOperation *operation,
        const QString &state,
        const RatingsReviewsDeliveryAdapter::Result &result,
        bool reconciled,
        QString *error);
    bool operationIsCurrent(
        const RatingsReviewsDeliveryOperation &operation,
        QString *reason) const;
    bool hasUnknownBarrier(const RatingsReviewsDeliveryOperation &operation) const;
    std::optional<RatingsReviewsProviderMapping> mappingForFixtureTitle(
        const QString &providerId,
        const QString &canonicalKey,
        const RatingsReviewsStore::Record *record);
    std::optional<RatingsReviewsDeliveryOperation> operation(
        const QString &operationId) const;
    qint64 now() const;
    QString newOperationId() const;
    void replaceProjectionState();
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    void updateFixturePhase(bool spoilerBlocked = false);
    int fixtureSendCount(const QString &providerId) const;
    int fixtureReconcileCount(const QString &providerId) const;
#endif

signals:
    void stateChanged();

private:
    struct FixtureProvider {
        RatingsReviewsDeliveryProviderCapability capability;
        RatingsReviewsDeliveryAdapter *adapter = nullptr;
    };

    Clock m_clock;
    QString m_activeProfileId;
    quint64 m_activeIncarnation = 0;
    RatingsReviewsStore *m_canonicalStore = nullptr;
    std::unique_ptr<RatingsReviewsProviderMappingStore> m_mappingStore;
    std::unique_ptr<RatingsReviewsDeliveryOutbox> m_outbox;
    std::unique_ptr<RatingsReviewsDeliveryReceiptStore> m_receipts;
    QHash<QString, FixtureProvider> m_fixtureProviders;
    ProfilePreferencesStore *m_fixturePreferences = nullptr;
    std::optional<RatingsReviewsStore::Identity> m_taggedFixtureIdentity;
#ifdef COLOSSEUM_RATINGS_REVIEWS_TESTING
    QString m_fixturePhase = QStringLiteral("ready");
#endif
};
