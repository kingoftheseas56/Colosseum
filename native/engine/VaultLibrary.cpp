#include "VaultLibrary.h"
#include "VaultIndex.h"
#include "VaultScanner.h"
#include "VaultConfig.h"
#include "VaultIdentity.h"
#include "VaultWatcher.h"
#include "VaultDownloadsRoot.h"
#include "ComicCoverId.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
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
    : QObject(parent), m_index(index), m_scanner(scanner), m_config(config)
{
    // revision tracks committed index truth. VaultIndex::changed() fires after a successful
    // publish()/upsert() only, so a bump always means new published truth to repaint from.
    if (m_index) {
        connect(m_index, &VaultIndex::changed, this, [this]() {
            ++m_revision;
            emit changed();
        });
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
        QVariantMap s;
        s.insert(QStringLiteral("key"), m.value(QStringLiteral("groupKey")));
        s.insert(QStringLiteral("title"), m.value(QStringLiteral("groupTitle")));
        s.insert(QStringLiteral("kind"), m.value(QStringLiteral("kind")));
        s.insert(QStringLiteral("count"), m.value(QStringLiteral("count")));
        s.insert(QStringLiteral("awayCount"), m.value(QStringLiteral("awayCount")));
        s.insert(QStringLiteral("errorCount"), m.value(QStringLiteral("errorCount")));
        s.insert(QStringLiteral("subtreePath"), m.value(QStringLiteral("subtreePath")));
        // A ready-to-bind cover URL for the tile: image://comiccover/<id> when the group has
        // an enriched comic cover, else empty (the tile falls back to its gradient + icon).
        const QString coverPath = m.value(QStringLiteral("coverPath")).toString();
        const QString coverEntry = m.value(QStringLiteral("coverEntry")).toString();
        s.insert(QStringLiteral("coverUrl"),
                 (!coverPath.isEmpty() && !coverEntry.isEmpty())
                     ? QStringLiteral("image://comiccover/")
                           + Colosseum::buildComicCoverId(coverPath, coverEntry)
                     : QString());
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
        m.insert(QStringLiteral("coverUrl"),
                 (!coverRef.isEmpty() && !path.isEmpty()
                  && m.value(QStringLiteral("kind")).toString() == QStringLiteral("comic"))
                     ? QStringLiteral("image://comiccover/") + Colosseum::buildComicCoverId(path, coverRef)
                     : QString());
        v = m;
    }
    return rows;
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
