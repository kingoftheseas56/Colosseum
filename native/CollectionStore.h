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
#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <memory>

namespace {
// Isolation gate (2026-08-14 fix). See ProgressStore.h's progressStoreTaggedIniPath() for the
// full writeup: CollectionStore hardcoded the QSettings two-arg registry constructor
// (org="Brotherhood", app="Colosseum"), so a tagged/isolated Lanista test session read AND
// wrote the real user's Collection shelf regardless of COLOSSEUM_APPDATA_TAG. Named per-store
// (collectionStore...) because CollectionStore.h and ProgressStore.h are both #included into
// main.cpp, and two identically-named functions in unnamed namespaces would collide in that
// one translation unit. Untagged: returns an empty string, meaning "use the registry" — the
// daily app is byte-for-byte unaffected.
inline QString collectionStoreTaggedIniPath(const QString &storeFileName) {
    if (!qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG"))
        return QString();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + storeFileName;
}
}

class CollectionStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit CollectionStore(QObject *parent = nullptr)
        : QObject(parent) {
        // Tagged (isolated Lanista test) sessions divert to a file under the tag's own
        // AppData root; untagged (the daily app) is unchanged — registry, same keys.
        const QString tagged = collectionStoreTaggedIniPath(QStringLiteral("collection-store.ini"));
        m_settings = tagged.isEmpty()
            ? std::make_unique<QSettings>(QStringLiteral("Brotherhood"), QStringLiteral("Colosseum"))
            : std::make_unique<QSettings>(tagged, QSettings::IniFormat);
        load();
    }

    // Test/diagnostic constructor: back the store with an explicit INI file so
    // harnesses stay hermetic. Mirrors ProgressStore's path constructor.
    explicit CollectionStore(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent),
          m_settings(std::make_unique<QSettings>(iniPath, QSettings::IniFormat)) {
        load();
    }

    int revision() const { return m_revision; }

    // Native sync seam: complete authoritative Collection state, without changing
    // the QML contract or creating a second persistence authority. The sync
    // adapter performs the portable/local-only projection.
    QVariantList syncEntries() const {
        QStringList keys = m_map.keys();
        keys.sort();
        QVariantList out;
        out.reserve(keys.size());
        for (const QString &key : keys)
            out.append(m_map.value(key).toMap());
        return out;
    }

    // Callers must include `type` on every entry (enforced at the QML call sites,
    // Tasks 4+): the universe-tile law — a tile without type opens a series as a
    // movie and dies. The store persists whatever it's given.
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
        m_map.clear();
        const QByteArray raw = m_settings->value(QStringLiteral("collection/entries")).toByteArray();
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
        m_settings->setValue(QStringLiteral("collection/entries"),
                            QJsonDocument(obj).toJson(QJsonDocument::Compact));
        m_settings->sync();
    }

    // A pointer (not a plain member) because which backing store to build — registry vs. a
    // tagged isolation file — is a runtime decision; see collectionStoreTaggedIniPath() above
    // and CollectionStore's default constructor.
    std::unique_ptr<QSettings> m_settings;
    QHash<QString, QVariant> m_map;
    int m_revision = 0;
};
