#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QPointer>
#include <QHash>
#include <QStringList>
#include <functional>

#include "BiblioCatalogTypes.h"
#include "VaultIndex.h"

class ComicsCatalog;
class ImdbCatalog;
class MalCatalog;
class VaultIndex;

// The Vault's certainty gate. This class owns the small, reversible identity
// transaction, while VaultIndex remains the durable source of truth.
class VaultIdentifier final : public QObject
{
    Q_OBJECT

public:
    struct Match {
        bool adopted = false;
        bool manualChoice = false;
        QString source;
        QString sourceId;
        QString title;
        QString type; // movie/series/mini only; unknown values remain empty.
        QString synopsis;
        QString coverUrl;
        QString world;
        int year = 0;
        // Phase-4 G1 ruling (2026-08-25): the adopted identity's IMDb rating (0 = the source
        // carried no fact) and its display-ready " · "-joined genre list (empty = none). The
        // detail sheet's provenance-badged meta line reads these — identified items only.
        double rating = 0;
        QString genres;
        // Slice 2 (browse-face execution plan): the total exact-candidate count from the last
        // matchGroup() catalogue lookup (comics+MAL, or IMDB alone) — 0 when no catalogue lookup
        // ran at all (e.g. the book path, or an ineligible group). >1 means the exactly-one
        // certainty gate correctly declined adoption for AMBIGUITY, not absence — the fact
        // recordAmbiguous() makes durable.
        int candidateCount = 0;
        BiblioIdentityLookup::Status bookLookupStatus = BiblioIdentityLookup::Status::Unavailable;
        QString bookLookupWarning;
        bool bookLibraryMatch = false;
        QString bookLookupReason;
        QList<BiblioIdentitySource> bookProvenance;
        BiblioIdentityCandidate bookWork;
        // Only generated catalogue matches carry these guards. Manual choices
        // keep their existing explicit-selection semantics.
        QString bookEvidenceFingerprint;
        quint64 bookLookupGeneration = 0;
        quint64 bookCatalogRevision = 0;
    };

    explicit VaultIdentifier(VaultIndex* index, ComicsCatalog* comics,
                             MalCatalog* mal, ImdbCatalog* imdb,
                             QObject* parent = nullptr);

    // Inject the existing Biblio service without giving Vault another database
    // or any refresh/network authority. The owner guards the callable's lifetime.
    void setBookCatalogLookup(QObject* owner,
        std::function<BiblioIdentityLookup(const QString&, const QString&)> lookup,
        std::function<BiblioIdentityLookup(const QString&, const QString&, const QString&)> online = {},
        std::function<bool(const BiblioIdentityCandidate&)> retain = {});
    void retryBookCatalog();

    Match matchGroup(const QString& groupKey) const;
    // Apply the same certainty gate to every existing group. The caller owns scheduling;
    // this method is a synchronous, testable pass over the current index snapshot.
    int autoIdentifyExisting();
    // Genre-only repair, independent of the identity/adoption gate. One attempt per
    // identity/session (including failures); successful facts persist in index-v1.
    using VideoGenreReply = std::function<void(const QVariantMap&)>;
    using VideoGenreLookup = std::function<void(const QString&, const QString&, VideoGenreReply)>;
    void setVideoGenreLookup(VideoGenreLookup lookup) { m_videoGenreLookup = std::move(lookup); }
    void backfillVideoGenres();
    static QString videoGenres(const QVariant& genres);
    static bool automaticPrivateEpub(const VaultIndex::FileRow& row);
    bool shouldAutoIdentify(const QList<VaultIndex::FileRow>& rows);
    // Empty means not junk; otherwise the size-and-shape reason for reversible filing.
    static QString junkClipReason(const VaultIndex::FileRow& row);
    bool applyGroup(const QString& groupKey, const Match& match);
    // Explicit manual selection: bypasses the auto-certainty matcher but retains the
    // same decorate-only write, preserving every file id and progress field.
    bool identifyGroupWith(const QString& groupKey, const Match& match);
    bool unidentifyGroup(const QString& groupKey);
    bool reshelveGroup(const QString& groupKey, const QString& kind);
    // Slice 2 (browse-face execution plan): record a durable "Vault isn't sure" fact when
    // matchGroup() found MORE THAN ONE exact catalogue candidate (auto-adoption correctly
    // declined by the exactly-one certainty gate — this does not change that gate). Never
    // overwrites an already-adopted or user-suppressed group. Owner-thread, decorate-only write
    // — same shape as applyGroup/unidentifyGroup; no independent off-thread SQLite writes.
    bool recordAmbiguous(const QString& groupKey, int candidateCount);

    // Files inside an Extras/Bonus/Featurettes/Behind-the-Scenes folder whose nearest ancestor
    // folder holds exactly one identified title are that title's extras: returns {fileId ->
    // parent identityId} for the unidentified video rows that qualify. Pure; the caller files them
    // through the existing physical-extras owner (VaultConfig::setExtrasParent).
    static QHash<QString, QString> physicalExtrasParents(const QList<VaultIndex::FileRow>& videoRows);

private:
    // A film whose own file name matched nothing, retried with its folder title (see matchGroup).
    QVariantList folderTitleHits(const QList<VaultIndex::FileRow>& rows, const VaultIndex::FileRow& row) const;
    void retryPending();
    void retryNextBookCatalog(quint64 generation);
    bool automaticOnlineBook(const VaultIndex::FileRow& row) const;
    bool snapshotBook(const VaultIndex::FileRow& row) const;
    const QList<VaultIndex::FileRow>& bookRowsAtRevision() const;
    mutable quint64 m_bookRowsRevision = static_cast<quint64>(-1);
    mutable QList<VaultIndex::FileRow> m_bookRows;
    mutable QString m_bookRowsFingerprint;
    QHash<QString, QString> m_epubAttempts; // one automatic migration probe per evidence fingerprint/session
    mutable QHash<QString, QString> m_pendingBookGroups; // group -> unchanged file evidence
    QStringList m_bookRetryKeys;
    bool m_bookRetryScheduled = false;
    QPointer<QObject> m_bookLookupOwner;
    std::function<BiblioIdentityLookup(const QString&, const QString&)> m_bookLookup;
    std::function<BiblioIdentityLookup(const QString&, const QString&, const QString&)> m_bookOnlineLookup;
    std::function<bool(const BiblioIdentityCandidate&)> m_bookRetain;
    quint64 m_bookLookupGeneration = 0;
    quint64 m_bookCatalogRevision = 0;
    mutable QSet<QString> m_pendingGroups;
    bool m_retryScheduled = false;
    VideoGenreLookup m_videoGenreLookup;
    QSet<QString> m_videoGenreAttempts;
    QHash<QString, QString> m_videoGenreFacts;
    VaultIndex* m_index = nullptr;
    ComicsCatalog* m_comics = nullptr;
    MalCatalog* m_mal = nullptr;
    ImdbCatalog* m_imdb = nullptr;
};
