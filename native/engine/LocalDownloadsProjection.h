#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace LocalDownloadsProjection {

QString itemKind(const QVariantMap &item);
QString chapterLabel(const QString &chapterId, const QString &storedLabel);
bool canRedownload(const QVariantMap &item);
QVariantList aggregateSeries(const QVariantList &items, const QString &world);

} // namespace LocalDownloadsProjection
