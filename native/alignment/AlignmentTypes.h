#pragma once

// AlignmentTypes — the plain data vocabulary of the audiobook↔EPUB read-along.
//
// One small thing: the records every alignment component passes around, plus the
// stable string "wire codes" that name a chapter's stage, a region's kind, and a
// failure — the exact spellings that persist in SQLite and surface (via the
// service) to QML. Nothing here does work; it is nouns, not verbs. The store
// (AlignmentStore), the service, and the controller all speak in these types so a
// cue means the same thing in the database, in C++, and on the reader's page.
//
// Design authority: docs/superpowers/specs/2026-07-21-audiobook-epub-read-along-design.md.
// Consumes Qt Core only.

#include <QString>
#include <QByteArray>
#include <QtGlobal>

namespace alignment {

// ── Chapter stage ────────────────────────────────────────────────────────────
// The per-chapter lifecycle:
//   Waiting -> Preparing -> Transcribing -> Matching -> Aligning -> Ready
//                                                \-> CouldntSync
// Ready and CouldntSync are terminal. Paused is an overlay on any non-terminal
// stage (tracked by the scheduler), not a stage of its own — a paused chapter
// keeps its last safe stage so it resumes, never restarts.
enum class Stage {
    Waiting,
    Preparing,
    Transcribing,
    Matching,
    Aligning,
    Ready,
    CouldntSync,
};

// ── Region kind ──────────────────────────────────────────────────────────────
// How a stretch of the chapter relates the audio to the book. Only `Aligned`
// runs carry a trustworthy sentence/word timing; the others are honest gaps that
// must be represented explicitly so nothing guessed is ever painted.
enum class RegionKind {
    Aligned,    // audio time <-> EPUB text, trusted
    BookOnly,   // EPUB text the narration skipped — no audio time
    AudioOnly,  // narration with no matching EPUB text (credits, music) — no highlight
    Uncertain,  // could not be resolved either way — unresolved
};

// ── Failure code ─────────────────────────────────────────────────────────────
// The frozen vocabulary of terminal chapter failures. The store persists the wire
// code; the service owns the approved plain-language copy shown to the reader. The
// coverage/gap gate below produces EditionMismatch when a chapter falls short.
enum class FailureCode {
    None,
    EditionMismatch,    // coverage/gap gate not met — the audio and book editions differ
    ChapterMatchMissing,
    AudioDecodeFailed,
    ModelMissing,
    ModelChecksumFailed,
    EpubIndexFailed,
    AlignmentFailed,
};

// ── Stable wire codes ────────────────────────────────────────────────────────
// These strings are the durable contract: they live in the DB and cross to QML.
// Never re-spell one without a schema migration.

inline QString stageWireCode(Stage s) {
    switch (s) {
        case Stage::Waiting:      return QStringLiteral("waiting");
        case Stage::Preparing:    return QStringLiteral("preparing");
        case Stage::Transcribing: return QStringLiteral("transcribing");
        case Stage::Matching:     return QStringLiteral("matching");
        case Stage::Aligning:     return QStringLiteral("aligning");
        case Stage::Ready:        return QStringLiteral("ready");
        case Stage::CouldntSync:  return QStringLiteral("couldnt_sync");
    }
    return QStringLiteral("waiting");
}

inline Stage stageFromWire(const QString &code) {
    if (code == QLatin1String("preparing"))    return Stage::Preparing;
    if (code == QLatin1String("transcribing")) return Stage::Transcribing;
    if (code == QLatin1String("matching"))     return Stage::Matching;
    if (code == QLatin1String("aligning"))     return Stage::Aligning;
    if (code == QLatin1String("ready"))        return Stage::Ready;
    if (code == QLatin1String("couldnt_sync")) return Stage::CouldntSync;
    return Stage::Waiting;
}

inline bool stageIsTerminal(Stage s) {
    return s == Stage::Ready || s == Stage::CouldntSync;
}

inline QString regionKindWireCode(RegionKind k) {
    switch (k) {
        case RegionKind::Aligned:   return QStringLiteral("aligned");
        case RegionKind::BookOnly:  return QStringLiteral("book_only");
        case RegionKind::AudioOnly: return QStringLiteral("audio_only");
        case RegionKind::Uncertain: return QStringLiteral("uncertain");
    }
    return QStringLiteral("uncertain");
}

inline RegionKind regionKindFromWire(const QString &code) {
    if (code == QLatin1String("aligned"))    return RegionKind::Aligned;
    if (code == QLatin1String("book_only"))  return RegionKind::BookOnly;
    if (code == QLatin1String("audio_only")) return RegionKind::AudioOnly;
    return RegionKind::Uncertain;
}

inline QString failureWireCode(FailureCode c) {
    switch (c) {
        case FailureCode::None:                return QString();
        case FailureCode::EditionMismatch:     return QStringLiteral("edition_mismatch");
        case FailureCode::ChapterMatchMissing: return QStringLiteral("chapter_match_missing");
        case FailureCode::AudioDecodeFailed:   return QStringLiteral("audio_decode_failed");
        case FailureCode::ModelMissing:        return QStringLiteral("model_missing");
        case FailureCode::ModelChecksumFailed: return QStringLiteral("model_checksum_failed");
        case FailureCode::EpubIndexFailed:     return QStringLiteral("epub_index_failed");
        case FailureCode::AlignmentFailed:     return QStringLiteral("alignment_failed");
    }
    return QString();
}

inline FailureCode failureFromWire(const QString &code) {
    if (code == QLatin1String("edition_mismatch"))      return FailureCode::EditionMismatch;
    if (code == QLatin1String("chapter_match_missing")) return FailureCode::ChapterMatchMissing;
    if (code == QLatin1String("audio_decode_failed"))   return FailureCode::AudioDecodeFailed;
    if (code == QLatin1String("model_missing"))         return FailureCode::ModelMissing;
    if (code == QLatin1String("model_checksum_failed")) return FailureCode::ModelChecksumFailed;
    if (code == QLatin1String("epub_index_failed"))     return FailureCode::EpubIndexFailed;
    if (code == QLatin1String("alignment_failed"))      return FailureCode::AlignmentFailed;
    return FailureCode::None;
}

// ── Identity ─────────────────────────────────────────────────────────────────
// What makes a pair's alignment reusable-or-stale. Change any fingerprint or
// engine/model id and the old cues for this pair are invalidated (only this pair).
struct PairIdentity {
    QString pairId;             // stable key for this book↔audiobook pairing
    QString epubFingerprint;    // changes when the EPUB file is replaced
    QString audioFingerprint;   // changes when the audiobook files are replaced
    QString language = QStringLiteral("en");
    QString engineVersion;      // the alignment engine build
    QString coarseModelId;      // whisper.cpp model identity
    QString alignmentModelId;   // ONNX CTC model identity
};

// A point (or span) inside the EPUB's canonical text — spine file + character
// offsets. The stored contract never depends on generated element ids; only on
// the canonical stream the EpubTextIndexer and the paper both reproduce.
struct CanonicalLocation {
    QString spineHref;
    qint64 canonicalStart = 0;
    qint64 canonicalEnd = 0;
};

// ── Cues ─────────────────────────────────────────────────────────────────────
struct WordCue {
    int sentenceOrdinal = 0;    // which sentence in the chapter this word belongs to
    int ordinal = 0;            // word position within its sentence
    qint64 startMs = 0;
    qint64 endMs = 0;
    qint64 canonicalStart = 0;
    qint64 canonicalEnd = 0;
    double confidence = 0.0;
};

struct SentenceCue {
    int ordinal = 0;            // sentence position within the chapter
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString spineHref;
    qint64 canonicalStart = 0;
    qint64 canonicalEnd = 0;
    QString sentenceHash;       // SHA-256 of the canonical sentence — invalidation anchor
    double confidence = 0.0;
    RegionKind regionKind = RegionKind::Aligned;
};

// An explicitly-represented run that is NOT a trusted aligned sentence: a book-only
// skip, an audio-only credit/interlude, or an unresolved uncertain stretch. Audio
// intervals are in ms; book-only regions carry a canonical span and no audio time
// (startMs == endMs).
struct RegionRecord {
    RegionKind kind = RegionKind::Uncertain;
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString spineHref;
    qint64 canonicalStart = -1;
    qint64 canonicalEnd = -1;
};

// The read-along state at a moment of audio time: the active sentence and the
// active word (each present only when a trusted cue contains the time).
struct ActiveCue {
    bool hasSentence = false;
    bool hasWord = false;
    SentenceCue sentence;
    WordCue word;
};

// A chapter's durable job state, read back for status surfaces and scheduling.
struct ChapterStatus {
    bool exists = false;
    int chapterIndex = -1;
    Stage stage = Stage::Waiting;
    qint64 audioStartMs = 0;
    qint64 audioEndMs = 0;
    double coverage = 0.0;
    double confidence = 0.0;
    FailureCode failureCode = FailureCode::None;
    QString failureDetail;
    int priority = 0;
    QByteArray checkpoint;
};

} // namespace alignment
