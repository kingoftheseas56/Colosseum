// TankobanCatalog.cpp — see header. [Agent 1 sub-exec (Sonnet), Tankoban catalogue independence]
#include "TankobanCatalog.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QSqlQuery>
#include <QVector>

#include <algorithm>

namespace {

// Zero-pad each run of digits so a plain lexicographic compare orders
// numerically ("2" before "10"); lowercases everything else for a stable,
// case-insensitive tiebreak on any non-numeric volume label. Mirrors
// VaultIndex::naturalSortKey's technique (native/engine/VaultIndex.cpp).
QString numericAwareKey(const QString& s)
{
    static const int kWidth = 12;
    QString out;
    out.reserve(s.size() + 16);
    int i = 0;
    const int n = s.size();
    while (i < n) {
        if (s.at(i).isDigit()) {
            int j = i;
            while (j < n && s.at(j).isDigit())
                ++j;
            const int len = j - i;
            if (len < kWidth)
                out += QString(kWidth - len, QLatin1Char('0'));
            out += s.mid(i, len);
            i = j;
        } else {
            out += s.at(i).toLower();
            ++i;
        }
    }
    return out;
}

} // namespace

TankobanCatalog::TankobanCatalog(const QString& dbPath, QObject* parent)
    : QObject(parent), m_conn(QStringLiteral("tankoban_catalog"))
{
    // resolve beside the exe first (deployed), then the repo layout (dev run) — MalCatalog's ladder
    QString path = dbPath;
    if (!QFileInfo::exists(path)) {
        const QString beside = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/../../") + dbPath;
        if (QFileInfo::exists(beside)) path = beside;
    }
    if (!QFileInfo::exists(path))
        return;                                  // no catalog — callers fall through honestly
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    m_ok = m_db.open();
}

TankobanCatalog::~TankobanCatalog()
{
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_conn);
}

QVariantMap TankobanCatalog::seriesInfo(int malId) const
{
    if (!m_ok)
        return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT volume_count, count_basis FROM series WHERE mal_id = ?"));
    q.addBindValue(malId);
    if (!q.exec() || !q.next())
        return {};
    QVariantMap m;
    m.insert(QStringLiteral("volumeCount"), q.value(0).toInt());
    m.insert(QStringLiteral("countBasis"), q.value(1).toString());
    return m;
}

QVariantList TankobanCatalog::volumes(int malId) const
{
    QVariantList out;
    if (!m_ok)
        return out;

    const QVariantMap info = seriesInfo(malId);
    if (info.isEmpty())
        return out; // unknown malId — nothing to synthesize or overlay

    const int count = info.value(QStringLiteral("volumeCount")).toInt();

    // Baked cover/name facts keyed by their exact number string.
    QMap<QString, QPair<QString, QString>> overlay;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT number, cover_url, name FROM volumes WHERE mal_id = ?"));
        q.addBindValue(malId);
        if (q.exec()) {
            while (q.next())
                overlay.insert(q.value(0).toString(),
                               qMakePair(q.value(1).toString(), q.value(2).toString()));
        }
    }

    QVector<QVariantMap> rows;
    QSet<QString> emitted;

    // Synthesize "1".."N" from the known count, overlaying baked facts where present.
    for (int n = 1; n <= count; ++n) {
        const QString num = QString::number(n);
        const auto it = overlay.constFind(num);
        QVariantMap m;
        m.insert(QStringLiteral("number"), num);
        m.insert(QStringLiteral("cover"), it != overlay.constEnd() ? it.value().first : QString());
        m.insert(QStringLiteral("name"), it != overlay.constEnd() ? it.value().second : QString());
        rows.append(m);
        emitted.insert(num);
    }

    // Any baked rows outside the synthesized range (e.g. count unknown, or a
    // harvest number the count doesn't cover) still surface — nothing invented,
    // nothing dropped.
    for (auto it = overlay.constBegin(); it != overlay.constEnd(); ++it) {
        if (emitted.contains(it.key()))
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("number"), it.key());
        m.insert(QStringLiteral("cover"), it.value().first);
        m.insert(QStringLiteral("name"), it.value().second);
        rows.append(m);
    }

    std::sort(rows.begin(), rows.end(), [](const QVariantMap& a, const QVariantMap& b) {
        return numericAwareKey(a.value(QStringLiteral("number")).toString())
             < numericAwareKey(b.value(QStringLiteral("number")).toString());
    });

    for (const auto& m : rows)
        out.append(m);
    return out;
}
