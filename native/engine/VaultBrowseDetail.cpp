#include "VaultBrowseDetail.h"
#include "VaultIndex.h"
#include "VaultKit.h"

#include <QFileInfo>

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
    m.insert(QStringLiteral("quality"),
             VaultKit::qualityLineFromFileName(QFileInfo(row.path).fileName()));
    m.insert(QStringLiteral("sizeBytes"), row.size);
    m.insert(QStringLiteral("sizeText"), humanSize(row.size));
    m.insert(QStringLiteral("where"), whereTextFor(row));
    m.insert(QStringLiteral("away"), row.away);
    // Honest failure (vault ux uplift S8): the copy's durable admission verdict travels with
    // the entry, plus the ONE quiet factual line the sheet prints under a rejected/errored
    // copy — the recorded human reason (admissionDetail, e.g. "no video track") when the
    // verdict is a rejection, else any extraction errorDetail; empty for a healthy copy.
    // A rejection that somehow recorded no detail still names its verdict rather than
    // staying silent — the sheet must never show less truth than the engine holds.
    QString statusDetail;
    if (row.admissionVerdict.startsWith(QLatin1String("Rejected")))
        statusDetail = !row.admissionDetail.isEmpty() ? row.admissionDetail : row.admissionVerdict;
    else if (!row.errorDetail.isEmpty())
        statusDetail = row.errorDetail;
    m.insert(QStringLiteral("admissionVerdict"), row.admissionVerdict);
    m.insert(QStringLiteral("statusDetail"), statusDetail);
    return m;
}

// Seconds -> "1h 47m" / "48m" — the same floor-based grammar as the app's existing duration
// formatter (qml/AccountActivityFormat.js durationText, its own worked example "37h 24m";
// VaultFolderView.qml's metaFor does the same Math.floor inline). That formatter lives in JS
// and is unreachable from this C++ projection, so this is its twin, not a new format. Empty
// for anything under one printable minute — the caller omits the line entirely rather than
// render the "-1" unprobed sentinel or a "0m" stub (ux uplift S8's rule).
QString runtimeTextFromSec(double durationSec)
{
    if (durationSec <= 0.0)
        return QString();
    const int totalMinutes = static_cast<int>(durationSec / 60.0); // trunc == floor for > 0
    const int hours = totalMinutes / 60;
    const int minutes = totalMinutes % 60;
    if (hours <= 0 && minutes <= 0)
        return QString();
    return hours > 0 ? QStringLiteral("%1h %2m").arg(hours).arg(minutes)
                     : QStringLiteral("%1m").arg(minutes);
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

    // Runtime (vault ux uplift S8) — the clicked copy's own measured duration (VaultEnricher's
    // ffprobe pass, S5, admitted video rows). The key is OMITTED while unknown (durationSec
    // still at its -1 sentinel, or a sub-minute file) so the sheet never renders "-1"/"0m" —
    // an extension of the shape, never a reshape; existing consumers never read it.
    const QString runtimeText = runtimeTextFromSec(primary.durationSec);
    if (!runtimeText.isEmpty())
        out.insert(QStringLiteral("runtimeText"), runtimeText);

    // Phase-4 G1 ruling (2026-08-25): the adopted identity's IMDb rating + genres, surfaced on
    // identified items only. Provenance-badged INLINE ("IMDb 8.1" — identitySource IMDB is the
    // only source the ruling carries facts for; MAL score is deliberately out). The keys are
    // OMITTED when the row carries no fact (an unidentified group can never have written them).
    // Votes still not carried, by the ruling's own word.
    if (primary.identityRating > 0)
        out.insert(QStringLiteral("ratingText"),
                   (primary.identitySource == QLatin1String("IMDB") ? QStringLiteral("IMDb")
                                                                     : primary.identitySource)
                   + QLatin1Char(' ')
                   + QString::number(primary.identityRating, 'f', 1));
    if (!primary.identityGenres.isEmpty())
        out.insert(QStringLiteral("genresLine"), primary.identityGenres);

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
