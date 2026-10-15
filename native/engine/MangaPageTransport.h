#pragma once

#include "MangaResult.h"

#include <QList>
#include <QNetworkRequest>
#include <QString>
#include <QVariantList>

namespace MangaPageTransport {

QList<PageInfo> normalizeTankoyomiPages(const QVariantList &rows,
                                        const QString &qualifiedChapterId);
QNetworkRequest requestForPage(const PageInfo &page,
                               const QString &chapterId);

} // namespace MangaPageTransport
