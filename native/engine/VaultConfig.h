#pragma once
// VaultConfig — the user-intent store (Slice 2). Holds the decisions the user
// makes about their Vault: which folders are roots (and whether their founding
// card has been confirmed), per-subtree kind overrides (the card's chip
// reassignments), the scanIgnore needles, and hidden items. Never written by
// the scanner — that side owns the rebuildable index (VaultIndex, Slice 3); the
// config/index separation is the Groundworks contract.
//
// File-backed JSON at <vaultDir>/config.json, atomic + last-known-good via
// VaultStoreIo (recoverable if a write is torn). The constructor takes the vault
// directory so tests point it at a QTemporaryDir and production passes
// <appdata>/vault; nothing here touches QStandardPaths directly.

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class VaultConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit VaultConfig(QString vaultDir, QObject* parent = nullptr);

    int revision() const { return m_revision; }
    // True when the primary config was unreadable and the .bak was restored.
    Q_INVOKABLE bool recoveredFromBackup() const { return m_recovered; }

    // ── Roots ──
    // Each root row: {path, confirmed, addedAtMs, synthetic?, hidden?}. The
    // `synthetic` and `hidden` fields are optional (absent = false) so a legacy
    // config.json loads clean. `synthetic` marks the pre-confirmed trusted
    // downloads root (no card); `hidden` marks a root the user removed from the
    // strip — a hidden root is suppressed from publish + rootCount, never deleted.
    Q_INVOKABLE QVariantList roots() const;            // [{path, confirmed, addedAtMs, synthetic?, hidden?}]
    Q_INVOKABLE bool hasRoot(const QString& path) const;
    Q_INVOKABLE bool isRootConfirmed(const QString& path) const;
    Q_INVOKABLE void addRoot(const QString& path, qint64 addedAtMs = 0);
    Q_INVOKABLE void confirmRoot(const QString& path);
    // For a synthetic root this HIDES it (sets hidden=true); for a user root it
    // truly deletes. The synthetic root carries real files owned by the Downloads
    // lane, so removing it from the Vault must not delete those files.
    Q_INVOKABLE void removeRoot(const QString& path);

    // ── Synthetic / hidden-root mechanics (Slice 18 — downloads root) ──
    // Adds the trusted downloads root pre-confirmed (no card). Idempotent: a
    // second call with the same path is a no-op.
    Q_INVOKABLE void addSyntheticRoot(const QString& path, qint64 addedAtMs = 0);
    Q_INVOKABLE bool isSyntheticRoot(const QString& path) const;
    Q_INVOKABLE bool isRootHidden(const QString& path) const;
    Q_INVOKABLE void setRootHidden(const QString& path, bool hidden);
    // True delete — removes the root row entirely. Never auto-called in
    // production (the chip remove uses setRootHidden); exposed for tests + a
    // future "forget this root entirely" affordance.
    Q_INVOKABLE void removeRootCompletely(const QString& path);

    // ── Per-subtree kind overrides (card chip reassignments) ──
    Q_INVOKABLE void setKind(const QString& subtreePath, const QString& kind);
    Q_INVOKABLE QString kindFor(const QString& subtreePath) const; // "" if none
    // All overrides as {normSubtreePath → kind} — snapshot once on the GUI thread and
    // hand to the off-thread census so a confirm shelves by the user's chip choices.
    Q_INVOKABLE QVariantMap kindOverrides() const;

    // ── scanIgnore needles ──
    Q_INVOKABLE QStringList scanIgnore() const;
    Q_INVOKABLE void setScanIgnore(const QStringList& needles);

    // ── Hidden items (hide-don't-delete) ──
    Q_INVOKABLE bool isHidden(const QString& fileId) const;
    Q_INVOKABLE void setHidden(const QString& fileId, bool hidden);

signals:
    void changed();

private:
    void load();
    void persist();
    void ensureShape();
    static QString norm(const QString& path); // cleanPath (+ toLower on Windows)
    int rootIndex(const QString& normPath) const;

    QString m_dir;
    QJsonObject m_doc;
    int m_revision = 0;
    bool m_recovered = false;
};
