#include "ActivityProjector.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <functional>
#include <limits>

// This file is a literal, function-by-function port of activity-reference.js
// (the reference packet's executable oracle). Where the JS looks redundant —
// e.g. titleStatsFor()'s own "latest touch" update followed by a second,
// differently-gated update in the playback loop — that redundancy is ported
// as-is rather than "simplified", because the golden fixtures encode its
// exact tie-break behavior. See CPP-PORT-CONTRACT.md §21 for the parity bar.

namespace ActivityProjector {

ValidationError::ValidationError(const QString &message)
    : std::runtime_error(message.toStdString()) {}

namespace {

[[noreturn]] void fail(const QString &message) {
    throw ValidationError(message);
}

// ---------------------------------------------------------------------------
// Schema vocabulary (activity-reference.js TYPES/WORLDS/KINDS/COMPLETION_REASONS)
// ---------------------------------------------------------------------------

bool isValidType(const QString &value) {
    return value == QLatin1String("playback_delta")
        || value == QLatin1String("reading_delta")
        || value == QLatin1String("media_completed");
}

bool isValidWorld(const QString &value) {
    return value == QLatin1String("theatre")
        || value == QLatin1String("tankoban")
        || value == QLatin1String("biblio");
}

bool isValidKind(const QString &value) {
    static const QSet<QString> kinds{
        QStringLiteral("movie"), QStringLiteral("episode"), QStringLiteral("manga_chapter"),
        QStringLiteral("comic_issue"), QStringLiteral("tankoban_volume"), QStringLiteral("book"),
        QStringLiteral("audiobook")};
    return kinds.contains(value);
}

bool isValidCompletionReason(const QString &value) {
    static const QSet<QString> reasons{
        QStringLiteral("guarded_90_percent"), QStringLiteral("eof"),
        QStringLiteral("full_page_coverage"), QStringLiteral("sequential_book_end")};
    return reasons.contains(value);
}

void validateWorldKind(const QString &world, const QString &kind) {
    if (world == QLatin1String("theatre")
        && (kind == QLatin1String("movie") || kind == QLatin1String("episode")))
        return;
    if (world == QLatin1String("tankoban")
        && (kind == QLatin1String("manga_chapter") || kind == QLatin1String("comic_issue")
            || kind == QLatin1String("tankoban_volume")))
        return;
    if (world == QLatin1String("biblio")
        && (kind == QLatin1String("book") || kind == QLatin1String("audiobook")))
        return;
    fail(QStringLiteral("invalid world/kind combination"));
}

// ---------------------------------------------------------------------------
// Raw-JSON field access (mirrors requireString/requireInteger)
// ---------------------------------------------------------------------------

QString requireString(const QJsonObject &obj, const QString &key, bool allowEmpty = false) {
    const QJsonValue value = obj.value(key);
    if (!value.isString() || (!allowEmpty && value.toString().isEmpty()))
        fail(QStringLiteral("invalid %1").arg(key));
    return value.toString();
}

qint64 requireInteger(const QJsonObject &obj, const QString &key) {
    const QJsonValue value = obj.value(key);
    if (!value.isDouble())
        fail(QStringLiteral("invalid %1").arg(key));

    constexpr qint64 lowSentinel = std::numeric_limits<qint64>::min();
    constexpr qint64 highSentinel = std::numeric_limits<qint64>::max();
    qint64 parsed = value.toInteger(lowSentinel);
    if (parsed == lowSentinel) {
        parsed = value.toInteger(highSentinel);
        if (parsed == highSentinel)
            fail(QStringLiteral("invalid %1").arg(key));
    }
    return parsed;
}

// ---------------------------------------------------------------------------
// Canonicalization for eventId dedupe (mirrors canonicalize/stableStringify)
// ---------------------------------------------------------------------------

QJsonValue canonicalize(const QJsonValue &value) {
    if (value.isArray()) {
        QJsonArray out;
        for (const QJsonValue &item : value.toArray())
            out.append(canonicalize(item));
        return out;
    }
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        QStringList keys = obj.keys();
        keys.sort(Qt::CaseSensitive); // matches JS Object.keys().sort() default code-unit order
        QJsonObject out;
        for (const QString &key : keys)
            out.insert(key, canonicalize(obj.value(key)));
        return out;
    }
    return value;
}

QString stableStringify(const QJsonObject &obj) {
    return QString::fromUtf8(
        QJsonDocument(canonicalize(obj).toObject()).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// Normalized event (validated, typed view of one ledger entry)
// ---------------------------------------------------------------------------

struct Event {
    QString type;
    QString eventId;
    QString sessionId;
    QString world;
    QString kind;
    QString titleKey;
    QString itemKey;
    QString title;
    QString itemLabel;
    QString cover;
    qint64 utcOffsetMinutes = 0;
    bool syncable = false;
    QString source;

    // playback_delta
    qint64 startAtMs = 0;
    qint64 endAtMs = 0;
    qint64 activeMs = 0;
    qint64 rateMilli = 0;

    // reading_delta / media_completed share atMs
    qint64 atMs = 0;
    QString readingForm;
    QStringList pageKeys;
    qint64 progressMicros = 0;

    // media_completed
    QString reason;

    QJsonObject raw; // original object, for canonical-stringify dedupe compare

    qint64 eventTime() const {
        return type == QLatin1String("playback_delta") ? startAtMs : atMs;
    }
};

Event parseAndValidateEvent(const QJsonValue &rawValue) {
    if (!rawValue.isObject())
        fail(QStringLiteral("event must be an object"));
    const QJsonObject obj = rawValue.toObject();

    Event ev;
    ev.raw = obj;

    const QJsonValue vValue = obj.value(QStringLiteral("v"));
    if (!vValue.isDouble() || vValue.toDouble() != 1.0)
        fail(QStringLiteral("invalid schema version"));

    const QJsonValue typeValue = obj.value(QStringLiteral("type"));
    if (!typeValue.isString() || !isValidType(typeValue.toString()))
        fail(QStringLiteral("invalid event type"));
    ev.type = typeValue.toString();

    ev.eventId = requireString(obj, QStringLiteral("eventId"));
    ev.sessionId = requireString(obj, QStringLiteral("sessionId"));

    const QJsonValue worldValue = obj.value(QStringLiteral("world"));
    if (!worldValue.isString() || !isValidWorld(worldValue.toString()))
        fail(QStringLiteral("invalid world"));
    ev.world = worldValue.toString();

    const QJsonValue kindValue = obj.value(QStringLiteral("kind"));
    if (!kindValue.isString() || !isValidKind(kindValue.toString()))
        fail(QStringLiteral("invalid kind"));
    ev.kind = kindValue.toString();

    validateWorldKind(ev.world, ev.kind);

    ev.titleKey = requireString(obj, QStringLiteral("titleKey"));
    ev.itemKey = requireString(obj, QStringLiteral("itemKey"));
    ev.title = requireString(obj, QStringLiteral("title"));

    // Optional fields: only validated when the key is actually present in the
    // JSON (JS: `event[key] !== undefined`) — absent is fine, present-but-wrong-
    // type (including explicit null) is not.
    if (obj.contains(QStringLiteral("itemLabel")))
        ev.itemLabel = requireString(obj, QStringLiteral("itemLabel"), true);
    if (obj.contains(QStringLiteral("cover")))
        ev.cover = requireString(obj, QStringLiteral("cover"), true);

    ev.utcOffsetMinutes = requireInteger(obj, QStringLiteral("utcOffsetMinutes"));
    if (ev.utcOffsetMinutes < -840 || ev.utcOffsetMinutes > 840)
        fail(QStringLiteral("invalid utcOffsetMinutes"));

    const QJsonValue syncableValue = obj.value(QStringLiteral("syncable"));
    if (!syncableValue.isBool())
        fail(QStringLiteral("invalid syncable"));
    ev.syncable = syncableValue.toBool();

    if (obj.contains(QStringLiteral("source")))
        ev.source = requireString(obj, QStringLiteral("source"), true);

    if (ev.type == QLatin1String("playback_delta")) {
        if (!(ev.kind == QLatin1String("movie") || ev.kind == QLatin1String("episode")
              || ev.kind == QLatin1String("audiobook")))
            fail(QStringLiteral("invalid playback kind"));
        ev.startAtMs = requireInteger(obj, QStringLiteral("startAtMs"));
        ev.endAtMs = requireInteger(obj, QStringLiteral("endAtMs"));
        ev.activeMs = requireInteger(obj, QStringLiteral("activeMs"));
        ev.rateMilli = requireInteger(obj, QStringLiteral("rateMilli"));
        if (ev.endAtMs <= ev.startAtMs)
            fail(QStringLiteral("invalid playback interval"));
        if (ev.activeMs != ev.endAtMs - ev.startAtMs)
            fail(QStringLiteral("activeMs mismatch"));
        if (ev.activeMs <= 0 || ev.activeMs > 30000)
            fail(QStringLiteral("invalid activeMs"));
        if (ev.rateMilli <= 0)
            fail(QStringLiteral("invalid rateMilli"));
    } else if (ev.type == QLatin1String("reading_delta")) {
        if (!(ev.kind == QLatin1String("manga_chapter") || ev.kind == QLatin1String("comic_issue")
              || ev.kind == QLatin1String("tankoban_volume") || ev.kind == QLatin1String("book")))
            fail(QStringLiteral("invalid reading kind"));
        ev.atMs = requireInteger(obj, QStringLiteral("atMs"));

        const QJsonValue formValue = obj.value(QStringLiteral("readingForm"));
        const QString form = formValue.isString() ? formValue.toString() : QString();
        if (form != QLatin1String("fixed") && form != QLatin1String("reflowable"))
            fail(QStringLiteral("invalid readingForm"));
        ev.readingForm = form;

        const QJsonValue pageKeysValue = obj.value(QStringLiteral("pageKeys"));
        if (!pageKeysValue.isArray())
            fail(QStringLiteral("invalid pageKeys"));
        QSet<QString> seenPageKeys;
        for (const QJsonValue &item : pageKeysValue.toArray()) {
            if (!item.isString() || item.toString().isEmpty())
                fail(QStringLiteral("invalid pageKey"));
            const QString pageKey = item.toString();
            if (seenPageKeys.contains(pageKey))
                fail(QStringLiteral("duplicate pageKey in event"));
            seenPageKeys.insert(pageKey);
            ev.pageKeys.append(pageKey);
        }

        ev.progressMicros = requireInteger(obj, QStringLiteral("progressMicros"));
        if (ev.progressMicros < 0)
            fail(QStringLiteral("invalid progressMicros"));
        if (ev.readingForm == QLatin1String("reflowable") && !ev.pageKeys.isEmpty())
            fail(QStringLiteral("reflowable pageKeys must be empty"));
        if (ev.pageKeys.isEmpty() && ev.progressMicros == 0)
            fail(QStringLiteral("empty reading_delta"));
    } else { // media_completed
        ev.atMs = requireInteger(obj, QStringLiteral("atMs"));
        const QJsonValue reasonValue = obj.value(QStringLiteral("reason"));
        if (!reasonValue.isString() || !isValidCompletionReason(reasonValue.toString()))
            fail(QStringLiteral("invalid completion reason"));
        ev.reason = reasonValue.toString();
    }

    return ev;
}

QVector<Event> validateAndDedupe(const QJsonArray &events) {
    QHash<QString, QString> canonicalById;
    QVector<Event> result;
    result.reserve(events.size());
    for (const QJsonValue &rawValue : events) {
        Event event = parseAndValidateEvent(rawValue);
        const QString canonical = stableStringify(event.raw);
        const auto it = canonicalById.constFind(event.eventId);
        if (it != canonicalById.constEnd()) {
            if (it.value() != canonical)
                fail(QStringLiteral("eventId conflict: %1").arg(event.eventId));
            continue; // exact duplicate — idempotent, ignored after the first copy
        }
        canonicalById.insert(event.eventId, canonical);
        result.append(std::move(event));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Local date/month + playback boundary splitting (mirrors localParts /
// splitPlaybackForMonth). Uses Howard Hinnant's civil-calendar algorithm
// (http://howardhinnant.github.io/date_algorithms.html, public domain) for
// proleptic-Gregorian day<->y/m/d conversion — this is exactly the calendar
// JS Date's UTC methods use, valid for the full qint64 range, and has no
// locale/timezone-database or machine-clock dependency (CPP-PORT-CONTRACT §12).
// ---------------------------------------------------------------------------

constexpr qint64 kMsPerDay = 86400000;

qint64 floorDiv(qint64 a, qint64 b) {
    qint64 q = a / b;
    const qint64 r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        --q;
    return q;
}

qint64 daysFromCivil(qint64 y, qint64 m, qint64 d) {
    y -= (m <= 2) ? 1 : 0;
    const qint64 era = (y >= 0 ? y : y - 399) / 400;
    const qint64 yoe = y - era * 400;
    const qint64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const qint64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civilFromDays(qint64 z, qint64 &y, qint64 &m, qint64 &d) {
    z += 719468;
    const qint64 era = (z >= 0 ? z : z - 146096) / 146097;
    const qint64 doe = z - era * 146097;
    const qint64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    const qint64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const qint64 mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2) ? 1 : 0;
}

struct LocalParts {
    qint64 year = 0;
    qint64 month = 0;
    qint64 day = 0;
    QString key;      // YYYY-MM-DD
    QString monthKey; // YYYY-MM
};

QString twoDigits(qint64 value) {
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

LocalParts localParts(qint64 ms, qint64 utcOffsetMinutes) {
    const qint64 shiftedMs = ms + utcOffsetMinutes * 60000;
    const qint64 days = floorDiv(shiftedMs, kMsPerDay);
    qint64 y = 0, m = 0, d = 0;
    civilFromDays(days, y, m, d);

    LocalParts parts;
    parts.year = y;
    parts.month = m;
    parts.day = d;
    parts.monthKey = QStringLiteral("%1-%2").arg(y).arg(twoDigits(m));
    parts.key = parts.monthKey + QLatin1Char('-') + twoDigits(d);
    return parts;
}

void assertMonthKey(const QString &monthKey) {
    static const QRegularExpression re(QStringLiteral("^\\d{4}-(0[1-9]|1[0-2])$"));
    if (!re.match(monthKey).hasMatch())
        fail(QStringLiteral("invalid month key"));
}

struct PlaybackSegment {
    qint64 startAtMs = 0;
    qint64 endAtMs = 0;
    qint64 activeMs = 0;
    QString localDate;
};

QVector<PlaybackSegment> splitPlaybackForMonth(const Event &event, const QString &monthKey) {
    const qint64 offsetMs = event.utcOffsetMinutes * 60000;
    QVector<PlaybackSegment> result;
    qint64 cursor = event.startAtMs;
    while (cursor < event.endAtMs) {
        const qint64 shiftedMs = cursor + offsetMs;
        const qint64 days = floorDiv(shiftedMs, kMsPerDay);
        qint64 y = 0, m = 0, d = 0;
        civilFromDays(days, y, m, d);
        const qint64 nextLocalMidnightShiftedMs = daysFromCivil(y, m, d + 1) * kMsPerDay;
        const qint64 nextBoundaryUtc = nextLocalMidnightShiftedMs - offsetMs;
        const qint64 segmentEnd = std::min(event.endAtMs, nextBoundaryUtc);

        const LocalParts parts = localParts(cursor, event.utcOffsetMinutes);
        if (parts.monthKey == monthKey) {
            PlaybackSegment segment;
            segment.startAtMs = cursor;
            segment.endAtMs = segmentEnd;
            segment.activeMs = segmentEnd - cursor;
            segment.localDate = parts.key;
            result.append(segment);
        }
        cursor = segmentEnd;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Ordering + lifetime dedupe (mirrors compareTimed / firstCompletionEvents /
// firstFixedPageFacts)
// ---------------------------------------------------------------------------

bool compareTimedLess(const Event *a, const Event *b) {
    const qint64 delta = a->eventTime() - b->eventTime();
    if (delta != 0)
        return delta < 0;
    return a->eventId < b->eventId; // code-unit order: deterministic, locale-independent
}

QHash<QString, const Event *> firstCompletionEvents(const QVector<Event> &events) {
    QVector<const Event *> completions;
    for (const Event &event : events)
        if (event.type == QLatin1String("media_completed"))
            completions.append(&event);
    std::stable_sort(completions.begin(), completions.end(), compareTimedLess);

    QHash<QString, const Event *> first;
    for (const Event *event : completions) {
        const QString key = event->kind + QLatin1Char('|') + event->itemKey;
        if (!first.contains(key))
            first.insert(key, event);
    }
    return first;
}

struct PageFact {
    QString eventId;
};

QHash<QString, PageFact> firstFixedPageFacts(const QVector<Event> &events) {
    QVector<const Event *> readings;
    for (const Event &event : events)
        if (event.type == QLatin1String("reading_delta") && event.readingForm == QLatin1String("fixed"))
            readings.append(&event);
    std::stable_sort(readings.begin(), readings.end(), compareTimedLess);

    QHash<QString, PageFact> first;
    for (const Event *event : readings) {
        for (const QString &pageKey : event->pageKeys) {
            const QString key = event->sessionId + QLatin1Char('|') + event->kind + QLatin1Char('|')
                + event->itemKey + QLatin1Char('|') + pageKey;
            if (!first.contains(key))
                first.insert(key, PageFact{event->eventId});
        }
    }
    return first;
}

// ---------------------------------------------------------------------------
// Title-level aggregation (mirrors titleStatsFor). Representative-event world/
// kind per role is tracked as small copies (not full Event pointers) since
// makeHighlight() only ever reads rep.world/rep.kind, and each role's world
// is either fixed by construction (tankoban/biblio_ebook/audiobook) or a
// per-touch kind copy (theatre/completion).
// ---------------------------------------------------------------------------

struct TitleStats {
    QString titleKey;
    QString title;
    QString cover;
    QString world;
    QString kind;
    qint64 watchMs = 0;
    qint64 listenMs = 0;
    qint64 tankobanPages = 0;
    qint64 bookPages = 0;
    qint64 progressMicros = 0;
    qint64 completionCount = 0;
    qint64 latestAtMs = -1;
    qint64 latestTheatreAtMs = -1;
    qint64 latestTankobanAtMs = -1;
    qint64 latestBookAtMs = -1;
    qint64 latestAudioAtMs = -1;
    qint64 latestCompletionAtMs = -1;

    bool hasRepTheatre = false;
    QString repTheatreKind;
    bool hasRepTankoban = false;
    QString repTankobanKind;
    bool hasRepBook = false;
    bool hasRepAudio = false;
    bool hasRepCompletion = false;
    QString repCompletionWorld;
    QString repCompletionKind;
};

// Insertion-ordered title map. Final highlight ordering always resolves ties
// down to a unique ascending titleKey, so insertion order never actually
// leaks into output — kept ordered anyway for a literal, obviously-correct
// mirror of the JS Map rather than relying on that proof staying true.
class TitleMap {
public:
    // Mirrors titleStatsFor(): create-if-missing, then the map's own "latest
    // touch" update using eventTime(event) with a >= comparator.
    TitleStats &statsFor(const Event &event) {
        auto it = m_index.constFind(event.titleKey);
        int index;
        if (it == m_index.constEnd()) {
            index = m_stats.size();
            TitleStats stats;
            stats.titleKey = event.titleKey;
            m_stats.append(stats);
            m_index.insert(event.titleKey, index);
        } else {
            index = it.value();
        }
        TitleStats &stats = m_stats[index];
        applyLatestTouch(stats, event.eventTime(), event, /*strictlyGreater=*/false);
        return stats;
    }

    static void applyLatestTouch(TitleStats &stats, qint64 atMs, const Event &event, bool strictlyGreater) {
        const bool shouldUpdate = strictlyGreater ? (atMs > stats.latestAtMs) : (atMs >= stats.latestAtMs);
        if (shouldUpdate) {
            stats.latestAtMs = atMs;
            stats.title = event.title;
            stats.cover = event.cover;
            stats.world = event.world;
            stats.kind = event.kind;
        }
    }

    const QVector<TitleStats> &all() const { return m_stats; }

private:
    QVector<TitleStats> m_stats;
    QHash<QString, int> m_index;
};

struct Moment {
    QString localDate;
    QString sessionId;
    QString world;
    QString kind;
    QString titleKey;
    QString itemKey;
    QString title;
    QString itemLabel;
    QString cover;
    qint64 watchMs = 0;
    qint64 listenMs = 0;
    qint64 pagesRead = 0;
    qint64 progressMicros = 0;
    bool completed = false;
    qint64 lastAtMs = -1;
};

// Insertion-ordered moment map (mirrors momentFor()'s Map). Recent Activity's
// sort tiebreak key omits localDate, so — unlike titles — two moments CAN tie
// on it; insertion order here is what makes that tie's resolution match the
// JS oracle's stable Array.prototype.sort() exactly.
class MomentMap {
public:
    Moment &momentFor(const Event &event, const QString &localDate) {
        const QString key = localDate + QLatin1Char('|') + event.sessionId + QLatin1Char('|')
            + event.kind + QLatin1Char('|') + event.itemKey;
        auto it = m_index.constFind(key);
        int index;
        if (it == m_index.constEnd()) {
            index = m_moments.size();
            Moment moment;
            moment.localDate = localDate;
            moment.sessionId = event.sessionId;
            moment.world = event.world;
            moment.kind = event.kind;
            moment.titleKey = event.titleKey;
            moment.itemKey = event.itemKey;
            moment.title = event.title;
            moment.itemLabel = event.itemLabel;
            moment.cover = event.cover;
            m_moments.append(moment);
            m_index.insert(key, index);
        } else {
            index = it.value();
        }
        return m_moments[index];
    }

    const QVector<Moment> &all() const { return m_moments; }

private:
    QVector<Moment> m_moments;
    QHash<QString, int> m_index;
};

// ---------------------------------------------------------------------------
// Highlights (mirrors compareMetric / makeHighlight / buildHighlights)
// ---------------------------------------------------------------------------

enum class HighlightRole { Theatre, Tankoban, BiblioEbook, Audiobook, Completion, Recent };

QString roleName(HighlightRole role) {
    switch (role) {
    case HighlightRole::Theatre: return QStringLiteral("theatre");
    case HighlightRole::Tankoban: return QStringLiteral("tankoban");
    case HighlightRole::BiblioEbook: return QStringLiteral("biblio_ebook");
    case HighlightRole::Audiobook: return QStringLiteral("audiobook");
    case HighlightRole::Completion: return QStringLiteral("completion");
    case HighlightRole::Recent: return QStringLiteral("recent");
    }
    return QString();
}

struct Highlight {
    QString role;
    QString titleKey;
    QString title;
    QString cover;
    QString world;
    QString kind;
    qint64 watchSeconds = 0;
    qint64 listenSeconds = 0;
    qint64 pagesRead = 0;
    qint64 progressMicros = 0;
    qint64 completedCount = 0;
    qint64 lastActivityAtMs = -1;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj.insert(QStringLiteral("role"), role);
        obj.insert(QStringLiteral("titleKey"), titleKey);
        obj.insert(QStringLiteral("title"), title);
        obj.insert(QStringLiteral("cover"), cover);
        obj.insert(QStringLiteral("world"), world);
        obj.insert(QStringLiteral("kind"), kind);
        obj.insert(QStringLiteral("watchSeconds"), watchSeconds);
        obj.insert(QStringLiteral("listenSeconds"), listenSeconds);
        obj.insert(QStringLiteral("pagesRead"), pagesRead);
        obj.insert(QStringLiteral("progressMicros"), progressMicros);
        obj.insert(QStringLiteral("completedCount"), completedCount);
        obj.insert(QStringLiteral("lastActivityAtMs"), lastActivityAtMs);
        return obj;
    }
};

Highlight makeHighlight(const TitleStats &stats, HighlightRole role) {
    QString world = stats.world;
    QString kind = stats.kind;
    switch (role) {
    case HighlightRole::Theatre:
        if (stats.hasRepTheatre) {
            world = QStringLiteral("theatre");
            kind = stats.repTheatreKind;
        }
        break;
    case HighlightRole::Tankoban:
        if (stats.hasRepTankoban) {
            world = QStringLiteral("tankoban");
            kind = stats.repTankobanKind;
        }
        break;
    case HighlightRole::BiblioEbook:
        if (stats.hasRepBook) {
            world = QStringLiteral("biblio");
            kind = QStringLiteral("book");
        }
        break;
    case HighlightRole::Audiobook:
        if (stats.hasRepAudio) {
            world = QStringLiteral("biblio");
            kind = QStringLiteral("audiobook");
        }
        break;
    case HighlightRole::Completion:
        if (stats.hasRepCompletion) {
            world = stats.repCompletionWorld;
            kind = stats.repCompletionKind;
        }
        break;
    case HighlightRole::Recent:
        // JS default branch uses stats.repLatest, which is always set in
        // lockstep with stats.world/kind (same condition, same event) — so it
        // never diverges from the stats.world/kind already assigned above.
        break;
    }

    Highlight highlight;
    highlight.role = roleName(role);
    highlight.titleKey = stats.titleKey;
    highlight.title = stats.title;
    highlight.cover = stats.cover;
    highlight.world = world;
    highlight.kind = kind;
    highlight.watchSeconds = stats.watchMs / 1000;
    highlight.listenSeconds = stats.listenMs / 1000;
    highlight.pagesRead = stats.tankobanPages + stats.bookPages;
    highlight.progressMicros = stats.progressMicros;
    highlight.completedCount = stats.completionCount;
    highlight.lastActivityAtMs = stats.latestAtMs;
    return highlight;
}

// Mirrors compareMetric(primary, latestKey, secondary): descending primary,
// then descending secondary (if any), then descending latestKey, then
// ascending titleKey as the final deterministic tiebreak.
bool metricLess(const TitleStats &a, const TitleStats &b,
                 qint64 TitleStats::*primary, qint64 TitleStats::*latestKey,
                 qint64 TitleStats::*secondary) {
    const qint64 ap = a.*primary;
    const qint64 bp = b.*primary;
    if (ap != bp)
        return ap > bp;
    if (secondary != nullptr) {
        const qint64 as = a.*secondary;
        const qint64 bs = b.*secondary;
        if (as != bs)
            return as > bs;
    }
    const qint64 al = a.*latestKey;
    const qint64 bl = b.*latestKey;
    if (al != bl)
        return al > bl;
    return a.titleKey < b.titleKey;
}

QJsonArray buildHighlights(const QVector<TitleStats> &all) {
    struct RoleSpec {
        HighlightRole role;
        qint64 TitleStats::*primary;
        qint64 TitleStats::*latest;
        qint64 TitleStats::*secondary;
        std::function<bool(const TitleStats &)> filter;
    };

    const QVector<RoleSpec> roles = {
        {HighlightRole::Theatre, &TitleStats::watchMs, &TitleStats::latestTheatreAtMs, nullptr,
         [](const TitleStats &s) { return s.watchMs > 0; }},
        {HighlightRole::Tankoban, &TitleStats::tankobanPages, &TitleStats::latestTankobanAtMs, nullptr,
         [](const TitleStats &s) { return s.tankobanPages > 0; }},
        {HighlightRole::BiblioEbook, &TitleStats::progressMicros, &TitleStats::latestBookAtMs,
         &TitleStats::bookPages,
         [](const TitleStats &s) { return s.progressMicros > 0 || s.bookPages > 0; }},
        {HighlightRole::Audiobook, &TitleStats::listenMs, &TitleStats::latestAudioAtMs, nullptr,
         [](const TitleStats &s) { return s.listenMs > 0; }},
        {HighlightRole::Completion, &TitleStats::completionCount, &TitleStats::latestCompletionAtMs,
         nullptr, [](const TitleStats &s) { return s.completionCount > 0; }},
    };

    QJsonArray chosen;
    QSet<QString> used;

    for (const RoleSpec &spec : roles) {
        QVector<const TitleStats *> candidates;
        for (const TitleStats &stats : all)
            if (spec.filter(stats))
                candidates.append(&stats);
        if (candidates.isEmpty())
            continue;

        std::stable_sort(candidates.begin(), candidates.end(),
            [&spec](const TitleStats *a, const TitleStats *b) {
                return metricLess(*a, *b, spec.primary, spec.latest, spec.secondary);
            });

        const TitleStats *winner = candidates.first();
        if (used.contains(winner->titleKey))
            continue;
        chosen.append(makeHighlight(*winner, spec.role).toJson());
        used.insert(winner->titleKey);
        if (chosen.size() == 4)
            return chosen; // matches JS's early return the instant the 4th slot fills
    }

    QVector<const TitleStats *> fallback;
    for (const TitleStats &stats : all)
        if (!used.contains(stats.titleKey) && stats.latestAtMs >= 0)
            fallback.append(&stats);
    std::stable_sort(fallback.begin(), fallback.end(), [](const TitleStats *a, const TitleStats *b) {
        if (a->latestAtMs != b->latestAtMs)
            return a->latestAtMs > b->latestAtMs;
        return a->titleKey < b->titleKey;
    });
    for (const TitleStats *stats : fallback) {
        chosen.append(makeHighlight(*stats, HighlightRole::Recent).toJson());
        if (chosen.size() == 4)
            break;
    }
    return chosen;
}

// ---------------------------------------------------------------------------
// Recent Activity (mirrors buildRecentActivity)
// ---------------------------------------------------------------------------

QString recentVerb(const Moment &moment) {
    if (moment.completed)
        return QStringLiteral("completed");
    if (moment.kind == QLatin1String("movie") || moment.kind == QLatin1String("episode"))
        return QStringLiteral("watched");
    if (moment.kind == QLatin1String("audiobook"))
        return QStringLiteral("listened");
    return QStringLiteral("read");
}

QJsonObject momentToJson(const Moment &moment) {
    QJsonObject obj;
    obj.insert(QStringLiteral("localDate"), moment.localDate);
    obj.insert(QStringLiteral("sessionId"), moment.sessionId);
    obj.insert(QStringLiteral("world"), moment.world);
    obj.insert(QStringLiteral("kind"), moment.kind);
    obj.insert(QStringLiteral("titleKey"), moment.titleKey);
    obj.insert(QStringLiteral("itemKey"), moment.itemKey);
    obj.insert(QStringLiteral("title"), moment.title);
    obj.insert(QStringLiteral("itemLabel"), moment.itemLabel);
    obj.insert(QStringLiteral("cover"), moment.cover);
    obj.insert(QStringLiteral("verb"), recentVerb(moment));
    obj.insert(QStringLiteral("watchSeconds"), moment.watchMs / 1000);
    obj.insert(QStringLiteral("listenSeconds"), moment.listenMs / 1000);
    obj.insert(QStringLiteral("pagesRead"), moment.pagesRead);
    obj.insert(QStringLiteral("progressMicros"), moment.progressMicros);
    obj.insert(QStringLiteral("completed"), moment.completed);
    obj.insert(QStringLiteral("lastAtMs"), moment.lastAtMs);
    return obj;
}

QJsonArray buildRecentActivity(const QVector<Moment> &all) {
    QVector<const Moment *> rows;
    rows.reserve(all.size());
    for (const Moment &moment : all)
        rows.append(&moment);

    std::stable_sort(rows.begin(), rows.end(), [](const Moment *a, const Moment *b) {
        if (a->lastAtMs != b->lastAtMs)
            return a->lastAtMs > b->lastAtMs;
        const QString aKey = a->kind + QLatin1Char('|') + a->itemKey + QLatin1Char('|') + a->sessionId;
        const QString bKey = b->kind + QLatin1Char('|') + b->itemKey + QLatin1Char('|') + b->sessionId;
        return aKey < bKey;
    });

    QJsonArray result;
    for (const Moment *moment : rows)
        result.append(momentToJson(*moment));
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// projectMonth (mirrors projectMonth)
// ---------------------------------------------------------------------------

QJsonObject projectMonth(const QJsonArray &events, const QString &monthKey) {
    assertMonthKey(monthKey);
    const QVector<Event> deduped = validateAndDedupe(events);
    const QHash<QString, const Event *> firstCompletions = firstCompletionEvents(deduped);
    const QHash<QString, PageFact> firstPages = firstFixedPageFacts(deduped);

    QSet<QString> activeDates;
    TitleMap titles;
    MomentMap moments;
    qint64 watchMs = 0;
    qint64 listenMs = 0;
    qint64 pagesRead = 0;
    qint64 completedCount = 0;

    for (const Event &event : deduped) {
        if (event.type == QLatin1String("playback_delta")) {
            for (const PlaybackSegment &segment : splitPlaybackForMonth(event, monthKey)) {
                TitleStats &stats = titles.statsFor(event);
                const qint64 atMs = std::max(segment.startAtMs, segment.endAtMs - 1);
                TitleMap::applyLatestTouch(stats, atMs, event, /*strictlyGreater=*/true);

                if (event.world == QLatin1String("theatre")
                    && (event.kind == QLatin1String("movie") || event.kind == QLatin1String("episode"))) {
                    watchMs += segment.activeMs;
                    stats.watchMs += segment.activeMs;
                    if (atMs >= stats.latestTheatreAtMs) {
                        stats.latestTheatreAtMs = atMs;
                        stats.hasRepTheatre = true;
                        stats.repTheatreKind = event.kind;
                    }
                } else if (event.world == QLatin1String("biblio") && event.kind == QLatin1String("audiobook")) {
                    listenMs += segment.activeMs;
                    stats.listenMs += segment.activeMs;
                    if (atMs >= stats.latestAudioAtMs) {
                        stats.latestAudioAtMs = atMs;
                        stats.hasRepAudio = true;
                    }
                }

                activeDates.insert(segment.localDate);
                Moment &moment = moments.momentFor(event, segment.localDate);
                if (event.world == QLatin1String("theatre"))
                    moment.watchMs += segment.activeMs;
                else
                    moment.listenMs += segment.activeMs;
                moment.lastAtMs = std::max(moment.lastAtMs, atMs);
            }
            continue;
        }

        const LocalParts parts = localParts(event.atMs, event.utcOffsetMinutes);
        if (parts.monthKey != monthKey)
            continue;

        if (event.type == QLatin1String("reading_delta")) {
            qint64 countedPages = 0;
            if (event.readingForm == QLatin1String("fixed")) {
                for (const QString &pageKey : event.pageKeys) {
                    const QString key = event.sessionId + QLatin1Char('|') + event.kind + QLatin1Char('|')
                        + event.itemKey + QLatin1Char('|') + pageKey;
                    const auto it = firstPages.constFind(key);
                    if (it != firstPages.constEnd() && it.value().eventId == event.eventId)
                        ++countedPages;
                }
            }
            if (countedPages == 0 && event.progressMicros == 0)
                continue;

            pagesRead += countedPages;
            activeDates.insert(parts.key);
            TitleStats &stats = titles.statsFor(event);
            if (event.world == QLatin1String("tankoban")) {
                stats.tankobanPages += countedPages;
                if (event.atMs >= stats.latestTankobanAtMs) {
                    stats.latestTankobanAtMs = event.atMs;
                    stats.hasRepTankoban = true;
                    stats.repTankobanKind = event.kind;
                }
            } else if (event.world == QLatin1String("biblio") && event.kind == QLatin1String("book")) {
                stats.bookPages += countedPages;
                stats.progressMicros += event.progressMicros;
                if (event.atMs >= stats.latestBookAtMs) {
                    stats.latestBookAtMs = event.atMs;
                    stats.hasRepBook = true;
                }
            }

            Moment &moment = moments.momentFor(event, parts.key);
            moment.pagesRead += countedPages;
            moment.progressMicros += event.progressMicros;
            moment.lastAtMs = std::max(moment.lastAtMs, event.atMs);
            continue;
        }

        // media_completed
        activeDates.insert(parts.key);
        TitleStats &stats = titles.statsFor(event);
        const QString completionKey = event.kind + QLatin1Char('|') + event.itemKey;
        const auto first = firstCompletions.constFind(completionKey);
        if (first != firstCompletions.constEnd() && first.value()->eventId == event.eventId) {
            ++completedCount;
            ++stats.completionCount;
            if (event.atMs >= stats.latestCompletionAtMs) {
                stats.latestCompletionAtMs = event.atMs;
                stats.hasRepCompletion = true;
                stats.repCompletionWorld = event.world;
                stats.repCompletionKind = event.kind;
            }
        }

        Moment &moment = moments.momentFor(event, parts.key);
        moment.completed = true;
        moment.lastAtMs = std::max(moment.lastAtMs, event.atMs);
    }

    QJsonObject result;
    result.insert(QStringLiteral("month"), monthKey);
    result.insert(QStringLiteral("watchSeconds"), watchMs / 1000);
    result.insert(QStringLiteral("listenSeconds"), listenMs / 1000);
    result.insert(QStringLiteral("pagesRead"), pagesRead);
    result.insert(QStringLiteral("completedCount"), completedCount);
    result.insert(QStringLiteral("activeDays"), static_cast<qint64>(activeDates.size()));
    result.insert(QStringLiteral("highlights"), buildHighlights(titles.all()));
    result.insert(QStringLiteral("recentActivity"), buildRecentActivity(moments.all()));
    return result;
}

// ---------------------------------------------------------------------------
// Slice D2 shared seam (see ActivityProjector.h) — thin wrappers over the
// exact same anonymous-namespace helpers projectMonth() already calls above.
// ---------------------------------------------------------------------------

void validateEvent(const QJsonObject &event) {
    parseAndValidateEvent(event);
}

QString canonicalEventJson(const QJsonObject &event) {
    return stableStringify(event);
}

QString localMonthKey(qint64 utcMs, qint64 utcOffsetMinutes) {
    return localParts(utcMs, utcOffsetMinutes).monthKey;
}

} // namespace ActivityProjector
