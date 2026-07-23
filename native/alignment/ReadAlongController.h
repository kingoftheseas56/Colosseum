#pragma once

// ReadAlongController — the one bidirectional read-along state machine.
//
// One small thing: a GUI-thread QObject that turns the store's alignment cues
// into the two things Reader2's read-along needs — a paint cue (which sentence,
// which word, to highlight right now) and a single committed audio/EPUB jump —
// and enforces the two rules that keep it honest:
//
//   • Nothing guessed is ever painted. A low-confidence word drops just the word
//     emphasis; a low-confidence sentence, a book-only skip, an audio-only
//     credit, or an uncertain stretch clears the highlight entirely. A
//     trustworthy sentence stays lit across a small low-confidence word gap
//     ("trusted-sentence carry").
//
//   • Every committed navigation converges on ONE seek. Scrub release, keyboard
//     skip, chapter jump, double-click, and programmatic seek all call
//     commitTime()/commitLocation(), and each emits exactly one
//     audioSeekRequested. Live playback (setPlayhead) never seeks; hover/scrub
//     preview (previewTime) never seeks and never paints.
//
// It is a plain state machine over AlignmentStore reads — no worker, no threads,
// no timers. The store answers cueAtTime()/timeAtLocation() logarithmically; the
// controller adds the confidence policy, the dedup (no repaint churn on an
// unchanged cue), and the following/detached follow state. QVariantMap shapes are
// the fixed wire to QML: a paint cue is
//   { "spineHref", "sentence": {start,end}, "word": {start,end}? }
// (an empty cue {} means paint nothing; "word" is omitted when no trusted word),
// and a location is { "spineHref", "canonicalStart", "canonicalEnd" }.
//
// Design authority: docs/superpowers/specs/2026-07-21-audiobook-epub-read-along-design.md
// (Read-along modes, Viewport following, Double-click seeking, the scrub bar's
// committed-seek path, and the Matching and Confidence Rules).
// Consumes Qt Core + the AlignmentStore. GUI thread only.
//
// [Agent 2 (Claude), biblio]

#include "AlignmentTypes.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtGlobal>

namespace alignment {

class AlignmentStore;

// The trust cutoff for painting. Sentence and word confidence are stored
// separately (design: Matching and Confidence Rules); a cue at or above this is
// trustworthy enough to highlight, below it is treated as a gap. Not a stored
// wire value — a controller-side presentation policy — so it lives here, not in
// AlignmentTypes.
namespace trust {
    constexpr double kMinConfidence = 0.5;
}

class ReadAlongController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap activeSentence READ activeSentence NOTIFY activeSentenceChanged)
    Q_PROPERTY(QVariantMap activeWord READ activeWord NOTIFY activeWordChanged)
    Q_PROPERTY(QString followState READ followState NOTIFY followStateChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(bool chapterReady READ chapterReady NOTIFY chapterReadyChanged)
public:
    // store must outlive the controller (injected, not owned). GUI thread only.
    explicit ReadAlongController(AlignmentStore *store, QObject *parent = nullptr);
    ~ReadAlongController() override = default;

    QVariantMap activeSentence() const { return m_activeSentence; }
    QVariantMap activeWord() const { return m_activeWord; }
    QString followState() const;
    QVariantMap preview() const { return m_preview; }
    bool chapterReady() const { return m_chapterReady; }

    // Audio playback advanced to (chapter, timeMs). Resolves the cue and, while
    // following, repaints (deduped) and keeps the sentence visible. Emits no seek.
    // A repeated same-(chapter,timeMs) call is a paused tick: it leaves the last
    // paint intact.
    Q_INVOKABLE void setPlayhead(const QString &pairId, int chapter, qint64 timeMs);

    // Hover/scrub preview: updates the preview property (time, chapter, and a
    // locator when that time is aligned). Emits NO seek and NO paint.
    Q_INVOKABLE void previewTime(const QString &pairId, int chapter, qint64 timeMs);

    // The one committed-seek path. commitTime emits exactly one
    // audioSeekRequested(chapter, timeMs, play=true). commitLocation resolves the
    // location to an audio time via timeAtLocation, then emits exactly one
    // audioSeekRequested plus one navigationRequested(location); a location with
    // no aligned time navigates the book but commits no seek.
    Q_INVOKABLE void commitTime(const QString &pairId, int chapter, qint64 timeMs);
    Q_INVOKABLE void commitLocation(const QString &pairId, const QVariantMap &location);

    // Manual gesture disengaged visual following (audio keeps playing — the
    // controller does not own audio). Stops auto-repaint/auto-scroll to the
    // playhead without clearing the current highlight.
    Q_INVOKABLE void detachFollow();
    // "Return to narration": re-engage following and repaint to the live playhead.
    Q_INVOKABLE void returnToNarration();

signals:
    void paintRequested(const QVariantMap &cue);
    void navigationRequested(const QVariantMap &location);
    void audioSeekRequested(int chapter, qint64 timeMs, bool play);
    void followStateChanged();
    void activeSentenceChanged();
    void activeWordChanged();
    void previewChanged();
    void chapterReadyChanged();

private:
    // What refreshPaint() resolved, so the caller can decide whether to emit an
    // ensure-visible navigation (nav is a caller decision — a committed jump owns
    // exactly one nav, so refreshPaint never emits one itself).
    struct PaintOutcome {
        bool sentenceChanged;   // the active sentence differs from the last paint
        bool hasSentence;       // a trusted sentence is being highlighted
    };

    // Build the paint cue for a store ActiveCue under the confidence policy.
    // Returns {} (empty) when nothing trustworthy should be highlighted.
    QVariantMap buildCue(const ActiveCue &ac) const;
    // Resolve the current playhead, apply the confidence policy, dedup against the
    // last emitted cue, and (unless deduped, or when forced) update the properties
    // and emit paintRequested. Never emits navigation.
    PaintOutcome refreshPaint(bool force);
    // Emit navigationRequested for the last painted sentence (ensure-visible).
    void emitSentenceNav();
    void setActiveMapsFromCue(const QVariantMap &cue);
    void refreshChapterReady();
    int chapterForTime(const QString &pairId, qint64 timeMs) const;
    void setFollowing(bool following);

    AlignmentStore *m_store = nullptr;   // injected, not owned

    // Current playhead (the live audio position we paint/return to).
    QString m_pairId;
    int m_chapter = -1;
    qint64 m_timeMs = -1;
    bool m_havePlayhead = false;

    bool m_following = true;
    bool m_chapterReady = false;

    QVariantMap m_activeSentence;   // { spineHref, start, end } or {}
    QVariantMap m_activeWord;       // { start, end } or {}
    QVariantMap m_preview;          // { timeMs, chapter, synced, spineHref?, ... }
    QVariantMap m_lastCue;          // last emitted paint cue — the dedup anchor
    bool m_havePainted = false;
};

} // namespace alignment
