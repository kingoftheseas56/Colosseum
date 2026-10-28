#include "TimedTextCore.h"

#include <QCryptographicHash>
#include <QSet>

#include <algorithm>
#include <iterator>
#include <limits>

namespace vinyl {
namespace {

qint64 saturatedAdd(qint64 lhs, qint64 rhs) noexcept
{
    const qint64 max = std::numeric_limits<qint64>::max();
    const qint64 min = std::numeric_limits<qint64>::min();
    if (rhs > 0 && lhs > max - rhs)
        return max;
    if (rhs < 0 && lhs < min - rhs)
        return min;
    return lhs + rhs;
}

qint64 saturatedSubtract(qint64 lhs, qint64 rhs) noexcept
{
    const qint64 max = std::numeric_limits<qint64>::max();
    const qint64 min = std::numeric_limits<qint64>::min();
    if (rhs > 0 && lhs < min + rhs)
        return min;
    if (rhs < 0 && lhs > max + rhs)
        return max;
    return lhs - rhs;
}

TimedTextResult<TimedTextDocument> validationFailure(TimedTextErrorCode code,
                                                     QString field,
                                                     QString detail)
{
    return TimedTextResult<TimedTextDocument>::failure(
        code, std::move(field), std::move(detail));
}

void addDocumentIdentityField(QCryptographicHash &hash, const QByteArray &value)
{
    hash.addData(QByteArray::number(value.size()));
    hash.addData(":");
    hash.addData(value);
    hash.addData("|");
}

std::optional<qsizetype> cueIndexAtClock(const TimedTextDocument &document, qint64 lyricClockMs)
{
    if ((document.kind() != TimedTextKind::LineSynced && document.kind() != TimedTextKind::WordSynced)
        || document.cues().isEmpty()) return std::nullopt;
    const auto &cues = document.cues();
    const auto it = std::upper_bound(cues.cbegin(), cues.cend(), lyricClockMs,
        [](qint64 clockMs, const TimedTextCue &cue) { return clockMs < cue.startMs(); });
    if (it == cues.cbegin()) return std::nullopt;
    const qsizetype index = std::distance(cues.cbegin(), it) - 1;
    const auto end = document.effectiveCueEndMs(index);
    if (end && lyricClockMs >= *end) return std::nullopt;
    return index;
}

} // namespace

QString timedTextKindName(TimedTextKind kind)
{
    switch (kind) {
    case TimedTextKind::Unavailable:
        return QStringLiteral("unavailable");
    case TimedTextKind::Plain:
        return QStringLiteral("plain");
    case TimedTextKind::LineSynced:
        return QStringLiteral("line_synced");
    case TimedTextKind::WordSynced:
        return QStringLiteral("word_synced");
    }
    return QStringLiteral("unknown");
}

QString timedTextErrorCodeName(TimedTextErrorCode code)
{
    switch (code) {
    case TimedTextErrorCode::None:
        return QStringLiteral("none");
    case TimedTextErrorCode::InvalidCueId:
        return QStringLiteral("invalid_cue_id");
    case TimedTextErrorCode::InvalidDocumentShape:
        return QStringLiteral("invalid_document_shape");
    case TimedTextErrorCode::InvalidTiming:
        return QStringLiteral("invalid_timing");
    case TimedTextErrorCode::NonMonotonicTiming:
        return QStringLiteral("non_monotonic_timing");
    case TimedTextErrorCode::OverlappingTiming:
        return QStringLiteral("overlapping_timing");
    case TimedTextErrorCode::InvalidWordTiming:
        return QStringLiteral("invalid_word_timing");
    case TimedTextErrorCode::DuplicateCueId:
        return QStringLiteral("duplicate_cue_id");
    }
    return QStringLiteral("unknown");
}

TimedTextResult<TimedTextCueId> TimedTextCueId::create(const QString &value)
{
    if (value.isEmpty() || value.size() > 256 || value.trimmed() != value) {
        return TimedTextResult<TimedTextCueId>::failure(
            TimedTextErrorCode::InvalidCueId,
            QStringLiteral("cueId"),
            QStringLiteral("Cue ID must be non-empty, bounded, and have no surrounding whitespace."));
    }

    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        if (code < 0x20 || code == 0x7f) {
            return TimedTextResult<TimedTextCueId>::failure(
                TimedTextErrorCode::InvalidCueId,
                QStringLiteral("cueId"),
                QStringLiteral("Cue ID must not contain control characters."));
        }
    }

    return TimedTextResult<TimedTextCueId>::success(TimedTextCueId(value));
}

TimedTextDocumentId TimedTextDocument::deriveDocumentId(
    const RecordingMbid &recordingMbid,
    const TimedTextProvenance &provenance,
    TimedTextKind kind,
    qint64 providerOffsetMs,
    const QString &plainText,
    const QVector<TimedTextCue> &cues)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addDocumentIdentityField(hash, QByteArrayLiteral("vinyl-timed-text-v1"));
    addDocumentIdentityField(hash, recordingMbid.toString().toUtf8());
    addDocumentIdentityField(hash, provenance.providerId().value().toUtf8());
    if (provenance.sourceItemId().has_value()) {
        addDocumentIdentityField(hash, QByteArrayLiteral("item"));
        addDocumentIdentityField(hash, provenance.sourceItemId()->value().toUtf8());
    } else {
        addDocumentIdentityField(hash, QByteArrayLiteral("no-item"));
    }
    addDocumentIdentityField(hash, timedTextKindName(kind).toUtf8());
    addDocumentIdentityField(hash, QByteArray::number(providerOffsetMs));
    addDocumentIdentityField(hash, plainText.toUtf8());
    addDocumentIdentityField(hash, QByteArray::number(cues.size()));

    for (const TimedTextCue &cue : cues) {
        addDocumentIdentityField(hash, cue.id().value().toUtf8());
        addDocumentIdentityField(hash, cue.text().toUtf8());
        addDocumentIdentityField(hash, QByteArray::number(cue.startMs()));
        addDocumentIdentityField(
            hash,
            cue.endMs().has_value()
                ? QByteArrayLiteral("end:") + QByteArray::number(*cue.endMs())
                : QByteArrayLiteral("end:none"));
        addDocumentIdentityField(hash, QByteArray::number(cue.words().size()));
        for (const TimedTextWord &word : cue.words()) {
            addDocumentIdentityField(hash, word.text().toUtf8());
            addDocumentIdentityField(hash, QByteArray::number(word.startMs()));
            addDocumentIdentityField(hash, QByteArray::number(word.endMs()));
        }
    }

    return TimedTextDocumentId(
        QStringLiteral("ttd:sha256:")
        + QString::fromLatin1(hash.result().toHex()));
}

TimedTextResult<TimedTextDocument> TimedTextDocument::unavailable(
    RecordingMbid recordingMbid,
    TimedTextProvenance provenance)
{
    const TimedTextDocumentId documentId = deriveDocumentId(
        recordingMbid,
        provenance,
        TimedTextKind::Unavailable,
        0,
        QString(),
        {});
    return TimedTextResult<TimedTextDocument>::success(
        TimedTextDocument(std::move(recordingMbid),
                          documentId,
                          TimedTextKind::Unavailable,
                          std::move(provenance),
                          0,
                          QString(),
                          {}));
}

TimedTextResult<TimedTextDocument> TimedTextDocument::plain(
    RecordingMbid recordingMbid,
    TimedTextProvenance provenance,
    QString text)
{
    if (text.isEmpty()) {
        return validationFailure(
            TimedTextErrorCode::InvalidDocumentShape,
            QStringLiteral("plainText"),
            QStringLiteral("Empty text is unavailable, not a plain timed-text document."));
    }

    const TimedTextDocumentId documentId = deriveDocumentId(
        recordingMbid,
        provenance,
        TimedTextKind::Plain,
        0,
        text,
        {});
    return TimedTextResult<TimedTextDocument>::success(
        TimedTextDocument(std::move(recordingMbid),
                          documentId,
                          TimedTextKind::Plain,
                          std::move(provenance),
                          0,
                          std::move(text),
                          {}));
}

TimedTextResult<TimedTextDocument> TimedTextDocument::synchronized(
    RecordingMbid recordingMbid,
    TimedTextProvenance provenance,
    TimedTextKind kind,
    QVector<TimedTextCue> cues,
    qint64 providerOffsetMs)
{
    if (kind != TimedTextKind::LineSynced && kind != TimedTextKind::WordSynced) {
        return validationFailure(
            TimedTextErrorCode::InvalidDocumentShape,
            QStringLiteral("kind"),
            QStringLiteral("Synchronized documents must be line-synced or word-synced."));
    }
    if (cues.isEmpty()) {
        return validationFailure(
            TimedTextErrorCode::InvalidDocumentShape,
            QStringLiteral("cues"),
            QStringLiteral("Synchronized documents require at least one cue."));
    }

    QSet<QString> cueIds;
    bool hasText = false;
    bool hasWordTiming = false;

    for (qsizetype i = 0; i < cues.size(); ++i) {
        const TimedTextCue &cue = cues.at(i);
        if (cueIds.contains(cue.id().value())) {
            return validationFailure(
                TimedTextErrorCode::DuplicateCueId,
                QStringLiteral("cues[%1].id").arg(i),
                QStringLiteral("Cue identity must be unique within one document."));
        }
        cueIds.insert(cue.id().value());

        if (cue.startMs() < 0) {
            return validationFailure(
                TimedTextErrorCode::InvalidTiming,
                QStringLiteral("cues[%1].startMs").arg(i),
                QStringLiteral("Cue start must be non-negative."));
        }
        if (i > 0 && cue.startMs() <= cues.at(i - 1).startMs()) {
            return validationFailure(
                TimedTextErrorCode::NonMonotonicTiming,
                QStringLiteral("cues[%1].startMs").arg(i),
                QStringLiteral("Cue starts must be strictly increasing; input is not silently re-sorted."));
        }
        if (cue.endMs().has_value() && *cue.endMs() <= cue.startMs()) {
            return validationFailure(
                TimedTextErrorCode::InvalidTiming,
                QStringLiteral("cues[%1].endMs").arg(i),
                QStringLiteral("Explicit cue end must be greater than cue start."));
        }
        if (!cue.text().isEmpty())
            hasText = true;
    }

    for (qsizetype i = 0; i < cues.size(); ++i) {
        const TimedTextCue &cue = cues.at(i);
        const std::optional<qint64> effectiveEnd = [&]() -> std::optional<qint64> {
            if (cue.endMs().has_value())
                return cue.endMs();
            if (i + 1 < cues.size())
                return cues.at(i + 1).startMs();
            return std::nullopt;
        }();

        if (i + 1 < cues.size()
            && cue.endMs().has_value()
            && *cue.endMs() > cues.at(i + 1).startMs()) {
            return validationFailure(
                TimedTextErrorCode::OverlappingTiming,
                QStringLiteral("cues[%1].endMs").arg(i),
                QStringLiteral("A cue may not overlap the next cue."));
        }

        if (kind == TimedTextKind::LineSynced && !cue.words().isEmpty()) {
            return validationFailure(
                TimedTextErrorCode::InvalidDocumentShape,
                QStringLiteral("cues[%1].words").arg(i),
                QStringLiteral("Word timing requires a word-synced document."));
        }

        std::optional<qint64> previousWordEnd;
        for (qsizetype wordIndex = 0; wordIndex < cue.words().size(); ++wordIndex) {
            const TimedTextWord &word = cue.words().at(wordIndex);
            hasWordTiming = true;
            if (!word.text().isEmpty())
                hasText = true;

            if (word.text().isEmpty()
                || word.startMs() < cue.startMs()
                || word.endMs() <= word.startMs()) {
                return validationFailure(
                    TimedTextErrorCode::InvalidWordTiming,
                    QStringLiteral("cues[%1].words[%2]").arg(i).arg(wordIndex),
                    QStringLiteral("Word timing must contain text and a positive interval inside its parent cue."));
            }
            if (effectiveEnd.has_value() && word.endMs() > *effectiveEnd) {
                return validationFailure(
                    TimedTextErrorCode::InvalidWordTiming,
                    QStringLiteral("cues[%1].words[%2].endMs").arg(i).arg(wordIndex),
                    QStringLiteral("Word timing may not extend beyond the parent cue interval."));
            }
            if (previousWordEnd.has_value() && word.startMs() < *previousWordEnd) {
                return validationFailure(
                    TimedTextErrorCode::OverlappingTiming,
                    QStringLiteral("cues[%1].words[%2].startMs").arg(i).arg(wordIndex),
                    QStringLiteral("Word intervals must be monotonic and non-overlapping."));
            }
            previousWordEnd = word.endMs();
        }
    }

    if (kind == TimedTextKind::WordSynced && !hasWordTiming) {
        return validationFailure(
            TimedTextErrorCode::InvalidDocumentShape,
            QStringLiteral("cues"),
            QStringLiteral("Word-synced documents require at least one nested word interval."));
    }
    if (!hasText) {
        return validationFailure(
            TimedTextErrorCode::InvalidDocumentShape,
            QStringLiteral("cues"),
            QStringLiteral("A synchronized document must contain textual content."));
    }

    const TimedTextDocumentId documentId = deriveDocumentId(
        recordingMbid,
        provenance,
        kind,
        providerOffsetMs,
        QString(),
        cues);
    return TimedTextResult<TimedTextDocument>::success(
        TimedTextDocument(std::move(recordingMbid),
                          documentId,
                          kind,
                          std::move(provenance),
                          providerOffsetMs,
                          QString(),
                          std::move(cues)));
}

std::optional<qint64> TimedTextDocument::effectiveCueEndMs(qsizetype index) const
{
    if (index < 0 || index >= m_cues.size())
        return std::nullopt;
    const TimedTextCue &cue = m_cues.at(index);
    if (cue.endMs().has_value())
        return cue.endMs();
    if (index + 1 < m_cues.size())
        return m_cues.at(index + 1).startMs();
    return std::nullopt;
}

qint64 TimedTextOffsets::effectiveOffsetMs() const noexcept
{
    return saturatedAdd(m_providerOffsetMs, m_globalOffsetMs);
}

std::optional<qsizetype> currentTimedTextCueIndex(
    const TimedTextDocument &document,
    qint64 recordingPositionMs,
    qint64 globalOffsetMs)
{
    if (recordingPositionMs < 0) return std::nullopt;
    const TimedTextOffsets offsets(document.providerOffsetMs(), globalOffsetMs);
    const qint64 lyricClockMs = saturatedAdd(recordingPositionMs, offsets.effectiveOffsetMs());
    return cueIndexAtClock(document, lyricClockMs);
}

std::optional<qsizetype> resynchronizeTimedTextCueIndex(
    const TimedTextDocument &document,
    qint64 recordingPositionMs,
    qint64 globalOffsetMs)
{
    return currentTimedTextCueIndex(document, recordingPositionMs, globalOffsetMs);
}

std::optional<qint64> timedTextCueSeekTargetMs(
    const TimedTextDocument &document,
    qsizetype cueIndex,
    qint64 globalOffsetMs)
{
    if (cueIndex < 0 || cueIndex >= document.cues().size())
        return std::nullopt;

    const TimedTextOffsets offsets(document.providerOffsetMs(), globalOffsetMs);
    const qint64 target = saturatedSubtract(
        document.cues().at(cueIndex).startMs(),
        offsets.effectiveOffsetMs());
    return std::max<qint64>(0, target);
}

std::optional<qint64> recordingPositionMsForPlayback(
    qint64 trackRelativePositionMs,
    std::optional<qint64> recordingTimeOffsetMs) noexcept
{
    if (trackRelativePositionMs < 0 || !recordingTimeOffsetMs.has_value())
        return std::nullopt;
    return saturatedAdd(trackRelativePositionMs, *recordingTimeOffsetMs);
}

std::optional<qsizetype> currentTimedTextCueIndexForPlayback(
    const TimedTextDocument &document,
    qint64 trackRelativePositionMs,
    std::optional<qint64> recordingTimeOffsetMs,
    qint64 globalOffsetMs)
{
    const auto recordingPosition = recordingPositionMsForPlayback(
        trackRelativePositionMs, recordingTimeOffsetMs);
    if (!recordingPosition.has_value())
        return std::nullopt;
    // Known source intros may precede recording zero. Apply calibration before
    // checking the lyric interval; a negative mapped recording time is not unknown.
    const TimedTextOffsets offsets(document.providerOffsetMs(), globalOffsetMs);
    return cueIndexAtClock(document, saturatedAdd(*recordingPosition, offsets.effectiveOffsetMs()));
}

std::optional<qint64> timedTextCuePlaybackSeekTargetMs(
    const TimedTextDocument &document,
    qsizetype cueIndex,
    std::optional<qint64> recordingTimeOffsetMs,
    qint64 globalOffsetMs)
{
    if (!recordingTimeOffsetMs.has_value())
        return std::nullopt;
    if (cueIndex < 0 || cueIndex >= document.cues().size())
        return std::nullopt;
    const TimedTextOffsets offsets(document.providerOffsetMs(), globalOffsetMs);
    const qint64 recordingTarget = saturatedSubtract(
        document.cues().at(cueIndex).startMs(), offsets.effectiveOffsetMs());
    const qint64 trackTarget = saturatedSubtract(
        recordingTarget, *recordingTimeOffsetMs);
    return std::max<qint64>(0, trackTarget);
}

bool canApplyTimedTextResult(const TimedTextRequestKey &requested,
                             const TimedTextRequestKey &current,
                             const TimedTextDocument &document) noexcept
{
    return requested == current
        && document.recordingMbid() == requested.recordingMbid();
}

} // namespace vinyl
