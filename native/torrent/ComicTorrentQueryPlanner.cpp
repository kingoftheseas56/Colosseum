#include "ComicTorrentQueryPlanner.h"

#include <QRegularExpression>
#include <QSet>

namespace {
// Order-preserving dedup key: case-folded, separators flattened, punctuation
// dropped, whitespace collapsed. The stored query keeps its original casing and
// punctuation so the indexers receive a human-readable title.
QString dedupKey(const QString& value)
{
    QString key = value.toLower();
    key.replace(QRegularExpression(QStringLiteral("[._\\-]+")), QStringLiteral(" "));
    key.replace(QRegularExpression(QStringLiteral("[^a-z0-9 ]")), QString());
    key.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return key.trimmed();
}

void addQuery(QStringList& out, QSet<QString>& seen, const QString& raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return;
    const QString key = dedupKey(trimmed);
    if (key.isEmpty() || seen.contains(key)) return;
    seen.insert(key);
    out.append(trimmed);
}
} // namespace

QStringList ComicTorrentQueryPlanner::automaticQueries(const QString& seriesTitle,
                                                       const QString& editionTitle,
                                                       const QString& isbn,
                                                       const QString& collects)
{
    QStringList out;
    QSet<QString> seen;

    // 1. Canonical edition title (fall back to the series when the edition is unnamed).
    const QString edition = editionTitle.trimmed();
    addQuery(out, seen, edition.isEmpty() ? seriesTitle : edition);

    // 2. ISBN, when present, is the least ambiguous identity.
    addQuery(out, seen, isbn);

    // 3. Collected range. Prefix the series exactly once, and only when the
    //    collected string does not already own the series name.
    const QString series = seriesTitle.trimmed();
    const QString range = collects.trimmed();
    if (!range.isEmpty()) {
        if (series.isEmpty() || range.startsWith(series, Qt::CaseInsensitive))
            addQuery(out, seen, range);
        else
            addQuery(out, seen, series + QLatin1Char(' ') + range);
    }

    return out;
}

QStringList ComicTorrentQueryPlanner::manualQuery(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) return {};
    return { trimmed };
}
