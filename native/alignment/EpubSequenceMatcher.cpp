#include "EpubSequenceMatcher.h"

#include <QHash>
#include <QVector>
#include <QStringList>

#include <algorithm>

// EpubSequenceMatcher — see the header for the plain-language design. This file is the
// mechanism: tokenize, find rare unambiguous shingles, chain them monotonically, then
// classify every remaining span with a bounded banded alignment. No models, no I/O.

namespace alignment {

namespace {

// ── Tuning (each justified up from zero) ──────────────────────────────────────
// Shingle length: 4 is the minimum of the 4-8 band. Shorter shingles yield more
// candidate anchors (denser locking); 4 tokens is already specific enough to be rare
// in real prose while surviving a single recognition slip in a nearby token.
constexpr int kShingleLen = 4;
// A valid plan needs SEVERAL separated anchors, not one lucky phrase. Below this the
// audio and the book do not correspond -> deliberate edition-mismatch rejection.
constexpr int kMinAnchors = 3;
// Anchors must be spread across the chapter, not clustered in one corner. Require the
// locked chain to span at least this fraction of the transcript's token range.
constexpr double kMinSpread = 0.25;
// A plan whose trusted Aligned runs cover less than this fraction of transcript tokens
// is not a real alignment either.
constexpr double kMinAlignedCoverage = 0.40;
// Band margin around the length difference for the between-anchor alignment. Anchors
// keep gaps small, so a small margin absorbs local spoken jitter.
constexpr int kBandMargin = 8;
// Hard bound so the "banded DP only between anchors" promise holds even for a rare huge
// two-sided gap: beyond this cell budget the span is declared Uncertain, never guessed.
constexpr qint64 kMaxDpCells = 250000;

// ── Token streams ─────────────────────────────────────────────────────────────
struct TextToken {
    int docIndex = 0;         // position in EpubIndex.documents (global reading order)
    QString spineHref;
    qint64 cStart = 0;        // canonical offset (inclusive) within its spine document
    qint64 cEnd = 0;          // canonical offset (exclusive)
    QString tok;
};
struct SpeechToken {
    int segIndex = 0;
    qint64 timeMs = 0;        // interpolated audio time (monotonic non-decreasing)
    QString tok;
};

static inline bool isTokChar(QChar c) {
    const ushort u = c.unicode();
    return (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9');
}

// Tokenize the book's ALREADY-canonical stream: maximal [a-z0-9] runs, keeping each
// token's canonical offset span so an anchor resolves to a stored location. Canonical
// is folded/lowercased upstream, so punctuation (periods in "dr.", "3.14"; quotes;
// dashes) is simply a separator here — the same rule the transcript side uses.
static QVector<TextToken> tokenizeBook(const EpubIndex &index) {
    QVector<TextToken> out;
    for (int d = 0; d < index.documents.size(); ++d) {
        const SpineDocument &doc = index.documents.at(d);
        const QString &s = doc.canonical;
        const int n = s.size();
        int i = 0;
        while (i < n) {
            if (!isTokChar(s.at(i))) { ++i; continue; }
            const int start = i;
            while (i < n && isTokChar(s.at(i))) ++i;
            TextToken t;
            t.docIndex = d;
            t.spineHref = doc.href;
            t.cStart = start;
            t.cEnd = i;
            t.tok = s.mid(start, i - start);
            out.append(t);
        }
    }
    return out;
}

// Fold a transcript slice the same way the canonical stream was folded, then extract
// [a-z0-9] tokens. NFD-decompose and drop marks (café -> cafe) so accented recognition
// output still matches the folded book; quotes/dashes/punctuation fall out as separators.
static QStringList tokenizeText(const QString &raw) {
    QStringList out;
    const QString nfd = raw.normalized(QString::NormalizationForm_D);
    QString cur;
    for (const QChar &c : nfd) {
        if (QChar::category(c.unicode()) == QChar::Mark_NonSpacing) continue;
        QChar lc = c;
        const ushort u = c.unicode();
        if (u >= 'A' && u <= 'Z') lc = QChar(u + 32);
        if (isTokChar(lc)) {
            cur.append(lc);
        } else if (!cur.isEmpty()) {
            out.append(cur);
            cur.clear();
        }
    }
    if (!cur.isEmpty()) out.append(cur);
    return out;
}

// Build the global speech-token stream. Each segment's [startMs,endMs] is spread across
// its tokens (slot midpoints); token times are clamped non-decreasing so an anchor's
// audioMs can never move backward regardless of segment jitter.
static QVector<SpeechToken> tokenizeSpeech(const QList<CoarseSegment> &segmentsIn) {
    QList<CoarseSegment> segments = segmentsIn;
    std::stable_sort(segments.begin(), segments.end(),
                     [](const CoarseSegment &a, const CoarseSegment &b) { return a.startMs < b.startMs; });
    QVector<SpeechToken> out;
    qint64 lastTime = 0;
    for (int s = 0; s < segments.size(); ++s) {
        const CoarseSegment &seg = segments.at(s);
        const QStringList toks = tokenizeText(seg.text);
        const int k = toks.size();
        if (k == 0) continue;
        const qint64 span = (seg.endMs > seg.startMs) ? (seg.endMs - seg.startMs) : 0;
        for (int j = 0; j < k; ++j) {
            qint64 t = seg.startMs + (span * (2 * j + 1)) / (2 * k);
            if (t < lastTime) t = lastTime;
            lastTime = t;
            SpeechToken st;
            st.segIndex = s;
            st.timeMs = t;
            st.tok = toks.at(j);
            out.append(st);
        }
    }
    return out;
}

// ── Shingles ──────────────────────────────────────────────────────────────────
static QString shingleKey(const QVector<TextToken> &v, int start) {
    QString k;
    for (int i = 0; i < kShingleLen; ++i) { if (i) k.append(QChar(u'\x1f')); k.append(v.at(start + i).tok); }
    return k;
}
static QString shingleKey(const QVector<SpeechToken> &v, int start) {
    QString k;
    for (int i = 0; i < kShingleLen; ++i) { if (i) k.append(QChar(u'\x1f')); k.append(v.at(start + i).tok); }
    return k;
}

// A shingle is an anchor candidate only if it occurs EXACTLY ONCE on each side and is
// identical — that uniqueness IS the rarity preference, and it is what makes an anchor
// unambiguous. Common/repeated phrases map to many positions and are skipped here; they
// still get aligned inside the bounded gap between the unique anchors that bracket them.
template <typename V>
static QHash<QString, int> uniqueShingles(const V &v) {
    QHash<QString, int> firstAt;    // key -> start index (only kept if unique)
    QHash<QString, int> count;
    const int last = v.size() - kShingleLen;
    for (int i = 0; i <= last; ++i) {
        const QString k = shingleKey(v, i);
        if (++count[k] == 1) firstAt.insert(k, i);
    }
    QHash<QString, int> out;
    for (auto it = firstAt.constBegin(); it != firstAt.constEnd(); ++it)
        if (count.value(it.key()) == 1) out.insert(it.key(), it.value());
    return out;
}

struct Candidate {
    int speechStart = 0;
    int textStart = 0;
    double score = 0.0;
};

// ── Banded gap classification ─────────────────────────────────────────────────
enum class PieceKind { Aligned, BookOnly, AudioOnly, Uncertain };
struct Piece {
    PieceKind kind = PieceKind::Uncertain;
    int speechLo = 0, speechHi = 0;   // half-open into the global speech stream
    int textLo = 0, textHi = 0;       // half-open into the global text stream
};

enum class Op { Match, Sub, InsSpeech, InsText };

// Classify one gap between two locked points. Degenerate one-sided gaps are trivial
// (all BookOnly or all AudioOnly). A two-sided gap runs a bounded banded alignment; a
// gap too large to bound is declared Uncertain rather than guessed.
static QList<Piece> classifyGap(const QVector<SpeechToken> &sp, const QVector<TextToken> &tx,
                                int sLo, int sHi, int tLo, int tHi) {
    QList<Piece> out;
    const int lenX = sHi - sLo;   // speech
    const int lenY = tHi - tLo;   // text
    if (lenX == 0 && lenY == 0) return out;
    if (lenX == 0) { out.append({PieceKind::BookOnly,  sLo, sHi, tLo, tHi}); return out; }
    if (lenY == 0) { out.append({PieceKind::AudioOnly, sLo, sHi, tLo, tHi}); return out; }
    if (static_cast<qint64>(lenX) * lenY > kMaxDpCells) {
        out.append({PieceKind::Uncertain, sLo, sHi, tLo, tHi});
        return out;
    }

    // Banded Needleman-Wunsch. Band absorbs the length difference plus local jitter.
    const int band = std::abs(lenX - lenY) + kBandMargin;
    const double kMatch = 2.0, kSub = -1.0, kGap = -1.0;
    const double NEG = -1e18;
    QVector<QVector<double>> dp(lenX + 1, QVector<double>(lenY + 1, NEG));
    QVector<QVector<char>> bt(lenX + 1, QVector<char>(lenY + 1, 0)); // 'd','s','x','y'
    dp[0][0] = 0.0;
    for (int i = 0; i <= lenX; ++i) {
        const int jStart = std::max(0, i - band);
        const int jEnd = std::min(lenY, i + band);
        for (int j = jStart; j <= jEnd; ++j) {
            if (i == 0 && j == 0) continue;
            double best = NEG; char op = 0;
            if (i > 0 && j > 0 && dp[i - 1][j - 1] > NEG) {
                const bool eq = sp.at(sLo + i - 1).tok == tx.at(tLo + j - 1).tok;
                const double cand = dp[i - 1][j - 1] + (eq ? kMatch : kSub);
                if (cand > best) { best = cand; op = eq ? 'd' : 's'; }
            }
            if (i > 0 && j >= i - band + 0 && dp[i - 1][j] > NEG) { // consume speech token (audio-only)
                const double cand = dp[i - 1][j] + kGap;
                if (cand > best) { best = cand; op = 'x'; }
            }
            if (j > 0 && dp[i][j - 1] > NEG) {                      // consume text token (book-only)
                const double cand = dp[i][j - 1] + kGap;
                if (cand > best) { best = cand; op = 'y'; }
            }
            dp[i][j] = best; bt[i][j] = op;
        }
    }

    // Backtrack to a forward op list.
    QVector<Op> ops;
    { int i = lenX, j = lenY;
      while (i > 0 || j > 0) {
          const char op = (i >= 0 && j >= 0) ? bt[i][j] : 0;
          if (op == 'd') { ops.append(Op::Match);     --i; --j; }
          else if (op == 's') { ops.append(Op::Sub);  --i; --j; }
          else if (op == 'x') { ops.append(Op::InsSpeech); --i; }
          else if (op == 'y') { ops.append(Op::InsText);   --j; }
          else if (i > 0 && j > 0) { ops.append(Op::Sub); --i; --j; } // band edge fallback
          else if (i > 0) { ops.append(Op::InsSpeech); --i; }
          else { ops.append(Op::InsText); --j; }
      }
      std::reverse(ops.begin(), ops.end()); }

    // Group runs. Diagonal runs (Match/Sub) are one Aligned piece if they contain any
    // true Match, else Uncertain (a genuine mismatch block). Speech-only runs are
    // AudioOnly; text-only runs are BookOnly. A lone Sub between Matches stays inside
    // the Aligned run — tolerated spoken variation.
    int si = sLo, ti = tLo, idx = 0;
    const int nOps = ops.size();
    while (idx < nOps) {
        const Op o = ops.at(idx);
        if (o == Op::Match || o == Op::Sub) {
            int sStart = si, tStart = ti; bool anyMatch = false;
            while (idx < nOps && (ops.at(idx) == Op::Match || ops.at(idx) == Op::Sub)) {
                if (ops.at(idx) == Op::Match) anyMatch = true;
                ++si; ++ti; ++idx;
            }
            out.append({anyMatch ? PieceKind::Aligned : PieceKind::Uncertain, sStart, si, tStart, ti});
        } else if (o == Op::InsSpeech) {
            int sStart = si;
            while (idx < nOps && ops.at(idx) == Op::InsSpeech) { ++si; ++idx; }
            out.append({PieceKind::AudioOnly, sStart, si, ti, ti});
        } else { // InsText
            int tStart = ti;
            while (idx < nOps && ops.at(idx) == Op::InsText) { ++ti; ++idx; }
            out.append({PieceKind::BookOnly, si, si, tStart, ti});
        }
    }
    return out;
}

// A book-bearing piece may straddle a spine-document boundary; RegionRecord holds ONE
// spineHref, so split such a piece at each document change (its text tokens are
// contiguous in global reading order). An Aligned/Uncertain piece is a 1:1 speech<->text
// run, so the speech span is split at the same offset — the second document keeps its own
// audio timing (the cross-spine case). Non-1:1 book-bearing pieces (BookOnly has no
// speech) put any speech on the first sub-piece.
static QList<Piece> splitByDoc(const QVector<TextToken> &tx, const Piece &p) {
    QList<Piece> out;
    if (p.textHi <= p.textLo) { out.append(p); return out; }
    const bool oneToOne = (p.speechHi - p.speechLo) == (p.textHi - p.textLo);
    int segStart = p.textLo;
    for (int t = p.textLo + 1; t <= p.textHi; ++t) {
        if (t == p.textHi || tx.at(t).docIndex != tx.at(segStart).docIndex) {
            Piece q = p;
            q.textLo = segStart; q.textHi = t;
            if (oneToOne) {
                q.speechLo = p.speechLo + (segStart - p.textLo);
                q.speechHi = p.speechLo + (t - p.textLo);
            } else if (segStart != p.textLo) {
                q.speechLo = p.speechHi; q.speechHi = p.speechHi; // extra text sub-pieces: no speech
            }
            out.append(q);
            segStart = t;
        }
    }
    return out;
}

} // namespace

MatchPlan EpubSequenceMatcher::match(const EpubIndex &index,
                                     const QList<CoarseSegment> &segments,
                                     const ChapterHint &hint) const {
    MatchPlan plan;

    const QVector<TextToken> tx = tokenizeBook(index);
    const QVector<SpeechToken> sp = tokenizeSpeech(segments);
    const int nText = tx.size();
    const int nSpeech = sp.size();

    auto reject = [&](const QString &why) -> MatchPlan {
        plan.matched = false;
        plan.rejectReason = why;
        plan.confidence = 0.0;
        plan.anchors.clear();
        plan.regions.clear();
        // Honest single Uncertain span over the whole audio window: nothing resolved.
        if (hint.audioEndMs > hint.audioStartMs)
            plan.regions.append({RegionKind::Uncertain, hint.audioStartMs, hint.audioEndMs, QString(), -1, -1});
        return plan;
    };

    if (nText < kShingleLen || nSpeech < kShingleLen)
        return reject(QStringLiteral("too little text to match (audio and book do not correspond)"));

    // ── Candidate anchors: shingles unique on BOTH sides and identical ────────
    const QHash<QString, int> textUniq = uniqueShingles(tx);
    const QHash<QString, int> speechUniq = uniqueShingles(sp);
    QVector<Candidate> cands;
    for (auto it = speechUniq.constBegin(); it != speechUniq.constEnd(); ++it) {
        const auto tHit = textUniq.constFind(it.key());
        if (tHit == textUniq.constEnd()) continue;
        const int sStart = it.value();
        const int tStart = tHit.value();
        // Proximity: reward a text position near where this speech position is expected
        // to land under a uniform stretch. A rare phrase near its expected spot beats a
        // far coincidental one.
        const double expT = (nText > 0) ? (static_cast<double>(sStart) / nSpeech) * nText : 0.0;
        const double dist = std::abs(static_cast<double>(tStart) - expT);
        const double prox = 1.0 / (1.0 + dist / std::max(1, nText));
        cands.append({sStart, tStart, 1.0 + prox});
    }

    if (cands.size() < kMinAnchors)
        return reject(QStringLiteral("only %1 reliable anchor(s) found (need >= %2); audio and book editions differ")
                          .arg(cands.size()).arg(kMinAnchors));

    // ── Monotonic lock: max-score chain strictly increasing in BOTH speech and text,
    //    non-overlapping (next.start > prev.start + shingle). This is the load-bearing
    //    invariant — an anchor can never move backward — and it resolves a repeated
    //    phrase to the occurrence consistent with its unique neighbours. ────────────
    std::sort(cands.begin(), cands.end(), [](const Candidate &a, const Candidate &b) {
        if (a.speechStart != b.speechStart) return a.speechStart < b.speechStart;
        return a.textStart < b.textStart;
    });
    const int m = cands.size();
    QVector<double> bestScore(m, 0.0);
    QVector<int> prev(m, -1);
    QVector<int> len(m, 1);
    int endBest = 0;
    for (int i = 0; i < m; ++i) {
        bestScore[i] = cands[i].score;
        for (int j = 0; j < i; ++j) {
            if (cands[j].speechStart + kShingleLen <= cands[i].speechStart &&
                cands[j].textStart + kShingleLen <= cands[i].textStart) {
                const double cand = bestScore[j] + cands[i].score;
                if (cand > bestScore[i] || (cand == bestScore[i] && len[j] + 1 > len[i])) {
                    bestScore[i] = cand; prev[i] = j; len[i] = len[j] + 1;
                }
            }
        }
        if (bestScore[i] > bestScore[endBest] ||
            (bestScore[i] == bestScore[endBest] && len[i] > len[endBest]))
            endBest = i;
    }
    QVector<int> chainIdx;
    for (int c = endBest; c != -1; c = prev[c]) chainIdx.append(c);
    std::reverse(chainIdx.begin(), chainIdx.end());

    if (chainIdx.size() < kMinAnchors)
        return reject(QStringLiteral("only %1 monotonic anchor(s) survive (need >= %2); audio and book editions differ")
                          .arg(chainIdx.size()).arg(kMinAnchors));

    // Spread: the locked anchors must span a meaningful fraction of the chapter, not
    // cluster in one corner.
    const int firstS = cands[chainIdx.first()].speechStart;
    const int lastS = cands[chainIdx.last()].speechStart + kShingleLen;
    const double spread = static_cast<double>(lastS - firstS) / std::max(1, nSpeech);
    if (spread < kMinSpread)
        return reject(QStringLiteral("anchors clustered (span %1%% of the chapter); not spread enough to trust")
                          .arg(static_cast<int>(spread * 100)));

    // ── Build the region partition: leading gap, then each anchor's Aligned span with
    //    the bounded gap after it, then the trailing gap. ──────────────────────────
    QList<Piece> pieces;
    int cursorS = 0, cursorT = 0;
    for (int c = 0; c < chainIdx.size(); ++c) {
        const Candidate &a = cands[chainIdx[c]];
        pieces += classifyGap(sp, tx, cursorS, a.speechStart, cursorT, a.textStart);
        pieces.append({PieceKind::Aligned, a.speechStart, a.speechStart + kShingleLen,
                       a.textStart, a.textStart + kShingleLen});
        cursorS = a.speechStart + kShingleLen;
        cursorT = a.textStart + kShingleLen;
    }
    pieces += classifyGap(sp, tx, cursorS, nSpeech, cursorT, nText);

    // Merge adjacent same-kind, same-document pieces to reduce fragmentation; then split
    // any book-bearing piece across spine boundaries so each region has one href.
    QList<Piece> merged;
    for (const Piece &p : pieces) {
        if (!merged.isEmpty()) {
            Piece &b = merged.last();
            const bool sameKind = b.kind == p.kind;
            const bool contig = b.speechHi == p.speechLo && b.textHi == p.textLo;
            if (sameKind && contig) { b.speechHi = p.speechHi; b.textHi = p.textHi; continue; }
        }
        merged.append(p);
    }
    QList<Piece> finalPieces;
    for (const Piece &p : merged)
        finalPieces += (p.textHi > p.textLo) ? splitByDoc(tx, p) : QList<Piece>{p};

    // ── Emit anchors ──────────────────────────────────────────────────────────
    double totalScore = 0.0;
    for (int c = 0; c < chainIdx.size(); ++c) {
        const Candidate &a = cands[chainIdx[c]];
        const TextToken &t0 = tx.at(a.textStart);
        const TextToken &t1 = tx.at(a.textStart + kShingleLen - 1);
        Anchor an;
        an.audioMs = sp.at(a.speechStart).timeMs;
        an.spineHref = t0.spineHref;
        an.canonicalStart = t0.cStart;
        an.canonicalEnd = t1.cEnd;
        an.score = a.score;
        plan.anchors.append(an);
        totalScore += a.score;
    }

    // ── Emit regions with a contiguous audio partition of [hintStart, hintEnd] ──
    // Audio-bearing kinds (Aligned/AudioOnly/Uncertain-with-speech) tile the timeline;
    // BookOnly points sit at the seam (startMs == endMs).
    auto timeOf = [&](int speechIdx) -> qint64 {
        if (speechIdx < 0) return hint.audioStartMs;
        if (speechIdx >= nSpeech) return hint.audioEndMs;
        return sp.at(speechIdx).timeMs;
    };
    // Index of the last audio-bearing piece, so it can extend to hintEnd.
    int lastAudioPiece = -1;
    for (int i = 0; i < finalPieces.size(); ++i)
        if (finalPieces[i].speechHi > finalPieces[i].speechLo) lastAudioPiece = i;

    qint64 cursorMs = hint.audioStartMs;
    int alignedSpeechTokens = 0;
    for (int i = 0; i < finalPieces.size(); ++i) {
        const Piece &p = finalPieces[i];
        const bool hasSpeech = p.speechHi > p.speechLo;
        const bool hasText = p.textHi > p.textLo;

        RegionRecord r;
        r.kind = (p.kind == PieceKind::Aligned)   ? RegionKind::Aligned
               : (p.kind == PieceKind::BookOnly)  ? RegionKind::BookOnly
               : (p.kind == PieceKind::AudioOnly) ? RegionKind::AudioOnly
                                                  : RegionKind::Uncertain;
        if (hasText) {
            r.spineHref = tx.at(p.textLo).spineHref;
            r.canonicalStart = tx.at(p.textLo).cStart;
            r.canonicalEnd = tx.at(p.textHi - 1).cEnd;
        } else {
            r.spineHref = QString();
            r.canonicalStart = -1;
            r.canonicalEnd = -1;
        }

        if (hasSpeech) {
            r.startMs = cursorMs;
            // End at the seam midpoint before the next audio token, or hintEnd if last.
            qint64 endMs;
            if (i == lastAudioPiece) {
                endMs = hint.audioEndMs;
            } else {
                const qint64 thisLast = timeOf(p.speechHi - 1);
                const qint64 nextFirst = timeOf(p.speechHi); // next global speech token
                endMs = (thisLast + nextFirst) / 2;
                if (endMs < r.startMs) endMs = r.startMs;
            }
            r.endMs = endMs;
            cursorMs = endMs;
            if (p.kind == PieceKind::Aligned) alignedSpeechTokens += (p.speechHi - p.speechLo);
        } else {
            // BookOnly / text-only piece: a point at the current seam.
            r.startMs = cursorMs;
            r.endMs = cursorMs;
        }
        plan.regions.append(r);
    }

    // ── Final acceptance: enough trusted coverage on top of the anchor gate ────
    plan.confidence = (nSpeech > 0) ? static_cast<double>(alignedSpeechTokens) / nSpeech : 0.0;
    if (plan.confidence < kMinAlignedCoverage)
        return reject(QStringLiteral("aligned coverage %1%% below floor; audio and book editions differ")
                          .arg(static_cast<int>(plan.confidence * 100)));

    plan.matched = true;
    plan.rejectReason.clear();
    return plan;
}

} // namespace alignment
