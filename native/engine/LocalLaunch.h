#pragma once
// LocalLaunch — the launch router (Slice 7). Given one OS-readable file, decide
// which surface opens it and whether it can be opened AT ALL, before any taskbar
// session is created (Preflight §6/§18): classify by extension, then let the
// destination backend validate — CBZ comics via CbzArchive, video via the decoded-
// frame admission probe, books by extension (Reader 2's backend stays
// authoritative at open). CBR stays classifiable for shelving but fails closed at launch until
// the comic reader has a real CBR page backend. An invalid file is rejected with a category and NO
// session; a valid one carries the vault content id its progress keys to.
//
// This is the pure decision layer. Wiring the doors themselves (ComicReader 2 /
// Reader 2 / Player 1, the Main.qml activateSession `vault:` branch, the
// PlayerPage local-subtitle gate, and the QML context-property registration)
// lands with the entry points (Slice 8) and reading/playing from the Vault
// (Slice 14); this slice ships the router + adapters and their tests.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "VaultRecent.h"

class VaultIdentity;

class LocalLaunch : public QObject
{
    Q_OBJECT

public:
    // vaultDir is <appdata>/vault in production, a QTemporaryDir in tests; empty →
    // the recent store is inert (no persistence). Static routing needs no instance.
    explicit LocalLaunch(QString vaultDir = QString(), QObject* parent = nullptr)
        : QObject(parent), m_recent(vaultDir) {}

    enum class Family { Unknown, Comic, Book, Video };
    enum class Reject { None, NotFound, Unsupported, Corrupt, NoDecoder };

    struct Route {
        Family family = Family::Unknown;
        bool accepted = false;
        Reject reject = Reject::Unsupported;
        QString vaultId; // "vault:<sha1>" — the session/progress key (accepted only)
        QString detail;
    };

    static Family classify(const QString& path); // by extension (VaultKit)
    static QString familyName(Family f);
    static QString rejectName(Reject r);

    // Backend validation. Comic: CBZ via CbzArchive; CBR deliberately fails closed until
    // Colosseum has a real in-place CBR reader. Video: the decoded-frame admission probe.
    // Book: accepted by extension here; the Arc 14 capability matrix must be runtime-verified
    // against Reader 2 before AZW3/DJVU are treated as supported launch formats.
    static bool validateComic(const QString& path);
    static bool validateVideo(const QString& path);
    static bool validateBook(const QString& path);

    // The full decision: classify -> validate -> route, rejecting before session.
    static Route route(const QString& path);

    // ── QML-facing open orchestration (Slice 8 entry points) ──────────────
    // `open` is fed the raw selections from the taskbar picker / an OS drag-drop /
    // Ctrl+O (file:// urls OR plain paths); it routes the FIRST one immediately and
    // stages the rest in the ephemeral Next-to-Open tray. C++ decides; QML opens
    // the door explicitly. { path, family, accepted, reject, vaultId, detail,
    // title, ignored, staged }
    Q_INVOKABLE QVariantMap routeInfo(const QString& pathOrUrl);
    Q_INVOKABLE QVariantMap open(const QStringList& pathsOrUrls);
    void setIdentity(VaultIdentity* identity) { m_identity = identity; }
    Q_INVOKABLE bool decideIdentityCeremony(const QString& relationship, const QString& choice);
    // Slice 20: temporary, non-persistent, never-auto-advancing Next-to-Open tray.
    Q_INVOKABLE QVariantList nextToOpenItems() const { return m_nextToOpen; }
    Q_INVOKABLE int stagedCount() const { return m_nextToOpen.size(); }
    Q_INVOKABLE QVariantMap openNextToOpen(int index);
    Q_INVOKABLE void removeNextToOpen(int index);

    // A dropped folder is NOT a file to route — the folder→Vault gesture is Slice 10, so
    // until then a folder drop shows an explain + "Select Media Files…" instead of
    // enumerating. QML uses this to tell a dropped folder from a dropped file.
    Q_INVOKABLE bool isDir(const QString& pathOrUrl) const;

    // ── Open Recent (Slice 9): the Open Media control remembers ──────────────
    // Every ACCEPTED open() records a move-to-front shortcut; the panel lists them
    // for one-click reopen, and clearRecent() wipes the shortcuts WITHOUT touching
    // reading progress (a separate store). recentChanged() rebinds the panel.
    Q_INVOKABLE QVariantList recentItems() const { return m_recent.items(); }
    Q_INVOKABLE void clearRecent();

signals:
    void recentChanged();
    void nextToOpenChanged();

private:
    QVariantMap openSingle(const QString& pathOrUrl);

    VaultRecent m_recent;
    QVariantList m_nextToOpen;
    VaultIdentity* m_identity = nullptr; // non-owning; shared with VaultLibrary
};
