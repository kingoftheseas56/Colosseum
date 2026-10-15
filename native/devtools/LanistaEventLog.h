#pragma once
// Rotating JSONL event stream — the bridge's companion (T2's v1.1 pattern).
// When polling is wasteful, agents tail events.jsonl instead.
#include <QJsonObject>
#include <QString>
#include <QStringList>

class LanistaEventLog
{
public:
    explicit LanistaEventLog(const QString& path);
    void append(QJsonObject event);              // stamps "at" itself
    QStringList tail(int limit) const;
    QString path() const { return m_path; }
private:
    QString m_path;
    static constexpr qint64 kRotateBytes = 5 * 1024 * 1024;
};
