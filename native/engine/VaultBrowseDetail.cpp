#include "VaultBrowseDetail.h"
#include "VaultIndex.h"
#include "VaultKit.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace {

QString humanSize(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    static const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        ++i;
    }
    const QString numText = (v >= 10.0) ? QString::number(qRound(v))
                                         : QString::number(v, 'f', 1);
    return numText + QLatin1Char(' ') + QLatin1String(kUnits[i]);
}

// A best-quality line parsed straight from the filename — resolution + source token, the same
// release vocabulary VaultKit's title cleaner already recognizes as noise and discards. Here it
// is the opposite: the ONE thing this slice keeps, because the detail sheet's whole job is the
// physical facts a title cleaner is built to throw away. No provider lookup, no confidence
// score — a plain read of the name on disk.
QString qualityLineFor(const QString& fileName)
{
    static const QRegularExpression resRe(QStringLiteral("(?i)\\b(2160p|1080p|720p|480p)\\b"));
    static const QRegularExpression srcRe(QStringLiteral(
        "(?i)\\b(WEBRip|WEB[-.]?DL|BluRay|BDRip|DVDRip|HDTV|Remux)\\b"));

    QStringList parts;
    const auto resMatch = resRe.match(fileName);
    if (resMatch.hasMatch())
        parts << resMatch.captured(1).toLower();

    const auto srcMatch = srcRe.match(fileName);
    if (srcMatch.hasMatch()) {
        QString token = srcMatch.captured(1);
        const QString lower = token.toLower();
        if (lower.startsWith(QLatin1String("webrip")))
            token = QStringLiteral("WEBRip");
        else if (lower.startsWith(QLatin1String("web")))
            token = QStringLiteral("WEB-DL");
        else if (lower == QLatin1String("bluray"))
            token = QStringLiteral("BluRay");
        else if (lower == QLatin1String("bdrip"))
            token = QStringLiteral("BDRip");
        else if (lower == QLatin1String("dvdrip"))
            token = QStringLiteral("DVDRip");
        else if (lower == QLatin1String("hdtv"))
            token = QStringLiteral("HDTV");
        else if (lower == QLatin1String("remux"))
            token = QStringLiteral("Remux");
        parts << token;
    }
    return parts.join(QLatin1Char(' '));
}

QString whereTextFor(const VaultIndex::FileRow& row)
{
    const QString rootName = QFileInfo(row.rootPath).fileName();
    const QString folderName = QFileInfo(row.subtreePath).fileName();
    if (rootName.isEmpty())
        return folderName;
    if (folderName.isEmpty())
        return rootName;
    return rootName + QStringLiteral(" / ") + folderName;
}

// VaultScanner's grouping (groupByFirstLevelSubdir) puts EVERY video nested under a film's
// folder — including its Extras/Featurettes files — in the SAME group/subtree; the browse
// projection folds those out at the STRUCTURAL level (planBrowseLevel never visits them), but
// this module reads the same rows the grid does, so it must apply the identical fold itself, or
// a trailer sharing the film's adopted identity would miscount as a second "copy" (found live:
// the Spider-Man fixture's Extras/Trailer.mp4 + Featurettes/Making-of.mp4 both landed in the
// SAME group as the film and, once identified, in the SAME rowsForIdentity() set).
bool rowIsExtra(const VaultIndex::FileRow& row)
{
    if (row.subfolder.isEmpty())
        return false;
    const QString top = row.subfolder.split(QLatin1Char('/')).first();
    return VaultKit::isExtrasDirName(top);
}

QVariantMap copyEntry(const VaultIndex::FileRow& row)
{
    QVariantMap m;
    m.insert(QStringLiteral("path"), row.path);
    m.insert(QStringLiteral("rootPath"), row.rootPath);
    m.insert(QStringLiteral("quality"), qualityLineFor(QFileInfo(row.path).fileName()));
    m.insert(QStringLiteral("sizeBytes"), row.size);
    m.insert(QStringLiteral("sizeText"), humanSize(row.size));
    m.insert(QStringLiteral("where"), whereTextFor(row));
    m.insert(QStringLiteral("away"), row.away);
    return m;
}

} // namespace

namespace VaultBrowseDetail {

QVariantMap detailFor(VaultIndex* index, const QString& key, const QStringList& scanIgnore)
{
    QVariantMap out;
    out.insert(QStringLiteral("found"), false);
    if (!index || key.isEmpty())
        return out;

    const QList<VaultIndex::FileRow> groupRows = index->rowsForGroup(key);
    if (groupRows.isEmpty())
        return out; // a stale key (rescanned/removed since the grid rendered it)

    // The group's PRIMARY file(s) — never an Extras/Featurettes row (see rowIsExtra above).
    QList<VaultIndex::FileRow> primaryInGroup;
    for (const VaultIndex::FileRow& r : groupRows)
        if (!rowIsExtra(r))
            primaryInGroup.append(r);
    const VaultIndex::FileRow& primary =
        !primaryInGroup.isEmpty() ? primaryInGroup.first() : groupRows.first();
    const bool identified = !primary.identityId.isEmpty() && !primary.identitySuppressed;

    const QList<VaultIndex::FileRow> rawCopyRows = identified
        ? index->rowsForIdentity(primary.identityId) : groupRows;
    QList<VaultIndex::FileRow> copyRows;
    for (const VaultIndex::FileRow& r : rawCopyRows)
        if (!rowIsExtra(r))
            copyRows.append(r);
    if (copyRows.isEmpty())
        copyRows = !primaryInGroup.isEmpty() ? primaryInGroup : groupRows; // defensive fallback

    out.insert(QStringLiteral("found"), true);
    out.insert(QStringLiteral("key"), key);
    const QString title = !primary.identityTitle.isEmpty() ? primary.identityTitle
                                                            : primary.groupTitle;
    out.insert(QStringLiteral("displayTitle"), title);
    out.insert(QStringLiteral("year"), primary.identityYear > 0 ? primary.identityYear : 0);
    out.insert(QStringLiteral("coverRef"), primary.kind == QLatin1String("video")
                                                 ? primary.coverRef : QString());

    const QString state = identified ? QStringLiteral("identified")
        : primary.identityState == QLatin1String("ambiguous") ? QStringLiteral("uncertain")
        : QStringLiteral("resolving");
    out.insert(QStringLiteral("identityState"), state);
    out.insert(QStringLiteral("identityLabel"),
               state == QLatin1String("identified") ? QStringLiteral("identity certain")
             : state == QLatin1String("uncertain")  ? QStringLiteral("identity uncertain")
                                                     : QStringLiteral("not yet identified"));

    QVariantList copies;
    QString bestQuality;
    for (const VaultIndex::FileRow& row : copyRows) {
        const QVariantMap c = copyEntry(row);
        if (bestQuality.isEmpty())
            bestQuality = c.value(QStringLiteral("quality")).toString();
        copies.append(c);
    }
    out.insert(QStringLiteral("copies"), copies);
    out.insert(QStringLiteral("copiesHeld"), copies.size());
    out.insert(QStringLiteral("bestQualityLine"), bestQuality);

    // Companions/extras are the CLICKED physical group's own folder facts — a multi-root
    // identified film may hold different companions per copy; this slice surfaces the one the
    // user actually opened, honestly, rather than merging facts across drives.
    const VaultKit::FilmPhysicalFacts physical =
        VaultKit::describeFilmFolder(primary.subtreePath, primary.path, scanIgnore);
    out.insert(QStringLiteral("companions"), QVariant(physical.companions));
    QVariantList extras;
    for (const VaultKit::FilmExtra& ex : physical.extras) {
        QVariantMap e;
        e.insert(QStringLiteral("title"), ex.title);
        e.insert(QStringLiteral("path"), ex.path);
        extras.append(e);
    }
    out.insert(QStringLiteral("extras"), extras);
    out.insert(QStringLiteral("ignoredCount"), physical.ignoredCount);

    QString evidence;
    if (state == QLatin1String("identified")) {
        evidence = QStringLiteral("Filename parsed to %1").arg(title);
        if (primary.identityYear > 0)
            evidence += QStringLiteral(" with year %1").arg(primary.identityYear);
        evidence += QStringLiteral(
            ". One matching title was found, and nothing here is overriding you.");
    } else if (state == QLatin1String("uncertain")) {
        evidence = QStringLiteral(
            "Filename parsed to %1. %2 possible matches were found — identify it to settle "
            "which one.").arg(title).arg(primary.identityCandidateCount);
    } else if (primary.identitySuppressed) {
        evidence = QStringLiteral(
            "Filename parsed to %1. You told Vault not to guess — it stays filename-honest "
            "until you identify it.").arg(title);
    } else {
        evidence = QStringLiteral(
            "Filename parsed to %1. Vault has not found a confident match yet.").arg(title);
    }
    out.insert(QStringLiteral("evidence"), evidence);

    // Play routes the copy the user actually clicked, not whichever copy happens to sort first.
    out.insert(QStringLiteral("playPath"), primary.path);

    return out;
}

} // namespace VaultBrowseDetail
