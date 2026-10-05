#pragma once
#include <QStringList>
#include <QVector>
#include <optional>
namespace MediaOverlay {
// Format-neutral timed text. Paths and semantic/structure values are plain strings.
struct Cue {
    QString identity; // Assigned by append; deterministic for the same ordered input.
    QString textPath;
    QString fragmentId;
    QString audioPath;
    qint64 beginMs = 0;
    std::optional<qint64> endMs;
    qsizetype order = -1;
    QStringList structurePath;
    QStringList semanticTypes;
    QString sourceTag = QStringLiteral("epub-media-overlay");
};
class CueTimeline {
public:
    bool append(Cue cue); // Rejects negative or reversed finite audio ranges.
    const QVector<Cue> &cues() const { return m_cues; }
    // Returned pointers remain valid until the next append or destruction.
    const Cue *cueAt(const QString &audioPath, qint64 ms) const;
    const Cue *cueFor(const QString &textPath, const QString &fragmentId) const;
    const Cue *next(const Cue &cue) const;
    const Cue *previous(const Cue &cue) const;
    const Cue *firstCueOf(const QString &textPath) const;
    QVector<const Cue *> cuesOf(const QString &audioPath) const;
private:
    QVector<Cue> m_cues;
    std::optional<qint64> effectiveEnd(qsizetype index) const;
    bool owns(const Cue &cue) const;
};
}
