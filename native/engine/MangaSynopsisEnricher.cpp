// native/engine/MangaSynopsisEnricher.cpp
#include "engine/MangaSynopsisEnricher.h"

#include "engine/MangaTankobanLogic.h" // MangaTankoban::volumeId / normalizeVolumeNumber

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace MangaTankoban {
namespace {

constexpr int kAppleMinSpacingMs = 3200;   // ≥3.2s between Apple request starts
constexpr int kMissTtlSecs = 24 * 60 * 60;         // a valid-but-empty response
constexpr qint64 kAcceptTtlSecs = 30LL * 24 * 60 * 60; // an accepted record

// Fold to a lower-case, punctuation-stripped, whitespace-collapsed token stream.
// Apostrophes are dropped entirely so "JoJo's" and "JoJos" fold to one token.
// This is the matching backbone for titles, series names and creator names.
QString foldWords(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (c == QLatin1Char('\'') || c == QChar(0x2019)) // ' and ’
            continue;
        out.append(c.isLetterOrNumber() ? c.toLower() : QLatin1Char(' '));
    }
    return out.simplified();
}

// Normalize free synopsis text for equality checks: case/space/punctuation-
// insensitive. Reused by acceptDistinctVolumeText so a series blurb and its
// lightly-reformatted echo compare equal.
QString normalizeText(const QString& s)
{
    return foldWords(s);
}

// The title must strongly match the series title or one of its aliases.
bool strongSeriesMatch(const QString& title, const SeriesSnapshot& series)
{
    const QString hay = foldWords(title);
    if (hay.isEmpty())
        return false;
    QStringList needles;
    if (!series.title.isEmpty())
        needles << series.title;
    needles << series.aliases;
    for (const QString& n : needles) {
        const QString fn = foldWords(n);
        if (!fn.isEmpty() && hay.contains(fn))
            return true;
    }
    return false;
}

// Extract an explicit volume number from a title using ONLY v / vol / volume
// markers followed by a number — never a bare number (so "Chapter 4" or a bare
// "4" is not mistaken for a volume). Returns the canonical number string.
bool explicitVolume(const QString& title, QString& number)
{
    static const QRegularExpression re(
        QStringLiteral(R"((?:\bvolumes?|\bvol\.?|\bv)\s*0*([0-9]+(?:\.[0-9]+)?))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(title);
    if (!m.hasMatch()) {
        number.clear();
        return false;
    }
    number = normalizeVolumeNumber(QVariant(m.captured(1)));
    return !number.isEmpty();
}

// Does `candidateNumber` name the same volume as the canonical `targetNumber`?
// Numeric when both parse (so "04" == "4"); else case-insensitive token equality
// (named/special volumes like "Extra").
bool sameVolume(const QString& candidateNumber, const QString& targetNumber)
{
    const QString a = normalizeVolumeNumber(QVariant(candidateNumber));
    const QString b = normalizeVolumeNumber(QVariant(targetNumber));
    if (a.isEmpty() || b.isEmpty())
        return false;
    bool okA = false, okB = false;
    const double da = a.toDouble(&okA);
    const double db = b.toDouble(&okB);
    if (okA && okB)
        return qFuzzyCompare(da + 1.0, db + 1.0);
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

// Reject editions that are not the plain target volume of the canonical record:
// non-English language editions and multi-volume collections. A same-volume
// repackage ("Special Edition", "Deluxe") is NOT disqualified — it is a genuine
// competing candidate and must reach the ambiguity check.
bool disqualifyingEdition(const QString& title)
{
    const QString l = title.toLower();
    static const char* const kLangEditions[] = {
        "french edition", "spanish edition", "german edition", "italian edition",
        "portuguese edition", "japanese edition", "latin edition", "russian edition",
    };
    for (const char* marker : kLangEditions)
        if (l.contains(QLatin1String(marker)))
            return true;
    if (l.contains(QString::fromUtf8("\xc3\xa9" "dition"))) // "édition" (split so \xa9 doesn't swallow 'd')
        return true;
    static const char* const kCollections[] = {
        "omnibus", "box set", "boxset", "complete collection", "complete series",
        "collector's edition set", "3-in-1", "2-in-1",
    };
    for (const char* marker : kCollections)
        if (l.contains(QLatin1String(marker)))
            return true;
    return false;
}

// A valid ISBN from an English-language registration group. Hyphens/spaces are
// stripped first. The English groups are 978-0 / 978-1 and the newer US range
// 979-8; deliberately conservative so "exact-isbn" is never over-claimed (e.g.
// 979-0/979-1 are French, and 978-4 is Japanese). ISBN-10 English groups are 0/1.
bool isEnglishIsbn(const QString& raw)
{
    QString d;
    for (const QChar c : raw)
        if (c.isDigit() || c == QLatin1Char('X') || c == QLatin1Char('x'))
            d.append(c.toUpper());
    if (d.size() == 13)
        return d.startsWith(QLatin1String("9780")) || d.startsWith(QLatin1String("9781"))
            || d.startsWith(QLatin1String("9798"));
    if (d.size() == 10)
        return d.startsWith(QLatin1Char('0')) || d.startsWith(QLatin1Char('1'));
    return false;
}

QString firstString(const QJsonValue& v)
{
    if (v.isString())
        return v.toString();
    if (v.isArray()) {
        const auto arr = v.toArray();
        for (const auto& e : arr)
            if (e.isString() && !e.toString().trimmed().isEmpty())
                return e.toString();
    }
    if (v.isObject()) // OL sometimes wraps as {"type":"/type/text","value":"..."}
        return v.toObject().value(QStringLiteral("value")).toString();
    return QString();
}

// Prefer a real prose description; fall back to a first-sentence snippet. Trimmed.
QString openLibraryText(const QJsonObject& doc)
{
    QString t = firstString(doc.value(QStringLiteral("description"))).trimmed();
    if (t.isEmpty())
        t = firstString(doc.value(QStringLiteral("first_sentence"))).trimmed();
    return t;
}

// True if any ISBN on the doc is an English-group ISBN. Search docs carry an
// "isbn" array; edition docs carry "isbn_13"/"isbn_10".
bool docHasEnglishIsbn(const QJsonObject& doc)
{
    const char* const keys[] = {"isbn", "isbn_13", "isbn_10"};
    for (const char* k : keys) {
        const auto arr = doc.value(QLatin1String(k)).toArray();
        for (const auto& e : arr)
            if (isEnglishIsbn(e.toString()))
                return true;
    }
    return false;
}

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

} // namespace

// ── Pure matching ─────────────────────────────────────────────────────────

SynopsisRecord MangaSynopsisEnricher::matchOpenLibrary(const SeriesSnapshot& series,
                                                       const VolumeRecord& volume,
                                                       const QByteArray& json)
{
    SynopsisRecord miss;
    miss.volumeId = volume.id;
    miss.confidence = QStringLiteral("none");

    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject())
        return miss;
    const QJsonObject root = jd.object();

    // Accept either a search response ({"docs":[...]}) or a single edition doc.
    QJsonArray docs;
    if (root.contains(QStringLiteral("docs")))
        docs = root.value(QStringLiteral("docs")).toArray();
    else
        docs.append(root); // treat the root object as one edition doc

    SynopsisRecord firstStrong; // remembered so we can prefer an ISBN-backed doc
    bool haveFirstStrong = false;

    for (const auto& dv : docs) {
        if (!dv.isObject())
            continue;
        const QJsonObject doc = dv.toObject();
        const QString title = doc.value(QStringLiteral("title")).toString();
        if (title.isEmpty() || !strongSeriesMatch(title, series))
            continue;
        if (disqualifyingEdition(title))
            continue;
        QString num;
        if (!explicitVolume(title, num) || !sameVolume(num, volume.number))
            continue; // genuine target-volume evidence required — never the bare series
        const QString text = openLibraryText(doc);
        if (text.isEmpty())
            continue;

        SynopsisRecord rec;
        rec.volumeId = volume.id;
        rec.source = QStringLiteral("openlibrary");
        rec.text = text;
        const QString key = doc.value(QStringLiteral("key")).toString();
        rec.sourceUrl = key.isEmpty() ? QString()
                                      : QStringLiteral("https://openlibrary.org") + key;
        const bool english = docHasEnglishIsbn(doc);
        rec.confidence = english ? QStringLiteral("exact-isbn")
                                 : QStringLiteral("exact-title-volume");
        rec.fetchedAt = nowIso();
        rec.accepted = true;

        if (english)
            return rec; // prefer a concrete English-ISBN-backed edition
        if (!haveFirstStrong) {
            firstStrong = rec;
            haveFirstStrong = true;
        }
    }
    if (haveFirstStrong)
        return firstStrong; // title+volume match, no English ISBN to pin it
    return miss;
}

SynopsisRecord MangaSynopsisEnricher::matchApple(const SeriesSnapshot& series,
                                                 const VolumeRecord& volume,
                                                 const QByteArray& json)
{
    SynopsisRecord miss;
    miss.volumeId = volume.id;
    miss.confidence = QStringLiteral("none");

    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject())
        return miss;
    const QJsonArray results = jd.object().value(QStringLiteral("results")).toArray();

    const QString seriesAuthor = foldWords(series.author);

    struct Cand {
        SynopsisRecord rec;
        bool authorAgrees = false;
    };
    QList<Cand> strong;

    for (const auto& rv : results) {
        if (!rv.isObject())
            continue;
        const QJsonObject r = rv.toObject();
        const QString track = r.value(QStringLiteral("trackName")).toString();
        if (track.isEmpty() || !strongSeriesMatch(track, series))
            continue;
        if (disqualifyingEdition(track))
            continue;
        QString num;
        if (!explicitVolume(track, num) || !sameVolume(num, volume.number))
            continue; // must be the explicit target volume
        const QString text = r.value(QStringLiteral("description")).toString().trimmed();
        if (text.isEmpty())
            continue;
        const QString url = r.value(QStringLiteral("trackViewUrl")).toString();
        if (!url.contains(QStringLiteral("books.apple")))
            continue; // source honesty — only accept a real Apple Books URL

        Cand c;
        c.rec.volumeId = volume.id;
        c.rec.source = QStringLiteral("apple");
        c.rec.text = text;
        c.rec.sourceUrl = url;
        c.rec.confidence = QStringLiteral("exact-title-volume");
        c.rec.fetchedAt = nowIso();
        c.rec.accepted = true;
        const QString artist = foldWords(r.value(QStringLiteral("artistName")).toString());
        c.authorAgrees = !seriesAuthor.isEmpty() && !artist.isEmpty()
            && (artist.contains(seriesAuthor) || seriesAuthor.contains(artist));
        strong.append(c);
    }

    if (strong.size() == 1)
        return strong.first().rec;
    if (strong.size() > 1) {
        // Author agreement is the only tie-breaker. Accept ONLY when exactly one
        // candidate agrees with the canonical author; otherwise (none or several)
        // there is no distinguishing signal — leave it empty, never guess.
        int agreeing = 0, agreeIdx = -1;
        for (int i = 0; i < strong.size(); ++i)
            if (strong[i].authorAgrees) {
                ++agreeing;
                agreeIdx = i;
            }
        if (agreeing == 1)
            return strong[agreeIdx].rec;
    }
    return miss;
}

bool MangaSynopsisEnricher::acceptDistinctVolumeText(const QString& candidateText,
                                                     const QString& compareText)
{
    const QString cand = normalizeText(candidateText);
    if (cand.isEmpty())
        return false;
    return cand != normalizeText(compareText);
}

// ── Cache ───────────────────────────────────────────────────────────────────

MangaSynopsisEnricher::MangaSynopsisEnricher(QNetworkAccessManager* nam,
                                             const QString& cachePath,
                                             QObject* parent)
    : QObject(parent), m_nam(nam), m_cachePath(cachePath)
{
    qRegisterMetaType<MangaTankoban::SynopsisRecord>("MangaTankoban::SynopsisRecord");
    loadCache();
}

MangaSynopsisEnricher::~MangaSynopsisEnricher() = default;

SynopsisRecord MangaSynopsisEnricher::cached(const QString& volumeId) const
{
    return m_cache.value(volumeId);
}

void MangaSynopsisEnricher::cacheRecord(const SynopsisRecord& record)
{
    if (record.volumeId.isEmpty())
        return;
    m_cache.insert(record.volumeId, record);
    saveCache();
}

void MangaSynopsisEnricher::loadCache()
{
    m_cache.clear();
    QFile f(m_cachePath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonParseError err{};
    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !jd.isObject())
        return;
    const QJsonObject root = jd.object();
    if (root.value(QStringLiteral("version")).toInt() != 1)
        return; // unknown schema — ignore rather than misread
    const QJsonObject records = root.value(QStringLiteral("records")).toObject();
    for (auto it = records.begin(); it != records.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        SynopsisRecord rec;
        rec.volumeId = o.value(QStringLiteral("volumeId")).toString(it.key());
        rec.text = o.value(QStringLiteral("text")).toString();
        rec.source = o.value(QStringLiteral("source")).toString();
        rec.sourceUrl = o.value(QStringLiteral("sourceUrl")).toString();
        rec.confidence = o.value(QStringLiteral("confidence")).toString();
        rec.fetchedAt = o.value(QStringLiteral("fetchedAt")).toString();
        rec.accepted = o.value(QStringLiteral("accepted")).toBool();
        if (!rec.volumeId.isEmpty())
            m_cache.insert(rec.volumeId, rec);
    }
}

void MangaSynopsisEnricher::saveCache()
{
    if (m_cachePath.isEmpty())
        return;
    QJsonObject records;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
        const SynopsisRecord& rec = it.value();
        QJsonObject o;
        o.insert(QStringLiteral("volumeId"), rec.volumeId);
        o.insert(QStringLiteral("text"), rec.text);
        o.insert(QStringLiteral("source"), rec.source);
        o.insert(QStringLiteral("sourceUrl"), rec.sourceUrl);
        o.insert(QStringLiteral("confidence"), rec.confidence);
        o.insert(QStringLiteral("fetchedAt"), rec.fetchedAt);
        o.insert(QStringLiteral("accepted"), rec.accepted);
        records.insert(rec.volumeId, o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("records"), records);

    QSaveFile f(m_cachePath);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

bool MangaSynopsisEnricher::isFresh(const SynopsisRecord& rec) const
{
    if (rec.volumeId.isEmpty() || rec.fetchedAt.isEmpty())
        return false;
    const QDateTime dt = QDateTime::fromString(rec.fetchedAt, Qt::ISODate);
    if (!dt.isValid())
        return false;
    const qint64 age = dt.secsTo(QDateTime::currentDateTimeUtc());
    if (age < 0)
        return false;
    return age < (rec.accepted ? kAcceptTtlSecs : static_cast<qint64>(kMissTtlSecs));
}

// ── Distinctness ────────────────────────────────────────────────────────────

bool MangaSynopsisEnricher::textIsDistinct(const Job& job, const QString& text) const
{
    if (!acceptDistinctVolumeText(text, job.seriesSynopsis))
        return false;
    const QStringList siblings = m_acceptedTextsBySeries.value(job.series.seriesId);
    for (const QString& t : siblings)
        if (!acceptDistinctVolumeText(text, t))
            return false;
    return true;
}

void MangaSynopsisEnricher::noteAccepted(const QString& seriesId, const QString& text)
{
    m_acceptedTextsBySeries[seriesId].append(text);
}

// ── Async cascade ───────────────────────────────────────────────────────────

void MangaSynopsisEnricher::enrichSeries(const SeriesSnapshot& series,
                                         const QString& seriesSynopsis)
{
    for (const VolumeRecord& vol : series.volumes) {
        Job job;
        job.volumeId = vol.id.isEmpty() ? volumeId(series.seriesId, vol.number) : vol.id;
        job.series = series;
        job.volume = vol;
        job.seriesSynopsis = seriesSynopsis;

        const SynopsisRecord existing = cached(job.volumeId);
        if (isFresh(existing)) {
            if (existing.accepted)
                emit synopsisReady(job.volumeId, existing); // serve cache, no refetch
            continue;
        }
        startOpenLibrary(job); // canonical rendering never waits on this
    }
}

void MangaSynopsisEnricher::startOpenLibrary(const Job& job)
{
    if (!m_nam)
        return;
    QUrl url(QStringLiteral("https://openlibrary.org/search.json"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"),
                   QStringLiteral("%1 Volume %2").arg(job.series.title, job.volume.number));
    if (!job.series.author.isEmpty())
        q.addQueryItem(QStringLiteral("author"), job.series.author);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    url.setQuery(q);

    auto* reply = m_nam->get(QNetworkRequest(url));
    const Job captured = job;
    connect(reply, &QNetworkReply::finished, this, [this, reply, captured]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            enqueueApple(captured); // network failure → no permanent negative; try Apple
            return;
        }
        SynopsisRecord rec = matchOpenLibrary(captured.series, captured.volume, reply->readAll());
        if (rec.accepted && textIsDistinct(captured, rec.text)) {
            rec.volumeId = captured.volumeId;
            cacheRecord(rec);
            noteAccepted(captured.series.seriesId, rec.text);
            emit synopsisReady(captured.volumeId, rec);
            return;
        }
        enqueueApple(captured); // no strong OL record (or not distinct) → Apple fallback
    });
}

void MangaSynopsisEnricher::enqueueApple(const Job& job)
{
    m_appleQueue.enqueue(job);
    scheduleApplePump();
}

void MangaSynopsisEnricher::scheduleApplePump()
{
    if (m_appleInFlight || m_appleQueue.isEmpty() || !m_nam)
        return;
    int delay = 0;
    if (m_lastAppleStart.isValid()) {
        const qint64 since = m_lastAppleStart.msecsTo(QDateTime::currentDateTimeUtc());
        if (since < kAppleMinSpacingMs)
            delay = static_cast<int>(kAppleMinSpacingMs - since);
    }
    QTimer::singleShot(delay, this, [this]() { startNextApple(); });
}

void MangaSynopsisEnricher::startNextApple()
{
    if (m_appleInFlight || m_appleQueue.isEmpty() || !m_nam)
        return;
    const Job job = m_appleQueue.dequeue();
    m_appleInFlight = true;
    m_lastAppleStart = QDateTime::currentDateTimeUtc();

    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("term"),
                   QStringLiteral("%1 Volume %2").arg(job.series.title, job.volume.number));
    q.addQueryItem(QStringLiteral("entity"), QStringLiteral("ebook"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("8"));
    q.addQueryItem(QStringLiteral("country"), QStringLiteral("us"));
    url.setQuery(q);

    auto* reply = m_nam->get(QNetworkRequest(url));
    const Job captured = job;
    connect(reply, &QNetworkReply::finished, this, [this, reply, captured]() {
        reply->deleteLater();
        m_appleInFlight = false;
        scheduleApplePump(); // release the single-flight gate for the next queued volume

        if (reply->error() != QNetworkReply::NoError)
            return; // network failure → no permanent negative
        SynopsisRecord rec = matchApple(captured.series, captured.volume, reply->readAll());
        if (rec.accepted && textIsDistinct(captured, rec.text)) {
            rec.volumeId = captured.volumeId;
            cacheRecord(rec);
            noteAccepted(captured.series.seriesId, rec.text);
            emit synopsisReady(captured.volumeId, rec);
            return;
        }
        // A valid response that yielded nothing acceptable: cache a 24h MISS so
        // the cascade doesn't re-hammer both providers on the next open.
        SynopsisRecord missRec;
        missRec.volumeId = captured.volumeId;
        missRec.confidence = QStringLiteral("none");
        missRec.accepted = false;
        missRec.fetchedAt = nowIso();
        cacheRecord(missRec);
    });
}

} // namespace MangaTankoban
