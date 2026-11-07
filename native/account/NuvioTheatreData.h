#pragma once

#include "TheatreAccountImport.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

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
};

QList<TheatreAccountImport::Item> project(const Snapshot &snapshot, const Local &local);

struct Writes {
    QJsonArray library;
    QJsonArray watched;
    QJsonArray unwatchMovies;
    QJsonArray progress;
};

Writes reconcile(const Snapshot &snapshot, const Local &local);
}
