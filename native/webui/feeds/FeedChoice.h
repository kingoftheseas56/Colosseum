#pragma once

#include "FeedValue.h"

#include <QVariantMap>

namespace WebFeedChoice {

inline QVariantMap route(const QString &world, const QString &source,
                         const QVariantMap &facet, const QString &sort,
                         bool showExplicit, int pageSize)
{
    QVariantMap value{{QStringLiteral("v"), 1},
                      {QStringLiteral("world"), world},
                      {QStringLiteral("source"), source},
                      {QStringLiteral("explicit"), showExplicit},
                      {QStringLiteral("pageSize"), qBound(1, pageSize, 100)}};
    if (!facet.isEmpty()) value.insert(QStringLiteral("facet"), WebFeedValue::jsonMap(facet));
    if (!sort.isEmpty()) value.insert(QStringLiteral("sort"), sort);
    return value;
}

inline QVariantMap choice(const QString &key, const QString &label,
                          const QVariantMap &target, const QString &sublabel = {},
                          const QString &art = {}, bool removable = false)
{
    QVariantMap value{{QStringLiteral("key"), key},
                      {QStringLiteral("label"), label},
                      {QStringLiteral("target"), WebFeedValue::jsonMap(target)}};
    if (!sublabel.isEmpty()) value.insert(QStringLiteral("sublabel"), sublabel);
    if (!art.isEmpty()) value.insert(QStringLiteral("art"), art);
    if (removable) value.insert(QStringLiteral("removable"), true);
    return value;
}

inline QVariantMap section(const QString &id, int index, const QString &title,
                           const QString &layout, const QVariantList &choices,
                           const QString &state = QStringLiteral("ready"))
{
    QVariantMap value = WebFeedValue::section(id, index, title, layout, {}, state);
    value.insert(QStringLiteral("choices"), choices);
    return value;
}

} // namespace WebFeedChoice
