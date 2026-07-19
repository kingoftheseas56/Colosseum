#pragma once
// CollectionStore — the "Your Collection" shelf: what the user CHOSE to save via
// the + Library toggle. NOT Continue: distinct from ProgressStore (auto history) —
// an entry can exist unstarted and survives finishing. One store, three worlds.
//
// QML side (the only contract):
//   Collection.add(world, { id, type, title, cover, payload })   // upsert; stamps addedAt
//   Collection.remove(world, id)
//   Collection.has(world, id)
//   Collection.items(world)        // newest-first by addedAt
//   Collection.revision            // bump on every change — name it in a binding to make
//                                  //   has()/items()-based bindings re-evaluate reactively.
//
// `type` must ride on every entry (universe-tile law: a tile without type opens a
// series as a movie and dies). `payload` is the world-specific reopen snapshot.
// Persistence mirrors ProgressStore: one JSON blob under "collection/entries".

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

class CollectionStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit CollectionStore(QObject *parent = nullptr)
        : QObject(parent) {
        load();
    }

    // Test/diagnostic constructor: back the store with an explicit INI file so
    // harnesses stay hermetic. Mirrors ProgressStore's path constructor.
    explicit CollectionStore(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent),
          m_settings(iniPath, QSettings::IniFormat) {
        load();
    }

    int revision() const { return m_revision; }

    Q_INVOKABLE void add(const QString &world, const QVariantMap &entry) {
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (world.isEmpty() || id.isEmpty())
            return;
        QVariantMap e = entry;
        e.insert(QStringLiteral("world"), world);
        if (!e.value(QStringLiteral("addedAt")).toLongLong())
            e.insert(QStringLiteral("addedAt"), QDateTime::currentMSecsSinceEpoch());
        m_map.insert(mapKey(world, id), e);
        save();
        bump();
    }

    Q_INVOKABLE void remove(const QString &world, const QString &id) {
        if (m_map.remove(mapKey(world, id))) { save(); bump(); }
    }

    Q_INVOKABLE bool has(const QString &world, const QString &id) const {
        return m_map.contains(mapKey(world, id));
    }

    Q_INVOKABLE QVariantList items(const QString &world) const {
        QVariantList out;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
            const QVariantMap e = it.value().toMap();
            if (e.value(QStringLiteral("world")).toString() == world)
                out.append(e);
        }
        std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("addedAt")).toLongLong()
                 > b.toMap().value(QStringLiteral("addedAt")).toLongLong();
        });
        return out;
    }

signals:
    void changed();

private:
    static QString mapKey(const QString &world, const QString &id) {
        return world + QStringLiteral("\x1f") + id;   // unit-separator: safe joiner
    }
    void bump() { ++m_revision; emit changed(); }

    void load() {
        const QByteArray raw = m_settings.value(QStringLiteral("collection/entries")).toByteArray();
        if (raw.isEmpty())
            return;
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        const QJsonObject obj = doc.object();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            m_map.insert(it.key(), it.value().toObject().toVariantMap());
    }
    void save() {
        QJsonObject obj;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
            obj.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
        m_settings.setValue(QStringLiteral("collection/entries"),
                            QJsonDocument(obj).toJson(QJsonDocument::Compact));
        m_settings.sync();
    }

    QSettings m_settings;
    QHash<QString, QVariant> m_map;
    int m_revision = 0;
};
