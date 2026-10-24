#pragma once

#include "../bc1-timed-text-prototype/TimedTextCore.h"

#include <QString>
#include <QUrl>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class AnnotationErrorCode {
    None,
    InvalidAnnotationId,
    InvalidReferent,
    InvalidTextAnchor,
    InvalidSummary,
    InvalidProvenance,
    InvalidBody,
    DuplicateReferentId,
    DuplicateAnnotationId,
    UnknownReferent,
    SourceDocumentMismatch,
    UnknownTimedTextCue,
    RecordingMismatch,
};

struct AnnotationError final {
    AnnotationErrorCode code = AnnotationErrorCode::None;
    QString field;
    QString detail;
};

QString annotationErrorCodeName(AnnotationErrorCode code);

template <typename T>
struct AnnotationResult final {
    std::optional<T> value;
    AnnotationError error;

    bool ok() const noexcept { return value.has_value(); }

    static AnnotationResult success(T value)
    {
        AnnotationResult out;
        out.value = std::move(value);
        return out;
    }

    static AnnotationResult failure(AnnotationErrorCode code,
                                    QString field,
                                    QString detail = QString())
    {
        AnnotationResult out;
        out.error = AnnotationError{code, std::move(field), std::move(detail)};
        return out;
    }
};

QString normalizeAnnotationPlainText(const QString &text);

class AnnotationId final
{
public:
    static AnnotationResult<AnnotationId> create(const QString &value);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const AnnotationId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit AnnotationId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class AnnotationTextAnchor final
{
public:
    static AnnotationResult<AnnotationTextAnchor> create(
        const QString &sourceText,
        const QString &fragment,
        qsizetype occurrence = 0);

    const QString &normalizedText() const noexcept { return m_normalizedText; }
    qsizetype rangeStart() const noexcept { return m_rangeStart; }
    qsizetype rangeLength() const noexcept { return m_rangeLength; }
    QString fragment() const;

    bool operator==(const AnnotationTextAnchor &other) const noexcept
    {
        return m_normalizedText == other.m_normalizedText
            && m_rangeStart == other.m_rangeStart
            && m_rangeLength == other.m_rangeLength;
    }

private:
    AnnotationTextAnchor(QString normalizedText,
                         qsizetype rangeStart,
                         qsizetype rangeLength)
        : m_normalizedText(std::move(normalizedText))
        , m_rangeStart(rangeStart)
        , m_rangeLength(rangeLength)
    {
    }

    QString m_normalizedText;
    qsizetype m_rangeStart = 0;
    qsizetype m_rangeLength = 0;
};

class AnnotationReferentId final
{
public:
    const QString &value() const noexcept { return m_value; }

    bool operator==(const AnnotationReferentId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    friend class AnnotationReferent;

    explicit AnnotationReferentId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class AnnotationReferent final
{
public:
    static AnnotationResult<AnnotationReferent> create(
        RecordingMbid recordingMbid,
        std::optional<TimedTextDocumentId> sourceDocumentId,
        std::optional<TimedTextCueId> cueId,
        std::optional<AnnotationTextAnchor> textAnchor);

    static AnnotationResult<AnnotationReferent> cueLinked(
        const TimedTextDocument &sourceDocument,
        TimedTextCueId cueId,
        std::optional<AnnotationTextAnchor> textAnchor = std::nullopt);

    static AnnotationResult<AnnotationReferent> untimed(
        RecordingMbid recordingMbid,
        AnnotationTextAnchor textAnchor);

    const AnnotationReferentId &id() const noexcept { return m_id; }
    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const std::optional<TimedTextDocumentId> &sourceDocumentId() const noexcept
    {
        return m_sourceDocumentId;
    }
    const std::optional<TimedTextCueId> &cueId() const noexcept { return m_cueId; }
    const std::optional<AnnotationTextAnchor> &textAnchor() const noexcept
    {
        return m_textAnchor;
    }

    AnnotationResult<qsizetype> resolveCueIndexIn(
        const TimedTextDocument &sourceDocument) const;

private:
    static AnnotationReferentId deriveId(
        const RecordingMbid &recordingMbid,
        const std::optional<TimedTextDocumentId> &sourceDocumentId,
        const std::optional<TimedTextCueId> &cueId,
        const std::optional<AnnotationTextAnchor> &textAnchor);

    AnnotationReferent(AnnotationReferentId id,
                       RecordingMbid recordingMbid,
                       std::optional<TimedTextDocumentId> sourceDocumentId,
                       std::optional<TimedTextCueId> cueId,
                       std::optional<AnnotationTextAnchor> textAnchor)
        : m_id(std::move(id))
        , m_recordingMbid(std::move(recordingMbid))
        , m_sourceDocumentId(std::move(sourceDocumentId))
        , m_cueId(std::move(cueId))
        , m_textAnchor(std::move(textAnchor))
    {
    }

    AnnotationReferentId m_id;
    RecordingMbid m_recordingMbid;
    std::optional<TimedTextDocumentId> m_sourceDocumentId;
    std::optional<TimedTextCueId> m_cueId;
    std::optional<AnnotationTextAnchor> m_textAnchor;
};

class AnnotationProvider final
{
public:
    static AnnotationResult<AnnotationProvider> create(
        ProviderId providerId,
        std::optional<ProviderItemId> sourceItemId = std::nullopt,
        std::optional<QUrl> sourceUrl = std::nullopt);

    const ProviderId &providerId() const noexcept { return m_providerId; }
    const std::optional<ProviderItemId> &sourceItemId() const noexcept
    {
        return m_sourceItemId;
    }
    const std::optional<QUrl> &sourceUrl() const noexcept { return m_sourceUrl; }

private:
    AnnotationProvider(ProviderId providerId,
                       std::optional<ProviderItemId> sourceItemId,
                       std::optional<QUrl> sourceUrl)
        : m_providerId(std::move(providerId))
        , m_sourceItemId(std::move(sourceItemId))
        , m_sourceUrl(std::move(sourceUrl))
    {
    }

    ProviderId m_providerId;
    std::optional<ProviderItemId> m_sourceItemId;
    std::optional<QUrl> m_sourceUrl;
};

class AnnotationSummary final
{
public:
    static AnnotationResult<AnnotationSummary> create(
        QString header,
        QString summary = QString());

    const QString &header() const noexcept { return m_header; }
    const QString &summary() const noexcept { return m_summary; }

private:
    AnnotationSummary(QString header, QString summary)
        : m_header(std::move(header))
        , m_summary(std::move(summary))
    {
    }

    QString m_header;
    QString m_summary;
};

enum class AnnotationBodyState {
    NotLoaded,
    Loaded,
    Unavailable,
    Removed,
};

QString annotationBodyStateName(AnnotationBodyState state);

class AnnotationBody final
{
public:
    static AnnotationBody notLoaded();
    static AnnotationResult<AnnotationBody> loadedPlainText(QString plainText);
    static AnnotationBody unavailable();
    static AnnotationBody removed();

    AnnotationBodyState state() const noexcept { return m_state; }
    const QString &plainText() const noexcept { return m_plainText; }

private:
    AnnotationBody(AnnotationBodyState state, QString plainText = QString())
        : m_state(state)
        , m_plainText(std::move(plainText))
    {
    }

    AnnotationBodyState m_state = AnnotationBodyState::NotLoaded;
    QString m_plainText;
};

class Annotation final
{
public:
    Annotation(AnnotationId id,
               RecordingMbid recordingMbid,
               AnnotationReferentId referentId,
               AnnotationProvider provider,
               AnnotationSummary summary,
               AnnotationBody body)
        : m_id(std::move(id))
        , m_recordingMbid(std::move(recordingMbid))
        , m_referentId(std::move(referentId))
        , m_provider(std::move(provider))
        , m_summary(std::move(summary))
        , m_body(std::move(body))
    {
    }

    const AnnotationId &id() const noexcept { return m_id; }
    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const AnnotationReferentId &referentId() const noexcept { return m_referentId; }
    const AnnotationProvider &provider() const noexcept { return m_provider; }
    const AnnotationSummary &summary() const noexcept { return m_summary; }
    const AnnotationBody &body() const noexcept { return m_body; }

private:
    AnnotationId m_id;
    RecordingMbid m_recordingMbid;
    AnnotationReferentId m_referentId;
    AnnotationProvider m_provider;
    AnnotationSummary m_summary;
    AnnotationBody m_body;
};

class AnnotationDocument final
{
public:
    static AnnotationResult<AnnotationDocument> create(
        RecordingMbid recordingMbid,
        QVector<AnnotationReferent> referents,
        QVector<Annotation> annotations);

    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const QVector<AnnotationReferent> &referents() const noexcept { return m_referents; }
    const QVector<Annotation> &annotations() const noexcept { return m_annotations; }

    QVector<Annotation> annotationsFor(
        const AnnotationReferentId &referentId) const;

private:
    AnnotationDocument(RecordingMbid recordingMbid,
                       QVector<AnnotationReferent> referents,
                       QVector<Annotation> annotations)
        : m_recordingMbid(std::move(recordingMbid))
        , m_referents(std::move(referents))
        , m_annotations(std::move(annotations))
    {
    }

    RecordingMbid m_recordingMbid;
    QVector<AnnotationReferent> m_referents;
    QVector<Annotation> m_annotations;
};

} // namespace vinyl
