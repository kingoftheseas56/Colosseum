#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

struct TankoyomiSeriesQuery {
    QString title;
    QString discoveryTitle;
    QStringList aliases;
    QStringList requiredTitleMarkers;
};
struct TankoyomiSeriesMatch {
    QVariantMap row;
    QString reason;
    bool accepted = false;
};
namespace TankoyomiSeriesMatcher {
QString foldTitle(const QString &title);
TankoyomiSeriesMatch match(const TankoyomiSeriesQuery &query,
                          const QVariantList &rows,
                          const QStringList &titleDecorators = {});
}
