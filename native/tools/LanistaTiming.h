#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace lanista {

inline QJsonObject timingMilestone(const QString &name, qint64 atMs)
{
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("atMs"), atMs}};
}

inline QJsonObject timingStep(int index, const QString &label, qint64 durationMs, bool pass)
{
    return QJsonObject{{QStringLiteral("index"), index},
                       {QStringLiteral("label"), label},
                       {QStringLiteral("durationMs"), durationMs},
                       {QStringLiteral("pass"), pass}};
}

inline QJsonObject timingDocument(const QString &sessionId,
                                  const QJsonArray &milestones,
                                  const QJsonArray &steps)
{
    return QJsonObject{{QStringLiteral("schema"), QStringLiteral("colosseum.lanista.timings.v1")},
                       {QStringLiteral("sessionId"), sessionId},
                       {QStringLiteral("milestones"), milestones},
                       {QStringLiteral("steps"), steps}};
}

} // namespace lanista
