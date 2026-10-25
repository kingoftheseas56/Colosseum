#pragma once

#include "../ba0-domain-prototype/VinylDomainTypes.h"
#include "../ba1-queue-core-prototype/VinylQueue.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <optional>
#include <utility>

namespace vinyl {

enum class TimedTextKind {
    Unavailable,
    Plain,
    LineSynced,
    WordSynced,
};

QString timedTextKindName(TimedTextKind kind);

enum class TimedTextErrorCode {
    None,
    InvalidCueId,
    InvalidDocumentShape,
    InvalidTiming,
    NonMonotonicTiming,
    OverlappingTiming,
    InvalidWordTiming,
    DuplicateCueId,
};

struct TimedTextError final {
    TimedTextErrorCode code = TimedTextErrorCode::None;
    QString field;
    QString detail;
};

QString timedTextErrorCodeName(TimedTextErrorCode code);

template <typename T>
struct TimedTextResult final {
    std::optional<T> value;
    TimedTextError error;

    bool ok() const noexcept { return value.has_value(); }

    static TimedTextResult success(T value)
    {
        TimedTextResult out;
        out.value = std::move(value);
        return out;
    }

    static TimedTextResult failure(TimedTextErrorCode code,
                                   QString field,
                                   QString detail = QString())
    {
        TimedTextResult out;
        out.error = TimedTextError{code, std::move(field), std::move(detail)};
        return out;
    }
};

class TimedTextDocumentId final
{
public:
    const QString &value() const noexcept { return m_value; }

    bool operator==(const TimedTextDocumentId &other) const noexcept
    {
        return m_value == other.m_value;
    }

    bool operator!=(const TimedTextDocumentId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    friend class TimedTextDocument;

    explicit TimedTextDocumentId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class TimedTextCueId final
{
public:
    static TimedTextResult<TimedTextCueId> create(const QString &value);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const TimedTextCueId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit TimedTextCueId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class TimedTextProvenance final
{
public:
    TimedTextProvenance(ProviderId providerId,
                        std::optional<ProviderItemId> sourceItemId = std::nullopt)
        : m_providerId(std::move(providerId))
        , m_sourceItemId(std::move(sourceItemId))
    {
    }

    const ProviderId &providerId() const noexcept { return m_providerId; }
    const std::optional<ProviderItemId> &sourceItemId() const noexcept
    {
        return m_sourceItemId;
    }

private:
    ProviderId m_providerId;
    std::optional<ProviderItemId> m_sourceItemId;
};

class TimedTextWord final
{
public:
    TimedTextWord(QString text, qint64 startMs, qint64 endMs)
        : m_text(std::move(text))
        , m_startMs(startMs)
        , m_endMs(endMs)
    {
    }

    const QString &text() const noexcept { return m_text; }
    qint64 startMs() const noexcept { return m_startMs; }
    qint64 endMs() const noexcept { return m_endMs; }

private:
    QString m_text;
    qint64 m_startMs = 0;
    qint64 m_endMs = 0;
};

class TimedTextCue final
{
public:
    TimedTextCue(TimedTextCueId id,
                 QString text,
                 qint64 startMs,
                 std::optional<qint64> endMs = std::nullopt,
                 QVector<TimedTextWord> words = {})
        : m_id(std::move(id))
        , m_text(std::move(text))
        , m_startMs(startMs)
        , m_endMs(endMs)
        , m_words(std::move(words))
    {
    }

    const TimedTextCueId &id() const noexcept { return m_id; }
    const QString &text() const noexcept { return m_text; }
    qint64 startMs() const noexcept { return m_startMs; }
    const std::optional<qint64> &endMs() const noexcept { return m_endMs; }
    const QVector<TimedTextWord> &words() const noexcept { return m_words; }

private:
    TimedTextCueId m_id;
    QString m_text;
    qint64 m_startMs = 0;
    std::optional<qint64> m_endMs;
    QVector<TimedTextWord> m_words;
};

class TimedTextDocument final
{
public:
    static TimedTextResult<TimedTextDocument> unavailable(
        RecordingMbid recordingMbid,
        TimedTextProvenance provenance);

    static TimedTextResult<TimedTextDocument> plain(
        RecordingMbid recordingMbid,
        TimedTextProvenance provenance,
        QString text);

    static TimedTextResult<TimedTextDocument> synchronized(
        RecordingMbid recordingMbid,
        TimedTextProvenance provenance,
        TimedTextKind kind,
        QVector<TimedTextCue> cues,
        qint64 providerOffsetMs = 0);

    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const TimedTextDocumentId &documentId() const noexcept { return m_documentId; }
    TimedTextKind kind() const noexcept { return m_kind; }
    const TimedTextProvenance &provenance() const noexcept { return m_provenance; }
    qint64 providerOffsetMs() const noexcept { return m_providerOffsetMs; }
    const QString &plainText() const noexcept { return m_plainText; }
    const QVector<TimedTextCue> &cues() const noexcept { return m_cues; }

    std::optional<qint64> effectiveCueEndMs(qsizetype index) const;

private:
    static TimedTextDocumentId deriveDocumentId(
        const RecordingMbid &recordingMbid,
        const TimedTextProvenance &provenance,
        TimedTextKind kind,
        qint64 providerOffsetMs,
        const QString &plainText,
        const QVector<TimedTextCue> &cues);

    TimedTextDocument(RecordingMbid recordingMbid,
                      TimedTextDocumentId documentId,
                      TimedTextKind kind,
                      TimedTextProvenance provenance,
                      qint64 providerOffsetMs,
                      QString plainText,
                      QVector<TimedTextCue> cues)
        : m_recordingMbid(std::move(recordingMbid))
        , m_documentId(std::move(documentId))
        , m_kind(kind)
        , m_provenance(std::move(provenance))
        , m_providerOffsetMs(providerOffsetMs)
        , m_plainText(std::move(plainText))
        , m_cues(std::move(cues))
    {
    }

    RecordingMbid m_recordingMbid;
    TimedTextDocumentId m_documentId;
    TimedTextKind m_kind = TimedTextKind::Unavailable;
    TimedTextProvenance m_provenance;
    qint64 m_providerOffsetMs = 0;
    QString m_plainText;
    QVector<TimedTextCue> m_cues;
};

class TimedTextOffsets final
{
public:
    TimedTextOffsets(qint64 providerOffsetMs, qint64 globalOffsetMs)
        : m_providerOffsetMs(providerOffsetMs)
        , m_globalOffsetMs(globalOffsetMs)
    {
    }

    qint64 providerOffsetMs() const noexcept { return m_providerOffsetMs; }
    qint64 globalOffsetMs() const noexcept { return m_globalOffsetMs; }
    qint64 effectiveOffsetMs() const noexcept;

private:
    qint64 m_providerOffsetMs = 0;
    qint64 m_globalOffsetMs = 0;
};

std::optional<qsizetype> currentTimedTextCueIndex(
    const TimedTextDocument &document,
    qint64 recordingPositionMs,
    qint64 globalOffsetMs = 0);

std::optional<qsizetype> resynchronizeTimedTextCueIndex(
    const TimedTextDocument &document,
    qint64 recordingPositionMs,
    qint64 globalOffsetMs = 0);

std::optional<qint64> timedTextCueSeekTargetMs(
    const TimedTextDocument &document,
    qsizetype cueIndex,
    qint64 globalOffsetMs = 0);

std::optional<qint64> recordingPositionMsForPlayback(
    qint64 trackRelativePositionMs,
    std::optional<qint64> recordingTimeOffsetMs) noexcept;

std::optional<qsizetype> currentTimedTextCueIndexForPlayback(
    const TimedTextDocument &document,
    qint64 trackRelativePositionMs,
    std::optional<qint64> recordingTimeOffsetMs,
    qint64 globalOffsetMs = 0);

std::optional<qint64> timedTextCuePlaybackSeekTargetMs(
    const TimedTextDocument &document,
    qsizetype cueIndex,
    std::optional<qint64> recordingTimeOffsetMs,
    qint64 globalOffsetMs = 0);

class TimedTextRequestKey final
{
public:
    TimedTextRequestKey(RecordingMbid recordingMbid, QueueOccurrenceId occurrenceId)
        : m_recordingMbid(std::move(recordingMbid))
        , m_occurrenceId(std::move(occurrenceId))
    {
    }

    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const QueueOccurrenceId &occurrenceId() const noexcept { return m_occurrenceId; }

    bool operator==(const TimedTextRequestKey &other) const noexcept
    {
        return m_recordingMbid == other.m_recordingMbid
            && m_occurrenceId == other.m_occurrenceId;
    }

private:
    RecordingMbid m_recordingMbid;
    QueueOccurrenceId m_occurrenceId;
};

bool canApplyTimedTextResult(const TimedTextRequestKey &requested,
                             const TimedTextRequestKey &current,
                             const TimedTextDocument &document) noexcept;

} // namespace vinyl
