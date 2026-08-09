#include "VaultLibrary.h"
#include "VaultIndex.h"
#include "VaultScanner.h"
#include "VaultConfig.h"
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
                           QObject* parent)
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
                });
        // A successful publish ends the shelving state (revision bumps via index.changed()).
        connect(m_scanner, &VaultScanner::indexPublished, this,
                [this](const QString&, int) { setScanning(false); });
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
    for (const QVariant& r : roots)
        if (r.toMap().value(QStringLiteral("confirmed")).toBool())
            ++n;
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
    // Never interrupt an in-flight scan or a card already up.
    if (m_scanning || cardVisible())
        return;
    const QVariantList roots = m_config->roots();
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
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

    // Publish the UNION of ALL confirmed roots — never one root alone (whole-index replace).
    QStringList confirmed;
    const QVariantList roots = m_config->roots();
    for (const QVariant& r : roots) {
        const QVariantMap m = r.toMap();
        if (m.value(QStringLiteral("confirmed")).toBool())
            confirmed.append(m.value(QStringLiteral("path")).toString());
    }

    // Snapshot every chip override on the GUI thread and hand it to the off-thread census
    // so the re-shelve honors the user's choices (Thread A). Keys are already normalized.
    QMap<QString, QString> overrides;
    const QVariantMap ov = m_config->kindOverrides();
    for (auto it = ov.constBegin(); it != ov.constEnd(); ++it)
        overrides.insert(it.key(), it.value().toString());

    // Clear the pill's scan counts so the brief shelving pass shows "Scanning …", not the
    // last census's stale "N of M".
    m_scanDone = 0;
    m_scanTotal = 0;
    emit scanProgressChanged();

    setScanning(true); // shelving
    m_scanner->publishConfirmed(confirmed, m_config->scanIgnore(), overrides);
}

void VaultLibrary::dismissCard()
{
    m_candidate.clear();
    m_candidateRoot.clear();
    emit candidateChanged();
}

void VaultLibrary::cancelScan()
{
    if (m_scanner)
        m_scanner->cancel();
}
