#pragma once

#include "TrackerDeliveryStore.h"

#include <QByteArray>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

#include <optional>

enum class MalMediaKind : quint8 {
    Anime,
    Manga
};

struct MalAuthConfiguration {
    QString clientId;
    QUrl authorizationEndpoint;
    QUrl tokenEndpoint;
    QUrl apiEndpoint;
    QUrl redirectUri;
    QUrl tokenBrokerEndpoint;
    QString appName;
    QString appVersion;

    bool usesBroker() const { return !tokenBrokerEndpoint.isEmpty(); }
};

struct MalListItem {
    MalMediaKind kind = MalMediaKind::Anime;
    QString malId;
    QString title;
    QString status;
    int progress = 0;
    int totalUnits = 0;
    qint64 updatedAtMs = 0;
};

struct MalRemoteIdentity {
    MalMediaKind kind = MalMediaKind::Anime;
    QString malId;
    int unitNumber = 0;
    bool listAggregate = false;

    QString remoteMediaId() const;
};

struct MalMutation {
    QString path;
    QUrlQuery form;
    int intendedProgress = 0;
    bool intendedCompleted = false;
};

bool malConfigurationIsValid(const MalAuthConfiguration &configuration);
std::optional<MalAuthConfiguration> malProductionConfiguration();

QByteArray malPkceVerifierFromEntropy(const QByteArray &entropy);
QByteArray malStateFromEntropy(const QByteArray &entropy);
bool malConstantTimeEqual(const QByteArray &left, const QByteArray &right);
QUrl malAuthorizationUrl(const MalAuthConfiguration &configuration,
                         const QByteArray &state,
                         const QByteArray &codeVerifier);

std::optional<MalRemoteIdentity> malRemoteIdentity(const QString &remoteMediaId);
std::optional<MalRemoteIdentity> malIdentityForCanonicalFact(
    const TrackerDeliveryFact &fact);
QString malRemoteMediaIdForCanonicalFact(const TrackerDeliveryFact &fact);

std::optional<MalMutation> malMutationForDelivery(
    const MalRemoteIdentity &identity,
    const TrackerDeliveryFact &fact,
    int totalUnits);

std::optional<MalListItem> malListItemFromJson(
    const QJsonObject &row,
    MalMediaKind kind);
QString malListKey(MalMediaKind kind, const QString &malId);
QString malSafeStatusLabel(const QString &status, MalMediaKind kind);
