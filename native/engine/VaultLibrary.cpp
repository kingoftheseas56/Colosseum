#include "VaultLibrary.h"
#include "VaultIndex.h"

VaultLibrary::VaultLibrary(VaultIndex* index, QObject* parent)
    : QObject(parent), m_index(index)
{
    // revision tracks committed index truth. VaultIndex::changed() fires after a successful
    // publish()/upsert() only, so every bump here means genuinely-new published truth the
    // shelves must repaint from (Preflight §1 revision rule — never on scan-start, cancel,
    // or a failed publish).
    if (m_index) {
        connect(m_index, &VaultIndex::changed, this, [this]() {
            ++m_revision;
            emit changed();
        });
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
