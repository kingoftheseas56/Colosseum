#include "ReadAlongController.h"

#include "AlignmentStore.h"

#include <QList>
#include <optional>

namespace alignment {

ReadAlongController::ReadAlongController(AlignmentStore *store, QObject *parent)
    : QObject(parent), m_store(store) {}

QString ReadAlongController::followState() const {
    return m_following ? QStringLiteral("following") : QStringLiteral("detached");
}

// ── The confidence policy — nothing guessed is ever painted ───────────────────
// Build the paint cue for a store lookup. cueAtTime() only ever returns aligned
// (trusted-region) sentences, so a book-only / audio-only / uncertain stretch
// arrives here as hasSentence==false and clears the highlight. Beyond that:
//   • sentence confidence below the cutoff  -> clear BOTH (empty cue)
//   • word confidence below the cutoff (or no word) -> keep the sentence, drop
//     the word emphasis (the "trusted-sentence carry" of the design's rules)
QVariantMap ReadAlongController::buildCue(const ActiveCue &ac) const {
    QVariantMap cue;
    if (!ac.hasSentence) return cue;                              // gap -> clear both
    if (ac.sentence.confidence < trust::kMinConfidence) return cue; // untrusted sentence -> clear both

    cue.insert(QStringLiteral("spineHref"), ac.sentence.spineHref);
    QVariantMap s;
    s.insert(QStringLiteral("start"), static_cast<int>(ac.sentence.canonicalStart));
    s.insert(QStringLiteral("end"),   static_cast<int>(ac.sentence.canonicalEnd));
    cue.insert(QStringLiteral("sentence"), s);

    if (ac.hasWord && ac.word.confidence >= trust::kMinConfidence) {
        QVariantMap w;
        w.insert(QStringLiteral("start"), static_cast<int>(ac.word.canonicalStart));
        w.insert(QStringLiteral("end"),   static_cast<int>(ac.word.canonicalEnd));
        cue.insert(QStringLiteral("word"), w);
    }
    return cue;
}

void ReadAlongController::setActiveMapsFromCue(const QVariantMap &cue) {
    QVariantMap newSentence;
    if (cue.contains(QStringLiteral("sentence"))) {
        const QVariantMap s = cue.value(QStringLiteral("sentence")).toMap();
        newSentence.insert(QStringLiteral("spineHref"), cue.value(QStringLiteral("spineHref")));
        newSentence.insert(QStringLiteral("start"), s.value(QStringLiteral("start")));
        newSentence.insert(QStringLiteral("end"),   s.value(QStringLiteral("end")));
    }
    QVariantMap newWord;
    if (cue.contains(QStringLiteral("word"))) {
        const QVariantMap w = cue.value(QStringLiteral("word")).toMap();
        newWord.insert(QStringLiteral("start"), w.value(QStringLiteral("start")));
        newWord.insert(QStringLiteral("end"),   w.value(QStringLiteral("end")));
    }
    if (newSentence != m_activeSentence) { m_activeSentence = newSentence; emit activeSentenceChanged(); }
    if (newWord != m_activeWord)         { m_activeWord = newWord;         emit activeWordChanged(); }
}

// Resolve the current playhead, apply the confidence policy, dedup against the
// last emitted cue, and (unless deduped) update the properties + emit
// paintRequested. Navigation is emitted by the CALLER (emitSentenceNav /
// commitLocation), never here — so a committed jump owns exactly one nav.
ReadAlongController::PaintOutcome ReadAlongController::refreshPaint(bool force) {
    PaintOutcome out{false, false};
    if (!m_havePlayhead) return out;

    const ActiveCue ac = m_store ? m_store->cueAtTime(m_pairId, m_timeMs) : ActiveCue{};
    const QVariantMap cue = buildCue(ac);

    out.hasSentence = cue.contains(QStringLiteral("sentence"));
    out.sentenceChanged =
        cue.value(QStringLiteral("spineHref")) != m_lastCue.value(QStringLiteral("spineHref"))
        || cue.value(QStringLiteral("sentence")) != m_lastCue.value(QStringLiteral("sentence"));

    const bool same = m_havePainted && cue == m_lastCue;
    if (!force && same) return out;   // dedup: no repaint churn on an unchanged cue

    m_lastCue = cue;
    m_havePainted = true;
    setActiveMapsFromCue(cue);
    emit paintRequested(cue);
    return out;
}

void ReadAlongController::emitSentenceNav() {
    if (!m_lastCue.contains(QStringLiteral("sentence"))) return;
    const QVariantMap s = m_lastCue.value(QStringLiteral("sentence")).toMap();
    QVariantMap loc;
    loc.insert(QStringLiteral("spineHref"), m_lastCue.value(QStringLiteral("spineHref")));
    loc.insert(QStringLiteral("canonicalStart"), s.value(QStringLiteral("start")));
    loc.insert(QStringLiteral("canonicalEnd"),   s.value(QStringLiteral("end")));
    emit navigationRequested(loc);
}

void ReadAlongController::refreshChapterReady() {
    bool ready = false;
    if (m_store && m_havePlayhead) {
        const ChapterStatus st = m_store->chapterStatus(m_pairId, m_chapter);
        ready = st.exists && st.stage == Stage::Ready;
    }
    if (ready != m_chapterReady) { m_chapterReady = ready; emit chapterReadyChanged(); }
}

int ReadAlongController::chapterForTime(const QString &pairId, qint64 timeMs) const {
    if (!m_store) return -1;
    const QList<ChapterStatus> chs = m_store->chapters(pairId);
    for (const ChapterStatus &st : chs) {
        if (timeMs >= st.audioStartMs && timeMs < st.audioEndMs)
            return st.chapterIndex;
    }
    return -1;
}

void ReadAlongController::setFollowing(bool following) {
    if (m_following == following) return;
    m_following = following;
    emit followStateChanged();
}

// ── Audio playback advanced ───────────────────────────────────────────────────
void ReadAlongController::setPlayhead(const QString &pairId, int chapter, qint64 timeMs) {
    // Paused tick: an identical playhead is not a new position — leave the last
    // trusted paint intact (no clear, no churn).
    if (m_havePlayhead && pairId == m_pairId && chapter == m_chapter && timeMs == m_timeMs)
        return;

    m_pairId = pairId;
    m_chapter = chapter;
    m_timeMs = timeMs;
    m_havePlayhead = true;

    refreshChapterReady();

    if (m_following) {
        const PaintOutcome out = refreshPaint(false);
        // Keep the active sentence visible only when it actually changes — never
        // recenter on every word (design: viewport following).
        if (out.hasSentence && out.sentenceChanged)
            emitSentenceNav();
    }
}

// ── Hover/scrub preview — NEVER commits ───────────────────────────────────────
void ReadAlongController::previewTime(const QString &pairId, int chapter, qint64 timeMs) {
    QVariantMap p;
    p.insert(QStringLiteral("timeMs"), timeMs);
    p.insert(QStringLiteral("chapter"), chapter);

    const ActiveCue ac = m_store ? m_store->cueAtTime(pairId, timeMs) : ActiveCue{};
    if (ac.hasSentence && ac.sentence.confidence >= trust::kMinConfidence) {
        // The store holds offsets, not text; hand the reader a trusted locator it
        // can render the excerpt from. "synced" drives the not-synced-yet copy.
        p.insert(QStringLiteral("synced"), true);
        p.insert(QStringLiteral("spineHref"), ac.sentence.spineHref);
        p.insert(QStringLiteral("canonicalStart"), static_cast<int>(ac.sentence.canonicalStart));
        p.insert(QStringLiteral("canonicalEnd"),   static_cast<int>(ac.sentence.canonicalEnd));
    } else {
        p.insert(QStringLiteral("synced"), false);
    }

    m_preview = p;
    emit previewChanged();
    // Deliberately no paintRequested and no audioSeekRequested — preview never commits.
}

// ── The one committed-seek path ───────────────────────────────────────────────
void ReadAlongController::commitTime(const QString &pairId, int chapter, qint64 timeMs) {
    // Design: commit one seek, resolve the destination cue, jump the EPUB,
    // repaint, and restore visual following.
    setFollowing(true);
    m_pairId = pairId;
    m_chapter = chapter;
    m_timeMs = timeMs;
    m_havePlayhead = true;
    refreshChapterReady();

    emit audioSeekRequested(chapter, timeMs, true);   // exactly one committed seek

    const PaintOutcome out = refreshPaint(true);
    if (out.hasSentence)
        emitSentenceNav();
}

void ReadAlongController::commitLocation(const QString &pairId, const QVariantMap &location) {
    CanonicalLocation loc;
    loc.spineHref      = location.value(QStringLiteral("spineHref")).toString();
    loc.canonicalStart = location.value(QStringLiteral("canonicalStart")).toLongLong();
    loc.canonicalEnd   = location.value(QStringLiteral("canonicalEnd")).toLongLong();

    const std::optional<qint64> t =
        m_store ? m_store->timeAtLocation(pairId, loc) : std::nullopt;

    if (t.has_value()) {
        const int chapter = chapterForTime(pairId, *t);
        setFollowing(true);
        m_pairId = pairId;
        m_chapter = chapter;
        m_timeMs = *t;
        m_havePlayhead = true;
        refreshChapterReady();

        emit audioSeekRequested(chapter, *t, true);   // exactly one committed seek
        emit navigationRequested(location);            // jump the EPUB to the location
        refreshPaint(true);                            // resolve destination cue + repaint
    } else {
        // Book-only / unaligned text has no audio time: navigate the book, but do
        // not fabricate a seek to an unknown time.
        emit navigationRequested(location);
    }
}

// ── Follow state ──────────────────────────────────────────────────────────────
void ReadAlongController::detachFollow() {
    // Stop auto-scrolling/repainting to the playhead. Audio is not ours to pause,
    // and the current highlight is left exactly where it is.
    setFollowing(false);
}

void ReadAlongController::returnToNarration() {
    setFollowing(true);
    const PaintOutcome out = refreshPaint(true);   // repaint to the live playhead
    if (out.hasSentence)
        emitSentenceNav();
}

} // namespace alignment
