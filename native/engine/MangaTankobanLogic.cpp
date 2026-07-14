#include "engine/MangaTankobanLogic.h"

#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QUrl>

namespace MangaTankoban {
namespace {

// Percent-escape a series id so its internal ':' becomes "%3A". QUrl's default
// encoding leaves only the unreserved set (A-Z a-z 0-9 - . _ ~) untouched, so
// every ':' is encoded — verified: "mangafire:berserk" -> "mangafire%3Aberserk".
QString escapeSeriesId(const QString& seriesId)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(seriesId));
}

// Strip a run of leading zeroes from a purely-numeric integer segment, keeping a
// single "0" when the segment is all zeroes. Non-numeric segments pass through.
QString stripLeadingZeroes(const QString& intPart)
{
    if (intPart.isEmpty())
        return intPart;
    for (const QChar c : intPart)
        if (!c.isDigit())
            return intPart; // named/garbage segment — leave it faithful
    int i = 0;
    while (i < intPart.size() - 1 && intPart.at(i) == QLatin1Char('0'))
        ++i;
    return intPart.mid(i);
}

// Try to read a chapter's numeric value for range matching. Chapters may key it
// as "number" or "chapter"; either is accepted. Returns false if unparseable.
bool chapterNumberOf(const QVariantMap& row, double& out)
{
    QVariant v = row.value(QStringLiteral("number"));
    if (!v.isValid() || v.toString().trimmed().isEmpty())
        v = row.value(QStringLiteral("chapter"));
    bool ok = false;
    const double d = v.toString().trimmed().toDouble(&ok);
    if (ok)
        out = d;
    return ok;
}

} // namespace

QString normalizeVolumeNumber(const QVariant& raw)
{
    QString s;
    // Floating-point variants render without trailing-zero noise so a source that
    // hands us 10.5 or 2.0 as a double still yields "10.5" / "2".
    if (raw.typeId() == QMetaType::Double || raw.typeId() == QMetaType::Float) {
        s = QString::number(raw.toDouble(), 'f', 6);
        if (s.contains(QLatin1Char('.'))) {
            while (s.endsWith(QLatin1Char('0')))
                s.chop(1);
            if (s.endsWith(QLatin1Char('.')))
                s.chop(1);
        }
    } else {
        s = raw.toString();
    }

    s = s.simplified(); // trims ends and collapses internal runs of whitespace
    if (s.isEmpty())
        return s;

    const int dot = s.indexOf(QLatin1Char('.'));
    const QString intPart = dot < 0 ? s : s.left(dot);
    const QString rest = dot < 0 ? QString() : s.mid(dot); // keeps the leading '.'
    return stripLeadingZeroes(intPart) + rest;
}

QString volumeId(const QString& seriesId, const QString& volumeNumber)
{
    return QStringLiteral("tankoban:") + escapeSeriesId(seriesId)
        + QStringLiteral(":volume:") + volumeNumber;
}

QString settingsKey(const QString& seriesId)
{
    return QStringLiteral("manga/tankobanMode/") + escapeSeriesId(seriesId);
}

SeriesSnapshot prepareSeries(const QVariantMap& descriptor,
                             const QVariantList& volumeRows,
                             const QVariantList& chapterRows)
{
    SeriesSnapshot snap;
    snap.seriesId = descriptor.value(QStringLiteral("seriesId")).toString();
    snap.title = descriptor.value(QStringLiteral("title")).toString();
    snap.author = descriptor.value(QStringLiteral("author")).toString();
    for (const QVariant& a : descriptor.value(QStringLiteral("aliases")).toList())
        snap.aliases << a.toString();

    // Build one canonical record per volume row — none is ever dropped.
    QHash<QString, int> byNumber; // normalized number -> first record index
    for (const QVariant& vRaw : volumeRows) {
        const QVariantMap row = vRaw.toMap();
        VolumeRecord rec;
        rec.number = normalizeVolumeNumber(row.value(QStringLiteral("number")));
        rec.id = volumeId(snap.seriesId, rec.number);
        rec.seriesId = snap.seriesId;
        rec.title = row.value(QStringLiteral("title")).toString();
        rec.cover = row.value(QStringLiteral("cover")).toString();
        rec.chapterStart = row.value(QStringLiteral("chapterStart")).toString();
        rec.chapterEnd = row.value(QStringLiteral("chapterEnd")).toString();
        if (!rec.number.isEmpty() && !byNumber.contains(rec.number))
            byNumber.insert(rec.number, snap.volumes.size());
        snap.volumes.append(rec);
    }

    // Phase 1: map chapters by their explicit "volume" field (input order kept).
    // Every id claimed here is remembered so the range fallback can never re-grab
    // a chapter that already has an explicit home.
    QSet<QString> claimed;
    for (const QVariant& cRaw : chapterRows) {
        const QVariantMap row = cRaw.toMap();
        const QString vol = normalizeVolumeNumber(row.value(QStringLiteral("volume")));
        if (vol.isEmpty())
            continue;
        const auto it = byNumber.constFind(vol);
        if (it == byNumber.constEnd())
            continue;
        const QString id = row.value(QStringLiteral("id")).toString();
        snap.volumes[it.value()].chapterIds << id;
        claimed.insert(id);
    }

    // Phase 2: for volumes STILL without chapters, fall back to the volume row's
    // chapterStart/chapterEnd range (again preserving chapter input order). A
    // chapter already claimed — in phase 1 or by an earlier range — is skipped so
    // no chapter is ever collected into two volumes.
    for (VolumeRecord& rec : snap.volumes) {
        if (!rec.chapterIds.isEmpty())
            continue;
        bool okStart = false, okEnd = false;
        const double start = rec.chapterStart.trimmed().toDouble(&okStart);
        const double end = rec.chapterEnd.trimmed().toDouble(&okEnd);
        if (!okStart || !okEnd)
            continue;
        for (const QVariant& cRaw : chapterRows) {
            const QVariantMap row = cRaw.toMap();
            double num = 0.0;
            if (!chapterNumberOf(row, num) || num < start || num > end)
                continue;
            const QString id = row.value(QStringLiteral("id")).toString();
            if (claimed.contains(id))
                continue;
            rec.chapterIds << id;
            claimed.insert(id);
        }
    }

    return snap;
}

} // namespace MangaTankoban
