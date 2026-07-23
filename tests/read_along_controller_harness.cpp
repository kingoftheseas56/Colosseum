// ReadAlongController harness — the single bidirectional read-along state machine.
//
// A :memory: AlignmentStore is fine here: the controller is single-threaded,
// GUI-side, one connection. We hand-seed one exact-match chapter (with a
// low-confidence word gap, a low-confidence sentence, and a trailing audio-only
// region) plus a second chapter, then drive the controller and assert on its
// signals. Exit code is the verdict (0 PASS / 1 FAIL).
//
// Exercises: boundary lookups (time on a cue edge), the confidence gaps
// (low-confidence word cleared but sentence kept = trusted-sentence carry;
// low-confidence sentence clears both; audio-only gap clears both), dedup (no
// repaint churn), preview (no seek, no paint), the ONE committed-seek path for
// commitTime and commitLocation (text->audio via timeAtLocation), detach stops
// follow-repaint / return resumes and repaints, pause freeze keeps last paint,
// and a chapter change repaints from the new chapter.

#include "alignment/ReadAlongController.h"
#include "alignment/AlignmentStore.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QVariantMap>
#include <cstdio>

using namespace alignment;

static bool g_failed = false;
static void check(bool ok, const char *msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); g_failed = true; }
}
#define CHECK(cond, msg) check((cond), (msg))

static PairIdentity makePair() {
    PairIdentity p;
    p.pairId = QStringLiteral("pair-1");
    p.epubFingerprint  = QStringLiteral("epub-fp-A");
    p.audioFingerprint = QStringLiteral("audio-fp-A");
    p.language = QStringLiteral("en");
    p.engineVersion = QStringLiteral("engine-1");
    p.coarseModelId = QStringLiteral("base.en@1");
    p.alignmentModelId = QStringLiteral("w2v2@1");
    return p;
}

// Chapter 0 (audio 0..12000): four contiguous aligned sentences over 0..10000
// plus a 2s trailing audio-only tail. s2 is deliberately LOW confidence (0.20)
// to exercise the untrusted-sentence clear; s1 carries a LOW-confidence word to
// exercise the trusted-sentence carry. Coverage = 10000/10000 narrative = 100%.
static QList<SentenceCue> ch0Sentences() {
    QList<SentenceCue> s;
    s.append({0, 0,    2000,  QStringLiteral("Text/ch1.xhtml"),   0,  40, QStringLiteral("s0"), 0.90, RegionKind::Aligned});
    s.append({1, 2000, 4000,  QStringLiteral("Text/ch1.xhtml"),  40, 120, QStringLiteral("s1"), 0.90, RegionKind::Aligned});
    s.append({2, 4000, 6000,  QStringLiteral("Text/ch1.xhtml"), 120, 200, QStringLiteral("s2"), 0.20, RegionKind::Aligned});
    s.append({3, 6000, 10000, QStringLiteral("Text/ch1.xhtml"), 200, 400, QStringLiteral("s3"), 0.90, RegionKind::Aligned});
    return s;
}
static QList<WordCue> ch0Words() {
    QList<WordCue> w;
    w.append({0, 0, 0,    1000, 0,  20, 0.90});   // s0 word0 trusted (t=500 -> here)
    w.append({0, 1, 1000, 2000, 20, 40, 0.90});   // s0 word1 trusted
    w.append({1, 0, 2000, 4000, 40, 120, 0.20});  // s1 lone word LOW conf -> emphasis dropped
    return w;
}
static QList<RegionRecord> ch0Regions() {
    QList<RegionRecord> r;
    r.append({RegionKind::AudioOnly, 10000, 12000, QString(), -1, -1}); // trailing credits gap
    return r;
}

// Chapter 1 (audio 12000..22000): one trusted sentence over 12000..20000 in a
// different spine file, plus a 2s audio-only tail. Coverage 8000/8000 = 100%.
static QList<SentenceCue> ch1Sentences() {
    QList<SentenceCue> s;
    s.append({0, 12000, 20000, QStringLiteral("Text/ch2.xhtml"), 0, 300, QStringLiteral("c2s0"), 0.90, RegionKind::Aligned});
    return s;
}
static QList<RegionRecord> ch1Regions() {
    QList<RegionRecord> r;
    r.append({RegionKind::AudioOnly, 20000, 22000, QString(), -1, -1});
    return r;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) { std::fprintf(stderr, "FAIL: temp dir\n"); return 1; }
    const QString dbPath = dir.filePath(QStringLiteral("readalong.db"));
    const PairIdentity pair = makePair();

    AlignmentStore store(dbPath);
    CHECK(store.isOpen(), "store opens");
    CHECK(store.upsertPair(pair), "pair upsert");
    CHECK(store.ensureChapter(pair.pairId, 0, 0, 12000, 0), "ensure chapter 0");
    CHECK(store.ensureChapter(pair.pairId, 1, 12000, 22000, 0), "ensure chapter 1");
    CHECK(store.publishReadyChapter(pair.pairId, 0, ch0Sentences(), ch0Words(), ch0Regions(), 0.91),
          "publish chapter 0 Ready");
    CHECK(store.publishReadyChapter(pair.pairId, 1, ch1Sentences(), {}, ch1Regions(), 0.90),
          "publish chapter 1 Ready");

    ReadAlongController c(&store);

    // Signal recorders.
    int paintCount = 0; QVariantMap lastPaint;
    int navCount = 0;   QVariantMap lastNav;
    int seekCount = 0;  int seekChapter = -1; qint64 seekTime = -1; bool seekPlay = false;
    int followChanges = 0;
    QObject::connect(&c, &ReadAlongController::paintRequested,
                     [&](const QVariantMap &m) { ++paintCount; lastPaint = m; });
    QObject::connect(&c, &ReadAlongController::navigationRequested,
                     [&](const QVariantMap &m) { ++navCount; lastNav = m; });
    QObject::connect(&c, &ReadAlongController::audioSeekRequested,
                     [&](int ch, qint64 t, bool play) { ++seekCount; seekChapter = ch; seekTime = t; seekPlay = play; });
    QObject::connect(&c, &ReadAlongController::followStateChanged,
                     [&]() { ++followChanges; });

    // ── 0. Default state ──────────────────────────────────────────────────────
    CHECK(c.followState() == QLatin1String("following"), "opens following");
    CHECK(c.activeSentence().isEmpty(), "opens with empty activeSentence");
    CHECK(c.activeWord().isEmpty(), "opens with empty activeWord");

    // ── 1. Trusted sentence + trusted word paints the full cue ────────────────
    c.setPlayhead(pair.pairId, 0, 500);
    CHECK(paintCount == 1, "first playhead emits one paint");
    CHECK(lastPaint.value("spineHref").toString() == QLatin1String("Text/ch1.xhtml"), "paint spineHref");
    CHECK(lastPaint.value("sentence").toMap().value("start").toInt() == 0
          && lastPaint.value("sentence").toMap().value("end").toInt() == 40, "paint sentence s0 offsets");
    CHECK(lastPaint.contains("word")
          && lastPaint.value("word").toMap().value("start").toInt() == 0
          && lastPaint.value("word").toMap().value("end").toInt() == 20, "paint word0 offsets");
    CHECK(c.activeSentence().value("start").toInt() == 0, "activeSentence property set");
    CHECK(c.activeWord().value("end").toInt() == 20, "activeWord property set");
    CHECK(c.chapterReady(), "chapter 0 is Ready");
    CHECK(navCount == 1, "following: entering a new sentence requests ensure-visible nav");
    CHECK(lastNav.value("canonicalStart").toInt() == 0, "nav location is the sentence");

    // ── 2. Dedup: same sentence+word identity emits nothing ───────────────────
    const int paintAt2 = paintCount;
    c.setPlayhead(pair.pairId, 0, 600); // still s0, still word0 [0,1000)
    CHECK(paintCount == paintAt2, "dedup: same cue identity emits no repaint");

    // ── 3. Trusted-sentence carry: low-confidence word -> sentence kept, word dropped
    c.setPlayhead(pair.pairId, 0, 3000); // s1 trusted, its lone word conf 0.20
    CHECK(paintCount == paintAt2 + 1, "moving to s1 repaints");
    CHECK(lastPaint.value("sentence").toMap().value("start").toInt() == 40, "s1 sentence painted");
    CHECK(!lastPaint.contains("word"), "low-confidence word omitted (trusted-sentence carry)");
    CHECK(c.activeSentence().value("start").toInt() == 40, "activeSentence is s1");
    CHECK(c.activeWord().isEmpty(), "activeWord cleared for the low-confidence word");

    // ── 4. Untrusted sentence clears BOTH (empty cue) ─────────────────────────
    c.setPlayhead(pair.pairId, 0, 5000); // s2 confidence 0.20
    CHECK(lastPaint.isEmpty(), "low-confidence sentence paints an empty cue");
    CHECK(c.activeSentence().isEmpty(), "activeSentence cleared for untrusted sentence");
    CHECK(c.activeWord().isEmpty(), "activeWord cleared for untrusted sentence");

    // ── 5. Audio-only gap clears BOTH ─────────────────────────────────────────
    c.setPlayhead(pair.pairId, 0, 6500); // back into trusted s3 first
    CHECK(!c.activeSentence().isEmpty() && c.activeSentence().value("start").toInt() == 200, "s3 trusted paints");
    c.setPlayhead(pair.pairId, 0, 11000); // trailing audio-only region, no aligned cue
    CHECK(lastPaint.isEmpty(), "audio-only gap paints empty cue");
    CHECK(c.activeSentence().isEmpty(), "activeSentence cleared in audio-only gap");

    // ── 6. Boundary lookups (half-open [start,end): start included, end excluded)
    c.setPlayhead(pair.pairId, 0, 1999); // last ms of s0
    CHECK(c.activeSentence().value("start").toInt() == 0, "t=1999 resolves s0 (end exclusive)");
    c.setPlayhead(pair.pairId, 0, 2000); // exact start of s1
    CHECK(c.activeSentence().value("start").toInt() == 40, "t=2000 boundary resolves s1 (start inclusive)");
    c.setPlayhead(pair.pairId, 0, 6000); // exact start of s3
    CHECK(c.activeSentence().value("start").toInt() == 200, "t=6000 boundary resolves s3");

    // ── 7. Preview emits NO seek and NO paint, updates the preview property ────
    {
        const int p0 = paintCount, s0 = seekCount, n0 = navCount;
        c.previewTime(pair.pairId, 0, 500);
        CHECK(paintCount == p0, "preview emits no paint");
        CHECK(seekCount == s0, "preview emits no seek");
        CHECK(navCount == n0, "preview emits no nav");
        CHECK(c.preview().value("timeMs").toLongLong() == 500, "preview timeMs recorded");
        CHECK(c.preview().value("chapter").toInt() == 0, "preview chapter recorded");
        CHECK(c.preview().value("synced").toBool(), "preview reports synced where aligned");
        CHECK(c.preview().value("spineHref").toString() == QLatin1String("Text/ch1.xhtml"), "preview locator");
        c.previewTime(pair.pairId, 0, 11000); // audio-only gap
        CHECK(!c.preview().value("synced").toBool(), "preview reports not-synced in a gap");
        CHECK(paintCount == p0 && seekCount == s0, "preview still no paint/seek in the gap");
    }

    // ── 8. commitTime: EXACTLY ONE committed seek ─────────────────────────────
    seekCount = 0;
    c.commitTime(pair.pairId, 0, 3000);
    CHECK(seekCount == 1, "commitTime emits exactly one audioSeekRequested");
    CHECK(seekChapter == 0 && seekTime == 3000 && seekPlay, "commitTime seek carries (chapter,time,play=true)");

    // ── 9. commitLocation: text->audio via timeAtLocation, ONE seek + ONE nav ──
    seekCount = 0; navCount = 0;
    QVariantMap loc;
    loc["spineHref"] = QStringLiteral("Text/ch1.xhtml");
    loc["canonicalStart"] = 47; // inside s1 [40,120) -> start_ms 2000
    loc["canonicalEnd"] = 47;
    c.commitLocation(pair.pairId, loc);
    CHECK(seekCount == 1, "commitLocation emits exactly one audioSeekRequested");
    CHECK(seekTime == 2000, "commitLocation resolved location -> audio time 2000");
    CHECK(seekChapter == 0, "commitLocation resolved the containing chapter");
    CHECK(navCount == 1, "commitLocation emits exactly one navigationRequested");
    CHECK(lastNav.value("spineHref").toString() == QLatin1String("Text/ch1.xhtml"), "commitLocation nav carries the location");

    // A location with no aligned time seeks nothing (honest: book-only text has no
    // audio time) but still navigates the book.
    seekCount = 0; navCount = 0;
    QVariantMap unaligned;
    unaligned["spineHref"] = QStringLiteral("Text/ch1.xhtml");
    unaligned["canonicalStart"] = 5000; // off the end of every cue
    unaligned["canonicalEnd"] = 5000;
    c.commitLocation(pair.pairId, unaligned);
    CHECK(seekCount == 0, "unaligned location commits no seek");
    CHECK(navCount == 1, "unaligned location still navigates the book");

    // ── 10. Detach stops follow-repaint; return resumes and repaints ──────────
    c.setPlayhead(pair.pairId, 0, 500); // re-anchor on s0, following
    const int followBefore = followChanges;
    c.detachFollow();
    CHECK(c.followState() == QLatin1String("detached"), "detachFollow -> detached");
    CHECK(followChanges == followBefore + 1, "detach emits followStateChanged");
    const QVariantMap sentenceWhileDetached = c.activeSentence();
    const int paintBeforeDetachedMove = paintCount;
    c.setPlayhead(pair.pairId, 0, 6500); // audio advances into s3 while detached
    CHECK(paintCount == paintBeforeDetachedMove, "detached: playhead advance emits no paint");
    CHECK(c.activeSentence() == sentenceWhileDetached, "detached: activeSentence frozen at last followed paint");
    c.returnToNarration();
    CHECK(c.followState() == QLatin1String("following"), "returnToNarration -> following");
    CHECK(paintCount == paintBeforeDetachedMove + 1, "return repaints to the current playhead");
    CHECK(c.activeSentence().value("start").toInt() == 200, "return repaints s3 (the detached playhead)");

    // ── 11. Pause freeze: a repeated same-time playhead keeps the last paint ───
    {
        const int p0 = paintCount;
        const QVariantMap frozen = c.activeSentence();
        c.setPlayhead(pair.pairId, 0, 6500); // identical to current playhead (paused tick)
        CHECK(paintCount == p0, "pause freeze: repeated same-time playhead emits no paint");
        CHECK(c.activeSentence() == frozen, "pause freeze: last trusted paint intact");
    }

    // ── 12. Chapter change repaints from the new chapter's cues ───────────────
    c.setPlayhead(pair.pairId, 1, 15000); // cross into chapter 1
    CHECK(c.chapterReady(), "chapter 1 is Ready");
    CHECK(c.activeSentence().value("spineHref").toString() == QLatin1String("Text/ch2.xhtml"),
          "chapter change repaints the new chapter's sentence");
    CHECK(c.activeSentence().value("end").toInt() == 300, "chapter 1 sentence offsets");

    // A chapter with no published alignment reports not-ready and clears paint.
    c.setPlayhead(pair.pairId, 9, 999999);
    CHECK(!c.chapterReady(), "unpublished chapter reports not-ready");
    CHECK(c.activeSentence().isEmpty(), "no cue at an unaligned time clears the paint");

    if (g_failed) return 1;
    std::fprintf(stdout,
        "PASS read-along lookup, confidence gaps, preview, commit, detach, return\n");
    std::fprintf(stdout, "VERDICT: read_along_controller_harness OK\n");
    return 0;
}
