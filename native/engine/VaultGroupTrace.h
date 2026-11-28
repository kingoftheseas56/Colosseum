#pragma once

#include "VaultIndex.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// Opt-in because paths and names are useful locally, and private. Trace only group
// decisions and never start a catalogue request or re-read the index for logging.
class VaultGroupTrace final
{
public:
    VaultGroupTrace(const char* event, const char* origin, const QString& key = {})
    {
        if (!enabled()) return;
        m_timer.start();
        set("event", QLatin1String(event));
        set("origin", QLatin1String(origin));
        set("group", key);
    }

    ~VaultGroupTrace()
    {
        if (!active()) return;
        set("durationUs", m_timer.nsecsElapsed() / 1000);
        qInfo().noquote() << "[vault-group]" << QJsonDocument(m_fields).toJson(QJsonDocument::Compact);
    }

    VaultGroupTrace(const VaultGroupTrace&) = delete;
    VaultGroupTrace& operator=(const VaultGroupTrace&) = delete;

    static bool enabled()
    {
        static const bool trace = qEnvironmentVariableIntValue("COLOSSEUM_VAULT_GROUP_TRACE") == 1;
        return trace;
    }

    bool active() const { return m_timer.isValid(); }

    void set(const char* key, const QJsonValue& value)
    {
        if (active()) m_fields.insert(QString::fromLatin1(key), value);
    }

    void reason(const char* value) { set("reason", QLatin1String(value)); }

    void rows(const QList<VaultIndex::FileRow>& rows)
    {
        if (!active()) return;
        set("members", rows.size());
        if (rows.isEmpty()) return;
        const auto& row = rows.constFirst();
        set("kind", row.kind);
        set("root", row.rootPath);
        set("representative", row.realName);
        set("groupTitle", row.groupTitle);
        set("parsedYear", row.parsedYear);
    }

    void provider(const char* name, bool available, bool searched, bool pending,
                  const QVariantList& candidates, const char* idKey, int rawCount)
    {
        if (!active()) return;
        QJsonArray sample;
        for (qsizetype i = 0; i < qMin(qsizetype(8), candidates.size()); ++i) {
            const auto hit = candidates.at(i).toMap();
            sample.append(QJsonObject{
                { "id", hit.value(QString::fromLatin1(idKey)).toString() },
                { "title", hit.value(QStringLiteral("title")).toString() },
                { "englishTitle", hit.value(QStringLiteral("title_english")).toString() },
                { "type", hit.value(QStringLiteral("type")).toString() },
                { "year", hit.value(QStringLiteral("year")).toInt() }
            });
        }
        set(name, QJsonObject{
            { "available", available }, { "searched", searched }, { "providerWaiting", pending },
            { "rawCount", rawCount }, { "candidateCount", candidates.size() }, { "sample", sample }
        });
    }

private:
    QElapsedTimer m_timer;
    QJsonObject m_fields;
};
