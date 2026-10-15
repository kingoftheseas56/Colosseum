#pragma once
// VaultStoreIo — the Vault stores' shared atomic JSON persistence (Slice 2).
//
// One place for the "write can't corrupt what's there" discipline both Vault
// stores need (spec §8): every write goes to a temp file, the current good file
// rotates to `<name>.bak` (last-known-good), then the temp is promoted; every
// read falls back to `.bak` and then to empty when the primary is unreadable.
// Header-only inline free functions — no extra translation unit, and the Qt
// Test exercises the recovery paths directly.

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

namespace VaultStoreIo {

// Parse a JSON object file; returns {} and sets *ok=false on missing/invalid.
inline QJsonObject parseObject(const QString& path, bool* ok)
{
    if (ok) *ok = false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray raw = f.readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    if (ok) *ok = true;
    return doc.object();
}

// Load <dir>/<name>, recovering from <name>.bak when the primary is
// missing/corrupt. `recovered` (out) is set true iff the .bak had to be used.
// Returns {} when neither is usable — a clean fresh start.
inline QJsonObject load(const QString& dir, const QString& name, bool* recovered = nullptr)
{
    if (recovered) *recovered = false;
    const QString main = QDir(dir).filePath(name);
    const QString bak  = main + QStringLiteral(".bak");

    bool ok = false;
    QJsonObject o = parseObject(main, &ok);
    if (ok)
        return o;
    o = parseObject(bak, &ok);
    if (ok) {
        if (recovered) *recovered = true;
        return o;
    }
    return {};
}

// Atomic save with last-known-good rotation: write `<name>.tmp`, rotate the
// current `<name>` to `<name>.bak`, then promote the temp to `<name>`. A crash
// between the rotate and the promote leaves the primary absent and the .bak
// good, so the next load() recovers.
inline bool save(const QString& dir, const QString& name, const QJsonObject& obj)
{
    QDir d(dir);
    if (!d.exists())
        QDir().mkpath(dir);

    const QString main = d.filePath(name);
    const QString bak  = main + QStringLiteral(".bak");
    const QString tmp  = main + QStringLiteral(".tmp");
    const QByteArray raw = QJsonDocument(obj).toJson(QJsonDocument::Indented);

    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        if (f.write(raw) != raw.size()) {
            f.close();
            QFile::remove(tmp);
            return false;
        }
        f.flush();
        f.close();
    }

    if (QFile::exists(main)) {
        QFile::remove(bak);
        QFile::rename(main, bak); // main -> bak (best-effort last-known-good)
    }
    QFile::remove(main);          // free the slot (no-op if the rename moved it)
    return QFile::rename(tmp, main);
}

} // namespace VaultStoreIo
