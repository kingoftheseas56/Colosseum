#pragma once
// VaultRecent — the Open Media "recent files" store (execution plan Slice 9). A small
// file-backed list of the local files most recently opened, so the Open Media control can
// offer one-click reopen. Most-recent-first, deduped by normalized path (reopening a file
// moves it to the front), capped at kMax.
//
// It records ONLY shortcuts — path + cleaned title + kind + content id — NEVER reading
// progress, which lives in its own store. Clearing this wipes the shortcuts and leaves
// reading progress untouched (spec §2). File-backed JSON at <vaultDir>/open-recent.json,
// atomic + last-known-good via VaultStoreIo, seedable for tests. Pure Qt Core; the ctor
// takes the vault directory so a test points it at a QTemporaryDir and production passes
// <appdata>/vault — nothing here touches QStandardPaths.
#include <QJsonArray>
#include <QString>
#include <QVariantList>

class VaultRecent
{
public:
    explicit VaultRecent(QString vaultDir);

    static constexpr int kMax = 12;

    // Move-to-front record of an opened file (dedup by normalized path), capped at kMax.
    void record(const QString& path, const QString& title,
                const QString& kind, const QString& vaultId);
    // [{path, title, kind, vaultId, available}] most-recent-first. `available` is a live
    // QFileInfo::exists() check so a dead entry shows state and offers nothing false
    // (the recovery flow itself is Slice 16/21).
    QVariantList items() const;
    int count() const { return m_items.size(); }
    // Wipe the shortcuts. Does NOT touch reading progress (a separate store).
    void clear();

private:
    void load();
    void persist();
    static QString norm(const QString& path);

    QString m_dir;
    QJsonArray m_items;
};
