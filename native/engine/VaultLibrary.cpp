#include "VaultLibrary.h"
#include "VaultIndex.h"
#include "VaultScanner.h"
#include "VaultConfig.h"
#include "VaultIdentity.h"
#include "VaultWatcher.h"
#include "VaultDownloadsRoot.h"
#include "ComicCoverId.h"
#include "VaultIdentifier.h"
#include "VaultKit.h"
#include "VaultBrowseAway.h"
#include "VaultBrowseDetail.h"
#include "VaultBrowseEmpty.h"
#include "VaultThumbnailer.h"
#include "VaultPosterFetcher.h"
#include "VaultArtworkResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QUrl>

// Mirror VaultConfig::norm so an offered-root key matches the normalized path in roots().
static QString normPath(const QString& p)
{
    QString n = QDir::cleanPath(p);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n;
}

// Browse-artwork execution plan, Slice 3 part 2: an Episode or Clip browse node's OWN `path` IS
// the video file (VaultKit::planBrowseLevel's loose-video leaf grammar) — never a group's
// folder-shaped groupKey/subtreePath the way a Film node's `n.path` is, so the Film branch's
// `rowsForGroup(n.path)` lookup cannot answer "what does the index know about this ONE file."
// VaultIndex::rowsForPath (added for this) closes that gap with an exact-path query, bounded to
// 0-or-1 rows in practice. Returns the resolver's answer, or "" when there's nothing yet (a miss
// still kicks that producer's async fetch/grab inside resolve() itself).
//
// Identify-catalogue routing fix: the caller now performs the rowsForPath() lookup itself (it also
// needs the row's stored `kind` for the projection) and hands the rows in, so a level of 300+
// episode leaves still costs exactly ONE exact-path query per leaf, not two.
static QString resolveVideoLeafCoverRef(const QList<VaultIndex::FileRow>& rows,
                                        VaultArtworkResolver* resolver, const QString& rowKey)
{
    if (!resolver || rows.isEmpty())
        return QString(); // not indexed yet (e.g. mid-scan) — honest "no facts", never invented
    const VaultIndex::FileRow& row = rows.first();
    VaultArtworkResolver::RowFacts facts;
    facts.rowKey = rowKey;
    facts.kind = QStringLiteral("video");
    facts.path = row.path;
    // VaultEnricher-adopted "file://" local artwork, if any — the row's own coverRef for a video
    // kind is already a ready-to-bind local ref (never an in-archive entry name, that's comic/book
    // only), same convention the Film branch and items()/series() already rely on.
    facts.localRef = row.coverRef;
    if (!row.identityId.isEmpty() && !row.identitySuppressed) {
        facts.identityId = row.identityId;
        facts.posterUrl = row.identityCoverUrl;
    }
    facts.size = row.size;
    facts.mtimeMs = row.mtimeMs;
    facts.durationSec = row.durationSec;
    return resolver->resolve(facts);
}

// The stored `kind` (comic|book|video) that a browse node inherits from the index rows underneath
// it. kind is DATA — VaultScanner's own per-file classification, persisted on every VaultIndex
// row — and is deliberately NOT re-derived from the structural nodeType: the identify flow
// branches on kind to pick a catalogue (IMDb for video, Comics/MAL otherwise), so a guess here is
// how a film ends up searched against comic catalogues. A mixed folder answers with its most
// common kind; the first row to reach that count wins a tie, which keeps one level's answer
// stable across calls. "" when nothing underneath is indexed yet (mid-scan) — an honest "not
// known", never an invented default.
static QString dominantRowKind(const QList<VaultIndex::FileRow>& rows)
{
    QMap<QString, int> tally;
    QString best;
    int bestCount = 0;
    for (const VaultIndex::FileRow& row : rows) {
        if (row.kind.isEmpty())
            continue;
        const int seen = ++tally[row.kind];
        if (seen > bestCount) {
            bestCount = seen;
            best = row.kind;
        }
    }
    return best;
}

// Vault ux uplift S6: the node's durable vault id — the live Progress join key. The id is
// "vault:" + SHA-1 of normalizedPath::size::mtimeMs (VaultIdentity), so QML cannot derive it
// from the row's path alone; without it on the row, VaultApi.joinRow's progressFraction/
// progressed override (and ProgressStore.watchedMark for the watched tick) are structurally
// unreachable from the browse face. An Episode/Clip node's exact-path rows are the file itself;
// the first row carrying an id wins (a one-file leaf has exactly one).
static void insertLeafJoinId(QVariantMap& m, const QList<VaultIndex::FileRow>& rows)
{
    for (const VaultIndex::FileRow& row : rows) {
        if (!row.id.isEmpty()) {
            m.insert(QStringLiteral("id"), row.id);
            return;
        }
    }
}

VaultLibrary::VaultLibrary(VaultIndex* index, VaultScanner* scanner, VaultConfig* config,
                           VaultIdentity* identity, QString cacheDir, QObject* parent)
    : QObject(parent), m_index(index), m_scanner(scanner), m_config(config), m_identity(identity)
{
    // Browse-artwork execution plan, Slice 3 part 2 — owned, parented to `this` (same lifetime
    // discipline as m_watcher below). artResolved(rowKey) re-fires as the narrow
    // browseArtResolved(rowKey) signal (see its own doc in VaultLibrary.h) rather than touching
    // m_revision/changed() — a resolved thumbnail must not force every OTHER revision-gated
    // projection to redo its own work.
    m_thumbnailer = new VaultThumbnailer(cacheDir, this);
    m_posterFetcher = new VaultPosterFetcher(cacheDir, this);
    m_artworkResolver = new VaultArtworkResolver(m_thumbnailer, m_posterFetcher, this);
    connect(m_artworkResolver, &VaultArtworkResolver::artResolved, this,
            &VaultLibrary::browseArtResolved);

    // revision tracks committed index truth. VaultIndex::changed() fires after a successful
    // publish()/upsert() only, so a bump always means new published truth to repaint from.
    if (m_index) {
        connect(m_index, &VaultIndex::changed, this, [this]() {
            ++m_revision;
            emit changed();
            if (m_identifier)
                scheduleAutoIdentify();
        });
    }
    if (m_identity) {
        connect(m_identity, &VaultIdentity::changed, this,
                &VaultLibrary::identityCeremoniesChanged);
    }
    if (m_scanner) {
        // Live census progress → the scan pill.
        connect(m_scanner, &VaultScanner::progress, this,
                [this](const QString& root, int done, int total, const QString&) {
                    m_scanningRoot = root;
                    m_scanDone = done;
                    m_scanTotal = total;
                    emit scanProgressChanged();
                });
        // A finished census: cancelled → clear; otherwise the candidate raises the card.
        connect(m_scanner, &VaultScanner::scanFinished, this,
                [this](const QString& root, const QVariantList& slices, bool cancelled) {
                    setScanning(false);
                    if (cancelled) {
                        m_candidate.clear();
                        m_candidateRoot.clear();
                    } else {
                        m_candidate = slices;
                        m_candidateRoot = root;
                    }
                    emit candidateChanged();
                    maybePublishPendingRevives();
                });
        // A successful publish ends the shelving state (revision bumps via index.changed()).
        connect(m_scanner, &VaultScanner::indexPublished, this,
                [this](const QString&, int) {
                    setScanning(false);
                    m_revivalRescanInFlight = false;
                    m_pendingReviveRoots.clear();
                });
    }

    // ── live shelves (Slice 15): the watcher owns the per-root QFileSystemWatcher + debounce; ──
    // VaultLibrary relays landings to the door (arrivalTick/liveArrival) and new-kind arrivals
    // to the one-slice confirmation card (S11 law). Watch the confirmed roots present at boot.
    m_watcher = new VaultWatcher(index, identity, config, this);
    connect(m_watcher, &VaultWatcher::landed, this, &VaultLibrary::onWatcherLanded);
    connect(m_watcher, &VaultWatcher::newKindArrival, this, &VaultLibrary::onWatcherNewKind);
    connect(m_watcher, &VaultWatcher::rootAvailabilityChanged, this,
            &VaultLibrary::onRootAvailabilityChanged);
    m_watcher->refresh();
}

QVariantList VaultLibrary::identityCeremonies() const
{
    return m_identity ? m_identity->pendingCeremonies() : QVariantList();
}

bool VaultLibrary::decideIdentityCeremony(const QString& relationship, const QString& choice)
{
    if (!m_identity)
        return false;
    const bool accepted = m_identity->decideCeremony(relationship, choice);
    if (accepted)
        emit identityCeremoniesChanged();
    return accepted;
}

void VaultLibrary::setIdentifier(VaultIdentifier* identifier)
{
    m_identifier = identifier;
    scheduleAutoIdentify();
}

void VaultLibrary::scheduleAutoIdentify()
{
    if (!m_identifier) {
        return;
    }
    if (m_autoIdentifyScheduled) {
        m_autoIdentifyDirty = true;
        return;
    }
    m_autoIdentifyScheduled = true;
    m_autoIdentifyDirty = false;
    m_autoIdentifyKeysReady = false;
    m_autoIdentifyCursor = 0;
    m_autoIdentifyKeys.clear();
    QTimer::singleShot(0, this, &VaultLibrary::runAutoIdentifySlice);
}

void VaultLibrary::runAutoIdentifySlice()
{
    if (!m_identifier) {
        m_autoIdentifyScheduled = false;
        return;
    }
    // The watcher already exposes the immersive gate. Yield rather than competing with a
    // reader/player, and resume when the local-media surface is available again.
    if (immersive()) {
        QTimer::singleShot(250, this, &VaultLibrary::runAutoIdentifySlice);
        return;
    }
    if (!m_autoIdentifyKeysReady) {
        const QStringList kinds = {QStringLiteral("comic"), QStringLiteral("book"),
                                   QStringLiteral("video")};
        for (const QString& kind : kinds) {
            for (const QVariant& value : m_index->groupsForKind(kind)) {
                const QString key = value.toMap().value(QStringLiteral("groupKey")).toString();
                if (!key.isEmpty() && !m_autoIdentifyKeys.contains(key))
                    m_autoIdentifyKeys.append(key);
            }
        }
        m_autoIdentifyKeysReady = true;
    }
    if (m_autoIdentifyCursor >= m_autoIdentifyKeys.size()) {
        const bool rerun = m_autoIdentifyDirty;
        m_autoIdentifyScheduled = false;
        m_autoIdentifyDirty = false;
        m_autoIdentifyKeysReady = false;
        m_autoIdentifyKeys.clear();
        if (rerun)
            scheduleAutoIdentify();
        return;
    }

    const QString groupKey = m_autoIdentifyKeys.at(m_autoIdentifyCursor++);
    const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    bool eligible = !rows.isEmpty();
    for (const VaultIndex::FileRow& row : rows) {
        if (!row.identityId.isEmpty() || row.identitySuppressed || row.away
            || !row.errorState.isEmpty()) {
            eligible = false;
            break;
        }
    }
    if (eligible) {
        const VaultIdentifier::Match match = m_identifier->matchGroup(groupKey);
        if (match.adopted)
            m_identifier->applyGroup(groupKey, match);
        else if (match.candidateCount > 1)
            // Ambiguous, not absent: record it durably so a tile can wear "Vault isn't sure"
            // instead of looking merely unscanned (browse-face execution plan, Slice 2).
            m_identifier->recordAmbiguous(groupKey, match.candidateCount);
    }
    // One group per event-loop turn keeps the pass progressive and lets the immersive gate,
    // watcher, and normal QML input continue to breathe between identities.
    QTimer::singleShot(0, this, &VaultLibrary::runAutoIdentifySlice);
}

int VaultLibrary::itemCount() const
{
    return m_index ? m_index->itemCount() : 0;
}

int VaultLibrary::rootCount() const
{
    if (!m_config)
        return 0;
    int n = 0;
    const QVariantList roots = m_config->roots();
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
        // A hidden root (the synthetic downloads root the user removed from the
        // strip) is suppressed — it must not count toward the chip count or
        // publish. isRootConfirmed already returns false for hidden roots.
        if (m.value(QStringLiteral("hidden")).toBool())
            continue;
        if (m.value(QStringLiteral("confirmed")).toBool() ||
            m.value(QStringLiteral("synthetic")).toBool())
            ++n;
    }
    return n;
}

QVariantList VaultLibrary::series(const QString& kind) const
{
    if (!m_index)
        return {};
    // groupsForKind → [{groupKey, subtreePath, groupTitle, kind, count}]. Normalize to the
    // shelf's series shape { key, title, kind, count, subtreePath } (key == groupKey).
    const QVariantList groups = m_index->groupsForKind(kind);
    QVariantList out;
    out.reserve(groups.size());
    for (const QVariant& g : groups) {
        const QVariantMap m = g.toMap();
        const QString groupKey = m.value(QStringLiteral("groupKey")).toString();
        const QList<VaultIndex::FileRow> groupRows = m_index->rowsForGroup(groupKey);
        bool allHidden = !groupRows.isEmpty();
        if (m_config) {
            for (const VaultIndex::FileRow& row : groupRows) {
                if (!m_config->isHidden(row.id)) {
                    allHidden = false;
                    break;
                }
            }
        } else {
            allHidden = false;
        }
        if (allHidden)
            continue;
        QVariantMap s;
        s.insert(QStringLiteral("key"), groupKey);
        const QString identityTitle = m.value(QStringLiteral("identityTitle")).toString();
        s.insert(QStringLiteral("title"), identityTitle.isEmpty()
                 ? m.value(QStringLiteral("groupTitle")) : identityTitle);
        s.insert(QStringLiteral("kind"), m.value(QStringLiteral("kind")));
        s.insert(QStringLiteral("count"), m.value(QStringLiteral("count")));
        s.insert(QStringLiteral("awayCount"), m.value(QStringLiteral("awayCount")));
        s.insert(QStringLiteral("errorCount"), m.value(QStringLiteral("errorCount")));
        s.insert(QStringLiteral("subtreePath"), m.value(QStringLiteral("subtreePath")));
        // Cover projection through the resolver ladder (browse-artwork execution plan, Slice 3
        // part 2) — mirrors browseAt()'s Film-branch decoration exactly: walk the group's OWN
        // rows (already fetched above for the allHidden check, so no extra query) for a local ref
        // (video's adopted "file://" art or a comic/book archive cover) plus the winning row's
        // identity id/poster URL, same "first covered row wins" rule, then hand it to
        // VaultArtworkResolver instead of ever setting a remote identityCoverUrl straight onto the
        // tile (that STOPS here — see resolve()'s own rung order for why local now wins over a
        // remote poster, a deliberate change from this method's old identity-wins override).
        QString localRef, coverIdentityId, posterUrl, factKind, factPath;
        qint64 factSize = 0, factMtimeMs = 0;
        double factDurationSec = -1.0;
        for (const VaultIndex::FileRow& row : groupRows) {
            if (coverIdentityId.isEmpty() && !row.identityId.isEmpty() && !row.identitySuppressed) {
                coverIdentityId = row.identityId;
                posterUrl = row.identityCoverUrl;
            }
            if (localRef.isEmpty()) {
                if (row.kind == QLatin1String("video") && !row.coverRef.isEmpty()) {
                    localRef = row.coverRef;
                } else if ((row.kind == QLatin1String("comic") || row.kind == QLatin1String("book"))
                           && !row.coverRef.isEmpty()) {
                    const QString coverProvider = row.kind == QLatin1String("book")
                        ? QStringLiteral("vaultbookcover") : QStringLiteral("comiccover");
                    localRef = QStringLiteral("image://") + coverProvider + QLatin1Char('/')
                             + Colosseum::buildComicCoverId(row.path, row.coverRef);
                }
            }
            if (factPath.isEmpty() && row.kind == QLatin1String("video")) {
                factKind = row.kind;
                factPath = row.path;
                factSize = row.size;
                factMtimeMs = row.mtimeMs;
                factDurationSec = row.durationSec;
            }
        }
        if (factKind.isEmpty() && !groupRows.isEmpty())
            factKind = groupRows.first().kind; // comic/book — the frame-grab rung no-ops for these
        QString coverUrl = localRef;
        if (m_artworkResolver) {
            VaultArtworkResolver::RowFacts facts;
            facts.rowKey = groupKey;
            facts.kind = factKind;
            facts.path = factPath;
            facts.localRef = localRef;
            facts.identityId = coverIdentityId;
            facts.posterUrl = posterUrl;
            facts.size = factSize;
            facts.mtimeMs = factMtimeMs;
            facts.durationSec = factDurationSec;
            coverUrl = m_artworkResolver->resolve(facts);
        }
        s.insert(QStringLiteral("coverUrl"), coverUrl);
        s.insert(QStringLiteral("identityId"), m.value(QStringLiteral("identityId")));
        s.insert(QStringLiteral("identSource"), m.value(QStringLiteral("identitySource")));
        const QString identitySynopsis = m.value(QStringLiteral("identitySynopsis")).toString();
        const QString identitySource = m.value(QStringLiteral("identitySource")).toString();
        s.insert(QStringLiteral("synopsis"), identitySynopsis);
        s.insert(QStringLiteral("synopsisSource"),
                 identitySource == QLatin1String("IMDB") && !identitySynopsis.isEmpty()
                     ? QStringLiteral("Cinemeta") : identitySource);
        s.insert(QStringLiteral("identityWorld"), m.value(QStringLiteral("identityWorld")));
        s.insert(QStringLiteral("identityYear"), m.value(QStringLiteral("identityYear")));
        out.append(s);
    }
    return out;
}

QVariantMap VaultLibrary::admissionById() const
{
    return m_index ? m_index->admissionById() : QVariantMap{};
}

QVariantList VaultLibrary::items(const QString& kind, const QString& seriesKey) const
{
    Q_UNUSED(kind);
    if (!m_index)
        return {};
    // Decorate each row with a ready-to-bind per-file cover URL, now through the SAME resolver
    // ladder browseAt()/series() use (browse-artwork execution plan, Slice 3 part 2): the row's
    // own local ref (a comic/book archive cover, or a video group's adopted "file://" art) wins;
    // failing that, the resolver's canonical-poster/frame-grab rungs. A remote identityCoverUrl is
    // only ever the resolver's `posterUrl` INPUT now — it never lands in coverUrl directly (that
    // override is what this method STOPS doing here, mirroring the browseAt() fix).
    QVariantList rows = m_index->filesInSubtree(seriesKey);
    for (QVariant& v : rows) {
        QVariantMap m = v.toMap();
        const QString coverRef = m.value(QStringLiteral("coverRef")).toString();
        const QString path = m.value(QStringLiteral("path")).toString();
        const QString rowKind = m.value(QStringLiteral("kind")).toString();
        const QString provider = rowKind == QLatin1String("book")
            ? QStringLiteral("vaultbookcover") : QStringLiteral("comiccover");
        QString localRef =
            (!coverRef.isEmpty() && !path.isEmpty()
             && (rowKind == QLatin1String("comic") || rowKind == QLatin1String("book")))
                ? QStringLiteral("image://") + provider + QLatin1Char('/')
                      + Colosseum::buildComicCoverId(path, coverRef)
                : QString();
        // A video row's coverRef (when VaultEnricher local-artwork adoption has populated it) is
        // ALREADY a namespaced "file://" ref, ready to use as-is — never re-derived here.
        if (localRef.isEmpty() && rowKind == QLatin1String("video") && !coverRef.isEmpty())
            localRef = coverRef;
        const QString identityCover = m.value(QStringLiteral("identityCoverUrl")).toString();
        const QString identityId = m.value(QStringLiteral("identityId")).toString();
        QString resolvedCover = localRef;
        if (m_artworkResolver) {
            VaultArtworkResolver::RowFacts facts;
            facts.rowKey = m.value(QStringLiteral("id")).toString();
            facts.kind = rowKind;
            facts.path = path;
            facts.localRef = localRef;
            facts.identityId = identityId;
            facts.posterUrl = identityCover;
            facts.size = m.value(QStringLiteral("size")).toLongLong();
            facts.mtimeMs = m.value(QStringLiteral("mtimeMs")).toLongLong();
            facts.durationSec = m.value(QStringLiteral("durationSec")).toDouble();
            resolvedCover = m_artworkResolver->resolve(facts);
        }
        m.insert(QStringLiteral("coverUrl"), resolvedCover);
        const QString identityTitle = m.value(QStringLiteral("identityTitle")).toString();
        m.insert(QStringLiteral("title"), identityTitle.isEmpty()
                 ? m.value(QStringLiteral("displayTitle")) : identityTitle);
        m.insert(QStringLiteral("identSource"), m.value(QStringLiteral("identitySource")));
        const QString identitySynopsis = m.value(QStringLiteral("identitySynopsis")).toString();
        const QString embeddedSynopsis = m.value(QStringLiteral("synopsis")).toString();
        m.insert(QStringLiteral("synopsis"), identitySynopsis.isEmpty() ? embeddedSynopsis : identitySynopsis);
        const QString identitySource = m.value(QStringLiteral("identitySource")).toString();
        m.insert(QStringLiteral("synopsisSource"), identitySynopsis.isEmpty()
                 ? m.value(QStringLiteral("metadataSource"))
                 : (identitySource == QLatin1String("IMDB")
                        ? QStringLiteral("Cinemeta") : identitySource));
        m.insert(QStringLiteral("identityId"), m.value(QStringLiteral("identityId")));
        m.insert(QStringLiteral("identityWorld"), m.value(QStringLiteral("identityWorld")));
        v = m;
    }
    return rows;
}

QVariantList VaultLibrary::hiddenSeries() const
{
    QVariantList out;
    if (!m_index || !m_config)
        return out;
    for (const QString& kind : {QStringLiteral("comic"), QStringLiteral("book"), QStringLiteral("video")}) {
        const QVariantList groups = m_index->groupsForKind(kind);
        for (const QVariant& value : groups) {
            const QVariantMap group = value.toMap();
            const QString key = group.value(QStringLiteral("groupKey")).toString();
            const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(key);
            if (rows.isEmpty())
                continue;
            bool allHidden = true;
            for (const VaultIndex::FileRow& row : rows) {
                if (!m_config->isHidden(row.id)) {
                    allHidden = false;
                    break;
                }
            }
            if (!allHidden)
                continue;
            QVariantMap s;
            s.insert(QStringLiteral("key"), key);
            const QString identityTitle = group.value(QStringLiteral("identityTitle")).toString();
            s.insert(QStringLiteral("title"), identityTitle.isEmpty()
                     ? group.value(QStringLiteral("groupTitle")) : identityTitle);
            s.insert(QStringLiteral("kind"), kind);
            s.insert(QStringLiteral("count"), group.value(QStringLiteral("count")));
            s.insert(QStringLiteral("awayCount"), group.value(QStringLiteral("awayCount")));
            s.insert(QStringLiteral("errorCount"), group.value(QStringLiteral("errorCount")));
            s.insert(QStringLiteral("subtreePath"), group.value(QStringLiteral("subtreePath")));
            s.insert(QStringLiteral("identityId"), group.value(QStringLiteral("identityId")));
            s.insert(QStringLiteral("identSource"), group.value(QStringLiteral("identitySource")));
            const QString identitySynopsis = group.value(QStringLiteral("identitySynopsis")).toString();
            const QString identitySource = group.value(QStringLiteral("identitySource")).toString();
            s.insert(QStringLiteral("synopsis"), identitySynopsis);
            s.insert(QStringLiteral("synopsisSource"),
                     identitySource == QLatin1String("IMDB") && !identitySynopsis.isEmpty()
                         ? QStringLiteral("Cinemeta") : identitySource);
            s.insert(QStringLiteral("identityWorld"), group.value(QStringLiteral("identityWorld")));
            const QString coverPath = group.value(QStringLiteral("coverPath")).toString();
            const QString coverEntry = group.value(QStringLiteral("coverEntry")).toString();
            const QString provider = kind == QLatin1String("book")
                ? QStringLiteral("vaultbookcover") : QStringLiteral("comiccover");
            s.insert(QStringLiteral("coverUrl"),
                     (!coverPath.isEmpty() && !coverEntry.isEmpty())
                         ? QStringLiteral("image://") + provider + QLatin1Char('/')
                               + Colosseum::buildComicCoverId(coverPath, coverEntry)
                         : group.value(QStringLiteral("identityCoverUrl")));
            s.insert(QStringLiteral("hidden"), true);
            out.append(s);
        }
    }
    return out;
}

QVariantList VaultLibrary::browseAt(const QString& rootOrPath) const
{
    QVariantList out;
    const QVariantList roots = m_config ? m_config->roots() : QVariantList();
    const QStringList scanIgnore = m_config ? m_config->scanIgnore() : QStringList();
    const QList<VaultKit::BrowseNode> nodes = VaultKit::planBrowseLevel(rootOrPath, scanIgnore);
    const bool levelRootAway = VaultBrowseAway::ownerRootAway(m_index, roots, rootOrPath);
    if (nodes.isEmpty() && levelRootAway) {
        // The drive that holds this level is gone: VaultKit::planBrowseLevel cannot walk a
        // vanished directory (it bails at `QDir::exists()`), so there is nothing here for the
        // loop below to decorate. Without this fallback the design's own contract ("tiles hold
        // position, marked away — nothing disappears", §4.7) would be false the moment a level's
        // OWN root goes away: the grid would read as empty instead of away. Serve the durable
        // index's memory of this level instead.
        return VaultBrowseAway::offlineBrowseAt(m_index, roots, rootOrPath);
    }
    out.reserve(nodes.size());
    for (const VaultKit::BrowseNode& n : nodes) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), n.key);
        m.insert(QStringLiteral("nodeType"), VaultKit::browseNodeTypeName(n.nodeType));
        m.insert(QStringLiteral("displayTitle"), n.displayTitle);
        m.insert(QStringLiteral("physicalFact"), n.physicalFact);
        m.insert(QStringLiteral("path"), n.path);
        // A row's coverRef is a ready-to-bind cover URL for the tile — VaultPosterCard/
        // VaultWideCard bind it as `source: coverRef` directly. Every branch below (Film AND
        // Folder/Show/Season containers) walks VaultArtworkResolver::resolve() to fill this in
        // (browse-artwork execution plan, Slice 3 part 2) — default empty here is only the
        // starting point a resolver miss (or an Episode/Clip node, untouched by this slice) keeps.
        m.insert(QStringLiteral("coverRef"), QString());
        QVariantMap counts;
        counts.insert(QStringLiteral("items"), n.mediaCount);
        m.insert(QStringLiteral("counts"), counts);

        // Decoration from today's index: reliable only for a film node, whose path IS a
        // group's subtreePath (one video file, one group — VaultScanner's own convention).
        // Deeper per-episode state and durable ambiguity are Slices 2/6's business; this slice
        // gives every other node an honest "resolving" default rather than inventing one.
        QString state = QStringLiteral("resolving");
        // The group's stored kind, carried through to QML so the identify gesture can pick the
        // right catalogue (see dominantRowKind above). Filled per node type from the index rows
        // each branch already fetches; a node with nothing indexed under it keeps "".
        QString kind;
        // Slice 6: away is a ROOT-WIDE fact (markRootAway() flips every row under one root in one
        // statement), so every node this call returns starts from the SAME verdict — whichever
        // confirmed root owns `rootOrPath`. Only a Film node overrides this with its own group's
        // precise per-row check below (kept — it is more exact when it has rows to check).
        bool away = levelRootAway;
        if (n.nodeType == VaultKit::BrowseNodeType::Clip) {
            // The kind classifier's own verdict, not an identity lookup (locked design: local-
            // only is certain-and-yours, never "not identified yet").
            state = QStringLiteral("localOnly");
            // Browse-artwork execution plan, Slice 3 part 2: a Clip is real footage (locked
            // design gives it a genuine frame from the file, e.g. Hemanth's own Cricket clips —
            // never a typographic-only face just because it isn't catalogue-identifiable). n.key
            // == n.path == the file itself for a Clip node (VaultKit's loose-video leaf grammar),
            // so the leaf's own exact-path row is what carries its stored kind.
            const QList<VaultIndex::FileRow> leafRows = (m_index && !n.path.isEmpty())
                ? m_index->rowsForPath(n.path) : QList<VaultIndex::FileRow>();
            kind = dominantRowKind(leafRows);
            insertLeafJoinId(m, leafRows);
            const QString resolved = resolveVideoLeafCoverRef(leafRows, m_artworkResolver, n.key);
            if (!resolved.isEmpty())
                m.insert(QStringLiteral("coverRef"), resolved);
        } else if (n.nodeType == VaultKit::BrowseNodeType::Episode) {
            // Slice 5 fix: title is structurally known from the filesystem walk + grammar parse
            // itself — see the Folder/Show/Season comment below for why "identified" is correct.
            state = QStringLiteral("identified");
            // Browse-artwork execution plan, Slice 3 part 2: an Episode gets the same real
            // frame-grab/poster treatment as a Clip (locked design's 16:9 still) — same lookup,
            // same reasoning, n.key == n.path == the file for an Episode node too, and the same
            // exact-path row answers its stored kind.
            const QList<VaultIndex::FileRow> leafRows = (m_index && !n.path.isEmpty())
                ? m_index->rowsForPath(n.path) : QList<VaultIndex::FileRow>();
            kind = dominantRowKind(leafRows);
            insertLeafJoinId(m, leafRows);
            const QString resolved = resolveVideoLeafCoverRef(leafRows, m_artworkResolver, n.key);
            if (!resolved.isEmpty())
                m.insert(QStringLiteral("coverRef"), resolved);
        } else if (n.nodeType == VaultKit::BrowseNodeType::Folder
                   || n.nodeType == VaultKit::BrowseNodeType::Show
                   || n.nodeType == VaultKit::BrowseNodeType::Season) {
            // Slice 5 fix: a folder/show/season's title is always structurally known from the
            // filesystem walk + grammar parse itself — there is no per-catalogue identification
            // pending the way a Film's canonical match is. Slice 1 deliberately left these at the
            // "resolving" default ("an honest default rather than inventing one" — its own
            // comment); Slice 5 found this blocks the browse grid's core interaction:
            // VaultPosterCard/VaultWideCard only mount their open MouseArea in the "settled" face
            // (faceState flips off "resolving"), so a container node could never be drilled into
            // or played by a real click. Away nuance is Slice 6's explicit business, not invented
            // here.
            state = QStringLiteral("identified");
            // A container that IS a group (a show folder whose node path equals the group key)
            // answers its own kind from the rows it holds. A pure ancestor folder — one whose
            // children are groups, not files — has no rows of its own here and honestly keeps ""
            // rather than inventing one from a child; identify is unreachable on a container tile
            // anyway (both cards only emit identifyRequested from the "uncertain" mark, and only a
            // Film node ever reaches that state), so "" costs the identify flow nothing. Fetched
            // once, above the resolver guard, because the artwork lookup below reads the SAME rows.
            const QList<VaultIndex::FileRow> containerRows = (m_index && !n.path.isEmpty())
                ? m_index->rowsForGroup(n.path) : QList<VaultIndex::FileRow>();
            kind = dominantRowKind(containerRows);
            // Browse-artwork execution plan (2026-08-14 fix): a container that IS a group — a
            // top-level show folder whose node path equals the group key — carries the show's
            // canonical identity on its own file rows (The Wire's episodes all carry
            // imdb:tt0306414 + its poster URL). Look that up and hand it to resolve()'s
            // canonical-poster rung, so an identified SHOW shows its Cinemeta poster, not only a
            // movie Film node did. A container that is not a single identified show — an
            // unidentified show, or a mixed folder — finds no winning identity and stays
            // typographic, which is correct. (A Season node inheriting its parent show's poster
            // when the season is not itself a group is a later refinement, tracked separately.)
            if (m_artworkResolver) {
                VaultArtworkResolver::RowFacts facts;
                facts.rowKey = n.key;
                facts.kind = VaultKit::browseNodeTypeName(n.nodeType);
                facts.path = n.path;
                for (const VaultIndex::FileRow& row : containerRows) {
                    if (!row.identityId.isEmpty() && !row.identitySuppressed
                        && !row.identityCoverUrl.isEmpty()) {
                        facts.identityId = row.identityId;
                        facts.posterUrl = row.identityCoverUrl;
                        break;
                    }
                }
                const QString resolved = m_artworkResolver->resolve(facts);
                if (!resolved.isEmpty())
                    m.insert(QStringLiteral("coverRef"), resolved);
            }
        } else if (n.nodeType == VaultKit::BrowseNodeType::Film && m_index) {
            const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(n.path);
            if (!rows.isEmpty()) {
                // The one branch that matters most for identify routing: a Film node is the ONLY
                // node type that ever reaches state "uncertain"/"resolving", i.e. the only tile
                // whose Identify affordance is reachable at all. Its group's rows carry the kind
                // the identify dialog needs to choose IMDb over the comic/manga catalogues.
                kind = dominantRowKind(rows);
                bool anyAway = false;
                bool identified = false;
                bool ambiguous = false;
                // Browse-artwork execution plan, Slice 3 part 2: `localRef` replaces the old
                // `coverRef` local — it NEVER carries a remote identityCoverUrl now (that STOPS;
                // a remote poster is only ever resolve()'s `posterUrl` INPUT below, never handed
                // straight to the tile). `coverIdentityId`/`posterUrl` capture the group's winning
                // identity the same "first covered row wins" pass already walks; `factPath`/
                // `factSize`/`factMtimeMs`/`factDurationSec` are the representative VIDEO file's
                // own facts for the resolver's frame-grab rung (VaultScanner's "one video file,
                // one group" convention means this is typically the group's only row).
                QString localRef, coverIdentityId, posterUrl, factKind, factPath;
                qint64 factSize = 0, factMtimeMs = 0;
                double factDurationSec = -1.0;
                for (const VaultIndex::FileRow& row : rows) {
                    if (row.away)
                        anyAway = true;
                    if (!row.identityId.isEmpty() && !row.identitySuppressed) {
                        identified = true;
                        if (coverIdentityId.isEmpty()) {
                            coverIdentityId = row.identityId;
                            posterUrl = row.identityCoverUrl;
                        }
                    }
                    if (row.identityState == QLatin1String("ambiguous"))
                        ambiguous = true;
                    // First covered row wins the group tile's LOCAL ref. Mirrors items()/series()
                    // so the browse grid and the folder view agree on a group's cover.
                    if (localRef.isEmpty()) {
                        if (row.kind == QLatin1String("video") && !row.coverRef.isEmpty()) {
                            localRef = row.coverRef;
                        } else if ((row.kind == QLatin1String("comic")
                                    || row.kind == QLatin1String("book"))
                                   && !row.coverRef.isEmpty()) {
                            const QString provider = row.kind == QLatin1String("book")
                                ? QStringLiteral("vaultbookcover")
                                : QStringLiteral("comiccover");
                            localRef = QStringLiteral("image://") + provider + QLatin1Char('/')
                                     + Colosseum::buildComicCoverId(row.path, row.coverRef);
                        }
                    }
                    if (factPath.isEmpty() && row.kind == QLatin1String("video")) {
                        factKind = row.kind;
                        factPath = row.path;
                        factSize = row.size;
                        factMtimeMs = row.mtimeMs;
                        factDurationSec = row.durationSec;
                    }
                }
                if (factKind.isEmpty())
                    factKind = rows.first().kind; // comic/book — frame-grab rung no-ops for these
                away = anyAway;
                // identified always wins (identify-in-place settles a previously-ambiguous
                // group through the same durable fact); suppressed reads as resolving
                // (filename-honest, not "unsure" — the user already made a call).
                state = identified ? QStringLiteral("identified")
                      : ambiguous  ? QStringLiteral("uncertain")
                                   : QStringLiteral("resolving");
                if (m_artworkResolver) {
                    VaultArtworkResolver::RowFacts facts;
                    facts.rowKey = n.key;
                    facts.kind = factKind;
                    facts.path = factPath;
                    facts.localRef = localRef;
                    facts.identityId = coverIdentityId;
                    facts.posterUrl = posterUrl;
                    facts.size = factSize;
                    facts.mtimeMs = factMtimeMs;
                    facts.durationSec = factDurationSec;
                    const QString resolved = m_artworkResolver->resolve(facts);
                    if (!resolved.isEmpty())
                        m.insert(QStringLiteral("coverRef"), resolved);
                } else if (!localRef.isEmpty()) {
                    m.insert(QStringLiteral("coverRef"), localRef);
                }
                // Slice 5 fix: a Film node's `path` was left as n.path — the CONTAINING FOLDER
                // (BrowseNode::path for a film is set from the child directory, never
                // overridden) — which broke Play (openMediaRequested expects a FILE).
                // VaultScanner's "one video file, one group" convention means the group's own
                // row IS that file, so the browse row must carry the file's real path here.
                m.insert(QStringLiteral("path"), rows.first().path);
                // Vault ux uplift S6: the group's durable vault id (the Progress join key —
                // see insertLeafJoinId's comment). The PRIMARY row is the join target: the
                // first video row (the film itself under the one-video-one-group convention,
                // never an Extras sibling), falling back to the first row for a comic/book
                // group whose progress is keyed under its own kind.
                QString joinId;
                for (const VaultIndex::FileRow& row : rows) {
                    if (row.kind == QLatin1String("video") && !row.id.isEmpty()) {
                        joinId = row.id;
                        break;
                    }
                }
                if (joinId.isEmpty())
                    joinId = rows.first().id;
                if (!joinId.isEmpty())
                    m.insert(QStringLiteral("id"), joinId);
            }
        }
        m.insert(QStringLiteral("kind"), kind);
        m.insert(QStringLiteral("state"), state);
        m.insert(QStringLiteral("away"), away);
        out.append(m);
    }
    return out;
}

QVariantList VaultLibrary::rootsDetail() const
{
    QVariantList out;
    if (!m_config)
        return out;
    const QVariantList roots = m_config->roots();
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("hidden")).toBool())
            continue;
        const bool confirmedOrSynthetic = m.value(QStringLiteral("confirmed")).toBool()
            || m.value(QStringLiteral("synthetic")).toBool();
        if (!confirmedOrSynthetic)
            continue; // an unconfirmed root has no founding card resolved yet — not railed
        const QString path = m.value(QStringLiteral("path")).toString();
        QVariantMap row;
        row.insert(QStringLiteral("path"), path);
        const QString name = QFileInfo(path).fileName();
        row.insert(QStringLiteral("name"), name.isEmpty() ? path : name);
        row.insert(QStringLiteral("available"), QDir(path).exists());
        row.insert(QStringLiteral("fileCount"), m_index ? m_index->rowsForRoot(path).size() : 0);
        row.insert(QStringLiteral("itemCount"), browseAt(path).size());
        out.append(row);
    }
    return out;
}

QVariantList VaultLibrary::recentArrivals(int limit) const
{
    QVariantList out;
    if (!m_index || limit <= 0)
        return out;
    const QVariantList groups = m_index->recentGroups(limit * 2); // headroom for all-hidden skips
    for (const QVariant& gv : groups) {
        if (out.size() >= limit)
            break;
        const QVariantMap g = gv.toMap();
        const QString groupKey = g.value(QStringLiteral("groupKey")).toString();
        const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
        if (rows.isEmpty())
            continue;
        bool allHidden = true;
        bool anyAway = false;
        bool ambiguous = false;
        QString identityTitle;
        for (const VaultIndex::FileRow& row : rows) {
            if (!m_config || !m_config->isHidden(row.id))
                allHidden = false;
            if (row.away)
                anyAway = true;
            if (identityTitle.isEmpty() && !row.identityId.isEmpty() && !row.identitySuppressed)
                identityTitle = row.identityTitle;
            if (row.identityState == QLatin1String("ambiguous"))
                ambiguous = true;
        }
        if (allHidden)
            continue;

        QVariantMap m;
        m.insert(QStringLiteral("key"), groupKey);
        m.insert(QStringLiteral("nodeType"),
                 rows.size() == 1 ? QStringLiteral("film") : QStringLiteral("show"));
        m.insert(QStringLiteral("displayTitle"), identityTitle.isEmpty()
                 ? g.value(QStringLiteral("groupTitle")) : identityTitle);
        // The carousel's fact line is the physical fact ONLY (locked design §4.10) — a count is
        // the one fact this slice can supply honestly for a multi-file group; quality is later.
        m.insert(QStringLiteral("physicalFact"), rows.size() == 1
                 ? QString() : QStringLiteral("%1 items").arg(rows.size()));
        // Slice 5 fix (mirrors the browseAt() Film fix above): a one-file group's carousel
        // row must carry the file's own path, not its containing folder, so the carousel's
        // Play affordance opens the right thing via openMediaRequested.
        m.insert(QStringLiteral("path"), rows.size() == 1
                 ? rows.first().path : g.value(QStringLiteral("subtreePath")));
        m.insert(QStringLiteral("coverRef"), QString());
        // Same stored kind browseAt() now carries: a carousel slide's "Details" opens the SAME
        // detail sheet a grid Film tile does, and the sheet's Identify hands its row on to the
        // identify dialog — so a carousel-opened film has to know it is a film too.
        m.insert(QStringLiteral("kind"), dominantRowKind(rows));
        m.insert(QStringLiteral("state"), !identityTitle.isEmpty() ? QStringLiteral("identified")
                 : ambiguous ? QStringLiteral("uncertain") : QStringLiteral("resolving"));
        m.insert(QStringLiteral("away"), anyAway);
        out.append(m);
    }
    return out;
}

QVariantMap VaultLibrary::browseDetail(const QString& key) const
{
    const QStringList scanIgnore = m_config ? m_config->scanIgnore() : QStringList();
    return VaultBrowseDetail::detailFor(m_index, key, scanIgnore);
}

QString VaultLibrary::browseEmptyCause(const QString& rootOrPath) const
{
    const QVariantList roots = m_config ? m_config->roots() : QVariantList();
    const bool hasAnyRoots = !rootsDetail().isEmpty();
    const bool levelHasRows = !browseAt(rootOrPath).isEmpty();
    // See VaultBrowseEmpty::isLevelAway's own comment for why the index's row-based away flag
    // alone is not enough. Scoped to THIS method only — VaultBrowseAway::ownerRootAway itself is
    // untouched so Slice 6's own pinned tests/behavior stay exactly as they are.
    const QString ownerRoot = VaultBrowseAway::ownerRootPath(roots, rootOrPath);
    const bool ownerDirExists = ownerRoot.isEmpty() ? true : QDir(ownerRoot).exists();
    const bool levelAway = VaultBrowseEmpty::isLevelAway(
        VaultBrowseAway::ownerRootAway(m_index, roots, rootOrPath),
        !ownerRoot.isEmpty(), ownerDirExists);
    return VaultBrowseEmpty::causeName(
        VaultBrowseEmpty::classify(hasAnyRoots, levelHasRows, levelAway));
}

int VaultLibrary::browseEmptyAwayCount(const QString& rootOrPath) const
{
    if (!m_index)
        return 0;
    const QVariantList roots = m_config ? m_config->roots() : QVariantList();
    const QString ownerRoot = VaultBrowseAway::ownerRootPath(roots, rootOrPath);
    if (ownerRoot.isEmpty())
        return 0;
    return m_index->rowsForRoot(ownerRoot).size();
}

bool VaultLibrary::identifyGroup(const QString& groupKey)
{
    if (!m_identifier)
        return false;
    const VaultIdentifier::Match match = m_identifier->matchGroup(groupKey);
    if (match.adopted)
        return m_identifier->applyGroup(groupKey, match);
    if (match.candidateCount > 1)
        m_identifier->recordAmbiguous(groupKey, match.candidateCount);
    return false;
}

bool VaultLibrary::identifyGroupWith(const QString& groupKey,
                                     const QVariantMap& chosenIdentity)
{
    if (!m_identifier)
        return false;
    VaultIdentifier::Match match;
    match.adopted = true;
    match.source = chosenIdentity.value(QStringLiteral("source")).toString();
    match.sourceId = chosenIdentity.value(QStringLiteral("sourceId")).toString();
    match.title = chosenIdentity.value(QStringLiteral("title")).toString();
    match.synopsis = chosenIdentity.value(QStringLiteral("synopsis")).toString();
    match.coverUrl = chosenIdentity.value(QStringLiteral("coverUrl")).toString();
    match.world = chosenIdentity.value(QStringLiteral("world")).toString();
    match.year = chosenIdentity.value(QStringLiteral("year")).toInt();
    return m_identifier->identifyGroupWith(groupKey, match);
}

bool VaultLibrary::unidentifyGroup(const QString& groupKey)
{
    return m_identifier && m_identifier->unidentifyGroup(groupKey);
}

bool VaultLibrary::reshelveGroup(const QString& groupKey, const QString& kind)
{
    if (!m_identifier || !m_index)
        return false;
    if (!m_identifier->reshelveGroup(groupKey, kind))
        return false;
    if (m_config)
        m_config->setKind(groupKey, kind);
    return true;
}

bool VaultLibrary::hideGroup(const QString& groupKey)
{
    if (!m_index || !m_config)
        return false;
    const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty())
        return false;
    for (const VaultIndex::FileRow& row : rows)
        m_config->setHidden(row.id, true);
    ++m_revision;
    emit changed();
    return true;
}

bool VaultLibrary::restoreGroup(const QString& groupKey)
{
    if (!m_index || !m_config)
        return false;
    const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty())
        return false;
    for (const VaultIndex::FileRow& row : rows)
        m_config->setHidden(row.id, false);
    ++m_revision;
    emit changed();
    return true;
}

bool VaultLibrary::enrichIdentity(const QString& groupKey, const QString& synopsis,
                                  const QString& coverUrl)
{
    if (!m_index || groupKey.isEmpty())
        return false;
    QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty())
        return false;
    bool changedFacts = false;
    for (VaultIndex::FileRow& row : rows) {
        if (row.identitySource != QLatin1String("IMDB") || row.identitySuppressed)
            continue;
        if (!synopsis.isEmpty() && row.identitySynopsis != synopsis) {
            row.identitySynopsis = synopsis;
            changedFacts = true;
        }
        if (!coverUrl.isEmpty() && row.identityCoverUrl != coverUrl) {
            row.identityCoverUrl = coverUrl;
            changedFacts = true;
        }
    }
    return changedFacts && m_index->upsertMany(rows);
}

bool VaultLibrary::revealInExplorer(const QString& path) const
{
#ifdef Q_OS_WIN
    const QFileInfo fi(path);
    if (path.trimmed().isEmpty() || !fi.exists())
        return false;
    const QString native = QDir::toNativeSeparators(fi.absoluteFilePath());
    // A folder opens to its contents; a file is revealed selected in its parent. Args go as a
    // QStringList — QProcess quotes for CommandLineToArgvW, so spaces/parens/unicode are safe with
    // NO manual quoting or shell.
    if (fi.isDir())
        return QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList{native});
    return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                   QStringList{QStringLiteral("/select,"), native});
#else
    Q_UNUSED(path);
    return false;
#endif
}

void VaultLibrary::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged();
}

void VaultLibrary::addFolder(const QString& pathOrUrl)
{
    if (!m_scanner || !m_config)
        return;
    QString path = pathOrUrl;
    const QUrl u(pathOrUrl);
    if (u.isLocalFile())
        path = u.toLocalFile();
    if (path.isEmpty())
        return;

    // Add the folder as an UNCONFIRMED root (user intent), then census it off-thread. The
    // candidate card rises on scanFinished; nothing is published until the user confirms.
    m_config->addRoot(path);
    m_offeredThisRun.insert(normPath(path)); // an explicit add is this run's offer for it
    beginCensus(path);
}

void VaultLibrary::beginCensus(const QString& path)
{
    // Clear any stale candidate, reset the pill, and kick the off-thread census.
    m_candidate.clear();
    m_candidateRoot.clear();
    emit candidateChanged();

    m_scanningRoot = path;
    m_scanDone = 0;
    m_scanTotal = 0;
    emit scanProgressChanged();
    setScanning(true);
    m_scanner->scanRoot(path, m_config->scanIgnore());
}

void VaultLibrary::offerUnconfirmedRoots()
{
    if (!m_scanner || !m_config)
        return;
    // Slice 18: the synthetic downloads root is pre-confirmed — if downloads exist
    // and it isn't in config yet, add it now and publish it alongside any confirmed
    // user roots. This is the "already present on first Vault open" contract.
    ensureDownloadsRoot();

    // Never interrupt an in-flight scan or a card already up.
    if (m_scanning || cardVisible())
        return;
    const QVariantList roots = m_config->roots();
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
        // Skip synthetic roots (pre-confirmed, no card) and hidden roots.
        if (m.value(QStringLiteral("synthetic")).toBool())
            continue;
        if (m.value(QStringLiteral("hidden")).toBool())
            continue;
        if (m.value(QStringLiteral("confirmed")).toBool())
            continue;
        const QString path = m.value(QStringLiteral("path")).toString(); // already normalized
        if (path.isEmpty() || m_offeredThisRun.contains(path))
            continue;
        m_offeredThisRun.insert(path);
        beginCensus(path);
        return; // one founding card at a time
    }
}

void VaultLibrary::confirmRoot(const QString& root, const QVariantMap& kindOverrides)
{
    if (!m_scanner || !m_config)
        return;

    // Persist the card's chip reassignments (subtreePath → kind), then mark the root confirmed.
    for (auto it = kindOverrides.constBegin(); it != kindOverrides.constEnd(); ++it)
        m_config->setKind(it.key(), it.value().toString());
    m_config->confirmRoot(root);

    m_candidate.clear();
    m_candidateRoot.clear();
    emit candidateChanged();

    publishAllConfirmed();
}

void VaultLibrary::republishAtBoot()
{
    if (m_bootRepublishDone)
        return;
    m_bootRepublishDone = true;
    if (!m_scanner || !m_config)
        return;

    bool hasPublishableRoot = false;
    for (const QVariant& r : m_config->roots()) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("hidden")).toBool())
            continue;
        if (m.value(QStringLiteral("confirmed")).toBool()
            || m.value(QStringLiteral("synthetic")).toBool()) {
            hasPublishableRoot = true;
            break;
        }
    }
    if (hasPublishableRoot)
        publishAllConfirmed();
}

void VaultLibrary::publishAllConfirmed()
{
    if (!m_scanner || !m_config)
        return;

    // Publish the UNION of ALL confirmed roots — never one root alone (whole-index replace).
    // A hidden root (the removed synthetic downloads root) is excluded from the census list.
    QStringList confirmed;
    const QVariantList roots = m_config->roots();
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("hidden")).toBool())
            continue;
        if (m.value(QStringLiteral("confirmed")).toBool() ||
            m.value(QStringLiteral("synthetic")).toBool())
            confirmed.append(m.value(QStringLiteral("path")).toString());
    }

    // Snapshot every chip override on the GUI thread and hand it to the off-thread census
    // so the re-shelve honors the user's choices (Thread A). Keys are already normalized.
    QMap<QString, QString> overrides;
    const QVariantMap ov = m_config->kindOverrides();
    for (auto it = ov.constBegin(); it != ov.constEnd(); ++it)
        overrides.insert(it.key(), it.value().toString());

    // Slice 18: derive the synthetic downloads root's rows when it is present + not
    // hidden. Folded into the UNION publish so downloads shelf alongside user roots.
    QList<VaultIndex::FileRow> extraRows;
    if (m_downloadsRoot && !m_downloadsRootPath.isEmpty() &&
        m_config->hasRoot(m_downloadsRootPath) &&
        !m_config->isRootHidden(m_downloadsRootPath)) {
        extraRows = m_downloadsRoot->rowsForDownloads(m_downloadsRootPath);
    }

    // Clear the pill's scan counts so the brief shelving pass shows "Scanning …", not the
    // last census's stale "N of M".
    m_scanDone = 0;
    m_scanTotal = 0;
    emit scanProgressChanged();

    setScanning(true); // shelving
    if (m_watcher)
        m_watcher->refresh();
    m_scanner->publishConfirmed(confirmed, m_config->scanIgnore(), overrides, extraRows);
}

void VaultLibrary::ensureDownloadsRoot()
{
    if (!m_config || !m_downloadsRoot || m_downloadsRootPath.isEmpty())
        return;
    // Only add the synthetic root if downloads actually exist — a fresh profile
    // with no downloads shows an empty Vault, not a phantom trusted root.
    if (!m_downloadsRoot->hasContainerDownloads())
        return;
    if (m_config->hasRoot(m_downloadsRootPath))
        return; // already known (confirmed, or hidden from a prior remove)
    m_config->addSyntheticRoot(m_downloadsRootPath);
    // Publish immediately so the downloads root is "already present on first Vault
    // open if downloads exist" — no card, no confirm step.
    publishAllConfirmed();
}

void VaultLibrary::removeDownloadsRoot()
{
    if (!m_config || m_downloadsRootPath.isEmpty() || !m_config->hasRoot(m_downloadsRootPath))
        return;
    // The chip's remove HIDES the synthetic root — never deletes. The files +
    // transfer history on the Downloads lane are untouched. A re-publish without
    // the synthetic rows drops them from the index.
    m_config->setRootHidden(m_downloadsRootPath, true);
    publishAllConfirmed();
}

void VaultLibrary::setDownloadsRoot(VaultDownloadsRoot* root, const QString& path)
{
    m_downloadsRoot = root;
    m_downloadsRootPath = path;
}

bool VaultLibrary::immersive() const
{
    return m_watcher ? m_watcher->immersive() : false;
}

void VaultLibrary::setImmersive(bool on)
{
    if (!m_watcher)
        return;
    m_watcher->setImmersive(on);
    emit immersiveChanged();
}

void VaultLibrary::onWatcherLanded(int count)
{
    if (count <= 0)
        return;
    ++m_arrivalTick; // the door's pulse clock — monotone, no counts (spec §3)
    emit liveArrival();
}

void VaultLibrary::onWatcherNewKind(const QString& root, const QVariantList& slices)
{
    // A landing too — the door pulses for any live-shelf arrival, card or not.
    ++m_arrivalTick;
    emit liveArrival();

    // One card at a time (S11 law): a scan in flight or a card already up defers this
    // arrival's card to the next rescan — never a second card on top.
    if (m_scanning || cardVisible() || slices.isEmpty())
        return;
    m_candidate = slices;
    m_candidateRoot = root;
    emit candidateChanged();
}

void VaultLibrary::onRootAvailabilityChanged(const QString& root, bool available)
{
    if (!m_index)
        return;

    if (!available) {
        // The root is gone: preserve every row and every progress identity in place.
        m_index->markRootAway(root, true);
        return;
    }

    // A root returned. Keep the old rows away until a successful silent rescan replaces them;
    // a rename that happened while the drive was absent must flow through VaultIdentity first.
    m_pendingReviveRoots.insert(normPath(root));
    maybePublishPendingRevives();
}

void VaultLibrary::maybePublishPendingRevives()
{
    if (m_pendingReviveRoots.isEmpty() || m_scanning || cardVisible()
        || m_revivalRescanInFlight)
        return;
    m_revivalRescanInFlight = true;
    publishAllConfirmed();
}

void VaultLibrary::rescanDegradedRoots()
{
    if (!m_scanner || !m_config || !m_watcher)
        return;
    if (m_scanning || cardVisible())
        return; // never interrupt a live scan or a card already up

    // Re-arm the watches first (a transient failure may already be gone — QFSW limits
    // lift, a network share reconnects).
    m_watcher->refresh();

    // Probe every confirmed root. A genuinely away drive is marked in place; a returned drive is
    // rescanned while its rows remain away so aliases/progress are reconciled. Watcher-only
    // degradation still gets the existing silent rescan fallback, but only when the root exists.
    bool anyRescan = false;
    const QVariantList roots = m_config->roots();
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
        if (!m.value(QStringLiteral("confirmed")).toBool())
            continue;
        const QString path = m.value(QStringLiteral("path")).toString();
        const bool exists = QDir(path).exists();
        if (!exists) {
            m_index->markRootAway(path, true);
            continue;
        }
        const QList<VaultIndex::FileRow> rows = m_index->rowsForRoot(path);
        const bool revived = !rows.isEmpty() && rows.first().away;
        if (revived || m_watcher->isRootDegraded(path))
            anyRescan = true;
    }
    if (!anyRescan)
        return;

    // Silent rescan of the UNION (publishAllConfirmed folds every confirmed user root +
    // the synthetic downloads root's derived rows, Slice 18). No card rises — a confirmed
    // root's rescan is silent by design; the door shows the quiet gold dot while it runs.
    publishAllConfirmed();
}

void VaultLibrary::rescanRoot(const QString& path)
{
    if (!m_scanner || !m_config)
        return;
    // Only a KNOWN, publishable root: confirmed or synthetic, not hidden. A stray path is
    // a no-op — this is a rail verb, not an add-folder side channel.
    const QString n = normPath(path);
    if (!m_config->hasRoot(n) || m_config->isRootHidden(n)
        || !(m_config->isRootConfirmed(n) || m_config->isSyntheticRoot(n))) {
        return;
    }
    // Silent union republish (no card — a confirmed root's rescan never re-ceremonies).
    // publishAllConfirmed() re-censuses EVERY confirmed root: the whole-index replace
    // means publishing one root alone would wipe its siblings, so "rescan this root"
    // rides the same union path confirm/boot/watcher already use.
    publishAllConfirmed();
}

void VaultLibrary::forgetRoot(const QString& path)
{
    if (!m_scanner || !m_config)
        return;
    const QString n = normPath(path);
    if (!m_config->hasRoot(n) || m_config->isRootHidden(n))
        return;
    // The synthetic downloads root's files belong to the Downloads lane — forgetting it
    // takes the reversible hide (the S9 chip remove), never the true delete that would
    // drop the config row remembering that ownership.
    if (m_config->isSyntheticRoot(n)) {
        removeDownloadsRoot();
        return;
    }
    // A user root: true delete of the CONFIG row (VaultConfig::removeRootCompletely,
    // exposed by VaultConfig.h for exactly this affordance), then the union republish —
    // the forgotten root's census is no longer in the confirmed list, so its rows drop,
    // while every surviving root's rows + identity state come through untouched. Files
    // on disk are never touched by either half.
    m_config->removeRootCompletely(n);
    m_offeredThisRun.remove(n);
    m_pendingReviveRoots.remove(n);
    publishAllConfirmed();
}

QStringList VaultLibrary::scanIgnore() const
{
    return m_config ? m_config->scanIgnore() : QStringList();
}

void VaultLibrary::setScanIgnore(const QStringList& needles)
{
    if (!m_config || !m_scanner)
        return;
    m_config->setScanIgnore(needles);
    // The needle layer threads through every walk; republish so the shelves reflect the
    // edit now (durable rows drop for excluded folders) instead of waiting for the next
    // unrelated confirm/watcher scan.
    publishAllConfirmed();
}

void VaultLibrary::dismissCard()
{
    m_candidate.clear();
    m_candidateRoot.clear();
    emit candidateChanged();
    maybePublishPendingRevives();
}

void VaultLibrary::cancelScan()
{
    if (m_scanner)
        m_scanner->cancel();
}
