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
//   Progress.forget(kind, id)   // drops the whole Continue tile: for a series, every
//                               //   episode of that show, not just the id passed in
//   Progress.revision              // bump on every change — name it in a binding to make
//                                  //   recent()-based bindings re-evaluate reactively.

#include <QObject>
#include <QSettings>
#include <QHash>
#include <QString>
#include <QStringList>
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

    // Test/diagnostic constructor: back the store with an explicit INI file instead of
    // the app's registry scope, so harnesses stay hermetic and never touch the user's
    // real Continue data. Mirrors SearchHistoryStore's path constructor.
    explicit ProgressStore(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent),
          m_settings(iniPath, QSettings::IniFormat) {
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
        const bool isSeriesEpisode =
            kind == QStringLiteral("video") && id.count(QLatin1Char(':')) >= 2;
        if (kind == QStringLiteral("video") && progress >= 0.90 && !isSeriesEpisode) {
            if (m_map.remove(key)) { save(); bump(); }
            return;
        }

        QVariantMap rec = entry;
        rec.insert(QStringLiteral("id"), id);
        rec.insert(QStringLiteral("kind"), kind);
        if (isSeriesEpisode && progress >= 0.90)
            rec.insert(QStringLiteral("watched"), true);
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

        QHash<QString, int> grouped;
        QVariantList deduped;
        for (const QVariant &entry : out) {
            const QVariantMap rec = entry.toMap();
            const QString group = continueGroupKey(rec);
            if (!grouped.contains(group)) {
                grouped.insert(group, deduped.size());
                deduped.append(entry);
                continue;
            }

            const int index = grouped.value(group);
            if (shouldPreferContinueCandidate(deduped.at(index).toMap(), rec))
                deduped[index] = entry;
        }
        std::sort(deduped.begin(), deduped.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("updatedAt")).toLongLong()
                 > b.toMap().value(QStringLiteral("updatedAt")).toLongLong();
        });
        out = deduped;

        if (limit > 0 && out.size() > limit)
            out = out.mid(0, limit);
        return out;
    }

    // Explicit removal — the "remove from Continue" affordance (the ✕ on a tile).
    // A Continue tile is one row PER GROUP: recent() collapses every episode of a series
    // into a single tile (see continueGroupKey). So forgetting a tile must drop the WHOLE
    // group — every episode of that series — not just the one episode the tile happened to
    // display. Removing a single episode would leave its siblings behind, and the next
    // recent() would re-surface an earlier one, so the show reappears. For movies and manga
    // the group IS the entry itself, so this stays a one-for-one removal.
    Q_INVOKABLE void forget(const QString &kind, const QString &id) {
        if (kind.isEmpty() || id.isEmpty())
            return;
        QVariantMap probe;
        probe.insert(QStringLiteral("kind"), kind);
        probe.insert(QStringLiteral("id"), id);
        const QString targetGroup = continueGroupKey(probe);

        QStringList doomed;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
            if (continueGroupKey(it.value().toMap()) == targetGroup)
                doomed.append(it.key());
        }
        if (doomed.isEmpty())
            return;
        for (const QString &key : doomed)
            m_map.remove(key);
        save();
        bump();
    }

    Q_INVOKABLE QVariantMap get(const QString &kind, const QString &id) const {
        return m_map.value(mapKey(kind, id)).toMap();
    }

    Q_INVOKABLE int lastSeason(const QString &seriesId) const {
        if (seriesId.isEmpty())
            return -1;
        return m_settings.value(QStringLiteral("video/lastSeason/") + seriesId, -1).toInt();
    }

    Q_INVOKABLE void rememberLastSeason(const QString &seriesId, int season) {
        if (seriesId.isEmpty() || season <= 0)
            return;
        const QString key = QStringLiteral("video/lastSeason/") + seriesId;
        if (m_settings.value(key, -1).toInt() == season)
            return;
        m_settings.setValue(key, season);
        m_settings.sync();
        bump();
    }

signals:
    void changed();

private:
    static QString mapKey(const QString &kind, const QString &id) {
        return kind + QStringLiteral("\x1f") + id;   // unit-separator: safe joiner
    }
    static QString seriesRootId(const QString &id) {
        if (id.count(QLatin1Char(':')) < 2)
            return id;
        const QStringList parts = id.split(QLatin1Char(':'));
        if (id.startsWith(QStringLiteral("tt")))
            return parts.value(0);
        return parts.value(0) + QLatin1Char(':') + parts.value(1);
    }
    static QString continueGroupKey(const QVariantMap &rec) {
        const QString kind = rec.value(QStringLiteral("kind")).toString();
        const QString id = rec.value(QStringLiteral("id")).toString();
        const QString groupId = kind == QStringLiteral("video") ? seriesRootId(id) : id;
        return mapKey(kind, groupId);
    }
    static bool shouldPreferContinueCandidate(const QVariantMap &current, const QVariantMap &candidate) {
        const bool currentWatched = current.value(QStringLiteral("watched")).toBool();
        const bool candidateWatched = candidate.value(QStringLiteral("watched")).toBool();
        if (currentWatched != candidateWatched)
            return !candidateWatched;
        return candidate.value(QStringLiteral("updatedAt")).toLongLong()
             > current.value(QStringLiteral("updatedAt")).toLongLong();
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
