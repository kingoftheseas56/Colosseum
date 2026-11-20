#include "VaultLibrary.h"
#include "ImdbCatalog.h"
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
#include "VaultLocation.h"
#include "DownloadMediaPaths.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFuture>
#include <QtConcurrent>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <vector>

// Mirror VaultConfig::norm so an offered-root key matches the normalized path in roots().
static QString normPath(const QString& p)
{
    return VaultLocation::normalize(p);
}

// Browse-artwork execution plan, Slice 3 part 2: an Episode or Clip browse node's OWN `path` IS
// the video file (VaultKit::planBrowseLevel's loose-video leaf grammar) — never a group's
// logical group key used by a Film node, so a group lookup cannot answer what the index knows
// about this exact file.
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

// Vault ux uplift S13: does a finished browse row pass the active filter? Empty/absent values
// are no-ops — the unfiltered projection stays the default and every predicate is ANDed. The
// watched-state filter is deliberately absent here: its fact (ProgressStore.watchedMark) lives
// in the Progress store, not the index, so QML applies it over the joined rows
// (VaultApi.filterRowsByWatched) — the same store-boundary split "recently played" follows.
static bool rowPassesFilter(const QVariantMap& row, const QVariantMap& filter)
{
    const QString kind = filter.value(QStringLiteral("kind")).toString();
    if (!kind.isEmpty() && row.value(QStringLiteral("kind")).toString() != kind)
        return false;
    const QString identState = filter.value(QStringLiteral("identState")).toString();
    if (!identState.isEmpty() && row.value(QStringLiteral("state")).toString() != identState)
        return false;
    const QString presence = filter.value(QStringLiteral("presence")).toString();
    const bool away = row.value(QStringLiteral("away")).toBool();
    if (presence == QLatin1String("present") && away)
        return false;
    if (presence == QLatin1String("away") && !away)
        return false;
    return true;
}

static bool filterHasAnyPredicate(const QVariantMap& filter)
{
    return !filter.value(QStringLiteral("kind")).toString().isEmpty()
        || !filter.value(QStringLiteral("identState")).toString().isEmpty()
        || !filter.value(QStringLiteral("presence")).toString().isEmpty();
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
    connect(m_artworkResolver, &VaultArtworkResolver::artReady, this,
            &VaultLibrary::browseArtworkReady);

    // revision tracks committed index truth. VaultIndex::changed() fires after a successful
    // publish()/upsert() only, so a bump always means new published truth to repaint from.
    if (m_index) {
        connect(m_index, &VaultIndex::changed, this, [this]() {
            m_artworkResolver->clearPendingRows();
            ++m_revision;
            emit changed();
            if (m_identifier && !m_index->isPreparingPersonalPaths())
                scheduleAutoIdentify();
        });
    }
    if (m_index) {
        connect(m_index, &VaultIndex::personalPathsPrepared, this, [this](bool success) {
            if (success && m_identity) {
                m_identity->setPersonalPaths(m_index->personalPaths());
                emit identityCeremoniesChanged();
            }
            if (!m_publishAfterPolicy)
                return;
            m_publishAfterPolicy = false;
            if (success) {
                publishAllConfirmed();
            } else {
                setScanning(false);
                setFirstFolderIndexing(false);
                emit indexingFailed();
            }
        });
    }
    if (m_config) {
        updatePersonalPaths();
        connect(m_config, &VaultConfig::personalPathsChanged, this, &VaultLibrary::updatePersonalPaths);
    }
    if (m_identity) {
        connect(m_identity, &VaultIdentity::changed, this,
                &VaultLibrary::identityCeremoniesChanged);
    }
    if (m_scanner) {
        // Live census progress → the scan pill.
        connect(m_scanner, &VaultScanner::scanActivity, this, &VaultLibrary::setScanActivity);
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
        connect(m_scanner, &VaultScanner::publicationFinished, this,
                [this](bool published, bool cancelled, bool scannedUserRoot) {
                    const bool first = m_firstFolderIndexing;
                    const bool completionSaved = !first || !published || !scannedUserRoot
                        || m_config->markFolderImportComplete();
                    setScanning(false);
                    setFirstFolderIndexing(false);
                    m_revivalRescanInFlight = false;
                    if (published)
                        m_pendingReviveRoots.clear();
                    if (cancelled)
                        emit indexingCancelled();
                    else if (!published || !completionSaved || (first && !scannedUserRoot))
                        emit indexingFailed();
                });
    }

    // ── live shelves (Slice 15): the watcher owns the per-root QFileSystemWatcher + debounce; ──
    // VaultLibrary relays landings to the door (arrivalTick/liveArrival) and new-kind arrivals
    // to the one-slice confirmation card (S11 law). Watch the confirmed roots present at boot.
#ifndef Q_OS_ANDROID
    m_watcher = new VaultWatcher(index, identity, config, this);
    connect(m_watcher, &VaultWatcher::landed, this, &VaultLibrary::onWatcherLanded);
    connect(m_watcher, &VaultWatcher::newKindArrival, this, &VaultLibrary::onWatcherNewKind);
    connect(m_watcher, &VaultWatcher::rootAvailabilityChanged, this,
            &VaultLibrary::onRootAvailabilityChanged);
    connect(m_watcher, &VaultWatcher::scanActivity, this,
            [this](const QString& root, int done, int total, const QString& phase) {
                if (m_scanning)
                    return;
                setScanActivity(root, done, total, phase);
                if (!m_watcherScanning) {
                    m_watcherScanning = true;
                    emit scanningChanged();
                }
            });
    connect(m_watcher, &VaultWatcher::scanEnded, this, [this] {
        if (m_watcherScanning) {
            m_watcherScanning = false;
            emit scanningChanged();
        }
    });
    m_watcher->refresh();
#endif
}

QVariantList VaultLibrary::identityCeremonies() const
{
    if (!m_identity || (m_index && m_index->isPreparingPersonalPaths()))
        return {};
    return m_identity->pendingCeremonies();
}

bool VaultLibrary::decideIdentityCeremony(const QString& relationship, const QString& choice)
{
    if (!m_identity || (m_index && m_index->isPreparingPersonalPaths()))
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

void VaultLibrary::refreshBookCatalog()
{
    scheduleAutoIdentify();
    ++m_revision;
    emit changed();
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
        m_identifier->backfillVideoGenres();
        fileFolderExtras();
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
    if (m_identifier->shouldAutoIdentify(rows)) {
        const VaultIdentifier::Match match = m_identifier->matchGroup(groupKey);
        if (match.adopted && !(match.source == QLatin1String("EPUB") && rows.first().identityId == match.sourceId))
            m_identifier->applyGroup(groupKey, match);
        else if (match.candidateCount > 1)
            // Ambiguous, not absent: record it durably so a tile can wear "Vault isn't sure"
            // instead of looking merely unscanned (browse-face execution plan, Slice 2).
            m_identifier->recordAmbiguous(groupKey, match.candidateCount);
    }
    // Give QML incubation and rendering a frame between groups even when a
    // long queue of unmatched titles is ready to inspect.
    QTimer::singleShot(16, this, &VaultLibrary::runAutoIdentifySlice);
}

void VaultLibrary::fileFolderExtras()
{
    if (!m_index || !m_config) return;
    const auto videos = m_index->rowsForKind(QStringLiteral("video"));
    const QHash<QString, QString> parents = VaultIdentifier::physicalExtrasParents(videos);
    QHash<QString, QStringList> byParent;
    for (auto it = parents.cbegin(); it != parents.cend(); ++it)
        if (m_config->extrasParent(it.key()).isEmpty() && !m_config->extrasDeclined(it.key()) && !m_config->isHidden(it.key()))
            byParent[it.value()].append(it.key());
    bool filed = false;
    for (auto it = byParent.cbegin(); it != byParent.cend(); ++it)
        filed = m_config->setExtrasParent(it.value(), it.key()) || filed;
    for (const auto& row : videos)
        if (!VaultIdentifier::junkClipReason(row).isEmpty() && !parents.contains(row.id)
            && m_config->extrasParent(row.id).isEmpty() && !m_config->extrasDeclined(row.id)
            && !m_config->isHidden(row.id)) {
            m_config->setHidden(row.id, true);
            filed = true;
        }
    if (filed) {
        ++m_revision;
        emit changed();
    }
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

void VaultLibrary::setImdbCatalog(ImdbCatalog* catalog)
{
    if (m_imdbCatalog == catalog) return;
    if (m_imdbCatalog) disconnect(m_imdbCatalog, nullptr, this, nullptr);
    m_imdbCatalog = catalog;
    if (catalog) {
        auto refresh = [this]() { ++m_revision; emit changed(); };
        connect(catalog, &ImdbCatalog::readyChanged, this, [this, catalog, refresh]() {
            refresh();
            // A first download can finish after the index/enrichment pass. Revisit
            // eligible groups through the same sliced pass used for index changes.
            if (catalog->available()) scheduleAutoIdentify();
        });
        connect(catalog, &ImdbCatalog::revisionChanged, this, refresh);
    }
    ++m_revision;
    emit changed();
}

QVariantList VaultLibrary::series(const QString& kind) const
{
    if (!m_index)
        return {};
    // groupsForKind → [{groupKey, subtreePath, groupTitle, kind, count}]. Normalize to the
    // shelf's series shape { key, title, kind, count, subtreePath } (key == groupKey).
    const QVariantList groups = m_index->groupsForKind(kind);
    const auto kindRows = m_index->rowsForKind(kind);
    const int configRevision = m_config ? m_config->revision() : -1;
    const bool countSupplemental = kind == QLatin1String("video")
        && (m_supplementalRevision != m_revision || m_supplementalConfigRevision != configRevision);
    if (countSupplemental) m_supplementalCounts.clear();
    QHash<QString, QList<VaultIndex::FileRow>> rowsByGroup;
    for (const auto& row : kindRows) {
        rowsByGroup[row.groupKey].append(row);
        if (countSupplemental && m_config && !m_config->isHidden(row.id)) {
            const auto parent = m_config->extrasParent(row.id);
            if (!parent.isEmpty()) ++m_supplementalCounts[parent];
        }
    }
    if (countSupplemental) {
        m_supplementalRevision = m_revision;
        m_supplementalConfigRevision = configRevision;
    }
    QVariantList out;
    out.reserve(groups.size());
    for (const QVariant& g : groups) {
        const QVariantMap m = g.toMap();
        const QString groupKey = m.value(QStringLiteral("groupKey")).toString();
        const QList<VaultIndex::FileRow> groupRows = rowsByGroup.value(groupKey);
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
        // An explicitly filed extra stops being an unmatched title while its
        // parent still exists. Removing the parent makes orphaned files visible.
        if (m_config && !groupRows.isEmpty()
            && std::all_of(groupRows.cbegin(), groupRows.cend(), [this](const auto& row) {
                const auto parent = m_config->extrasParent(row.id);
                return !parent.isEmpty() && !m_index->rowsForIdentity(parent).isEmpty();
            }))
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
        s.insert(QStringLiteral("identityTitle"), identityTitle);
        const QString identityId = m.value(QStringLiteral("identityId")).toString();
        QString identityType;
        for (const auto& row : groupRows) {
            if (row.identitySuppressed || row.identityId != identityId || row.identityTitle.isEmpty()) continue;
            if (row.identityType == QLatin1String("movie") || row.identityType == QLatin1String("series")
                || row.identityType == QLatin1String("mini")) { identityType = row.identityType; break; }
        }
        s.insert(QStringLiteral("identityType"), identityType);
        s.insert(QStringLiteral("supplementalCount"),
                 kind == QLatin1String("video") && !groupRows.isEmpty() && !groupRows.first().identitySuppressed
                     ? m_supplementalCounts.value(groupRows.first().identityId) : 0);
        static const QRegularExpression imdbId(QStringLiteral("^(?:imdb:)?(tt[0-9]+)$"));
        const auto imdbMatch = imdbId.match(identityId);
        if (kind == QLatin1String("video") && imdbMatch.hasMatch()) {
            const QString tt = imdbMatch.captured(1);
            if (identityType.isEmpty()) {
                const auto presentation = m_imdbCatalog ? m_imdbCatalog->titlePresentation(tt) : QVariantMap{};
                s.insert(QStringLiteral("identityType"), presentation.value(QStringLiteral("type")));
            }
            // Same public, ID-only artwork endpoints Theatre already uses.
            s.insert(QStringLiteral("backdropUrl"), QStringLiteral("https://live.metahub.space/background/medium/") + tt + QStringLiteral("/img"));
            s.insert(QStringLiteral("logoUrl"), QStringLiteral("https://live.metahub.space/logo/medium/") + tt + QStringLiteral("/img"));
        }
        out.append(s);
    }
    return out;
}

bool VaultLibrary::attachExtrasToShow(const QString& groupKey, const QString& showGroupKey)
{
    if (!m_index || !m_config || groupKey.isEmpty() || groupKey == showGroupKey) return false;
    const auto source = m_index->rowsForGroup(groupKey);
    const auto target = m_index->rowsForGroup(showGroupKey);
    if (source.isEmpty() || target.isEmpty()) return false;
    const auto& parent = target.first();
    if (parent.kind != QLatin1String("video") || parent.identityId.isEmpty()
        || parent.identitySuppressed || parent.identityTitle.isEmpty()) return false;
    bool show = parent.identityType == QLatin1String("series") || parent.identityType == QLatin1String("mini");
    for (const auto& value : seasonFactsForShow(showGroupKey).value(QStringLiteral("seasons")).toList()) {
        const auto season = value.toMap();
        if (!season.value(QStringLiteral("key")).toString().isEmpty()
            && !season.value(QStringLiteral("supplemental")).toBool()) show = true;
    }
    if (!show && m_imdbCatalog) {
        QString tt = parent.identityId;
        if (tt.startsWith(QStringLiteral("imdb:"))) tt.remove(0, 5);
        const auto type = m_imdbCatalog->titlePresentation(tt).value(QStringLiteral("type")).toString();
        show = type == QLatin1String("series") || type == QLatin1String("mini");
    }
    if (!show) return false;
    QStringList ids;
    for (const auto& row : source) {
        if (row.kind != QLatin1String("video") || !row.identityId.isEmpty()
            || row.id.isEmpty() || row.identitySuppressed) return false;
        ids.append(row.id);
    }
    if (!m_config->setExtrasParent(ids, parent.identityId)) return false;
    ++m_revision; emit changed();
    return true;
}

QVariantList VaultLibrary::extrasForShow(const QString& showGroupKey) const
{
    QVariantList out;
    if (!m_index || !m_config) return out;
    const auto parents = m_index->rowsForGroup(showGroupKey);
    if (parents.isEmpty() || parents.first().identityId.isEmpty() || parents.first().identitySuppressed) return out;
    const auto identity = parents.first().identityId;
    for (const auto& row : m_index->rowsForKind(QStringLiteral("video"))) {
        if (m_config->extrasParent(row.id) != identity || m_config->isHidden(row.id)) continue;
        out.append(QVariantMap{{QStringLiteral("id"), row.id}, {QStringLiteral("path"), row.path},
            {QStringLiteral("title"), row.displayTitle}, {QStringLiteral("away"), row.away},
            {QStringLiteral("supplemental"), true}, {QStringLiteral("kind"), row.kind}});
    }
    return out;
}

bool VaultLibrary::detachExtras(const QString& fileId)
{
    if (!m_config || fileId.isEmpty() || m_config->extrasParent(fileId).isEmpty()) return false;
    // Detaching is a decision: record it so the automatic folder filing never re-files this file.
    if (!m_config->declineExtras({fileId})) return false;
    ++m_revision; emit changed();
    return true;
}

QVariantMap VaultLibrary::admissionById() const
{
    return m_index ? m_index->admissionById() : QVariantMap{};
}

QVariantList VaultLibrary::items(const QString& kind, const QString& seriesKey) const
{
    return projectItems(kind, seriesKey, false);
}

QVariantList VaultLibrary::shelfItems(const QString& kind, const QString& seriesKey) const
{
    return projectItems(kind, seriesKey, true);
}

QVariantList VaultLibrary::projectItems(const QString& kind, const QString& seriesKey, bool shelf) const
{
    if (!m_index)
        return {};
    // Decorate each row with a ready-to-bind per-file cover URL, now through the SAME resolver
    // ladder browseAt()/series() use (browse-artwork execution plan, Slice 3 part 2): the row's
    // own local ref (a comic/book archive cover, or a video group's adopted "file://" art) wins;
    // failing that, the resolver's canonical-poster/frame-grab rungs. A remote identityCoverUrl is
    // only ever the resolver's `posterUrl` INPUT now — it never lands in coverUrl directly (that
    // override is what this method STOPS doing here, mirroring the browseAt() fix).
    QVariantList rows = m_index->filesForGroup(kind, seriesKey);
    if (rows.isEmpty() && m_index->rowsForGroup(seriesKey).isEmpty()) {
        // Legacy physical browse keys are distinct from logical group keys. Never fall
        // through just because an existing group belongs to a different medium.
        for (const auto& value : m_index->filesInSubtree(seriesKey))
            if (value.toMap().value(QStringLiteral("kind")).toString() == kind)
                rows.append(value);
    }
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
        if (m_artworkResolver && !shelf) {
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
        if (shelf && rowKind == QLatin1String("video")) {
            const QString topFolder = m.value(QStringLiteral("subfolder")).toString().section(QLatin1Char('/'), 0, 0);
            m.insert(QStringLiteral("extra"), VaultKit::isExtrasDirName(topFolder));
            m.insert(QStringLiteral("seriesKey"), seriesKey);
        }
        const QString identityTitle = m.value(QStringLiteral("identityTitle")).toString();
        if (kind == QLatin1String("book") && m_bookIdentityDisplay && !identityId.isEmpty()) {
            const auto display = m_bookIdentityDisplay(identityId, identityTitle);
            if (!display.isEmpty()) m.insert(QStringLiteral("author"), display.value(QStringLiteral("author")));
        }
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
        QHash<QString, QList<VaultIndex::FileRow>> rowsByGroup;
        for (const auto& row : m_index->rowsForKind(kind))
            rowsByGroup[row.groupKey].append(row);
        for (const QVariant& value : groups) {
            const QVariantMap group = value.toMap();
            const QString key = group.value(QStringLiteral("groupKey")).toString();
            const QList<VaultIndex::FileRow> rows = rowsByGroup.value(key);
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

QVariantList VaultLibrary::browseAt(const QString& rootOrPath, const QString& sort,
                                    const QVariantMap& filter) const
{
    QVariantList out;
    if (m_index && m_index->isPreparingPersonalPaths())
        return out;
    const QVariantList roots = m_config ? m_config->roots() : QVariantList();
    if (VaultLocation::isContentUri(rootOrPath))
        return VaultBrowseAway::indexedBrowseAt(m_index, roots, rootOrPath, sort, filter);

    const QStringList scanIgnore = m_config ? m_config->scanIgnore() : QStringList();
    QList<VaultKit::BrowseNode> nodes;
    if (m_index && m_config && m_config->isSyntheticRoot(rootOrPath)) {
        QMap<QString, VaultKit::BrowseNode> groups;
        for (const auto& row : m_index->rowsForRoot(rootOrPath)) {
            if (!QFileInfo::exists(row.path) || m_config->isHidden(row.id)) continue;
            auto& node = groups[row.groupKey];
            node.key = row.groupKey;
            node.path = row.subtreePath;
            node.displayTitle = row.groupTitle;
            node.nodeType = row.kind == QLatin1String("video")
                    && row.groupKey.contains(QLatin1String("::show::"))
                ? VaultKit::BrowseNodeType::Show : VaultKit::BrowseNodeType::Film;
            ++node.mediaCount;
        }
        nodes = groups.values();
    } else {
        nodes = VaultKit::planBrowseLevel(rootOrPath, scanIgnore, nullptr,
                                           m_index ? m_index->personalPaths() : VaultLocation::PersonalPaths{});
    }
    // Logical keys come from the index; filesystem browsing continues to use physical paths.
    QHash<QString, QString> comicGroupsByPath;
    if (m_index) {
        for (const auto& value : m_index->groupsForKind(QStringLiteral("comic"))) {
            const auto group = value.toMap();
            comicGroupsByPath.insert(normPath(group.value(QStringLiteral("subtreePath")).toString()),
                                     group.value(QStringLiteral("groupKey")).toString());
        }
    }
    auto groupRowsForNode = [this, &comicGroupsByPath](const VaultKit::BrowseNode& node, QString& key) {
        QList<VaultIndex::FileRow> rows;
        if (!m_index) return rows;
        rows = m_index->rowsForGroup(node.key);
        if (rows.isEmpty() && node.key == node.path && normPath(node.path) != node.path) {
            rows = m_index->rowsForGroup(normPath(node.path));
            if (!rows.isEmpty()) key = rows.first().groupKey;
        }
        if (rows.isEmpty() && node.path != node.key)
            rows = m_index->rowsForGroup(node.path);
        if (rows.isEmpty()) {
            const auto logicalKey = comicGroupsByPath.value(normPath(node.path));
            if (!logicalKey.isEmpty()) {
                rows = m_index->rowsForGroup(logicalKey);
                if (!rows.isEmpty()) key = logicalKey;
            }
        }
        return rows;
    };
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
    // Vault ux uplift S12 — the per-node numeric sort keys, gathered only when a non-natural
    // ordering asked for them (the default path pays nothing). Index i parallels out[i]. The
    // title key is computed post-loop from the finished rows so it always sorts by the title
    // the tile actually displays.
    const bool wantTitle = sort == QLatin1String("title");
    const bool wantNewest = sort == QLatin1String("newest");
    const bool wantSize = sort == QLatin1String("size");
    const bool wantFacts = wantTitle || wantNewest || wantSize;
    struct NodeFacts { qint64 newestMs = 0; qint64 sizeBytes = 0; };
    std::vector<NodeFacts> facts;
    if (wantFacts)
        facts.resize(nodes.size());
    // newest/size over a row list the branch already fetched (Film group rows, an Episode/
    // Clip's own exact-path row, a group container's own rows).
    auto accumulate = [&facts](qsizetype idx, const QList<VaultIndex::FileRow>& rows) {
        if (idx < 0 || idx >= static_cast<qsizetype>(facts.size()))
            return;
        for (const VaultIndex::FileRow& row : rows) {
            facts[size_t(idx)].newestMs = qMax(facts[size_t(idx)].newestMs, row.mtimeMs);
            facts[size_t(idx)].sizeBytes += row.size;
        }
    };
    auto subtreeFallback = [this, &facts](qsizetype idx, const QString& path) {
        if (idx < 0 || idx >= static_cast<qsizetype>(facts.size()) || !m_index)
            return;
        qint64 newestMs = 0, sizeBytes = 0;
        if (m_index->subtreeFacts(path, &newestMs, &sizeBytes)) {
            facts[size_t(idx)].newestMs = qMax(facts[size_t(idx)].newestMs, newestMs);
            facts[size_t(idx)].sizeBytes += sizeBytes;
        }
    };
    out.reserve(nodes.size());
    for (qsizetype nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const VaultKit::BrowseNode& n = nodes.at(nodeIdx);
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

        // Decoration uses the indexed logical group, which may differ from the physical path.
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
            accumulate(nodeIdx, leafRows);
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
            accumulate(nodeIdx, leafRows);
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
            // A container that resolves to an indexed group
            // answers its own kind from the rows it holds. A pure ancestor folder — one whose
            // children are groups, not files — has no rows of its own here and honestly keeps ""
            // rather than inventing one from a child; identify is unreachable on a container tile
            // anyway (both cards only emit identifyRequested from the "uncertain" mark, and only a
            // Film node ever reaches that state), so "" costs the identify flow nothing. Fetched
            // once, above the resolver guard, because the artwork lookup below reads the SAME rows.
            QString groupKey = n.key;
            const auto containerRows = groupRowsForNode(n, groupKey);
            m.insert(QStringLiteral("key"), groupKey);
            kind = dominantRowKind(containerRows);
            // S12: a group container's own rows carry its newest/size; a pure ancestor
            // folder (no rows of its own) takes the subtree-prefix aggregate instead —
            // filesInSubtree is exact-match, hence VaultIndex::subtreeFacts.
            accumulate(nodeIdx, containerRows);
            subtreeFallback(nodeIdx, n.path);
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
                VaultArtworkResolver::RowFacts rowFacts;
                rowFacts.rowKey = groupKey;
                rowFacts.kind = VaultKit::browseNodeTypeName(n.nodeType);
                rowFacts.path = n.path;
                for (const VaultIndex::FileRow& row : containerRows) {
                    if (!row.identityId.isEmpty() && !row.identitySuppressed
                        && !row.identityCoverUrl.isEmpty()) {
                        rowFacts.identityId = row.identityId;
                        rowFacts.posterUrl = row.identityCoverUrl;
                        break;
                    }
                }
                const QString resolved = m_artworkResolver->resolve(rowFacts);
                if (!resolved.isEmpty())
                    m.insert(QStringLiteral("coverRef"), resolved);
            }
        } else if (n.nodeType == VaultKit::BrowseNodeType::Film && m_index) {
            QString groupKey = n.key;
            const auto rows = groupRowsForNode(n, groupKey);
            m.insert(QStringLiteral("key"), groupKey);
            accumulate(nodeIdx, rows); // S12: the group's own rows carry its newest/size
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
                    VaultArtworkResolver::RowFacts rowFacts;
                    rowFacts.rowKey = groupKey;
                    rowFacts.kind = factKind;
                    rowFacts.path = factPath;
                    rowFacts.localRef = localRef;
                    rowFacts.identityId = coverIdentityId;
                    rowFacts.posterUrl = posterUrl;
                    rowFacts.size = factSize;
                    rowFacts.mtimeMs = factMtimeMs;
                    rowFacts.durationSec = factDurationSec;
                    const QString resolved = m_artworkResolver->resolve(rowFacts);
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
    // Vault ux uplift S12 — the non-natural orderings reorder the FINISHED rows (titles are
    // final there, including the Film branch's stored-identity preference). A permutation is
    // sorted instead of the rows themselves so the natural-key tiebreak can address facts[i]
    // and out[i] in lockstep. Stable by construction: equal keys keep planBrowseLevel's §4.2
    // order (folders < shows < films), and the explicit natural-key tiebreak makes the result
    // deterministic even across SQL/walk orderings.
    if (wantFacts) {
        // naturalSortKey once per row: "title" IS this order, and newest/size tie-break by it.
        std::vector<QString> naturalKeys;
        naturalKeys.reserve(out.size());
        for (const QVariant& rv : out)
            naturalKeys.push_back(VaultIndex::naturalSortKey(
                rv.toMap().value(QStringLiteral("displayTitle")).toString()));
        std::vector<int> order(int(out.size()));
        for (int i = 0; i < int(out.size()); ++i)
            order[size_t(i)] = i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            if (wantNewest) {
                if (facts[size_t(a)].newestMs != facts[size_t(b)].newestMs)
                    return facts[size_t(a)].newestMs > facts[size_t(b)].newestMs;
            } else if (wantSize) {
                if (facts[size_t(a)].sizeBytes != facts[size_t(b)].sizeBytes)
                    return facts[size_t(a)].sizeBytes > facts[size_t(b)].sizeBytes;
            }
            return naturalKeys[size_t(a)].compare(naturalKeys[size_t(b)]) < 0;
        });
        QVariantList sorted;
        sorted.reserve(out.size());
        for (int i : order)
            sorted.append(out.at(i));
        out = sorted;
    }
    // Vault ux uplift S13 — the filter pass, applied LAST (over finished, ordered rows: the
    // sort already ran, so a filter toggle cannot reshuffle the survivors). Absent
    // predicates (or no filter at all) keep the projection byte-for-byte unfiltered.
    if (filterHasAnyPredicate(filter)) {
        QVariantList kept;
        kept.reserve(out.size());
        for (const QVariant& rv : out) {
            if (rowPassesFilter(rv.toMap(), filter))
                kept.append(rv);
        }
        out = kept;
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
        row.insert(QStringLiteral("synthetic"), m.value(QStringLiteral("synthetic")));
        const QString name = VaultLocation::isContentUri(path)
            ? QString() : QFileInfo(path).fileName();
        row.insert(QStringLiteral("name"), name.isEmpty() ? path : name);
        const QList<VaultIndex::FileRow> rootRows
            = m_index ? m_index->rowsForRoot(path) : QList<VaultIndex::FileRow>();
        const bool available = m.value(QStringLiteral("synthetic")).toBool() || (VaultLocation::isContentUri(path)
            ? (rootRows.isEmpty() || !rootRows.first().away)
            : QDir(path).exists());
        row.insert(QStringLiteral("available"), available);
        row.insert(QStringLiteral("fileCount"), rootRows.size());
        QSet<QString> groupKeys;
        for (const auto& file : rootRows)
            if (!m_config->isHidden(file.id)) groupKeys.insert(file.groupKey);
        row.insert(QStringLiteral("itemCount"), groupKeys.size());
        // Vault ux uplift S11 — per-root error facts, the "needs attention" affordance's data.
        // One walk supplies both counts: `errorCount` is the total rows carrying any recorded
        // error state (corrupt/rejected — the store's admissionDetail/errorDetail rows); the
        // capped `errorItems` list is the plain "path · reason" list the rail's attention panel
        // shows (capped so one bad root with thousands of rows cannot build a million-triangle
        // list; the count stays total). Reasons are never fabricated: a row whose ErrorDetail
        // is empty but whose admission verdict is a rejection still lists the verdict's state
        // name as its reason (the store's own human text preference falls back the same way).
        // `watcherDegraded` folds BOTH watcher failure classes a root can carry — root-watch
        // registration failure (QFSW limits / network drives) and the recursive-registration
        // over-budget class (the 512-dir cap, m_treeDegraded); the rail states the
        // rescan-on-open consequence for whichever is true.
        int errorCount = 0;
        QVariantList errorItems;
        const int kItemCap = 8;
        const bool watcherDegraded = m_watcher && (m_watcher->isRootDegraded(path)
                                                   || m_watcher->isTreeWatchDegraded(path));
        for (const VaultIndex::FileRow& r : rootRows) {
            if (r.errorState.isEmpty())
                continue;
            ++errorCount;
            if (errorItems.size() < kItemCap) {
                QVariantMap item;
                item.insert(QStringLiteral("path"), r.path);
                const QString reason = !r.errorDetail.isEmpty() ? r.errorDetail : r.errorState;
                item.insert(QStringLiteral("reason"), reason);
                errorItems.append(item);
            }
        }
        row.insert(QStringLiteral("errorCount"), errorCount);
        row.insert(QStringLiteral("errorItems"), errorItems);
        row.insert(QStringLiteral("watcherDegraded"), watcherDegraded);
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

QVariantList VaultLibrary::searchLibrary(const QString& query, int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    const int groupCap = limit > 0 ? limit : 60;
    // Headroom: many matching FILES can share one group (a season of episodes); the fold
    // below collapses them, so fetch several files per group slot before capping groups.
    const QList<VaultIndex::FileRow> rows = m_index->rowsMatching(query, groupCap * 6);
    struct GroupFold {
        int rowCount = 0;
        bool anyAway = false;
        bool ambiguous = false;
        QString identityTitle;   // first non-suppressed adopted title wins
        QString firstVideoId;    // the S6 join key
        QString firstId;
        QString firstPath;       // a single-file group's own path (the Play/Details target)
        QString subtreePath;
        QString crumbFact;       // "root / subfolder" — where the hit lives
        QList<VaultIndex::FileRow> rowsForKind;
    };
    QMap<QString, GroupFold> groups; // first-seen order = mtime-desc (rowsMatching's ORDER BY)
    for (const VaultIndex::FileRow& row : rows) {
        if (m_config && m_config->isHidden(row.id))
            continue; // the Hidden shelf's own population — search never resurrects it
        GroupFold& g = groups[row.groupKey];
        if (++g.rowCount == 1) {
            g.firstPath = row.path;
            g.firstId = row.id;
            g.subtreePath = row.subtreePath;
            const QString rootName = QFileInfo(row.rootPath).fileName();
            g.crumbFact = row.subfolder.isEmpty()
                ? rootName : (rootName + QStringLiteral(" / ") + row.subfolder);
            if (groups.size() > groupCap)
                break; // the cap is on GROUPS, not files
        }
        if (row.away)
            g.anyAway = true;
        if (row.identityState == QLatin1String("ambiguous"))
            g.ambiguous = true;
        if (g.identityTitle.isEmpty() && !row.identityId.isEmpty()
            && !row.identitySuppressed && !row.identityTitle.isEmpty()) {
            g.identityTitle = row.identityTitle;
        }
        if (g.firstVideoId.isEmpty() && row.kind == QLatin1String("video")
            && !row.id.isEmpty()) {
            g.firstVideoId = row.id;
        }
        g.rowsForKind.append(row);
    }
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const GroupFold& g = it.value();
        if (g.rowCount == 0)
            continue; // a fully-hidden group never entered the fold; belt and braces
        QVariantMap m;
        m.insert(QStringLiteral("key"), it.key());
        m.insert(QStringLiteral("nodeType"),
                 g.rowCount == 1 ? QStringLiteral("film") : QStringLiteral("show"));
        // The title the tile displays: the adopted identity's title when there is one, else
        // the census group title — the same preference recentArrivals holds.
        QString title = g.identityTitle;
        if (title.isEmpty()) {
            for (const VaultIndex::FileRow& row : g.rowsForKind) {
                if (!row.groupTitle.isEmpty()) {
                    title = row.groupTitle;
                    break;
                }
            }
        }
        m.insert(QStringLiteral("displayTitle"), title);
        m.insert(QStringLiteral("physicalFact"), g.crumbFact);
        m.insert(QStringLiteral("path"), g.rowCount == 1 ? g.firstPath : g.subtreePath);
        m.insert(QStringLiteral("coverRef"), QString());
        m.insert(QStringLiteral("kind"), dominantRowKind(g.rowsForKind));
        const QString joinId = !g.firstVideoId.isEmpty() ? g.firstVideoId : g.firstId;
        if (!joinId.isEmpty())
            m.insert(QStringLiteral("id"), joinId);
        m.insert(QStringLiteral("state"), !g.identityTitle.isEmpty()
                 ? QStringLiteral("identified")
                 : g.ambiguous ? QStringLiteral("uncertain") : QStringLiteral("resolving"));
        m.insert(QStringLiteral("away"), g.anyAway);
        QVariantMap counts;
        counts.insert(QStringLiteral("items"), g.rowCount);
        m.insert(QStringLiteral("counts"), counts);
        out.append(m);
        if (out.size() >= groupCap)
            break;
    }
    return out;
}

// Vault ux uplift S17 — the derived show structure a show page needs WITHOUT knowing what the
// user watched: one entry per season (ordinal via VaultKit::seasonOrdinalFromDirName — the
// planBrowseLevel rule; an unrecognized subfolder counts as season 0 "extras-ish", honestly
// unparsed), each with its total + the ordered episode list the page joins watched state
// against ({path, id, season, episode, title}). Derivation ONLY: no watcher, no identity
// state, no persistence (the encyclopedia's no-grouping trap — this is per-call, rebuildable
// from the index rows, which are themselves a rebuildable product). A key that holds no rows
// (a sibling-collapsed virtual show sentinel, an unscanned folder) returns empty — honest.
QVariantMap VaultLibrary::seasonFactsForShow(const QString& showFolderPath) const
{
    QVariantMap out;
    if (!m_index || showFolderPath.isEmpty())
        return out;
    const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(showFolderPath);
    if (rows.isEmpty())
        return out;
    struct SeasonEntry {
        int ordinal = 0;
        QString key;
        QString label;
        bool supplemental = false;
        QList<VaultIndex::FileRow> episodes; // natural order set at the end
    };
    QMap<QString, SeasonEntry> seasons;
    QStringList siblingPaths;
    for (const auto& row : rows) siblingPaths.append(row.path);
    for (const VaultIndex::FileRow& row : rows) {
        int ord = 0;
        QString entryKey;
        QString entryLabel;
        bool supplemental = false;
        const QStringList relParts = row.subfolder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (int i = 0; i < relParts.size(); ++i) {
            const QString& part = relParts.at(i);
            if (!VaultKit::isSeasonLikeDirName(part))
                continue;
            ord = VaultKit::seasonOrdinalFromDirName(part);
            entryKey = QDir(showFolderPath).filePath(relParts.mid(0, i + 1).join(QLatin1Char('/')));
            entryLabel = QStringLiteral("Season %1").arg(ord);
            break;
        }
        if (entryKey.isEmpty()) {
            const auto episode = VaultKit::videoEpisodeContext(row.path, siblingPaths).number;
            const bool extra = std::any_of(relParts.cbegin(), relParts.cend(),
                [](const QString& part) { return VaultKit::isExtrasDirName(part); });
            // Release-named season folders need not be bare "Season N". Use
            // the held file's numbering, never a season span claimed by its folder.
            if (episode.matched && !extra) {
                ord = episode.absolute ? 1 : episode.season;
                entryKey = showFolderPath + QStringLiteral("::season::") + QString::number(ord);
                entryLabel = ord == 0 ? QStringLiteral("Specials") : QStringLiteral("Season %1").arg(ord);
            }
        }
        if (entryKey.isEmpty() && !relParts.isEmpty()) {
            const QString top = relParts.first();
            entryKey = QDir(showFolderPath).filePath(top);
            entryLabel = VaultKit::cleanMediaFolderTitle(top);
            if (entryLabel.isEmpty())
                entryLabel = top;
            supplemental = true;
        }
        const QString mapKey = entryKey.isEmpty()
            ? QStringLiteral("__flat__")
            : QStringLiteral("path:") + normPath(entryKey);
        SeasonEntry& s = seasons[mapKey];
        s.ordinal = ord;
        s.key = entryKey;
        s.label = entryLabel;
        s.supplemental = supplemental;
        s.episodes.append(row);
    }
    QList<SeasonEntry> orderedSeasons = seasons.values();
    QCollator seasonCollator;
    seasonCollator.setNumericMode(true);
    seasonCollator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(orderedSeasons.begin(), orderedSeasons.end(),
              [&seasonCollator](const SeasonEntry& a, const SeasonEntry& b) {
                  if (a.supplemental != b.supplemental)
                      return !a.supplemental;
                  if (!a.supplemental && a.ordinal != b.ordinal)
                      return a.ordinal < b.ordinal;
                  return seasonCollator.compare(a.label, b.label) < 0;
              });
    QVariantList seasonList;
    for (SeasonEntry& s : orderedSeasons) {
        std::sort(s.episodes.begin(), s.episodes.end(),
                  [](const VaultIndex::FileRow& a, const VaultIndex::FileRow& b) {
                      if (a.sortKey != b.sortKey)
                          return a.sortKey < b.sortKey;
                      return a.realName < b.realName;
                  });
        QVariantList season;
        for (const VaultIndex::FileRow& row : s.episodes) {
            QVariantMap ep;
            ep.insert(QStringLiteral("path"), row.path);
            ep.insert(QStringLiteral("id"), row.id);
            const VaultKit::SeasonEpisode parsed = VaultKit::videoEpisodeContext(row.path, siblingPaths).number;
            ep.insert(QStringLiteral("season"), parsed.season ? parsed.season : s.ordinal);
            ep.insert(QStringLiteral("episode"), parsed.episode);
            ep.insert(QStringLiteral("title"),
                      row.displayTitle.isEmpty() ? row.realName : row.displayTitle);
            season.append(ep);
        }
        QVariantMap sm;
        sm.insert(QStringLiteral("season"), s.ordinal);
        sm.insert(QStringLiteral("key"), s.key);
        sm.insert(QStringLiteral("label"), s.label);
        sm.insert(QStringLiteral("supplemental"), s.supplemental);
        sm.insert(QStringLiteral("total"), s.episodes.size());
        sm.insert(QStringLiteral("episodes"), season);
        seasonList.append(sm);
    }
    out.insert(QStringLiteral("seasons"), seasonList);
    out.insert(QStringLiteral("total"), rows.size());
    return out;
}

QVariantMap VaultLibrary::browseDetail(const QString& key) const
{
    const QStringList scanIgnore = m_config ? m_config->scanIgnore() : QStringList();
    return VaultBrowseDetail::detailFor(m_index, key, scanIgnore);
}

QVariantMap VaultLibrary::shelfEpisode(const QString& seriesKey, const QString& id) const
{
    if (!m_index)
        return {};
    const auto rows = m_index->rowsForGroup(seriesKey);
    QStringList siblings;
    const VaultIndex::FileRow* selected = nullptr;
    for (const auto& row : rows) {
        siblings.append(row.path);
        if (row.id == id)
            selected = &row;
    }
    if (!selected)
        return {};
    const auto episode = VaultKit::videoEpisodeContext(selected->path, siblings).number;
    if (!episode.matched || episode.episode <= 0)
        return {};
    const auto parts = selected->subfolder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const auto seasonFolder = std::find_if(parts.cbegin(), parts.cend(), VaultKit::isSeasonLikeDirName);
    if (seasonFolder == parts.cend() && std::any_of(parts.cbegin(), parts.cend(), VaultKit::isExtrasDirName))
        return {};
    const int season = episode.season ? episode.season
        : seasonFolder == parts.cend() ? (episode.absolute ? 1 : 0)
                                       : VaultKit::seasonOrdinalFromDirName(*seasonFolder);
    return {{QStringLiteral("season"), season}, {QStringLiteral("episode"), episode.episode}};
}

QVariantMap VaultLibrary::presentationDetail(const QString& key) const
{
    return VaultBrowseDetail::presentationFor(m_index, key);
}

QString VaultLibrary::browseEmptyCause(const QString& rootOrPath,
                                       const QVariantMap& filter) const
{
    const QVariantList roots = m_config ? m_config->roots() : QVariantList();
    const bool hasAnyRoots = !rootsDetail().isEmpty();
    const bool filterActive = filterHasAnyPredicate(filter);
    const bool levelHasRows = !browseAt(rootOrPath, QStringLiteral("natural"), filter).isEmpty();
    // Vault ux uplift S13 — the fourth cause's production trigger: the filtered projection is
    // empty but the level HAS rows unfiltered. The extra walk is paid only on the
    // filtered-empty path (a filtered level that still shows rows classifies from ONE walk,
    // exactly as before).
    bool filteredOut = false;
    if (!levelHasRows && filterActive)
        filteredOut = !browseAt(rootOrPath).isEmpty();
    // See VaultBrowseEmpty::isLevelAway's own comment for why the index's row-based away flag
    // alone is not enough. Scoped to THIS method only — VaultBrowseAway::ownerRootAway itself is
    // untouched so Slice 6's own pinned tests/behavior stay exactly as they are.
    const QString ownerRoot = VaultBrowseAway::ownerRootPath(roots, rootOrPath);
    const bool ownerDirExists = ownerRoot.isEmpty() || (m_config && m_config->isSyntheticRoot(ownerRoot))
        || QDir(ownerRoot).exists();
    const bool levelAway = VaultBrowseEmpty::isLevelAway(
        VaultBrowseAway::ownerRootAway(m_index, roots, rootOrPath),
        !ownerRoot.isEmpty(), ownerDirExists);
    return VaultBrowseEmpty::causeName(
        VaultBrowseEmpty::classify(hasAnyRoots, levelHasRows, levelAway, filteredOut));
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
    match.type = chosenIdentity.value(QStringLiteral("type")).toString();
    match.synopsis = chosenIdentity.value(QStringLiteral("synopsis")).toString();
    match.coverUrl = chosenIdentity.value(QStringLiteral("coverUrl")).toString();
    match.world = chosenIdentity.value(QStringLiteral("world")).toString();
    match.year = chosenIdentity.value(QStringLiteral("year")).toInt();
    match.genres = VaultIdentifier::videoGenres(chosenIdentity.value(QStringLiteral("genres")));
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
    const auto rows = m_index->rowsForGroup(groupKey);
    QString selector = groupKey;
    if (!rows.isEmpty() && rows.first().kind == QLatin1String("comic")
        && rows.first().subtreePath != groupKey
        && std::any_of(rows.cbegin(), rows.cend(), [](const auto& row) {
            return VaultKit::kindForFile(row.path) != VaultKit::MediaKind::Comic;
        }))
        selector = rows.first().subtreePath;
    if (!m_identifier->reshelveGroup(groupKey, kind))
        return false;
    if (m_config)
        m_config->setKind(selector, kind);
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
#elif defined(Q_OS_LINUX)
    const QFileInfo fi(path);
    if (path.trimmed().isEmpty() || !fi.exists())
        return false;
    const QString xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
    if (xdgOpen.isEmpty())
        return false;
    // Freedesktop file managers do not share Explorer's /select contract. Open the
    // containing directory for a file, or the directory itself for a folder.
    const QString folder = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    return QProcess::startDetached(xdgOpen, QStringList{folder});
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
    if (m_watcher) m_watcher->setPublishing(scanning);
    emit scanningChanged();
    emit scanProgressChanged();
}

void VaultLibrary::setScanActivity(const QString& root, int done, int total, const QString& phase)
{
    if (m_scanningRoot == root && m_scanDone == done && m_scanTotal == total && m_scanPhase == phase)
        return;
    m_scanningRoot = root;
    m_scanDone = done;
    m_scanTotal = total;
    m_scanPhase = phase;
    emit scanProgressChanged();
}

void VaultLibrary::setFirstFolderIndexing(bool first)
{
    if (first == m_firstFolderIndexing)
        return;
    m_firstFolderIndexing = first;
    emit firstFolderIndexingChanged();
}

namespace {
constexpr int kFolderSelectionPageSize = 96;

QVariantMap folderSelectionLocation(const QFileInfo& info)
{
    const QString absolute = info.absoluteFilePath();
    const QString canonical = info.canonicalFilePath();
    const QString path = !canonical.isEmpty() && normPath(canonical) == normPath(absolute)
        ? canonical : absolute;
    const QStorageInfo storage(path);
    QString mount = QDir::fromNativeSeparators(storage.rootPath());
    QString volume = storage.name().trimmed();
#ifdef Q_OS_WIN
    if (path.size() >= 3 && path.at(1) == QLatin1Char(':')) {
        mount = path.left(3);
        const QString drive = path.left(2).toUpper();
        volume = volume.isEmpty() ? drive : VaultLibrary::tr("%1 (%2)").arg(volume, drive);
    }
#endif
    if (mount.isEmpty())
        mount = QStringLiteral("/");
    if (volume.isEmpty())
        volume = QDir::toNativeSeparators(mount);
    const auto segments = QDir(mount).relativeFilePath(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return {{QStringLiteral("volume"), volume},
            {QStringLiteral("segments"), segments == QStringList{QStringLiteral(".")} ? QStringList{} : segments},
            {QStringLiteral("nativePath"), QDir::toNativeSeparators(path)}};
}

struct FolderSelectionPage {
    QVariantList entries;
    QVariantMap location;
    bool more = false;
    QString error;
};
}

struct VaultLibrary::FolderCursor {
    explicit FolderCursor(QString path) : directory(std::move(path)) {}
    QString directory;
    std::unique_ptr<QDirIterator> entries;
    std::atomic<bool> cancelled{false};
};

QVariantMap VaultLibrary::folderSelection() const
{
    if (m_selectionRoot.isEmpty())
        return {};
    QVariantList entries = m_selectionEntries;
    qsizetype includedCount = 0;
    for (QVariant& value : entries) {
        auto entry = value.toMap();
        const QString path = entry.value(QStringLiteral("path")).toString();
        const bool dir = entry.value(QStringLiteral("directory")).toBool();
        const QString normalized = normPath(path);
        const auto& explicitPaths = dir ? m_selectionDraft.directories : m_selectionDraft.files;
        const bool excluded = m_selectionDraft.excludes(path, dir);
        entry.insert(QStringLiteral("included"), !excluded);
        entry.insert(QStringLiteral("inherited"), excluded && !explicitPaths.contains(normalized));
        if (!excluded)
            ++includedCount;
        value = entry;
    }
    auto belongsToRoot = [this](const QString& path) {
        return path != m_selectionRoot && VaultLocation::containsPath(m_selectionRoot, path);
    };
    const auto excludedCount = std::count_if(m_selectionDraft.directories.cbegin(), m_selectionDraft.directories.cend(), belongsToRoot)
        + std::count_if(m_selectionDraft.files.cbegin(), m_selectionDraft.files.cend(), belongsToRoot);
    return {{QStringLiteral("root"), m_selectionRoot},
            {QStringLiteral("directory"), m_selectionDirectory},
            {QStringLiteral("location"), m_selectionPersonalView ? m_selectionRootLocation : m_selectionLocation},
            {QStringLiteral("entries"), entries},
            {QStringLiteral("includedCount"), includedCount},
            {QStringLiteral("excludedCount"), excludedCount},
            {QStringLiteral("canGoUp"), normPath(m_selectionDirectory) != m_selectionRoot},
            {QStringLiteral("busy"), m_selectionBusy},
            {QStringLiteral("more"), m_selectionMore},
            {QStringLiteral("previous"), m_selectionPersonalView ? m_selectionOffset > 0 : m_selectionPage > 0},
            {QStringLiteral("page"), 1 + (m_selectionPersonalView ? m_selectionOffset / kFolderSelectionPageSize : m_selectionPage)},
            {QStringLiteral("canSave"), !m_selectionDraft.excludes(m_selectionRoot, true)
                && (m_selectionRootReady || m_config->isRootConfirmed(m_selectionRoot))},
            {QStringLiteral("personalView"), m_selectionPersonalView},
            {QStringLiteral("editing"), m_config->isRootConfirmed(m_selectionRoot)},
            {QStringLiteral("error"), m_selectionError}};
}

void VaultLibrary::openFolderSelection(const QString& pathOrUrl)
{
    if (!m_config || !m_scanner)
        return;
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (!QDir::isAbsolutePath(path) || VaultLocation::isContentUri(path) || m_config->isSyntheticRoot(path))
        return;
    cancelFolderSelection();
    if (cardVisible())
        dismissCard();
    m_selectionRoot = normPath(path);
    m_selectionDraft = m_config->personalPaths();
    readFolderSelection(QDir::cleanPath(path));
}

void VaultLibrary::readFolderSelection(const QString& path)
{
    if (m_folderCursor) m_folderCursor->cancelled.store(true);
    m_folderCursor = std::make_shared<FolderCursor>(path);
    m_selectionDirectory = path;
    m_selectionLocation = normPath(path) == m_selectionRoot ? m_selectionRootLocation : QVariantMap{};
    m_selectionError.clear();
    m_selectionEntries.clear();
    m_selectionPages.clear();
    m_selectionPage = 0;
    m_selectionBusy = false;
    m_selectionMore = false;
    m_selectionCursorMore = false;
    if (m_selectionDraft.excludes(path, true)) {
        m_selectionError = tr("An enclosing folder excludes this location. Open that source to include it again.");
        emit folderSelectionChanged();
        return;
    }
    fetchFolderSelectionPage();
}

void VaultLibrary::fetchFolderSelectionPage()
{
    if (!m_folderCursor || m_selectionBusy)
        return;
    const auto cursor = m_folderCursor;
    m_selectionBusy = true;
    emit folderSelectionChanged();
    auto work = QtConcurrent::run([cursor]() {
        FolderSelectionPage page;
        if (!cursor->entries) {
            const QFileInfo info(cursor->directory);
            if (!info.isDir() || !info.isReadable() || !VaultKit::isDirectDirectory(info)) {
                page.error = VaultLibrary::tr("This folder is unavailable or cannot be read.");
                return page;
            }
            page.location = folderSelectionLocation(info);
            cursor->entries = std::make_unique<QDirIterator>(cursor->directory,
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        }
        // Limit examined entries as well as results, even in drives with lots of system/junction entries.
        for (int i = 0; i < kFolderSelectionPageSize && !cursor->cancelled.load()
             && cursor->entries->hasNext(); ++i) {
            cursor->entries->next();
            const QFileInfo entry = cursor->entries->fileInfo();
            const bool directory = entry.isDir();
            if (entry.isJunction() || (directory && VaultKit::isIgnoredDir(entry.fileName())))
                continue;
            page.entries.append(QVariantMap{{QStringLiteral("path"), entry.absoluteFilePath()},
                                            {QStringLiteral("name"), entry.fileName()},
                                            {QStringLiteral("directory"), directory}});
        }
        page.more = !cursor->cancelled.load() && cursor->entries->hasNext();
        return page;
    });
    work.then(this, [this, cursor](FolderSelectionPage page) {
        if (m_folderCursor != cursor || m_selectionPersonalView)
            return;
        m_selectionBusy = false;
        if (!page.location.isEmpty())
            m_selectionLocation = std::move(page.location);
        if (normPath(cursor->directory) == m_selectionRoot) {
            m_selectionRootLocation = m_selectionLocation;
            m_selectionRootReady = page.error.isEmpty();
        }
        m_selectionEntries = std::move(page.entries);
        m_selectionPage = m_selectionPages.size();
        m_selectionPages.append(m_selectionEntries);
        m_selectionCursorMore = m_selectionMore = page.more;
        m_selectionError = std::move(page.error);
        emit folderSelectionChanged();
    });
}

void VaultLibrary::browseFolderSelection(const QString& childPath)
{
    if (m_selectionBusy || m_selectionPersonalView || m_selectionRoot.isEmpty()
        || m_selectionDraft.excludes(childPath, true))
        return;
    for (const QVariant& value : m_selectionEntries) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("directory")).toBool()
            && entry.value(QStringLiteral("path")).toString() == childPath) {
            readFolderSelection(childPath);
            return;
        }
    }
}

void VaultLibrary::folderSelectionUp()
{
    if (m_selectionPersonalView || m_selectionRoot.isEmpty()
        || normPath(m_selectionDirectory) == m_selectionRoot)
        return;
    const QString parent = QFileInfo(m_selectionDirectory).path();
    if (VaultLocation::containsPath(m_selectionRoot, normPath(parent)))
        readFolderSelection(parent);
}

void VaultLibrary::nextFolderSelectionPage()
{
    if (!m_selectionMore || m_selectionBusy)
        return;
    if (m_selectionPersonalView) {
        m_selectionOffset += kFolderSelectionPageSize;
        loadPersonalFolderChoices();
    } else if (m_selectionPage + 1 < m_selectionPages.size()) {
        m_selectionEntries = m_selectionPages.at(++m_selectionPage);
        m_selectionMore = m_selectionPage + 1 < m_selectionPages.size() || m_selectionCursorMore;
        emit folderSelectionChanged();
    } else {
        fetchFolderSelectionPage();
    }
}

void VaultLibrary::previousFolderSelectionPage()
{
    if (m_selectionBusy)
        return;
    if (m_selectionPersonalView && m_selectionOffset > 0) {
        m_selectionOffset -= kFolderSelectionPageSize;
        loadPersonalFolderChoices();
    } else if (!m_selectionPersonalView && m_selectionPage > 0) {
        m_selectionEntries = m_selectionPages.at(--m_selectionPage);
        m_selectionMore = true;
        emit folderSelectionChanged();
    }
}

void VaultLibrary::showPersonalFolderChoices(bool show)
{
    if (m_selectionRoot.isEmpty() || show == m_selectionPersonalView)
        return;
    m_selectionPersonalView = show;
    if (!show) {
        readFolderSelection(m_selectionRoot);
        return;
    }
    if (m_folderCursor) m_folderCursor->cancelled.store(true);
    m_folderCursor.reset();
    m_selectionBusy = false;
    m_selectionOffset = 0;
    loadPersonalFolderChoices();
}

void VaultLibrary::loadPersonalFolderChoices()
{
    m_selectionEntries.clear();
    m_selectionError.clear();
    m_selectionMore = false;
    int index = 0;
    auto append = [&](const QSet<QString>& paths, bool directory) {
        QStringList sorted = paths.values();
        sorted.sort(Qt::CaseInsensitive);
        for (const QString& path : sorted) {
            if (path == m_selectionRoot || !VaultLocation::containsPath(m_selectionRoot, path))
                continue;
            if (index++ < m_selectionOffset)
                continue;
            if (m_selectionEntries.size() >= kFolderSelectionPageSize) {
                m_selectionMore = true;
                break;
            }
            const QString name = QDir(m_selectionRoot).relativeFilePath(path);
            m_selectionEntries.append(QVariantMap{{QStringLiteral("path"), path},
                                                   {QStringLiteral("name"), name},
                                                   {QStringLiteral("directory"), directory}});
        }
    };
    append(m_selectionDraft.directories, true);
    if (!m_selectionMore) append(m_selectionDraft.files, false);
    emit folderSelectionChanged();
}

void VaultLibrary::toggleFolderSelectionEntry(const QString& path, bool directory)
{
    if (m_selectionRoot.isEmpty() || m_selectionBusy)
        return;
    for (const QVariant& value : m_selectionEntries) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("path")).toString() != path
            || entry.value(QStringLiteral("directory")).toBool() != directory)
            continue;
        const QString normalized = normPath(path);
        auto& paths = directory ? m_selectionDraft.directories : m_selectionDraft.files;
        if (!paths.remove(normalized) && !m_selectionDraft.excludes(path, directory))
            paths.insert(normalized);
        if (m_selectionPersonalView) {
            // Keep the current page populated when restoring its last choice.
            if (m_selectionEntries.size() == 1 && m_selectionOffset > 0)
                m_selectionOffset -= kFolderSelectionPageSize;
            loadPersonalFolderChoices();
        } else {
            emit folderSelectionChanged();
        }
        return;
    }
}

bool VaultLibrary::confirmFolderSelection()
{
    if (m_selectionRoot.isEmpty() || m_selectionBusy
        || !folderSelection().value(QStringLiteral("canSave")).toBool())
        return false;
    if (DownloadMediaPaths::moving() && m_selectionDraft != m_config->personalPaths()) {
        m_selectionError = tr("Finish moving downloads before saving exclusions.");
        emit folderSelectionChanged();
        return false;
    }
    if (!m_config->applyFolderSelection(m_selectionRoot, m_selectionDraft)) {
        m_selectionError = tr("Vault could not save these choices. Try again.");
        emit folderSelectionChanged();
        return false;
    }
    m_offeredThisRun.insert(m_selectionRoot);
    cancelFolderSelection();
    publishAllConfirmed();
    return true;
}

void VaultLibrary::cancelFolderSelection()
{
    if (m_selectionRoot.isEmpty())
        return;
    if (m_folderCursor) m_folderCursor->cancelled.store(true);
    m_folderCursor.reset();
    m_selectionRoot.clear();
    m_selectionDirectory.clear();
    m_selectionLocation.clear();
    m_selectionRootLocation.clear();
    m_selectionEntries.clear();
    m_selectionPages.clear();
    m_selectionDraft = {};
    m_selectionError.clear();
    m_selectionBusy = false;
    m_selectionRootReady = false;
    m_selectionMore = false;
    m_selectionPersonalView = false;
    m_selectionOffset = 0;
    m_selectionPage = 0;
    m_selectionCursorMore = false;
    emit folderSelectionChanged();
}

void VaultLibrary::updatePersonalPaths()
{
    emit personalPathsChanging();
    const auto choices = m_config->personalPaths();
    if (m_scanner && m_scanning)
        m_scanner->invalidate();
    if (m_identity)
        m_identity->setPersonalPaths(choices);
    if (m_index) {
        if (!m_index->setPersonalPaths(choices))
            emit indexingFailed();
        else if (m_identity && !m_index->isPreparingPersonalPaths())
            m_identity->setPersonalPaths(m_index->personalPaths());
    }
    m_candidate.clear();
    m_candidateRoot.clear();
    emit candidateChanged();
    emit personalPathsChanged();
    emit identityCeremoniesChanged();
}

void VaultLibrary::addFolder(const QString& pathOrUrl)
{
#ifndef Q_OS_ANDROID
    openFolderSelection(pathOrUrl);
#else
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
#endif
}

void VaultLibrary::beginCensus(const QString& path)
{
    // Clear any stale candidate, reset the pill, and kick the off-thread census.
    m_candidate.clear();
    m_candidateRoot.clear();
    emit candidateChanged();

    setScanActivity(path, 0, 0, QStringLiteral("discovering"));
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
    if (m_scanning || cardVisible() || !m_selectionRoot.isEmpty())
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
#ifdef Q_OS_ANDROID
        beginCensus(path);
#else
        openFolderSelection(path);
#endif
        return; // one founding choice at a time
    }
}

void VaultLibrary::confirmRoot(const QString& root, const QVariantMap& kindOverrides)
{
    if (!m_scanner || !m_config || !m_selectionRoot.isEmpty())
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
        // Synthetic rows come from their owner below. Scanning a logical root
        // as an absent drive would resurrect deleted downloads as away entries.
        if (m.value(QStringLiteral("synthetic")).toBool())
            continue;
        if (m.value(QStringLiteral("confirmed")).toBool())
            confirmed.append(m.value(QStringLiteral("path")).toString());
    }

#ifndef Q_OS_ANDROID
    setFirstFolderIndexing(!confirmed.isEmpty() && !m_config->hasCompletedFolderImport());
#endif
    if (m_index && m_index->isPreparingPersonalPaths()) {
        m_publishAfterPolicy = true;
        setScanActivity(confirmed.value(0), 0, 0, QStringLiteral("privacy"));
        setScanning(true);
        // Starts another attempt after a failed check, or joins the active one.
        if (!m_index->setPersonalPaths(m_config->personalPaths())) {
            m_publishAfterPolicy = false;
            setScanning(false);
            setFirstFolderIndexing(false);
            emit indexingFailed();
        }
        return;
    }
    m_publishAfterPolicy = false;

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
    setScanActivity(confirmed.value(0), 0, 0, QStringLiteral("discovering"));
    setScanning(true); // shelving
    if (m_watcher)
        m_watcher->refresh();
    const auto personal = m_index ? m_index->personalPaths() : m_config->personalPaths();
    m_scanner->publishConfirmed(confirmed, m_config->scanIgnore(), overrides, extraRows, personal);
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
    if (m_downloadsRoot)
        disconnect(m_downloadsRoot, &VaultDownloadsRoot::changed, this, &VaultLibrary::refreshDownloads);
    m_downloadsRoot = root;
    m_downloadsRootPath = normPath(path);
    if (root)
        connect(root, &VaultDownloadsRoot::changed, this, &VaultLibrary::refreshDownloads);
}

void VaultLibrary::refreshDownloads()
{
    if (!m_config || !m_downloadsRoot || m_downloadsRootPath.isEmpty()) return;
    if (!m_config->hasRoot(m_downloadsRootPath)) {
        if (!m_downloadsRoot->hasContainerDownloads()) return;
        m_config->addSyntheticRoot(m_downloadsRootPath);
    }
    publishAllConfirmed();
}

bool VaultLibrary::immersive() const
{
    if (m_watcher)
        return m_watcher->immersive();
    return m_scanner ? m_scanner->applySuspended() : false;
}

void VaultLibrary::setImmersive(bool on)
{
    const bool changed = immersive() != on;
    if (m_watcher)
        m_watcher->setImmersive(on);
    if (m_scanner)
        m_scanner->setApplySuspended(on);
    if (changed)
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
    if (m_scanning || cardVisible() || !m_selectionRoot.isEmpty() || slices.isEmpty())
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
    if (m_publishAfterPolicy) {
        m_publishAfterPolicy = false;
        setScanning(false);
        setFirstFolderIndexing(false);
        emit indexingCancelled();
        return;
    }
    if (m_scanner)
        m_scanner->cancel();
}
