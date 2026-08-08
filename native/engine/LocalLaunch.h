#pragma once
// LocalLaunch — the launch router (Slice 7). Given one OS-readable file, decide
// which surface opens it and whether it can be opened AT ALL, before any taskbar
// session is created (Preflight §6/§18): classify by extension, then let the
// destination backend validate — comics via CbzArchive, video via the decoded-
// frame admission probe, books by extension (Reader 2's backend stays
// authoritative at open). An invalid file is rejected with a category and NO
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
#include <QVariantMap>

class LocalLaunch : public QObject
{
    Q_OBJECT

public:
    explicit LocalLaunch(QObject* parent = nullptr) : QObject(parent) {}

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

    // Backend validation. Comic: CBZ via CbzArchive (CBR accepted by extension —
    // Colosseum has no general in-place CBR reader yet; content validation +
    // reading deferred). Video: the decoded-frame admission probe. Book: accepted
    // by extension (Reader 2 validates at open).
    static bool validateComic(const QString& path);
    static bool validateVideo(const QString& path);
    static bool validateBook(const QString& path);

    // The full decision: classify -> validate -> route, rejecting before session.
    static Route route(const QString& path);

    // ── QML-facing open orchestration (Slice 8 entry points) ──────────────
    // `open` is fed the raw selections from the taskbar picker / an OS drag-drop /
    // Ctrl+O (file:// urls OR plain paths); it routes the FIRST one — the
    // Next-to-Open tray is Slice 20, so the rest are honestly ignored — and hands
    // QML a ready-to-dispatch decision. C++ decides; QML opens the door.
    //   { path, family, accepted, reject, vaultId, detail, title, ignored }
    Q_INVOKABLE QVariantMap routeInfo(const QString& pathOrUrl) const;
    Q_INVOKABLE QVariantMap open(const QStringList& pathsOrUrls);

    // A dropped folder is NOT a file to route — the folder→Vault gesture is Slice 10, so
    // until then a folder drop shows an explain + "Select Media Files…" instead of
    // enumerating. QML uses this to tell a dropped folder from a dropped file.
    Q_INVOKABLE bool isDir(const QString& pathOrUrl) const;
};
