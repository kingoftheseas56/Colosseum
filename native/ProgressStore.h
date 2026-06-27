#pragma once

// Progress — the continue / resume backbone exposed to QML as `Progress`.
// It is one small thing: a persisted note of "what you opened and how far you got."
// The player writes it as you watch; the manga reader writes it as you read; every
// Continue row reads it back. No network, no scraping — just memory + disk, like a
// bookmark file. Persisted via QSettings (the same lightweight-state mechanism the
// manga reader already uses for prefs), so it survives a restart.
//
// QML side (the only contract):
//   Progress.record({ id, kind, caption, title, sub, cover, c1, c2, progress, resume })
//   Progress.recent(kind, limit)   // kind "" = all (the unified home row); newest first
//   Progress.forget(kind, id)
//   Progress.revision              // bump on every change — name it in a binding to make
//                                  //   recent()-based bindings re-evaluate reactively.

#include <QObject>
#include <QSettings>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <algorithm>

class ProgressStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit ProgressStore(QObject *parent = nullptr)
        : QObject(parent),
          m_settings(QStringLiteral("Brotherhood"), QStringLiteral("Colosseum")) {
        load();
    }

    int revision() const { return m_revision; }

    // Upsert one resume entry. `entry` is a plain JS object from QML; `id` + `kind`
    // identify it, the rest is the latest display/resume payload. A video watched to
    // the end (>= 0.97) is dropped so Continue never shows finished films; reading
    // progress is per-chapter and never auto-dropped (finishing a chapter ≠ finishing
    // the series — use forget() for an explicit "remove from Continue").
    Q_INVOKABLE void record(const QVariantMap &entry) {
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const QString id   = entry.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || kind.isEmpty())
            return;
        const QString key = mapKey(kind, id);

        // Finished threshold matches Tankoban 2's proven StreamProgress::isFinished (>= 90%):
        // a film watched past 90% is "done" and drops off Continue. (TB2 advances series to the
        // next episode instead of dropping — a future enhancement here; for now we drop.)
        const double progress = entry.value(QStringLiteral("progress")).toDouble();
        if (kind == QStringLiteral("video") && progress >= 0.90) {
            if (m_map.remove(key) > 0) { save(); bump(); }
            return;
        }

        QVariantMap rec = entry;
        rec.insert(QStringLiteral("id"), id);
        rec.insert(QStringLiteral("kind"), kind);
        rec.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
        m_map.insert(key, rec);
        save();
        bump();
    }

    // Recent entries, newest first. kind "" → all kinds; limit <= 0 → no cap.
    Q_INVOKABLE QVariantList recent(const QString &kind = QString(), int limit = 0) const {
        QVariantList out;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
            const QVariantMap rec = it.value().toMap();
            if (!kind.isEmpty() && rec.value(QStringLiteral("kind")).toString() != kind)
                continue;
            out.append(rec);
        }
        std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("updatedAt")).toLongLong()
                 > b.toMap().value(QStringLiteral("updatedAt")).toLongLong();
        });
        if (limit > 0 && out.size() > limit)
            out = out.mid(0, limit);
        return out;
    }

    // Explicit removal — e.g. a future "remove from Continue" affordance.
    Q_INVOKABLE void forget(const QString &kind, const QString &id) {
        if (m_map.remove(mapKey(kind, id)) > 0) { save(); bump(); }
    }

signals:
    void changed();

private:
    static QString mapKey(const QString &kind, const QString &id) {
        return kind + QStringLiteral("\x1f") + id;   // unit-separator: safe joiner
    }
    void bump() { ++m_revision; emit changed(); }

    void load() {
        m_map.clear();
        const QByteArray blob =
            m_settings.value(QStringLiteral("continue/entries")).toByteArray();
        const QJsonDocument doc = QJsonDocument::fromJson(blob);
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
                m_map.insert(it.key(), it.value().toObject().toVariantMap());
        }
    }
    void save() {
        QJsonObject obj;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
            obj.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
        m_settings.setValue(QStringLiteral("continue/entries"),
                            QJsonDocument(obj).toJson(QJsonDocument::Compact));
        m_settings.sync();
    }

    QSettings m_settings;
    QHash<QString, QVariant> m_map;   // "kind\x1fid" → entry map
    int m_revision = 0;
};
