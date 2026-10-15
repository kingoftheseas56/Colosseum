#include "DebugLogBuffer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

DebugLogBuffer& DebugLogBuffer::instance()
{
    static DebugLogBuffer s_instance;
    return s_instance;
}

void DebugLogBuffer::info(const QString& source, const QString& message, const QJsonObject& details)
{
    append(Level::Info, source, message, details);
}

void DebugLogBuffer::warning(const QString& source, const QString& message, const QJsonObject& details)
{
    append(Level::Warning, source, message, details);
}

void DebugLogBuffer::error(const QString& source, const QString& message, const QJsonObject& details)
{
    append(Level::Error, source, message, details);
}

void DebugLogBuffer::append(Level level, const QString& source, const QString& message, const QJsonObject& details)
{
    Entry entry;
    entry.level = level;
    entry.source = source;
    entry.message = message;
    entry.timestampMs = QDateTime::currentMSecsSinceEpoch();
    entry.details = details;

    QMutexLocker lock(&m_mutex);

    m_entries.push_back(entry);
    while (static_cast<int>(m_entries.size()) > kCapacity) {
        m_entries.pop_front();
    }

    if (level == Level::Error) {
        m_lastError = entry;
        m_hasLastError = true;
    }
}

QJsonArray DebugLogBuffer::recent(int limit) const
{
    QMutexLocker lock(&m_mutex);

    const int safeLimit = qBound(1, limit, kCapacity);
    const int total = static_cast<int>(m_entries.size());
    const int start = qMax(0, total - safeLimit);

    QJsonArray arr;
    for (int i = start; i < total; ++i) {
        arr.append(toJson(m_entries[i]));
    }
    return arr;
}

QJsonObject DebugLogBuffer::lastError() const
{
    QMutexLocker lock(&m_mutex);
    return m_hasLastError ? toJson(m_lastError) : QJsonObject{};
}

void DebugLogBuffer::flushToDiskIfEnabled() const
{
    if (qgetenv("TANKOBAN_DEBUG_LOG") != "1") {
        return;
    }

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDir.isEmpty()) {
        return;  // No writable app-data location; cannot persist.
    }

    QDir dir(dataDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    const QString path = dir.absoluteFilePath("debug.log");
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;  // Silent failure — debug logging must never disrupt shutdown.
    }

    QJsonDocument doc;
    {
        QMutexLocker lock(&m_mutex);
        QJsonArray arr;
        for (const auto& entry : m_entries) {
            arr.append(toJson(entry));
        }
        doc.setArray(arr);
    }

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODate)
           << " session-end-flush\n"
           << QString::fromUtf8(doc.toJson(QJsonDocument::Compact))
           << "\n";
    file.close();
}

QJsonObject DebugLogBuffer::toJson(const Entry& entry)
{
    QJsonObject obj;
    obj["level"] = QString::fromLatin1(levelName(entry.level));
    obj["source"] = entry.source;
    obj["message"] = entry.message;
    obj["timestamp_ms"] = entry.timestampMs;
    if (!entry.details.isEmpty()) {
        obj["details"] = entry.details;
    }
    return obj;
}

const char* DebugLogBuffer::levelName(Level level)
{
    switch (level) {
    case Level::Info:    return "info";
    case Level::Warning: return "warning";
    case Level::Error:   return "error";
    }
    return "info";
}
