#include "devtools/LanistaEventLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

LanistaEventLog::LanistaEventLog(const QString& path) : m_path(path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
}

void LanistaEventLog::append(QJsonObject event)
{
    event.insert(QStringLiteral("at"),
                 QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    QFile f(m_path);
    if (f.size() > kRotateBytes) {                 // rotate: keep one predecessor
        QFile::remove(m_path + QStringLiteral(".1"));
        QFile::rename(m_path, m_path + QStringLiteral(".1"));
    }
    // Best-effort: if the file cannot be opened the mark is silently dropped
    // (this is a diagnostic log, never app state) — so an empty/absent
    // events.jsonl means "could not write", not a mystery.
    if (f.open(QIODevice::Append))
        f.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n");
}

QStringList LanistaEventLog::tail(int limit) const
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList lines;
    while (!f.atEnd()) lines.append(QString::fromUtf8(f.readLine()).trimmed());
    return lines.mid(qMax(0, int(lines.size()) - limit));
}
