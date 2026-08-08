#pragma once
// VaultIdentity — the content-addressed file identity registry (Slice 2). Gives
// every local file a stable id so its progress survives rename/move/restart
// (spec §8). The id is `vault:` + SHA-1 of `normalizedPath::size::mtimeMs`
// (Groundworks contract, decision 2 of the plan), so a plain rename yields a NEW
// computed id — and reconcile() re-attaches it: a known id whose file has
// vanished, matched by an UNIQUE fresh file of the same (size, mtimeMs)
// signature, is aliased to the new file so progress follows. Ambiguous matches
// (two candidates) are left parked, never silently merged — the "same content,
// new location" ceremony is Slice 21.
//
// File-backed JSON at <vaultDir>/identity.json via VaultStoreIo. Progress keys
// to resolve(computeId(...)); the recorded path aliases are the Reader 2 bridge
// hook (Slice 16 pairs them with BookStores::keyFor to migrate book state).

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class VaultIdentity : public QObject
{
    Q_OBJECT

public:
    explicit VaultIdentity(QString vaultDir, QObject* parent = nullptr);

    // Pure id helpers.
    static QString normalizePath(const QString& path); // cleanPath (+ toLower on Windows)
    static QString computeId(const QString& path, qint64 size, qint64 mtimeMs);

    // The id to key this file's progress with, registering it if new. At open
    // time an unknown file is fresh unless its computed id is already an alias
    // (a prior reconcile migrated it); rename detection itself is reconcile()'s.
    Q_INVOKABLE QString idForFile(const QString& path, qint64 size, qint64 mtimeMs);
    Q_INVOKABLE QString resolve(const QString& id) const; // alias -> canonical
    Q_INVOKABLE bool knows(const QString& id) const;

    // Batch reconcile against a fresh scan's file facts. Migrates unambiguous
    // renames/moves so progress follows; parks the ambiguous/vanished. Pure over
    // (facts, registry) — the Qt Test drives it directly, no disk churn.
    struct FileFacts {
        QString path;
        qint64 size = 0;
        qint64 mtimeMs = 0;
    };
    struct ReconcileResult {
        // each: [oldId, newComputedId, oldPath, newPath]
        QList<QStringList> migrated;
        QStringList fresh;  // newly registered ids
        QStringList parked; // known ids whose file vanished without a unique match
    };
    ReconcileResult reconcile(const QList<FileFacts>& current);

    // Path aliases recorded by migrations (the Reader 2 bridge hook).
    Q_INVOKABLE QVariantList pathAliases() const; // [{oldPath, newPath}]

signals:
    void changed();

private:
    struct Entry {
        QString id;
        QString path;
        qint64 size = 0;
        qint64 mtimeMs = 0;
    };

    void load();
    void persist();

    QHash<QString, Entry> m_byId;    // canonical id -> current entry
    QHash<QString, QString> m_alias; // computed id -> canonical id
    QList<QStringList> m_pathAliases; // [oldPath, newPath]
    QString m_dir;
};
