#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QString>

class ColosseumWebBridge;

namespace ComicDetailRoutes {
QVariantMap pack(ColosseumWebBridge &bridge, const QString &seriesId,
                 const QString &title, const QString &resumeUnitId);
QVariantMap universe(const QString &title, const QVariantList &posts, int year);
}
