// ComickCatalogClient.cpp — see the header for the two-step story.

#include "engine/ComickCatalogClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace tankoban::manga::comick {

namespace {

constexpr const char* kDbBase =
    "https://raw.githubusercontent.com/kingoftheseas56/colosseum-volume-db/main/db/";
constexpr const char* kComickApi = "https://api.comick.dev";

// Comick answers 403 to anything that doesn't look like a browser — no token, no
// account, just the User-Agent. Probed 2026-07-29: a bare Qt client is refused, this
// string is served. Every "Comick is down" false alarm so far has been a missing UA,
// so do not trim this to a polite library string the way the MangaDex client could.
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/125.0.0.0 Safari/537.36";

constexpr int kTimeoutMs = 8000;
// The chapters payload for a long series is multiple megabytes (My Hero Academia's
// all-language pull is ~1 MB, One Piece far more), and an 8s transfer timeout
// false-fails on a normal connection. Measured 2026-07-29.
constexpr int kChaptersTimeoutMs = 20000;
constexpr int kSearchLimit = 8;

void applyHeaders(QNetworkRequest& req, int timeoutMs)
{
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(timeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
}

// Lowercase alphanumerics only — strip everything else so an exact title hit can beat
// relevance order, and "One Piece" doesn't match "One Piece Academy". This is one half
// of the shared series-resolution rule; see the note at the best-match loop in
// stepSearch for the other half and for why the two implementations have to agree.
QString matchKey(const QString& raw)
{
    QString out;
    out.reserve(raw.size());
    for (QChar c : raw.toLower()) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9'))
            out.append(c);
    }
    return out;
}

// Comick sends `chap`/`vol` as JSON STRINGS today — every row of every fixture
// probed on 2026-07-29. A JSON NUMBER therefore means the schema changed under us,
// and it matters because Qt cannot tell JSON `3` from JSON `3.0`: both arrive as the
// double 3.0.
//
// Read that harder, because it decides what this function can even aspire to: a JSON
// number CANNOT REPRESENT THIS DOMAIN AT ALL. Sub-chapter labels are ordinals, so
// "110.30" and "110.3" are the 30th and the 3rd side chapters of 110 — two different
// chapters — and both are the double 110.3. No rendering rule recovers the difference,
// because the difference is gone before this function is reached. So the qInfo shout
// IS the answer here; the rendering below is damage control that keeps today's corpus
// grouping while somebody goes and looks.
//
// DELIBERATE DIVERGENCE FROM THE PYTHON, recorded rather than mirrored. The batch
// path does `str(raw).strip()`, so a JSON 3.0 would become "3.0" — which the shared
// key parser reads as chapter 3's 0th SIDE chapter, key (3, 0, "0"), not chapter 3.
// That is almost certainly a bug rather than an intention, so we render an integral
// value WITHOUT a decimal ("3") and a fractional one with it ("3.5"), and shout about
// it. The case is unobserved on both sides. If this warning ever fires, fix BOTH
// implementations together — the point of the port is that a series groups the same
// whether it came from the database or a live scrape, and this is the one place they
// would silently disagree.
QString labelFromJson(const QJsonValue& value, const char* field, bool* warned)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble()) {
        const double n = value.toDouble();
        QString rendered;
        if (n == std::floor(n) && std::fabs(n) < 1e15)
            rendered = QString::number(static_cast<qlonglong>(n));
        else
            rendered = QString::number(n);
        if (warned && !*warned) {
            *warned = true;
            qInfo("[comick] SCHEMA CHANGE: chapter field '%s' arrived as a JSON number "
                  "(rendered as '%s'); Comick has always sent strings — revisit BOTH this "
                  "client and the Python batch pipeline before trusting the grouping",
                  field, qUtf8Printable(rendered));
        }
        return rendered;
    }
    return QString();   // null, absent, or some other type: the grouper rejects "" anyway
}

QVariantList toEmitList(QList<VolumeRange> vols)
{
    std::stable_sort(vols.begin(), vols.end(),
                     [](const VolumeRange& a, const VolumeRange& b) {
                         return a.number < b.number;
                     });
    QVariantList out;
    out.reserve(vols.size());
    for (const VolumeRange& vol : vols) {
        out.append(QVariantMap{
            {QStringLiteral("number"), static_cast<double>(vol.number)},
            // Always empty — see the header. The shelf draws a numbered placeholder
            // for an undownloaded volume and uses the volume's own first page once
            // it is on disk; there is no per-volume cover to fetch from either source.
            {QStringLiteral("cover"), QString()},
            {QStringLiteral("chapterStart"), vol.chapterStart},
            {QStringLiteral("chapterEnd"), vol.chapterEnd}});
    }
    return out;
}

} // namespace

// ---- pure parse: a published DB record -------------------------------------

// The record's own `qualified` flag is READ but not TRUSTED alone: a stale or
// hand-edited file must not be able to smuggle a broken shelf past the gate. So the
// gate runs again here, locally, and the record ships only when BOTH agree.
//
// THE HONEST LIMIT, stated plainly because it would be easy to read this as a full
// re-verification and it is not: the gate's coverage check (check 6) judges the raw
// chapter rows, and a published record does not carry them — it carries the collapsed
// volume ranges those rows produced. So the local re-gate re-verifies checks 1-5 only
// (numbering quirk, non-empty, first volume 0/1, unbroken volume numbers,
// non-overlapping spans) and takes coverage on the batch job's word, which is the
// only side that ever held the evidence. Passing gateVolumes an empty row list is
// what makes check 6 a no-op; fabricating rows to feed it would manufacture a
// guarantee we do not have.
//
// STRUCTURE IS CHECKED HERE, BEFORE THE GATE SEES IT, and that division matters. The
// gate SKIPS a volume whose span won't parse rather than judging it, and returns
// {true, ""} when every span is unparseable — safe for the live path, where the only
// spans it ever sees are formatChapterKey's own output, but a published record is the
// first thing to hand it externally-authored strings. An interrupted batch write that
// left volumes 41-42 of a 42-volume record with chapterStart:"" would pass checks 1-5
// untouched and ship a shelf with two tiles that have no chapters behind them. So a
// missing `number` and an unparseable span are rejected in this function. The fix
// belongs here and NOT in ComickVolumeGrouper, which is mirrored line-for-line against
// the Python batch core and must not drift from it.
//
// `complete` in the record is IGNORED on purpose: it is a hardcoded legacy field that
// is true on every record ever written and carries no information. `qualified` is the
// verdict.
ParsedRecord parseDbRecord(const QByteArray& json)
{
    ParsedRecord out;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return out;                                     // ok stays false
    const QJsonObject root = doc.object();
    const QJsonValue volumesValue = root.value(QLatin1String("volumes"));
    if (!volumesValue.isArray())
        return out;                                     // not a record, however valid the JSON

    out.ok = true;

    // The first structural fault found, kept verbatim for the reason line. Reading
    // continues past it so `vols` still holds every well-formed entry; the record is
    // refused below whatever the gate goes on to think of the survivors.
    QString structureProblem;
    QList<VolumeRange> vols;
    const QJsonArray volumesArray = volumesValue.toArray();
    vols.reserve(volumesArray.size());
    for (int i = 0; i < volumesArray.size(); ++i) {
        const QJsonObject obj = volumesArray.at(i).toObject();
        VolumeRange range;

        // `number` must be PRESENT and whole. Absent, null, a string, or fractional all
        // fall to 0 through toInt(), and 0 is a legal first volume (Death Note opens on
        // it), so a one-entry record would otherwise sail through the unbroken-run check
        // on a number it never actually carried.
        const QJsonValue numberValue = obj.value(QLatin1String("number"));
        if (!numberValue.isDouble() || numberValue.toDouble() != std::floor(numberValue.toDouble())) {
            if (structureProblem.isEmpty()) {
                structureProblem = QStringLiteral("volume entry %1 has no whole `number`")
                                       .arg(i + 1);
            }
            continue;
        }
        range.number = numberValue.toInt();
        range.chapterStart = obj.value(QLatin1String("chapterStart")).toString();
        range.chapterEnd = obj.value(QLatin1String("chapterEnd")).toString();

        // Both ends must read as chapter labels. A blank or junk span is exactly what
        // the gate would wave through by skipping it — see the note above.
        ChapterKey probe;
        if (!parseChapterKey(range.chapterStart, &probe)
            || !parseChapterKey(range.chapterEnd, &probe)) {
            if (structureProblem.isEmpty()) {
                structureProblem = QStringLiteral("volume %1 carries no usable chapter range")
                                       .arg(range.number);
            }
            continue;
        }
        vols.append(range);
    }

    const bool claimsQualified = root.value(QLatin1String("qualified")).toBool(false);
    const bool quirk = root.value(QLatin1String("numberingQuirk")).toBool(false);
    const QString recordReason = root.value(QLatin1String("gateReason")).toString();
    const GateVerdict local = gateVolumes(vols, quirk, QList<ChapterRow>{});

    if (!claimsQualified) {
        // The batch job saw the raw rows and said no. Its reason is the better one.
        out.gateReason = !recordReason.isEmpty()
                             ? recordReason
                             : (local.qualified ? QStringLiteral("record is not qualified")
                                                : local.reason);
        return out;
    }
    if (!structureProblem.isEmpty()) {
        // Ahead of the gate's verdict on purpose: once a span is unreadable the gate is
        // judging a record it can only partly see, so its answer isn't the honest one.
        out.gateReason = QStringLiteral("record claims qualified but ") + structureProblem;
        return out;
    }
    if (!local.qualified) {
        out.gateReason = QStringLiteral("record claims qualified but fails local re-gate: ")
                         + local.reason;
        return out;
    }

    out.qualified = true;
    out.volumes = toEmitList(vols);
    return out;
}

// ---- pure parse: a live chapters payload ------------------------------------

QList<ChapterRow> parseChapterRows(const QByteArray& json)
{
    QList<ChapterRow> rows;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return rows;
    const QJsonValue chapters = doc.object().value(QLatin1String("chapters"));
    if (!chapters.isArray())
        return rows;

    // One warning per field per payload, not per row: a schema change fires on every
    // one of thousands of rows and would bury the log it is trying to reach.
    bool warnedChap = false;
    bool warnedVol = false;

    const QJsonArray array = chapters.toArray();
    rows.reserve(array.size());
    for (const QJsonValue& entry : array) {
        if (!entry.isObject())
            continue;
        const QJsonObject obj = entry.toObject();
        // Untagged rows are kept: they cast no vote, but gateVolumes counts them when
        // it checks coverage, so dropping them here would hide real holes.
        rows.append(ChapterRow{
            labelFromJson(obj.value(QLatin1String("chap")), "chap", &warnedChap),
            labelFromJson(obj.value(QLatin1String("vol")), "vol", &warnedVol)});
    }
    return rows;
}

// ---- the client -------------------------------------------------------------

struct ComickCatalogClient::PendingFetch {
    QString weebCentralId;
    QString title;
    QString hid;
};

ComickCatalogClient::ComickCatalogClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

// One greppable line per outcome — the acceptance test reads the log to tell which
// path served a series, and a shelf that quietly came from the wrong one is exactly
// the thing that would go unnoticed.
void ComickCatalogClient::emitReady(PendingFetchPtr pending, const QVariantList& volumes,
                                    const QString& line)
{
    qInfo("%s", qUtf8Printable(line));
    emit catalogReady(pending->title, volumes);
}

// `line` lets the two gate refusals keep their own distinguishable shape; everything
// else gets the generic one. Either way a failure ALWAYS logs — five of the seven
// terminal exits are down here, and MangaEngine logging the reason on the far side of
// the signal is its business, not a substitute for this client accounting for itself.
void ComickCatalogClient::emitFailure(PendingFetchPtr pending, const QString& reason,
                                      const QString& line)
{
    if (line.isEmpty()) {
        qInfo("[comick] no shelf for '%s': %s", qUtf8Printable(pending->title),
              qUtf8Printable(reason));
    } else {
        qInfo("%s", qUtf8Printable(line));
    }
    emit catalogFailed(pending->title, reason);
}

void ComickCatalogClient::fetchSeries(const QString& weebCentralId, const QString& title)
{
    auto pending = std::make_shared<PendingFetch>();
    pending->weebCentralId = weebCentralId.trimmed();
    pending->title = title;

    if (pending->weebCentralId.isEmpty()) {
        // No DB key to look up — the record is filed by WeebCentral id.
        stepSearch(pending);
        return;
    }
    stepDbRecord(pending);
}

// ---- step 1: the published volume DB ---------------------------------------

void ComickCatalogClient::stepDbRecord(PendingFetchPtr pending)
{
    const QUrl url(QString::fromLatin1(kDbBase)
                   + QString::fromLatin1(QUrl::toPercentEncoding(pending->weebCentralId))
                   + QStringLiteral(".json"));
    QNetworkRequest req(url);
    applyHeaders(req, kTimeoutMs);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending]() {
        reply->deleteLater();
        // A 404 is the ordinary case for a series the batch job hasn't reached yet.
        if (reply->error() != QNetworkReply::NoError) {
            qInfo("[comick] db miss for '%s' (%s) — falling through to a live scrape",
                  qUtf8Printable(pending->title), qUtf8Printable(reply->errorString()));
            stepSearch(pending);
            return;
        }
        const ParsedRecord record = parseDbRecord(reply->readAll());
        if (!record.ok) {
            qInfo("[comick] db record for '%s' is malformed — falling through to a live scrape",
                  qUtf8Printable(pending->title));
            stepSearch(pending);
            return;
        }
        if (!record.qualified) {
            // NOT re-scraped on purpose. The batch job reached this verdict holding the
            // full chapter rows; a live scrape here would re-run the same grouper with
            // strictly less information and could only agree or be wrong.
            emitFailure(pending, record.gateReason,
                        QStringLiteral("[comick] db record for '%1' not qualified: %2")
                            .arg(pending->title, record.gateReason));
            return;
        }
        emitReady(pending, record.volumes,
                  QStringLiteral("[comick] db cache hit for '%1' (%2 volumes)")
                      .arg(pending->title).arg(record.volumes.size()));
    });
}

// ---- step 2: title -> Comick hid --------------------------------------------

void ComickCatalogClient::stepSearch(PendingFetchPtr pending)
{
    QUrl url(QString::fromLatin1(kComickApi) + QStringLiteral("/v1.0/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), pending->title);
    query.addQueryItem(QStringLiteral("limit"), QString::number(kSearchLimit));
    url.setQuery(query);

    QNetworkRequest req(url);
    applyHeaders(req, kTimeoutMs);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emitFailure(pending, QStringLiteral("comick search: ") + reply->errorString());
            return;
        }
        // The search endpoint answers with a bare array, not a {data:[...]} envelope.
        const QJsonArray items = QJsonDocument::fromJson(reply->readAll()).array();
        if (items.isEmpty()) {
            emitFailure(pending, QStringLiteral("no Comick match for title"));
            return;
        }

        // Best match: an exact normalized hit on the slug title or any md_title beats
        // search order, which ranks near-namesakes high (the shape comes from the
        // MangaDex client, which searches title + altTitles the same way).
        //
        // SHARED RULE — keep in step with colosseum-volume-db/comick_volume_db/
        // comick_client.py:pick_best(), which implements the same three parts: an
        // 8-candidate window, alphanumeric-lowercase normalization, and an exact hit on
        // `title` OR any `md_titles[].title`, else the first result. This is the step
        // that decides WHICH COMIC gets grouped, so a divergence here doesn't produce a
        // different shelf for the same comic — it produces a shelf for a DIFFERENT
        // comic depending on whether the series was pre-baked or scraped live, which is
        // the exact failure the port exists to prevent, one layer above where the
        // grouper harness can see it.
        //
        // The md_titles half is INERT on today's corpus, and worth saying so rather
        // than implying it earns its place: measured 2026-07-30, all 11 seeded series
        // match on `title` at result index 0, so a title-only rule resolves every one
        // of them identically. It is kept because it is the more forgiving rule for a
        // series whose Comick title differs from WeebCentral's spelling, and because
        // the two implementations must not differ — not because it is protecting
        // anything now. (The Python was 10 candidates and title-only until 2026-07-30;
        // it was ported to this rule and all 11 published records rebuilt to the same
        // comickHid.)
        const QString want = matchKey(pending->title);
        QString bestHid = items.first().toObject().value(QLatin1String("hid")).toString();
        for (const QJsonValue& itemValue : items) {
            const QJsonObject item = itemValue.toObject();
            QStringList names;
            names << item.value(QLatin1String("title")).toString();
            const QJsonArray mdTitles = item.value(QLatin1String("md_titles")).toArray();
            for (const QJsonValue& altValue : mdTitles)
                names << altValue.toObject().value(QLatin1String("title")).toString();
            const bool exact = std::any_of(names.cbegin(), names.cend(),
                [&want](const QString& name) { return matchKey(name) == want; });
            if (exact) {
                bestHid = item.value(QLatin1String("hid")).toString();
                break;
            }
        }
        if (bestHid.isEmpty()) {
            emitFailure(pending, QStringLiteral("Comick match had no hid"));
            return;
        }
        pending->hid = bestHid;
        stepChapters(pending);
    });
}

// ---- step 3: hid -> every chapter, every language ---------------------------

void ComickCatalogClient::stepChapters(PendingFetchPtr pending)
{
    QUrl url(QString::fromLatin1(kComickApi) + QStringLiteral("/comic/") + pending->hid
             + QStringLiteral("/chapters"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("100000"));
    query.addQueryItem(QStringLiteral("chap-order"), QStringLiteral("1"));
    // NO lang filter, deliberately. English scanlators routinely omit the volume tag
    // while other languages carry it: My Hero Academia yields 3 usable volumes from
    // en rows alone and a complete 1-42 across all languages. That defect is what
    // started this migration — do not "tidy" a lang=en back in.
    url.setQuery(query);

    QNetworkRequest req(url);
    applyHeaders(req, kChaptersTimeoutMs);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emitFailure(pending, QStringLiteral("comick chapters: ") + reply->errorString());
            return;
        }
        const QList<ChapterRow> rows = parseChapterRows(reply->readAll());
        if (rows.isEmpty()) {
            emitFailure(pending, QStringLiteral("Comick returned no chapters"));
            return;
        }

        const QList<VolumeRange> vols = groupVolumes(rows);
        const bool quirk = numberingIsOddball(rows);
        const GateVerdict verdict = gateVolumes(vols, quirk, rows);
        if (!verdict.qualified) {
            emitFailure(pending, verdict.reason,
                        QStringLiteral("[comick] live scrape for '%1' not qualified: %2")
                            .arg(pending->title, verdict.reason));
            return;
        }
        emitReady(pending, toEmitList(vols),
                  QStringLiteral("[comick] live scrape for '%1': %2 volumes qualified")
                      .arg(pending->title).arg(vols.size()));
    });
}

} // namespace tankoban::manga::comick
