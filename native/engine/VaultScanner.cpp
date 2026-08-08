#include "VaultScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QVariantMap>
#include <QtConcurrent>

VaultScanner::VaultScanner(VaultIndex* index, VaultIdentity* identity, QObject* parent)
    : QObject(parent), m_index(index), m_identity(identity)
{
}

QString VaultScanner::subfolderOf(const QString& subtree, const QString& filePath)
{
    const QString parent = QFileInfo(filePath).absolutePath();
    const QString rel = QDir(subtree).relativeFilePath(parent);
    if (rel == QLatin1String("."))
        return QString();
    return rel;
}

VaultScanner::RawResult VaultScanner::buildScan(
    QString root, QStringList scanIgnore, quint64 generation,
    std::shared_ptr<VaultKit::CancellationToken> cancel)
{
    RawResult r;
    r.root = root;
    r.generation = generation;

    const VaultKit::CancellationToken* c = cancel.get();
    const QStringList needles = VaultKit::sanitizeIgnoreNeedles(scanIgnore);
    const auto groups = VaultKit::groupByFirstLevelSubdir(
        {root}, VaultKit::allMediaFilters(), c, needles);
    // groupByFirstLevelSubdir swallows cancellation and returns early-empty, so
    // check here too: a cancelled scan must report cancelled (NOT an empty
    // result, which applyResult would publish and wipe the previous contents).
    if (c && c->isCancelled()) {
        r.cancelled = true;
        return r;
    }

    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (c && c->isCancelled()) {
            r.cancelled = true;
            return r;
        }
        const QString key = it.key();
        const bool loose = key.endsWith(QStringLiteral("::LOOSE"));
        const QString subtree = loose ? root : key;
        const QStringList files = it.value();

        const VaultKit::LeafClassification cls = VaultKit::classifyLeaf(files);
        if (cls.dominant == VaultKit::MediaKind::Unknown)
            continue;

        QString groupTitle = VaultKit::cleanMediaFolderTitle(QFileInfo(subtree).fileName());
        if (groupTitle.isEmpty())
            groupTitle = QFileInfo(subtree).fileName();

        QVariantMap sm;
        sm[QStringLiteral("subtreePath")] = subtree;
        sm[QStringLiteral("groupTitle")] = groupTitle;
        sm[QStringLiteral("kind")] = VaultKit::kindName(cls.dominant);
        sm[QStringLiteral("count")] = cls.counts.value(cls.dominant, 0);
        sm[QStringLiteral("mixed")] = cls.mixed;
        sm[QStringLiteral("loose")] = loose;
        sm[QStringLiteral("leftoverCount")] = cls.leftovers.size();
        r.sliceModel.append(sm);

        // Only the dominant-kind files shelve; leftovers stay named-but-unshelved.
        for (const QString& f : files) {
            if (c && c->isCancelled()) {
                r.cancelled = true;
                return r;
            }
            if (VaultKit::kindForFile(f) != cls.dominant)
                continue;

            const QFileInfo fi(f);
            VaultIdentity::FileFacts ff;
            ff.path = f;
            ff.size = fi.size();
            ff.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
            r.facts.append(ff);

            VaultIndex::FileRow row;
            row.rootPath = root;
            row.subtreePath = subtree;
            row.groupKey = subtree;
            row.groupTitle = groupTitle;
            row.kind = VaultKit::kindName(cls.dominant);
            row.path = f;
            row.realName = fi.fileName();
            QString disp = VaultKit::cleanMediaFolderTitle(fi.completeBaseName());
            row.displayTitle = disp.isEmpty() ? fi.completeBaseName() : disp;
            row.subfolder = subfolderOf(subtree, f);
            row.size = ff.size;
            row.mtimeMs = ff.mtimeMs;
            row.format = fi.suffix().toLower();
            r.rows.append(row);
        }
    }
    return r;
}

void VaultScanner::applyResult(const RawResult& result)
{
    if (result.generation != m_generation)
        return; // stale — a newer scan superseded this one

    m_scanning = false;

    // applyResult only DELIVERS the candidate census for the confirmation card
    // (Slice 11). Publication is a separate, confirm-triggered step (publishConfirmed
    // → applyPublish) that aggregates ALL confirmed roots — so a single root's census
    // never reaches VaultIndex::publish() alone, where the whole-index replace would
    // wipe the other roots.
    emit scanFinished(result.root, result.sliceModel, result.cancelled);

    if (!m_pending.isEmpty()) {
        const auto next = m_pending.takeFirst();
        scanRoot(next.first, next.second);
    }
}

void VaultScanner::applyPublish(const QList<RawResult>& results, quint64 generation)
{
    if (generation != m_generation)
        return; // a newer scan/publish superseded this aggregate

    m_scanning = false;

    // A cancelled census in the set aborts the WHOLE publish — no partial truth; the
    // previous index generation stands (VaultIndex::publish is atomic regardless).
    for (const RawResult& r : results) {
        if (r.cancelled)
            return;
    }

    // Aggregate every confirmed root's facts + rows, reconcile identity ONCE, assign
    // ids, then publish the UNION in a single transactional replace.
    QList<VaultIdentity::FileFacts> allFacts;
    QList<VaultIndex::FileRow> allRows;
    for (const RawResult& r : results) {
        allFacts.append(r.facts);
        allRows.append(r.rows);
    }
    m_identity->reconcile(allFacts);
    for (VaultIndex::FileRow& row : allRows)
        row.id = m_identity->idForFile(row.path, row.size, row.mtimeMs);

    // Emit indexPublished ONLY on a successful publish — a failed/rolled-back publish
    // must NOT tell the shelves "new truth landed"; the previous generation is intact.
    if (m_index->publish(allRows))
        emit indexPublished(QString(), m_index->itemCount());
}

void VaultScanner::publishConfirmed(const QStringList& confirmedRoots,
                                    const QStringList& scanIgnore)
{
    // Supersede any in-flight scan/publish, then census EVERY confirmed root fresh
    // off-thread: the index is a rebuildable product, so a confirm rebuilds the whole
    // Vault from all confirmed roots and publishes their union atomically (decision 4 +
    // Preflight §3: preserve multi-root atomicity, never a per-root replace).
    m_scanning = true;
    const quint64 gen = nextGeneration();
    m_cancel = std::make_shared<VaultKit::CancellationToken>();
    auto cancel = m_cancel;

    auto* watcher = new QFutureWatcher<QList<RawResult>>(this);
    connect(watcher, &QFutureWatcher<QList<RawResult>>::finished, this,
            [this, watcher, gen]() {
                const QList<RawResult> results = watcher->result();
                watcher->deleteLater();
                applyPublish(results, gen);
            });
    watcher->setFuture(QtConcurrent::run(
        [confirmedRoots, scanIgnore, gen, cancel]() {
            QList<RawResult> out;
            for (const QString& root : confirmedRoots)
                out.append(buildScan(root, scanIgnore, gen, cancel));
            return out;
        }));
}

void VaultScanner::scanRoot(const QString& root, const QStringList& scanIgnore)
{
    if (m_scanning) {
        m_pending.append(qMakePair(root, scanIgnore)); // buffered, never dropped
        return;
    }
    m_scanning = true;
    const quint64 gen = nextGeneration();
    m_cancel = std::make_shared<VaultKit::CancellationToken>();
    auto cancel = m_cancel;

    auto* watcher = new QFutureWatcher<RawResult>(this);
    connect(watcher, &QFutureWatcher<RawResult>::finished, this, [this, watcher]() {
        const RawResult res = watcher->result();
        watcher->deleteLater();
        applyResult(res);
    });
    watcher->setFuture(
        QtConcurrent::run(&VaultScanner::buildScan, root, scanIgnore, gen, cancel));
}

void VaultScanner::cancel()
{
    if (m_cancel)
        m_cancel->cancel();
}
