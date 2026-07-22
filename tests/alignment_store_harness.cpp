// AlignmentStore harness — durable audiobook↔EPUB alignment store.
// Fixture SQLite db in a QTemporaryDir; exit code = verdict (0 PASS / 1 FAIL).
// Run from native/build-msvc so the deployed sqldrivers/qsqlite.dll is found.
//
// Exercises: schema creation, pair upsert, stage checkpoints, atomic Ready
// publication, time->word and text->time lookup, gate rollback (nothing partial
// persists), fingerprint invalidation (asset replace wipes; engine upgrade
// preserves), chapter retry, and pair restart.

#include "alignment/AlignmentStore.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

using namespace alignment;

static bool g_failed = false;
static void check(bool ok, const char *msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); g_failed = true; }
}
#define CHECK(cond, msg) check((cond), (msg))

// The chapter-0 "exact match" fixture: three contiguous aligned sentences over a
// 10s chapter with a 1.5s trailing audio-only tail, and four word cues inside the
// second sentence. Coverage = 8.5s / 8.5s narrative = 100%.
static QList<SentenceCue> ch0Sentences() {
    QList<SentenceCue> s;
    s.append({0, 0,    1760, QStringLiteral("Text/ch1.xhtml"),   0,  40, QStringLiteral("s0"), 0.90, RegionKind::Aligned});
    s.append({1, 1760, 4000, QStringLiteral("Text/ch1.xhtml"),  40, 120, QStringLiteral("s1"), 0.92, RegionKind::Aligned});
    s.append({2, 4000, 8500, QStringLiteral("Text/ch1.xhtml"), 120, 300, QStringLiteral("s2"), 0.90, RegionKind::Aligned});
    return s;
}
static QList<WordCue> ch0Words() {
    QList<WordCue> w;
    w.append({1, 0, 1760, 1780, 40, 50, 0.90});
    w.append({1, 1, 1780, 1800, 50, 60, 0.90});
    w.append({1, 2, 1800, 1815, 60, 72, 0.90});
    w.append({1, 3, 1815, 1900, 72, 90, 0.90}); // contains t=1820 -> ordinal 3
    return w;
}
static QList<RegionRecord> ch0Regions() {
    QList<RegionRecord> r;
    r.append({RegionKind::AudioOnly, 8500, 10000, QString(), -1, -1}); // trailing credits
    return r;
}

static PairIdentity makePair(const QString &engine = QStringLiteral("engine-1"),
                             const QString &epubFp = QStringLiteral("epub-fp-A")) {
    PairIdentity p;
    p.pairId = QStringLiteral("pair-1");
    p.epubFingerprint = epubFp;
    p.audioFingerprint = QStringLiteral("audio-fp-A");
    p.language = QStringLiteral("en");
    p.engineVersion = engine;
    p.coarseModelId = QStringLiteral("base.en@1");
    p.alignmentModelId = QStringLiteral("w2v2@1");
    return p;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) { std::fprintf(stderr, "FAIL: temp dir\n"); return 1; }
    const QString dbPath = dir.filePath(QStringLiteral("alignment.db"));
    const PairIdentity pair = makePair();

    // ── 1. schema + upsert + checkpoint ───────────────────────────────────────
    {
        AlignmentStore store(dbPath);
        CHECK(store.isOpen(), "store opens and creates schema");
        CHECK(store.upsertPair(pair), "pair upsert");
        CHECK(store.hasPair(pair.pairId), "pair present after upsert");
        CHECK(store.ensureChapter(pair.pairId, 0, 0, 10000, 0), "ensure chapter 0");
        CHECK(store.saveCheckpoint(pair.pairId, 0, Stage::Matching, "anchors:4"), "checkpoint");
        const ChapterStatus mid = store.chapterStatus(pair.pairId, 0);
        CHECK(mid.exists && mid.stage == Stage::Matching, "checkpoint stage persists");
        CHECK(mid.checkpoint == QByteArray("anchors:4"), "checkpoint blob persists");
        CHECK(!store.cueAtTime(pair.pairId, 1820).hasSentence, "no cues before publish");
        // saveCheckpoint refuses to fabricate Ready
        CHECK(!store.saveCheckpoint(pair.pairId, 0, Stage::Ready, "x"), "checkpoint cannot set Ready");

        // ── 2. atomic Ready publication + lookups ─────────────────────────────
        CHECK(store.publishReadyChapter(pair.pairId, 0, ch0Sentences(), ch0Words(), ch0Regions(), 0.91), "publish Ready");
        const ActiveCue cue = store.cueAtTime(pair.pairId, 1820);
        CHECK(cue.hasSentence && cue.sentence.ordinal == 1, "time -> sentence ordinal 1");
        CHECK(cue.hasWord && cue.word.ordinal == 3, "time -> word ordinal 3");
        const auto t = store.timeAtLocation(pair.pairId, {QStringLiteral("Text/ch1.xhtml"), 47});
        CHECK(t.has_value() && t.value() == 1760, "text -> time 1760");
        CHECK(!store.timeAtLocation(pair.pairId, {QStringLiteral("Text/ch1.xhtml"), 5000}).has_value(),
              "text off the end -> no time");
        const ChapterStatus ready = store.chapterStatus(pair.pairId, 0);
        CHECK(ready.stage == Stage::Ready, "chapter 0 Ready is terminal");
        CHECK(ready.coverage >= 0.80, "coverage recorded >= 0.80");
        CHECK(store.overallState(pair.pairId) == QLatin1String("ready"), "overall state ready");
    }

    // ── 3. durability across a full reopen ────────────────────────────────────
    {
        AlignmentStore store(dbPath);
        CHECK(store.isOpen(), "reopen existing db");
        const ActiveCue cue = store.cueAtTime(pair.pairId, 1820);
        CHECK(cue.hasWord && cue.word.ordinal == 3, "Ready cues survive reopen");
        CHECK(store.chapterStatus(pair.pairId, 0).stage == Stage::Ready, "Ready stage survives reopen");
    }

    // ── 4-9: gate, invalidation, retry, restart on a live store ───────────────
    {
        AlignmentStore store(dbPath);

        // 4. coverage gate rollback: chapter 1 spans 60s but aligned covers only 10s.
        CHECK(store.ensureChapter(pair.pairId, 1, 10000, 70000, 0), "ensure chapter 1");
        QList<SentenceCue> thin;
        thin.append({0, 10000, 20000, QStringLiteral("Text/ch2.xhtml"), 0, 40, QStringLiteral("t0"), 0.9, RegionKind::Aligned});
        const bool published1 = store.publishReadyChapter(pair.pairId, 1, thin, {}, {}, 0.5);
        CHECK(!published1, "coverage below 80% is rejected");
        const ChapterStatus fail1 = store.chapterStatus(pair.pairId, 1);
        CHECK(fail1.stage == Stage::CouldntSync, "rejected chapter is CouldntSync");
        CHECK(fail1.failureCode == FailureCode::EditionMismatch, "rejected chapter maps edition_mismatch");
        CHECK(!store.cueAtTime(pair.pairId, 15000).hasSentence, "rejected publish persisted NO cues");

        // 5. gap gate rollback: chapter 2 coverage ~81% but a 33s uncertain run.
        CHECK(store.ensureChapter(pair.pairId, 2, 70000, 280000, 0), "ensure chapter 2");
        QList<SentenceCue> wide;
        wide.append({0, 70000, 240000, QStringLiteral("Text/ch3.xhtml"), 0, 400, QStringLiteral("u0"), 0.9, RegionKind::Aligned});
        QList<RegionRecord> gap;
        gap.append({RegionKind::Uncertain, 245000, 278000, QString(), -1, -1}); // 33s > 30s
        const bool published2 = store.publishReadyChapter(pair.pairId, 2, wide, {}, gap, 0.7);
        CHECK(!published2, "unresolved run > 30s is rejected despite adequate coverage");
        CHECK(store.chapterStatus(pair.pairId, 2).stage == Stage::CouldntSync, "gap-rejected chapter CouldntSync");

        // 6. retry clears the failed chapter back to Waiting, keeps the pair.
        CHECK(store.retryChapter(pair.pairId, 1), "retry chapter 1");
        const ChapterStatus retried = store.chapterStatus(pair.pairId, 1);
        CHECK(retried.exists && retried.stage == Stage::Waiting, "retried chapter back to Waiting");
        CHECK(retried.failureCode == FailureCode::None, "retry clears the failure code");
        CHECK(store.chapterStatus(pair.pairId, 0).stage == Stage::Ready, "retry left chapter 0 Ready");

        // 7. engine upgrade preserves a usable result (no silent delete).
        CHECK(store.upsertPair(makePair(QStringLiteral("engine-2"))), "upsert engine upgrade");
        CHECK(store.cueAtTime(pair.pairId, 1820).hasWord, "engine upgrade preserved chapter 0 cues");
        CHECK(store.pairIdentity(pair.pairId).engineVersion == QLatin1String("engine-2"), "engine version updated");

        // 8. restart clears all generated alignment, keeps the chapter list.
        CHECK(store.restartPair(pair.pairId), "restart pair");
        CHECK(!store.cueAtTime(pair.pairId, 1820).hasSentence, "restart wiped chapter 0 cues");
        const ChapterStatus afterRestart = store.chapterStatus(pair.pairId, 0);
        CHECK(afterRestart.exists && afterRestart.stage == Stage::Waiting, "restart kept chapter row at Waiting");

        // 9. asset replacement (epub fingerprint change) invalidates the pair's jobs.
        CHECK(store.publishReadyChapter(pair.pairId, 0, ch0Sentences(), ch0Words(), ch0Regions(), 0.91), "re-publish chapter 0");
        CHECK(store.cueAtTime(pair.pairId, 1820).hasWord, "chapter 0 Ready again before asset swap");
        CHECK(store.upsertPair(makePair(QStringLiteral("engine-2"), QStringLiteral("epub-fp-B"))), "upsert new epub fingerprint");
        CHECK(!store.chapterStatus(pair.pairId, 0).exists, "asset replacement invalidated chapter jobs");
        CHECK(!store.cueAtTime(pair.pairId, 1820).hasSentence, "asset replacement wiped cues");
    }

    if (g_failed) return 1;
    std::fprintf(stdout, "PASS alignment store schema, atomic publication, lookup, invalidation, retry, restart\n");
    return 0;
}
