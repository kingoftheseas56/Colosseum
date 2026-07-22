#include "AlignmentStore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>
#include <QAtomicInt>
#include <QHash>
#include <algorithm>

namespace alignment {

namespace {
QAtomicInt g_connCounter = 0;

// A convenience: fetch this store's open connection by name every call. Holding a
// QSqlDatabase member would keep a copy alive and make removeDatabase() warn; the
// name-based lookup sidesteps that entirely.
QSqlDatabase db(const QString &conn) { return QSqlDatabase::database(conn); }
} // namespace

AlignmentStore::AlignmentStore(const QString &dbPath)
    : m_conn(QStringLiteral("alignment_store_%1").arg(g_connCounter.fetchAndAddOrdered(1))) {
    QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    d.setDatabaseName(dbPath);
    if (!d.open())
        return;
    QSqlQuery pragma(d);
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    m_open = createSchema();
}

AlignmentStore::~AlignmentStore() {
    if (QSqlDatabase::contains(m_conn)) {
        {
            QSqlDatabase d = db(m_conn);
            if (d.isOpen())
                d.close();
        }
        QSqlDatabase::removeDatabase(m_conn);
    }
}

bool AlignmentStore::createSchema() {
    QSqlDatabase d = db(m_conn);
    QSqlQuery q(d);
    // Schema is the approved-plan contract, verbatim, guarded with IF NOT EXISTS so
    // reopening an existing database is a no-op rather than an error.
    const char *ddl[] = {
        "CREATE TABLE IF NOT EXISTS pair_alignment(pair_id TEXT PRIMARY KEY, epub_fingerprint TEXT NOT NULL,"
        " audio_fingerprint TEXT NOT NULL, language TEXT NOT NULL CHECK(language='en'), engine_version TEXT NOT NULL,"
        " coarse_model_id TEXT NOT NULL, alignment_model_id TEXT NOT NULL, overall_state TEXT NOT NULL,"
        " created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS chapter_job(id INTEGER PRIMARY KEY, pair_id TEXT NOT NULL, audio_chapter_index INTEGER NOT NULL,"
        " audio_start_ms INTEGER NOT NULL, audio_end_ms INTEGER NOT NULL, stage TEXT NOT NULL, checkpoint BLOB,"
        " coverage REAL NOT NULL DEFAULT 0, confidence REAL NOT NULL DEFAULT 0, failure_code TEXT, failure_detail TEXT,"
        " priority INTEGER NOT NULL, UNIQUE(pair_id,audio_chapter_index))",
        "CREATE TABLE IF NOT EXISTS sentence_cue(id INTEGER PRIMARY KEY, chapter_job_id INTEGER NOT NULL, ordinal INTEGER NOT NULL,"
        " start_ms INTEGER NOT NULL, end_ms INTEGER NOT NULL, spine_href TEXT NOT NULL, canonical_start INTEGER NOT NULL,"
        " canonical_end INTEGER NOT NULL, sentence_hash TEXT NOT NULL, confidence REAL NOT NULL, region_kind TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS word_cue(id INTEGER PRIMARY KEY, sentence_cue_id INTEGER NOT NULL, ordinal INTEGER NOT NULL,"
        " start_ms INTEGER NOT NULL, end_ms INTEGER NOT NULL, canonical_start INTEGER NOT NULL,"
        " canonical_end INTEGER NOT NULL, confidence REAL NOT NULL)",
        "CREATE INDEX IF NOT EXISTS cue_by_time ON sentence_cue(chapter_job_id,start_ms,end_ms)",
        "CREATE INDEX IF NOT EXISTS cue_by_text ON sentence_cue(spine_href,canonical_start,canonical_end)",
        "CREATE INDEX IF NOT EXISTS word_by_time ON word_cue(sentence_cue_id,start_ms,end_ms)",
    };
    for (const char *stmt : ddl) {
        if (!q.exec(QLatin1String(stmt)))
            return false;
    }
    return true;
}

bool AlignmentStore::hasPair(const QString &pairId) const {
    if (!m_open) return false;
    QSqlQuery q(db(m_conn));
    q.prepare(QStringLiteral("SELECT 1 FROM pair_alignment WHERE pair_id=?"));
    q.addBindValue(pairId);
    return q.exec() && q.next();
}

PairIdentity AlignmentStore::pairIdentity(const QString &pairId) const {
    PairIdentity p;
    if (!m_open) return p;
    QSqlQuery q(db(m_conn));
    q.prepare(QStringLiteral("SELECT epub_fingerprint,audio_fingerprint,language,engine_version,"
                             "coarse_model_id,alignment_model_id FROM pair_alignment WHERE pair_id=?"));
    q.addBindValue(pairId);
    if (q.exec() && q.next()) {
        p.pairId = pairId;
        p.epubFingerprint  = q.value(0).toString();
        p.audioFingerprint = q.value(1).toString();
        p.language         = q.value(2).toString();
        p.engineVersion    = q.value(3).toString();
        p.coarseModelId    = q.value(4).toString();
        p.alignmentModelId = q.value(5).toString();
    }
    return p;
}

QString AlignmentStore::overallState(const QString &pairId) const {
    if (!m_open) return QString();
    QSqlQuery q(db(m_conn));
    q.prepare(QStringLiteral("SELECT overall_state FROM pair_alignment WHERE pair_id=?"));
    q.addBindValue(pairId);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return QString();
}

bool AlignmentStore::upsertPair(const PairIdentity &pair) {
    if (!m_open || pair.pairId.isEmpty()) return false;
    if (pair.language != QLatin1String("en")) return false; // English only, permanently

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSqlDatabase d = db(m_conn);
    if (!d.transaction()) return false;

    const PairIdentity existing = pairIdentity(pair.pairId);
    const bool exists = hasPair(pair.pairId);

    if (exists) {
        // Asset replacement (fingerprint change) invalidates this pair's results.
        // Engine/model change alone does not — a usable result survives an upgrade.
        const bool assetChanged = existing.epubFingerprint != pair.epubFingerprint
                               || existing.audioFingerprint != pair.audioFingerprint;
        if (assetChanged) {
            QSqlQuery del(d);
            del.prepare(QStringLiteral(
                "DELETE FROM word_cue WHERE sentence_cue_id IN "
                "(SELECT sc.id FROM sentence_cue sc JOIN chapter_job cj ON sc.chapter_job_id=cj.id WHERE cj.pair_id=?)"));
            del.addBindValue(pair.pairId);
            if (!del.exec()) { d.rollback(); return false; }
            QSqlQuery del2(d);
            del2.prepare(QStringLiteral(
                "DELETE FROM sentence_cue WHERE chapter_job_id IN (SELECT id FROM chapter_job WHERE pair_id=?)"));
            del2.addBindValue(pair.pairId);
            if (!del2.exec()) { d.rollback(); return false; }
            QSqlQuery del3(d);
            del3.prepare(QStringLiteral("DELETE FROM chapter_job WHERE pair_id=?"));
            del3.addBindValue(pair.pairId);
            if (!del3.exec()) { d.rollback(); return false; }
        }
        QSqlQuery up(d);
        up.prepare(QStringLiteral(
            "UPDATE pair_alignment SET epub_fingerprint=?, audio_fingerprint=?, language=?, engine_version=?,"
            " coarse_model_id=?, alignment_model_id=?, updated_at=? WHERE pair_id=?"));
        up.addBindValue(pair.epubFingerprint);
        up.addBindValue(pair.audioFingerprint);
        up.addBindValue(pair.language);
        up.addBindValue(pair.engineVersion);
        up.addBindValue(pair.coarseModelId);
        up.addBindValue(pair.alignmentModelId);
        up.addBindValue(now);
        up.addBindValue(pair.pairId);
        if (!up.exec()) { d.rollback(); return false; }
    } else {
        QSqlQuery ins(d);
        ins.prepare(QStringLiteral(
            "INSERT INTO pair_alignment(pair_id,epub_fingerprint,audio_fingerprint,language,engine_version,"
            "coarse_model_id,alignment_model_id,overall_state,created_at,updated_at)"
            " VALUES(?,?,?,?,?,?,?,?,?,?)"));
        ins.addBindValue(pair.pairId);
        ins.addBindValue(pair.epubFingerprint);
        ins.addBindValue(pair.audioFingerprint);
        ins.addBindValue(pair.language);
        ins.addBindValue(pair.engineVersion);
        ins.addBindValue(pair.coarseModelId);
        ins.addBindValue(pair.alignmentModelId);
        ins.addBindValue(QStringLiteral("waiting"));
        ins.addBindValue(now);
        ins.addBindValue(now);
        if (!ins.exec()) { d.rollback(); return false; }
    }

    if (!d.commit()) { d.rollback(); return false; }
    recomputeOverall(pair.pairId);
    return true;
}

bool AlignmentStore::ensureChapter(const QString &pairId, int chapterIndex,
                                   qint64 audioStartMs, qint64 audioEndMs, int priority) {
    if (!m_open || !hasPair(pairId)) return false;
    QSqlDatabase d = db(m_conn);
    QSqlQuery q(d);
    // Insert-or-refresh: a new job starts Waiting; an existing one keeps its stage
    // (and cues) while its bounds/priority track the current audiobook file model.
    q.prepare(QStringLiteral(
        "INSERT INTO chapter_job(pair_id,audio_chapter_index,audio_start_ms,audio_end_ms,stage,priority)"
        " VALUES(?,?,?,?,'waiting',?)"
        " ON CONFLICT(pair_id,audio_chapter_index) DO UPDATE SET"
        " audio_start_ms=excluded.audio_start_ms, audio_end_ms=excluded.audio_end_ms, priority=excluded.priority"));
    q.addBindValue(pairId);
    q.addBindValue(chapterIndex);
    q.addBindValue(audioStartMs);
    q.addBindValue(audioEndMs);
    q.addBindValue(priority);
    if (!q.exec()) return false;
    recomputeOverall(pairId);
    return true;
}

std::optional<qint64> AlignmentStore::chapterJobId(const QString &pairId, int chapterIndex) const {
    QSqlQuery q(db(m_conn));
    q.prepare(QStringLiteral("SELECT id FROM chapter_job WHERE pair_id=? AND audio_chapter_index=?"));
    q.addBindValue(pairId);
    q.addBindValue(chapterIndex);
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return std::nullopt;
}

bool AlignmentStore::saveCheckpoint(const QString &pairId, int chapterIndex,
                                    Stage stage, const QByteArray &checkpoint) {
    if (!m_open) return false;
    if (stage == Stage::Ready) return false; // Ready is only ever set atomically by publish
    const auto jobId = chapterJobId(pairId, chapterIndex);
    if (!jobId) return false;

    QSqlDatabase d = db(m_conn);
    if (!d.transaction()) return false;
    QSqlQuery q(d);
    q.prepare(QStringLiteral("UPDATE chapter_job SET stage=?, checkpoint=? WHERE id=?"));
    q.addBindValue(stageWireCode(stage));
    q.addBindValue(checkpoint);
    q.addBindValue(*jobId);
    if (!q.exec()) { d.rollback(); return false; }
    if (!d.commit()) { d.rollback(); return false; }
    recomputeOverall(pairId);
    return true;
}

bool AlignmentStore::clearChapterCues(qint64 chapterJobId) {
    QSqlDatabase d = db(m_conn);
    QSqlQuery delW(d);
    delW.prepare(QStringLiteral(
        "DELETE FROM word_cue WHERE sentence_cue_id IN (SELECT id FROM sentence_cue WHERE chapter_job_id=?)"));
    delW.addBindValue(chapterJobId);
    if (!delW.exec()) return false;
    QSqlQuery delS(d);
    delS.prepare(QStringLiteral("DELETE FROM sentence_cue WHERE chapter_job_id=?"));
    delS.addBindValue(chapterJobId);
    return delS.exec();
}

bool AlignmentStore::publishReadyChapter(const QString &pairId, int chapterIndex,
                                         const QList<SentenceCue> &sentences,
                                         const QList<WordCue> &words,
                                         const QList<RegionRecord> &regions,
                                         double confidence) {
    if (!m_open) return false;
    const auto jobId = chapterJobId(pairId, chapterIndex);
    if (!jobId) return false;

    // Read the chapter's audio bounds — the denominator basis for coverage.
    qint64 audioStart = 0, audioEnd = 0;
    {
        QSqlQuery q(db(m_conn));
        q.prepare(QStringLiteral("SELECT audio_start_ms,audio_end_ms FROM chapter_job WHERE id=?"));
        q.addBindValue(*jobId);
        if (!q.exec() || !q.next()) return false;
        audioStart = q.value(0).toLongLong();
        audioEnd   = q.value(1).toLongLong();
    }

    // ── Compute the Ready gate ────────────────────────────────────────────────
    // Narrative speech duration excludes audio-only lead/tail material. Coverage is
    // the fraction of that duration spanned by trusted aligned sentence cues. An
    // unresolved (uncertain) run may not exceed 30s.
    qint64 leadTail = 0;
    qint64 maxUncertainRun = 0;
    for (const RegionRecord &r : regions) {
        if (r.kind == RegionKind::AudioOnly) {
            if (r.startMs <= audioStart || r.endMs >= audioEnd)
                leadTail += (r.endMs - r.startMs);
        } else if (r.kind == RegionKind::Uncertain) {
            maxUncertainRun = std::max<qint64>(maxUncertainRun, r.endMs - r.startMs);
        }
    }
    const qint64 narrativeStart = audioStart + std::max<qint64>(0, [&] {
        qint64 lead = 0;
        for (const RegionRecord &r : regions)
            if (r.kind == RegionKind::AudioOnly && r.startMs <= audioStart)
                lead += (r.endMs - r.startMs);
        return lead;
    }());
    const qint64 narrativeEnd = audioEnd - std::max<qint64>(0, [&] {
        qint64 tail = 0;
        for (const RegionRecord &r : regions)
            if (r.kind == RegionKind::AudioOnly && r.endMs >= audioEnd)
                tail += (r.endMs - r.startMs);
        return tail;
    }());
    const qint64 narrativeDuration = (audioEnd - audioStart) - leadTail;

    qint64 coveredMs = 0;
    for (const SentenceCue &s : sentences) {
        if (s.regionKind != RegionKind::Aligned) continue;
        const qint64 a = std::max(s.startMs, narrativeStart);
        const qint64 b = std::min(s.endMs, narrativeEnd);
        if (b > a) coveredMs += (b - a);
    }
    const double coverage = narrativeDuration > 0
        ? static_cast<double>(coveredMs) / static_cast<double>(narrativeDuration) : 0.0;

    const bool gatePass = narrativeDuration > 0
                       && coverage >= gate::kMinCoverage
                       && maxUncertainRun <= gate::kMaxUnresolvedRunMs;

    // ── One transaction: replace cues, then either publish Ready or fail closed ──
    QSqlDatabase d = db(m_conn);
    if (!d.transaction()) return false;
    if (!clearChapterCues(*jobId)) { d.rollback(); return false; }

    if (!gatePass) {
        // Fail closed: honest CouldntSync(edition_mismatch), zero cues persisted.
        QSqlQuery fail(d);
        fail.prepare(QStringLiteral(
            "UPDATE chapter_job SET stage='couldnt_sync', checkpoint=NULL, coverage=?, confidence=?,"
            " failure_code=?, failure_detail=? WHERE id=?"));
        fail.addBindValue(coverage);
        fail.addBindValue(confidence);
        fail.addBindValue(failureWireCode(FailureCode::EditionMismatch));
        fail.addBindValue(QString());
        fail.addBindValue(*jobId);
        if (!fail.exec()) { d.rollback(); return false; }
        if (!d.commit()) { d.rollback(); return false; }
        recomputeOverall(pairId);
        return false;
    }

    // Insert aligned sentences and the explicit gap regions (regions are stored as
    // sentence_cue rows carrying their region_kind and no canonical/audio span).
    QHash<int, qint64> sentenceRowByOrdinal;
    // A null QString binds as SQL NULL; spine_href and sentence_hash are NOT NULL,
    // so region rows (which carry no href/hash) must coalesce to an empty string.
    auto nonNull = [](const QString &s) { return s.isNull() ? QStringLiteral("") : s; };
    auto insertSentenceRow = [&](int ordinal, qint64 startMs, qint64 endMs, const QString &href,
                                 qint64 cs, qint64 ce, const QString &hash, double conf,
                                 RegionKind kind, qint64 *outId) -> bool {
        QSqlQuery ins(d);
        ins.prepare(QStringLiteral(
            "INSERT INTO sentence_cue(chapter_job_id,ordinal,start_ms,end_ms,spine_href,canonical_start,"
            "canonical_end,sentence_hash,confidence,region_kind) VALUES(?,?,?,?,?,?,?,?,?,?)"));
        ins.addBindValue(*jobId);
        ins.addBindValue(ordinal);
        ins.addBindValue(startMs);
        ins.addBindValue(endMs);
        ins.addBindValue(nonNull(href));
        ins.addBindValue(cs);
        ins.addBindValue(ce);
        ins.addBindValue(nonNull(hash));
        ins.addBindValue(conf);
        ins.addBindValue(regionKindWireCode(kind));
        if (!ins.exec()) return false;
        if (outId) *outId = ins.lastInsertId().toLongLong();
        return true;
    };

    for (const SentenceCue &s : sentences) {
        qint64 rowId = 0;
        if (!insertSentenceRow(s.ordinal, s.startMs, s.endMs, s.spineHref, s.canonicalStart,
                               s.canonicalEnd, s.sentenceHash, s.confidence, s.regionKind, &rowId)) {
            d.rollback(); return false;
        }
        if (s.regionKind == RegionKind::Aligned)
            sentenceRowByOrdinal.insert(s.ordinal, rowId);
    }
    int regionOrdinal = 100000; // region rows carry ordinals above real sentences
    for (const RegionRecord &r : regions) {
        if (!insertSentenceRow(regionOrdinal++, r.startMs, r.endMs, r.spineHref, r.canonicalStart,
                               r.canonicalEnd, QString(), 0.0, r.kind, nullptr)) {
            d.rollback(); return false;
        }
    }

    for (const WordCue &w : words) {
        const auto it = sentenceRowByOrdinal.constFind(w.sentenceOrdinal);
        if (it == sentenceRowByOrdinal.constEnd()) { d.rollback(); return false; } // word without a sentence
        QSqlQuery ins(d);
        ins.prepare(QStringLiteral(
            "INSERT INTO word_cue(sentence_cue_id,ordinal,start_ms,end_ms,canonical_start,canonical_end,confidence)"
            " VALUES(?,?,?,?,?,?,?)"));
        ins.addBindValue(*it);
        ins.addBindValue(w.ordinal);
        ins.addBindValue(w.startMs);
        ins.addBindValue(w.endMs);
        ins.addBindValue(w.canonicalStart);
        ins.addBindValue(w.canonicalEnd);
        ins.addBindValue(w.confidence);
        if (!ins.exec()) { d.rollback(); return false; }
    }

    QSqlQuery ready(d);
    ready.prepare(QStringLiteral(
        "UPDATE chapter_job SET stage='ready', checkpoint=NULL, coverage=?, confidence=?,"
        " failure_code=NULL, failure_detail=NULL WHERE id=?"));
    ready.addBindValue(coverage);
    ready.addBindValue(confidence);
    ready.addBindValue(*jobId);
    if (!ready.exec()) { d.rollback(); return false; }

    if (!d.commit()) { d.rollback(); return false; }
    recomputeOverall(pairId);
    return true;
}

bool AlignmentStore::failChapter(const QString &pairId, int chapterIndex,
                                 FailureCode code, const QString &detail) {
    if (!m_open) return false;
    const auto jobId = chapterJobId(pairId, chapterIndex);
    if (!jobId) return false;
    QSqlDatabase d = db(m_conn);
    if (!d.transaction()) return false;
    if (!clearChapterCues(*jobId)) { d.rollback(); return false; }
    QSqlQuery q(d);
    q.prepare(QStringLiteral(
        "UPDATE chapter_job SET stage='couldnt_sync', checkpoint=NULL, failure_code=?, failure_detail=? WHERE id=?"));
    q.addBindValue(failureWireCode(code));
    q.addBindValue(detail);
    q.addBindValue(*jobId);
    if (!q.exec()) { d.rollback(); return false; }
    if (!d.commit()) { d.rollback(); return false; }
    recomputeOverall(pairId);
    return true;
}

ActiveCue AlignmentStore::cueAtTime(const QString &pairId, qint64 timeMs) const {
    ActiveCue out;
    if (!m_open) return out;
    QSqlQuery q(db(m_conn));
    // Trusted (aligned) sentence whose half-open interval [start,end) contains the time.
    q.prepare(QStringLiteral(
        "SELECT sc.id,sc.ordinal,sc.start_ms,sc.end_ms,sc.spine_href,sc.canonical_start,sc.canonical_end,"
        "sc.sentence_hash,sc.confidence FROM sentence_cue sc JOIN chapter_job cj ON sc.chapter_job_id=cj.id"
        " WHERE cj.pair_id=? AND sc.region_kind='aligned' AND sc.start_ms<=? AND sc.end_ms>?"
        " ORDER BY sc.start_ms DESC LIMIT 1"));
    q.addBindValue(pairId);
    q.addBindValue(timeMs);
    q.addBindValue(timeMs);
    if (!q.exec() || !q.next())
        return out;

    const qint64 sentenceRow = q.value(0).toLongLong();
    out.hasSentence = true;
    out.sentence.ordinal        = q.value(1).toInt();
    out.sentence.startMs        = q.value(2).toLongLong();
    out.sentence.endMs          = q.value(3).toLongLong();
    out.sentence.spineHref      = q.value(4).toString();
    out.sentence.canonicalStart = q.value(5).toLongLong();
    out.sentence.canonicalEnd   = q.value(6).toLongLong();
    out.sentence.sentenceHash   = q.value(7).toString();
    out.sentence.confidence     = q.value(8).toDouble();
    out.sentence.regionKind     = RegionKind::Aligned;

    QSqlQuery w(db(m_conn));
    w.prepare(QStringLiteral(
        "SELECT ordinal,start_ms,end_ms,canonical_start,canonical_end,confidence FROM word_cue"
        " WHERE sentence_cue_id=? AND start_ms<=? AND end_ms>? ORDER BY start_ms DESC LIMIT 1"));
    w.addBindValue(sentenceRow);
    w.addBindValue(timeMs);
    w.addBindValue(timeMs);
    if (w.exec() && w.next()) {
        out.hasWord = true;
        out.word.sentenceOrdinal = out.sentence.ordinal;
        out.word.ordinal        = w.value(0).toInt();
        out.word.startMs        = w.value(1).toLongLong();
        out.word.endMs          = w.value(2).toLongLong();
        out.word.canonicalStart = w.value(3).toLongLong();
        out.word.canonicalEnd   = w.value(4).toLongLong();
        out.word.confidence     = w.value(5).toDouble();
    }
    return out;
}

std::optional<qint64> AlignmentStore::timeAtLocation(const QString &pairId,
                                                     const CanonicalLocation &loc) const {
    if (!m_open) return std::nullopt;
    QSqlQuery q(db(m_conn));
    q.prepare(QStringLiteral(
        "SELECT sc.start_ms FROM sentence_cue sc JOIN chapter_job cj ON sc.chapter_job_id=cj.id"
        " WHERE cj.pair_id=? AND sc.region_kind='aligned' AND sc.spine_href=?"
        " AND sc.canonical_start<=? AND sc.canonical_end>?"
        " ORDER BY sc.canonical_start DESC LIMIT 1"));
    q.addBindValue(pairId);
    q.addBindValue(loc.spineHref);
    q.addBindValue(loc.canonicalStart);
    q.addBindValue(loc.canonicalStart);
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return std::nullopt;
}

ChapterStatus AlignmentStore::chapterStatus(const QString &pairId, int chapterIndex) const {
    ChapterStatus st;
    st.chapterIndex = chapterIndex;
    if (!m_open) return st;
    QSqlQuery q(db(m_conn));
    q.prepare(QStringLiteral(
        "SELECT stage,audio_start_ms,audio_end_ms,coverage,confidence,failure_code,failure_detail,priority,checkpoint"
        " FROM chapter_job WHERE pair_id=? AND audio_chapter_index=?"));
    q.addBindValue(pairId);
    q.addBindValue(chapterIndex);
    if (q.exec() && q.next()) {
        st.exists        = true;
        st.stage         = stageFromWire(q.value(0).toString());
        st.audioStartMs  = q.value(1).toLongLong();
        st.audioEndMs    = q.value(2).toLongLong();
        st.coverage      = q.value(3).toDouble();
        st.confidence    = q.value(4).toDouble();
        st.failureCode   = failureFromWire(q.value(5).toString());
        st.failureDetail = q.value(6).toString();
        st.priority      = q.value(7).toInt();
        st.checkpoint    = q.value(8).toByteArray();
    }
    return st;
}

QList<ChapterStatus> AlignmentStore::chapters(const QString &pairId) const {
    QList<ChapterStatus> out;
    if (!m_open) return out;
    QSqlQuery q(db(m_conn));
    q.prepare(QStringLiteral(
        "SELECT audio_chapter_index,stage,audio_start_ms,audio_end_ms,coverage,confidence,"
        "failure_code,failure_detail,priority,checkpoint FROM chapter_job WHERE pair_id=?"
        " ORDER BY audio_chapter_index ASC"));
    q.addBindValue(pairId);
    if (q.exec()) {
        while (q.next()) {
            ChapterStatus st;
            st.exists        = true;
            st.chapterIndex  = q.value(0).toInt();
            st.stage         = stageFromWire(q.value(1).toString());
            st.audioStartMs  = q.value(2).toLongLong();
            st.audioEndMs    = q.value(3).toLongLong();
            st.coverage      = q.value(4).toDouble();
            st.confidence    = q.value(5).toDouble();
            st.failureCode   = failureFromWire(q.value(6).toString());
            st.failureDetail = q.value(7).toString();
            st.priority      = q.value(8).toInt();
            st.checkpoint    = q.value(9).toByteArray();
            out.append(st);
        }
    }
    return out;
}

bool AlignmentStore::retryChapter(const QString &pairId, int chapterIndex) {
    if (!m_open) return false;
    const auto jobId = chapterJobId(pairId, chapterIndex);
    if (!jobId) return false;
    QSqlDatabase d = db(m_conn);
    if (!d.transaction()) return false;
    if (!clearChapterCues(*jobId)) { d.rollback(); return false; }
    QSqlQuery q(d);
    q.prepare(QStringLiteral(
        "UPDATE chapter_job SET stage='waiting', checkpoint=NULL, coverage=0, confidence=0,"
        " failure_code=NULL, failure_detail=NULL WHERE id=?"));
    q.addBindValue(*jobId);
    if (!q.exec()) { d.rollback(); return false; }
    if (!d.commit()) { d.rollback(); return false; }
    recomputeOverall(pairId);
    return true;
}

bool AlignmentStore::restartPair(const QString &pairId) {
    if (!m_open || !hasPair(pairId)) return false;
    QSqlDatabase d = db(m_conn);
    if (!d.transaction()) return false;
    QSqlQuery delW(d);
    delW.prepare(QStringLiteral(
        "DELETE FROM word_cue WHERE sentence_cue_id IN "
        "(SELECT sc.id FROM sentence_cue sc JOIN chapter_job cj ON sc.chapter_job_id=cj.id WHERE cj.pair_id=?)"));
    delW.addBindValue(pairId);
    if (!delW.exec()) { d.rollback(); return false; }
    QSqlQuery delS(d);
    delS.prepare(QStringLiteral(
        "DELETE FROM sentence_cue WHERE chapter_job_id IN (SELECT id FROM chapter_job WHERE pair_id=?)"));
    delS.addBindValue(pairId);
    if (!delS.exec()) { d.rollback(); return false; }
    QSqlQuery reset(d);
    reset.prepare(QStringLiteral(
        "UPDATE chapter_job SET stage='waiting', checkpoint=NULL, coverage=0, confidence=0,"
        " failure_code=NULL, failure_detail=NULL WHERE pair_id=?"));
    reset.addBindValue(pairId);
    if (!reset.exec()) { d.rollback(); return false; }
    if (!d.commit()) { d.rollback(); return false; }
    recomputeOverall(pairId);
    return true;
}

void AlignmentStore::recomputeOverall(const QString &pairId) {
    if (!m_open) return;
    int total = 0, ready = 0, waiting = 0;
    {
        QSqlQuery q(db(m_conn));
        q.prepare(QStringLiteral("SELECT stage FROM chapter_job WHERE pair_id=?"));
        q.addBindValue(pairId);
        if (q.exec()) {
            while (q.next()) {
                ++total;
                const Stage s = stageFromWire(q.value(0).toString());
                if (s == Stage::Ready) ++ready;
                else if (s == Stage::Waiting) ++waiting;
            }
        }
    }
    QString state;
    if (total == 0 || waiting == total) state = QStringLiteral("waiting");
    else if (ready == total)            state = QStringLiteral("ready");
    else if (ready > 0)                 state = QStringLiteral("partial");
    else                                state = QStringLiteral("syncing");

    QSqlQuery up(db(m_conn));
    up.prepare(QStringLiteral("UPDATE pair_alignment SET overall_state=?, updated_at=? WHERE pair_id=?"));
    up.addBindValue(state);
    up.addBindValue(QDateTime::currentMSecsSinceEpoch());
    up.addBindValue(pairId);
    up.exec();
}

} // namespace alignment
