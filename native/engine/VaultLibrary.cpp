#include "VaultLibrary.h"
#include "VaultIndex.h"
#include "VaultScanner.h"
#include "VaultConfig.h"

#include <QUrl>

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
        out.append(s);
    }
    return out;
}

QVariantList VaultLibrary::items(const QString& kind, const QString& seriesKey) const
{
    Q_UNUSED(kind);
    return m_index ? m_index->filesInSubtree(seriesKey) : QVariantList{};
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
    setScanning(true); // shelving
    m_scanner->publishConfirmed(confirmed, m_config->scanIgnore());
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
