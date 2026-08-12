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

VaultLibrary::VaultLibrary(VaultIndex* index, VaultScanner* scanner, VaultConfig* config,
                           VaultIdentity* identity, QObject* parent)
    : QObject(parent), m_index(index), m_scanner(scanner), m_config(config), m_identity(identity)
{
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
        // A ready-to-bind cover URL for the tile: image://comiccover/<id> when the group has
        // an enriched comic cover, else empty (the tile falls back to its gradient + icon).
        const QString coverPath = m.value(QStringLiteral("coverPath")).toString();
        const QString coverEntry = m.value(QStringLiteral("coverEntry")).toString();
        const QString identityCover = m.value(QStringLiteral("identityCoverUrl")).toString();
        const QString provider = kind == QLatin1String("book")
            ? QStringLiteral("vaultbookcover") : QStringLiteral("comiccover");
        s.insert(QStringLiteral("coverUrl"), !identityCover.isEmpty() ? identityCover
                 : (!coverPath.isEmpty() && !coverEntry.isEmpty())
                     ? QStringLiteral("image://") + provider + QLatin1Char('/')
                           + Colosseum::buildComicCoverId(coverPath, coverEntry)
                     : QString());
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
    // Decorate each row with a ready-to-bind per-file cover URL (comics carry a CBZ cover entry
    // after enrichment) so the folder view never re-derives the native id in QML. Books/video and
    // un-enriched comics get "" → the row falls back to its kind icon.
    QVariantList rows = m_index->filesInSubtree(seriesKey);
    for (QVariant& v : rows) {
        QVariantMap m = v.toMap();
        const QString coverRef = m.value(QStringLiteral("coverRef")).toString();
        const QString path = m.value(QStringLiteral("path")).toString();
        const QString rowKind = m.value(QStringLiteral("kind")).toString();
        const QString provider = rowKind == QLatin1String("book")
            ? QStringLiteral("vaultbookcover") : QStringLiteral("comiccover");
        m.insert(QStringLiteral("coverUrl"),
                 (!coverRef.isEmpty() && !path.isEmpty()
                  && (rowKind == QLatin1String("comic") || rowKind == QLatin1String("book")))
                     ? QStringLiteral("image://") + provider + QLatin1Char('/')
                           + Colosseum::buildComicCoverId(path, coverRef)
                     : QString());
        const QString identityCover = m.value(QStringLiteral("identityCoverUrl")).toString();
        if (!identityCover.isEmpty())
            m.insert(QStringLiteral("coverUrl"), identityCover);
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
    const QStringList scanIgnore = m_config ? m_config->scanIgnore() : QStringList();
    const QList<VaultKit::BrowseNode> nodes = VaultKit::planBrowseLevel(rootOrPath, scanIgnore);
    out.reserve(nodes.size());
    for (const VaultKit::BrowseNode& n : nodes) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), n.key);
        m.insert(QStringLiteral("nodeType"), VaultKit::browseNodeTypeName(n.nodeType));
        m.insert(QStringLiteral("displayTitle"), n.displayTitle);
        m.insert(QStringLiteral("physicalFact"), n.physicalFact);
        m.insert(QStringLiteral("path"), n.path);
        // Local artwork adoption (Slice 3): a Film node's coverRef, decorated below alongside
        // state/away, is the VaultEnricher-adopted "file://" ref for VIDEO groups only — a
        // comic/book Film node's coverRef stays "" here on purpose, since that column holds a
        // bare in-archive entry name for those kinds (comics/books already have their own
        // image://.../ translation in series()/items(); this field would be meaningless without
        // it). Every other node type carries no per-episode art in this slice.
        m.insert(QStringLiteral("coverRef"), QString());
        QVariantMap counts;
        counts.insert(QStringLiteral("items"), n.mediaCount);
        m.insert(QStringLiteral("counts"), counts);

        // Decoration from today's index: reliable only for a film node, whose path IS a
        // group's subtreePath (one video file, one group — VaultScanner's own convention).
        // Deeper per-episode state and durable ambiguity are Slices 2/6's business; this slice
        // gives every other node an honest "resolving" default rather than inventing one.
        QString state = QStringLiteral("resolving");
        bool away = false;
        if (n.nodeType == VaultKit::BrowseNodeType::Clip) {
            // The kind classifier's own verdict, not an identity lookup (locked design: local-
            // only is certain-and-yours, never "not identified yet").
            state = QStringLiteral("localOnly");
        } else if (n.nodeType == VaultKit::BrowseNodeType::Film && m_index) {
            const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(n.path);
            if (!rows.isEmpty()) {
                bool anyAway = false;
                bool identified = false;
                bool ambiguous = false;
                QString coverRef;
                for (const VaultIndex::FileRow& row : rows) {
                    if (row.away)
                        anyAway = true;
                    if (!row.identityId.isEmpty() && !row.identitySuppressed)
                        identified = true;
                    if (row.identityState == QLatin1String("ambiguous"))
                        ambiguous = true;
                    if (coverRef.isEmpty() && row.kind == QLatin1String("video")
                        && !row.coverRef.isEmpty())
                        coverRef = row.coverRef;
                }
                away = anyAway;
                // identified always wins (identify-in-place settles a previously-ambiguous
                // group through the same durable fact); suppressed reads as resolving
                // (filename-honest, not "unsure" — the user already made a call).
                state = identified ? QStringLiteral("identified")
                      : ambiguous  ? QStringLiteral("uncertain")
                                   : QStringLiteral("resolving");
                if (!coverRef.isEmpty())
                    m.insert(QStringLiteral("coverRef"), coverRef);
            }
        }
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
        m.insert(QStringLiteral("path"), g.value(QStringLiteral("subtreePath")));
        m.insert(QStringLiteral("coverRef"), QString());
        m.insert(QStringLiteral("state"), !identityTitle.isEmpty() ? QStringLiteral("identified")
                 : ambiguous ? QStringLiteral("uncertain") : QStringLiteral("resolving"));
        m.insert(QStringLiteral("away"), anyAway);
        out.append(m);
    }
    return out;
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
