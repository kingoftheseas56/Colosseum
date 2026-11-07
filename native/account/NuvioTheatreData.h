#pragma once

#include "TheatreAccountImport.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QStringList>

#include <optional>

namespace NuvioTheatre {
struct Profile {
    QString id;
    int index = 0;
    QString name;
    bool locked = true;
    bool usesPrimaryAddons = false;
};

QList<Profile> profiles(const QJsonArray &rows, const QString &accountId);

struct Snapshot {
    QJsonArray library;
    QJsonArray watched;
    QJsonArray progress;
    QJsonArray addons;
    // The documented full progress read is limited to 200 rows. Older
    // unknown remote rows cannot safely be overwritten from that window.
    qint64 progressWindowFloor = 0;
};

struct Local {
    QVariantList library;
    QVariantList progress;
    QVariantList history;
    QHash<QString, int> movieMarks;
    QHash<QString, qint64> movieMarkTimes;
    qint64 unwatchAfter = 0;
    QHash<QString, qint64> episodeRemovalTimes;
};

enum class RemovalKind { Library, Watched, Progress };

struct Removal {
    RemovalKind kind = RemovalKind::Library;
    QString id;
    QString type;
    QStringList contentIds;
    qint64 at = 0;
    qint64 confirmedAt = 0;
    bool wholeTitle = false;

    QJsonObject serialize() const;
    static std::optional<Removal> deserialize(const QJsonObject &row);
    bool sameTarget(const Removal &other) const;
    bool matches(const QJsonObject &row) const;
    bool blocks(const QJsonObject &row) const;
};

std::optional<Removal> makeRemoval(RemovalKind kind, const QString &id,
    const QString &type, bool wholeTitle, qint64 at, const Local &local);

// Keep an offline deletion effective while a stale provider copy is still visible.
void suppressRemoved(Snapshot &snapshot, const QList<Removal> &removals);
bool isSuppressed(RemovalKind kind, const QJsonObject &row, const QList<Removal> &removals);
QJsonArray deletionKeys(const Removal &removal, const QJsonArray &remoteRows);

QList<TheatreAccountImport::Item> project(const Snapshot &snapshot, const Local &local);

struct Writes {
    QJsonArray library;
    QJsonArray watched;
    QJsonArray unwatchMovies;
    QJsonArray progress;
};

Writes reconcile(const Snapshot &snapshot, const Local &local);
}
