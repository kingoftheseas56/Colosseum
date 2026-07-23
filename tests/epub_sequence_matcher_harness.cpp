// EpubSequenceMatcher harness — proves the pure matcher lines coarse narration up
// against the authoritative EPUB text and, above all, stays HONEST:
//   • committed anchors never move backward (audio time AND canonical position),
//   • every span is explicitly classified (aligned / book_only / audio_only / uncertain)
//     with no unexplained hole in the audio timeline,
//   • a different-book transcript is REJECTED (matched=false) rather than force-fit.
//
// The EpubIndex and CoarseSegment fixtures are hand-built here (no real .epub); the
// per-case verdicts (matched, minAnchors, expected region kinds) are driven from
// tests/fixtures/alignment/matching/manifest.json.
//
// Usage: epub_sequence_matcher_harness [manifest.json]
//   No arg -> ALIGNMENT_MATCHING_DIR/manifest.json (set by CMake).
// Verdict via exit code (0 PASS / 1 FAIL).
//
// [Agent 2 (Claude), biblio]

#include "alignment/EpubSequenceMatcher.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSet>
#include <cstdio>

using namespace alignment;

static bool g_failed = false;
static void check(bool ok, const char *msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); g_failed = true; }
}
#define CHECK(cond, msg) check((cond), (msg))

// ── Fixture builders ──────────────────────────────────────────────────────────
// A SpineDocument only needs href + canonical for the matcher; display/map/sentences
// are unused by matching, so we leave them empty.
static SpineDocument doc(const QString &href, const QString &canonical) {
    SpineDocument d;
    d.href = href;
    d.canonical = canonical;
    return d;
}
static EpubIndex indexOf(const QList<SpineDocument> &docs) {
    EpubIndex idx;
    idx.ok = true;
    idx.documents = docs;
    return idx;
}
static CoarseSegment seg(qint64 startMs, qint64 endMs, const QString &text, double conf = 0.9) {
    return CoarseSegment{startMs, endMs, text, conf};
}

// The reusable authoritative chapter text (already in canonical form: lowercase, single
// spaces, punctuation kept as it would fold).
static const char *kBody =
    "the lighthouse keeper climbed the spiral stairs before the pale dawn. "
    "salt wind rattled the iron railing while gulls wheeled above the grey harbor. "
    "he lit the enormous lamp and watched its beam sweep across the restless midnight water. "
    "far below a small fishing boat struggled against the rising tide. "
    "the old man counted each revolution of the lantern with quiet patience.";

// Split a run of text into N coarse segments over [t0, t1] (by words, roughly even).
static QList<CoarseSegment> segmentize(const QString &text, int parts, qint64 t0, qint64 t1) {
    const QStringList words = text.split(QChar(u' '), Qt::SkipEmptyParts);
    QList<CoarseSegment> out;
    const int per = static_cast<int>(std::max<qsizetype>(1, (words.size() + parts - 1) / parts));
    const qint64 span = t1 - t0;
    int idx = 0, made = 0;
    const int total = static_cast<int>((words.size() + per - 1) / per);
    while (idx < words.size()) {
        QStringList chunk;
        for (int k = 0; k < per && idx < words.size(); ++k, ++idx) chunk << words.at(idx);
        const qint64 a = t0 + (span * made) / std::max(1, total);
        const qint64 b = t0 + (span * (made + 1)) / std::max(1, total);
        out << seg(a, b, chunk.join(QChar(u' ')));
        ++made;
    }
    return out;
}

// ── Case registry ─────────────────────────────────────────────────────────────
struct Case {
    EpubIndex index;
    QList<CoarseSegment> segments;
    ChapterHint hint;
};

static QHash<QString, Case> buildCases() {
    QHash<QString, Case> cases;
    const QString body = QString::fromLatin1(kBody);
    const ChapterHint win{0, 20000};

    // exact — transcript is the book, verbatim.
    cases.insert(QStringLiteral("exact"),
                 Case{indexOf({doc(QStringLiteral("Text/ch1.xhtml"), body)}),
                      segmentize(body, 4, 0, 20000), win});

    // differing chapter numbering — audio says "chapter five", book says "chapter three";
    // the body still aligns by content.
    cases.insert(QStringLiteral("differing_numbering"),
                 Case{indexOf({doc(QStringLiteral("Text/ch1.xhtml"),
                                   QStringLiteral("chapter three. ") + body)}),
                      segmentize(QStringLiteral("chapter five. ") + body, 4, 0, 20000), win});

    // opening credits — leading narration with no book text, then the body.
    {
        const QString credit =
            QStringLiteral("this recording is a blackstone audio production narrated by evelyn shaw");
        QList<CoarseSegment> s;
        s << seg(0, 3000, credit);
        s << segmentize(body, 3, 3000, 20000);
        cases.insert(QStringLiteral("opening_credits"),
                     Case{indexOf({doc(QStringLiteral("Text/ch1.xhtml"), body)}), s, win});
    }

    // skipped paragraph — the book has a paragraph the narration skipped.
    {
        const QString firstHalf =
            QStringLiteral("the lighthouse keeper climbed the spiral stairs before the pale dawn. "
                           "salt wind rattled the iron railing while gulls wheeled above the grey harbor.");
        const QString skipped =
            QStringLiteral(" a flock of cormorants settled upon the jagged rocks beneath the cliff edge "
                           "waiting silently for the coming storm to pass over the northern cape.");
        const QString secondHalf =
            QStringLiteral(" he lit the enormous lamp and watched its beam sweep across the restless midnight water. "
                           "far below a small fishing boat struggled against the rising tide.");
        cases.insert(QStringLiteral("skipped_paragraph"),
                     Case{indexOf({doc(QStringLiteral("Text/ch1.xhtml"), firstHalf + skipped + secondHalf)}),
                          segmentize(firstHalf + secondHalf, 4, 0, 20000), win});
    }

    // repeated phrase — a distinctive phrase occurs twice; the matcher must resolve each
    // occurrence to the right place via rare-shingle preference + the monotonic lock.
    {
        const QString phrase = QStringLiteral("the crimson bell tolled twice");
        const QString text =
            QStringLiteral("deep within the ancient stone tower ") + phrase +
            QStringLiteral(" and every weary sailor paused to listen carefully across the quiet bay. "
                           "then the heavy fog rolled slowly inward and ") + phrase +
            QStringLiteral(" again beyond the distant harbor wall while lanterns flickered along the wooden pier.");
        cases.insert(QStringLiteral("repeated_phrase"),
                     Case{indexOf({doc(QStringLiteral("Text/ch1.xhtml"), text)}),
                          segmentize(text, 4, 0, 20000), win});
    }

    // punctuation / abbreviation variants — book keeps "dr.", "mrs.", "3.14", a hyphen;
    // the transcript spells them without the period, with an em dash — same tokens.
    {
        const QString bookText =
            QStringLiteral("dr. aldous meade adjusted the brass telescope and recorded 3.14 degrees of "
                           "parallax in his worn leather journal. mrs. holloway prepared the evening tea "
                           "while the storm-tossed sea battered the old observatory on the northern hill.");
        const QString heard =
            QStringLiteral("Dr Aldous Meade adjusted the brass telescope and recorded 3.14 degrees of "
                           "parallax in his worn leather journal. Mrs Holloway prepared the evening tea "
                           "while the storm—tossed sea battered the old observatory on the northern hill.");
        cases.insert(QStringLiteral("punctuation_variants"),
                     Case{indexOf({doc(QStringLiteral("Text/ch1.xhtml"), bookText)}),
                          segmentize(heard, 4, 0, 20000), win});
    }

    // cross-spine audio — one audio chapter reads across two spine documents.
    {
        const QString ch1 =
            QStringLiteral("the caravan crossed the amber desert beneath a merciless sun while the camels "
                           "groaned under heavy silken bundles bound for the distant eastern markets.");
        const QString ch2 =
            QStringLiteral("at the crowded oasis the weary traders unpacked their fragrant spices and counted "
                           "silver coins beneath the swaying palms as merchants haggled into the cooling night.");
        cases.insert(QStringLiteral("cross_spine"),
                     Case{indexOf({doc(QStringLiteral("Text/ch1.xhtml"), ch1),
                                   doc(QStringLiteral("Text/ch2.xhtml"), ch2)}),
                          segmentize(ch1 + QStringLiteral(" ") + ch2, 5, 0, 20000), win});
    }

    // deliberate mismatch — the transcript is a completely different book.
    {
        const QString other =
            QStringLiteral("the quarterly earnings report exceeded every analyst projection by a comfortable "
                           "margin driven primarily by accelerating cloud infrastructure revenue and disciplined "
                           "operating expenditure across all regional business units.");
        cases.insert(QStringLiteral("deliberate_mismatch"),
                     Case{indexOf({doc(QStringLiteral("Text/ch1.xhtml"), body)}),
                          segmentize(other, 4, 0, 20000), win});
    }

    return cases;
}

// spineHref -> document index, for asserting canonical monotonicity across spines.
static QHash<QString, int> hrefToDoc(const EpubIndex &idx) {
    QHash<QString, int> m;
    for (int i = 0; i < idx.documents.size(); ++i) m.insert(idx.documents.at(i).href, i);
    return m;
}

// Assert the anchor chain never moves backward in EITHER audio time or canonical
// position (document order, then canonical offset).
static bool anchorsMonotonic(const MatchPlan &plan, const QHash<QString, int> &docOf) {
    for (int i = 1; i < plan.anchors.size(); ++i) {
        const Anchor &a = plan.anchors.at(i - 1);
        const Anchor &b = plan.anchors.at(i);
        if (b.audioMs < a.audioMs) return false;
        const int da = docOf.value(a.spineHref, 0), db = docOf.value(b.spineHref, 0);
        if (db < da) return false;
        if (db == da && b.canonicalStart < a.canonicalStart) return false;
    }
    return true;
}

// Assert every region is explicitly and consistently classified, and that the
// audio-bearing regions tile [hintStart, hintEnd] with no unexplained hole.
static bool gapsFullyClassified(const MatchPlan &plan, const ChapterHint &hint) {
    // Per-kind shape invariants.
    for (const RegionRecord &r : plan.regions) {
        switch (r.kind) {
            case RegionKind::Aligned:
                if (r.canonicalStart < 0 || r.spineHref.isEmpty() || r.endMs <= r.startMs) return false;
                break;
            case RegionKind::BookOnly:
                if (r.canonicalStart < 0 || r.spineHref.isEmpty() || r.endMs != r.startMs) return false;
                break;
            case RegionKind::AudioOnly:
                if (r.canonicalStart != -1 || r.endMs <= r.startMs) return false;
                break;
            case RegionKind::Uncertain:
                break; // uncertain may carry either shape
        }
    }
    // Audio-timeline partition: every ms in [start,end) belongs to exactly one region.
    QList<RegionRecord> audio;
    for (const RegionRecord &r : plan.regions) if (r.endMs > r.startMs) audio.append(r);
    std::sort(audio.begin(), audio.end(),
              [](const RegionRecord &a, const RegionRecord &b) { return a.startMs < b.startMs; });
    if (audio.isEmpty()) return false;
    if (audio.first().startMs != hint.audioStartMs) return false;
    if (audio.last().endMs != hint.audioEndMs) return false;
    for (int i = 1; i < audio.size(); ++i)
        if (audio.at(i).startMs != audio.at(i - 1).endMs) return false;
    return true;
}

static bool hasKind(const MatchPlan &plan, RegionKind k) {
    for (const RegionRecord &r : plan.regions) if (r.kind == k) return true;
    return false;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    QString manifestPath;
    if (argc >= 2) {
        manifestPath = QString::fromLocal8Bit(argv[1]);
    } else {
#ifdef ALIGNMENT_MATCHING_DIR
        manifestPath = QStringLiteral(ALIGNMENT_MATCHING_DIR) + QStringLiteral("/manifest.json");
#else
        std::fprintf(stderr, "FAIL: no manifest given and ALIGNMENT_MATCHING_DIR undefined\n");
        return 1;
#endif
    }
    QFile mf(manifestPath);
    if (!mf.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "FAIL: cannot open %s\n", qPrintable(manifestPath));
        return 1;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
    mf.close();

    const QHash<QString, Case> cases = buildCases();
    const EpubSequenceMatcher matcher;
    const QJsonArray caseArr = manifest.value(QStringLiteral("cases")).toArray();
    CHECK(!caseArr.isEmpty(), "manifest lists cases");

    for (const QJsonValue &cv : caseArr) {
        const QJsonObject co = cv.toObject();
        const QString name = co.value(QStringLiteral("name")).toString();
        const bool wantMatch = co.value(QStringLiteral("matched")).toBool();
        const int minAnchors = co.value(QStringLiteral("minAnchors")).toInt();

        const auto it = cases.constFind(name);
        if (it == cases.constEnd()) {
            std::fprintf(stderr, "FAIL: no fixture for case '%s'\n", qPrintable(name));
            g_failed = true;
            continue;
        }
        const Case &c = it.value();
        const MatchPlan plan = matcher.match(c.index, c.segments, c.hint);
        const QByteArray tag = name.toLocal8Bit();

        // Self-documenting summary: what this case actually produced.
        int nA = 0, nB = 0, nAu = 0, nU = 0;
        for (const RegionRecord &r : plan.regions) {
            if (r.kind == RegionKind::Aligned) ++nA;
            else if (r.kind == RegionKind::BookOnly) ++nB;
            else if (r.kind == RegionKind::AudioOnly) ++nAu;
            else ++nU;
        }
        std::fprintf(stdout,
                     "  %-20s matched=%d anchors=%2d regions[aligned=%d book_only=%d audio_only=%d uncertain=%d] conf=%.2f%s%s\n",
                     tag.constData(), plan.matched ? 1 : 0, static_cast<int>(plan.anchors.size()),
                     nA, nB, nAu, nU, plan.confidence,
                     plan.rejectReason.isEmpty() ? "" : " reject=",
                     plan.rejectReason.isEmpty() ? "" : plan.rejectReason.toLocal8Bit().constData());

        if (!wantMatch) {
            CHECK(!plan.matched, (tag + ": mismatch is rejected (matched=false)").constData());
            CHECK(!plan.rejectReason.isEmpty(), (tag + ": rejection carries a reason").constData());
            CHECK(plan.anchors.isEmpty(), (tag + ": rejected plan forces no anchors").constData());
            continue;
        }

        const QHash<QString, int> docOf = hrefToDoc(c.index);
        CHECK(plan.matched, (tag + ": accepted (matched=true)").constData());
        CHECK(plan.rejectReason.isEmpty(), (tag + ": accepted plan has no reject reason").constData());
        CHECK(plan.anchors.size() >= minAnchors, (tag + ": enough separated anchors").constData());
        CHECK(anchorsMonotonic(plan, docOf), (tag + ": anchors never move backward").constData());
        CHECK(gapsFullyClassified(plan, c.hint), (tag + ": every gap classified, audio fully partitioned").constData());

        const QJsonArray kinds = co.value(QStringLiteral("expectKinds")).toArray();
        for (const QJsonValue &kv : kinds) {
            const RegionKind k = regionKindFromWire(kv.toString());
            CHECK(hasKind(plan, k),
                  (tag + ": region kind '" + kv.toString().toLocal8Bit() + "' present").constData());
        }

        // Case-specific honesty checks.
        if (name == QLatin1String("repeated_phrase")) {
            // The latest anchor must resolve to the LATER occurrence (large canonical
            // offset), never wrap back to the earlier duplicate.
            qint64 maxCanon = -1;
            const qint64 canonLen = c.index.documents.first().canonical.size();
            for (const Anchor &a : plan.anchors) maxCanon = std::max(maxCanon, a.canonicalStart);
            CHECK(maxCanon > canonLen / 2, (tag + ": late anchor resolves to the later occurrence").constData());
        }
        if (name == QLatin1String("cross_spine")) {
            QSet<int> docsHit;
            for (const Anchor &a : plan.anchors) docsHit.insert(docOf.value(a.spineHref, -1));
            CHECK(docsHit.contains(0) && docsHit.contains(1),
                  (tag + ": anchors span both spine documents").constData());
            bool ch2Region = false;
            for (const RegionRecord &r : plan.regions)
                if (r.spineHref == QLatin1String("Text/ch2.xhtml")) ch2Region = true;
            CHECK(ch2Region, (tag + ": a region carries the second spine href").constData());
        }
    }

    if (g_failed) {
        std::fprintf(stderr, "VERDICT: FAIL\n");
        return 1;
    }
    std::fprintf(stdout, "PASS monotonic anchors, explicit gaps, mismatch rejection\n");
    std::fprintf(stdout, "VERDICT: PASS\n");
    return 0;
}
