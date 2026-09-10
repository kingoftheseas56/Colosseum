// BookStores.h
//
// Shared JSON store helpers, lifted out of BookBridge.cpp (the OLD reader's
// persistence) so the NEW reader (reader2) can read/write the same JSON shapes.
// The free functions below preserve the legacy installation-wide files. Reader2
// uses ScopedStore for profile-private records so one profile never sees another
// profile's progress, settings, bookmarks, or annotations.
//
// The legacy directory resolves via QStandardPaths::writableLocation(AppDataLocation)
// (+ "/book_reader"), same as before. Under QStandardPaths::setTestModeEnabled(true)
// (set by test harnesses) that location is automatically redirected to a sandbox,
// so tests never touch a real user's stores.

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace BookStores {

// A route-bound reader store. An empty root is sealed: reads return empty values
// and writes do nothing, and no directory is created. A non-empty root is created
// only for a write. The legacy constructor keeps the old installation-wide path
// for callers that still own the explicit local/legacy policy (including Vault's
// migration code); profile callers pass a managed profile root instead.
class ScopedStore final {
public:
    ScopedStore();

    void useLegacyRoot();
    void useProfileRoot(const QString &profileRoot);
    void seal();

    bool isSealed() const;
    QString root() const;

    QJsonObject readStore(const QString &fileName) const;
    void        writeStore(const QString &fileName, const QJsonObject &all) const;

    QJsonObject get(const QString &fileName, const QString &bookId) const;
    void        save(const QString &fileName, const QString &bookId,
                     const QJsonObject &data) const;

    QJsonArray  listGet(const QString &fileName, const QString &bookId) const;
    QJsonObject listSave(const QString &fileName, const QString &bookId,
                         QJsonObject item) const;
    QJsonObject listDelete(const QString &fileName, const QString &bookId,
                           const QString &itemId) const;
    void        listClear(const QString &fileName, const QString &bookId) const;

private:
    QString m_root;
};

// ── canonical store key ──
// SHA1[:20] of the path-normalized absolute path. This is the ONE place the
// fingerprint is derived: the old reader (BookBridge::progressKey) and the fresh
// reader (Reader2Bridge::bookKey) both delegate here, so the zero-migration promise
// can never drift between them.
QString keyFor(const QString& absPath);

// ── legacy installation-wide raw whole-file JSON object store ──
QJsonObject readStore(const QString& fileName);
void        writeStore(const QString& fileName, const QJsonObject& all);

// ── keyed single-object pattern (progress.json: bookId -> object) ──
QJsonObject get(const QString& fileName, const QString& bookId);
void        save(const QString& fileName, const QString& bookId, const QJsonObject& data);

// ── shared {bookId: [items]} list pattern (bookmarks.json / annotations.json) ──
QJsonArray  listGet(const QString& fileName, const QString& bookId);
QJsonObject listSave(const QString& fileName, const QString& bookId, QJsonObject item);
QJsonObject listDelete(const QString& fileName, const QString& bookId, const QString& itemId);
void        listClear(const QString& fileName, const QString& bookId);

} // namespace BookStores
