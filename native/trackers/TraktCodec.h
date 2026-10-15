#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

#include "TrackerCredentialVault.h"

enum class TraktMediaKind : quint8 { Movie, Episode };

struct TraktMediaIdentity {
    TraktMediaKind kind = TraktMediaKind::Movie;
    QString traktId;
};

struct TraktRemoteFact {
    QString remoteMediaId;
    QString providerItemId;
    QString displayTitle;
    QStringList exactLocalIds;
    int progress = 0;
    bool completed = false;
    qint64 occurredAtMs = 0;
    QString fingerprint;
    bool historyEvidence = false;
    double exactProgress = 0.0;
};

std::optional<TraktMediaIdentity> traktParseRemoteMediaId(const QString &value);
QString traktRemoteMediaId(TraktMediaKind kind, const QString &traktId);
QJsonObject traktScrobbleBody(const QString &remoteMediaId, int progressHundredths);
QJsonObject traktHistoryBody(const QString &remoteMediaId, qint64 watchedAtMs);
QString traktSettingsUuid(const QJsonDocument &document);
QList<TraktRemoteFact> traktParseHistory(const QJsonDocument &document);
QList<TraktRemoteFact> traktParsePlayback(const QJsonDocument &document);
std::optional<TrackerCredential> traktRotatedCredential(const TrackerCredential &previous,
                                                        const QJsonDocument &document,
                                                        qint64 nowMs);
bool traktShouldSuppressLegacyRelay(bool directConnected, bool scrobbleCapable, bool liveEnabled);
