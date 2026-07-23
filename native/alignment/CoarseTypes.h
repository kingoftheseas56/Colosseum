#pragma once

// CoarseTypes — the plain vocabulary of coarse (discovery) speech recognition.
//
// One small thing: what the native English coarse transcriber (whisper.cpp, Task 9)
// hands the matcher — a timestamped, low-resolution guess at what the narration
// says over a bounded window. It is matching EVIDENCE only: never displayed, never
// inserted into the book, never trusted for word timing. The EPUB remains the sole
// source of displayed words; this is the clue the EpubSequenceMatcher uses to line
// the audio up against that authoritative text.
//
// Shared so both the transcriber (produces) and the matcher (consumes) name the same
// thing. Consumes Qt Core only. Nouns, not verbs.
//
// [Agent 2 (Claude), biblio]

#include <QString>

namespace alignment {

// One coarse recognized speech span: an audio interval and the low-resolution words
// heard in it, with the recognizer's own confidence. Times are milliseconds into the
// audiobook; text is discovery evidence, not display prose.
struct CoarseSegment {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    double confidence = 0.0;
};

} // namespace alignment
