#pragma once

#include "../bc1-timed-text-prototype/TimedTextCore.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <optional>
#include <utility>

namespace vinyl {

enum class TranslationErrorCode {
    None,
    InvalidTargetLanguage,
    InvalidDocumentShape,
    SourceKindMismatch,
    SourceDocumentMismatch,
    RecordingMismatch,
    InvalidTranslationText,
    DuplicateCueId,
    UnknownCueId,
};

struct TranslationError final {
    TranslationErrorCode code = TranslationErrorCode::None;
    QString field;
    QString detail;
};

QString translationErrorCodeName(TranslationErrorCode code);

template <typename T>
struct TranslationResult final {
    std::optional<T> value;
    TranslationError error;

    bool ok() const noexcept { return value.has_value(); }

    static TranslationResult success(T value)
    {
        TranslationResult out;
        out.value = std::move(value);
        return out;
    }

    static TranslationResult failure(TranslationErrorCode code,
                                     QString field,
                                     QString detail = QString())
    {
        TranslationResult out;
        out.error = TranslationError{code, std::move(field), std::move(detail)};
        return out;
    }
};

class TranslationLanguage final
{
public:
    static TranslationResult<TranslationLanguage> create(const QString &value);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const TranslationLanguage &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit TranslationLanguage(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class TranslationProvenance final
{
public:
    TranslationProvenance(ProviderId providerId,
                          std::optional<ProviderItemId> resultItemId = std::nullopt)
        : m_providerId(std::move(providerId))
        , m_resultItemId(std::move(resultItemId))
    {
    }

    const ProviderId &providerId() const noexcept { return m_providerId; }
    const std::optional<ProviderItemId> &resultItemId() const noexcept
    {
        return m_resultItemId;
    }

private:
    ProviderId m_providerId;
    std::optional<ProviderItemId> m_resultItemId;
};

enum class TranslationCueState {
    Missing,
    Untranslated,
    Translated,
};

QString translationCueStateName(TranslationCueState state);

class CueTranslation final
{
public:
    static TranslationResult<CueTranslation> translated(TimedTextCueId cueId,
                                                        QString text);
    static CueTranslation untranslated(TimedTextCueId cueId);

    const TimedTextCueId &cueId() const noexcept { return m_cueId; }
    TranslationCueState state() const noexcept { return m_state; }
    const std::optional<QString> &translatedText() const noexcept
    {
        return m_translatedText;
    }

private:
    CueTranslation(TimedTextCueId cueId,
                   TranslationCueState state,
                   std::optional<QString> translatedText)
        : m_cueId(std::move(cueId))
        , m_state(state)
        , m_translatedText(std::move(translatedText))
    {
    }

    TimedTextCueId m_cueId;
    TranslationCueState m_state = TranslationCueState::Missing;
    std::optional<QString> m_translatedText;
};

class TimedTextTranslationDocument final
{
public:
    static TranslationResult<TimedTextTranslationDocument> plain(
        const TimedTextDocument &original,
        TranslationLanguage targetLanguage,
        TranslationProvenance provenance,
        std::optional<QString> translatedText);

    static TranslationResult<TimedTextTranslationDocument> synchronized(
        const TimedTextDocument &original,
        TranslationLanguage targetLanguage,
        TranslationProvenance provenance,
        QVector<CueTranslation> cueTranslations);

    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const TimedTextDocumentId &sourceDocumentId() const noexcept { return m_sourceDocumentId; }
    TimedTextKind sourceKind() const noexcept { return m_sourceKind; }
    const TranslationLanguage &targetLanguage() const noexcept
    {
        return m_targetLanguage;
    }
    const TranslationProvenance &provenance() const noexcept
    {
        return m_provenance;
    }
    const std::optional<QString> &plainTranslatedText() const noexcept
    {
        return m_plainTranslatedText;
    }
    const QVector<CueTranslation> &cueTranslations() const noexcept
    {
        return m_cueTranslations;
    }

private:
    TimedTextTranslationDocument(
        RecordingMbid recordingMbid,
        TimedTextDocumentId sourceDocumentId,
        TimedTextKind sourceKind,
        TranslationLanguage targetLanguage,
        TranslationProvenance provenance,
        std::optional<QString> plainTranslatedText,
        QVector<CueTranslation> cueTranslations)
        : m_recordingMbid(std::move(recordingMbid))
        , m_sourceDocumentId(std::move(sourceDocumentId))
        , m_sourceKind(sourceKind)
        , m_targetLanguage(std::move(targetLanguage))
        , m_provenance(std::move(provenance))
        , m_plainTranslatedText(std::move(plainTranslatedText))
        , m_cueTranslations(std::move(cueTranslations))
    {
    }

    RecordingMbid m_recordingMbid;
    TimedTextDocumentId m_sourceDocumentId;
    TimedTextKind m_sourceKind = TimedTextKind::Unavailable;
    TranslationLanguage m_targetLanguage;
    TranslationProvenance m_provenance;
    std::optional<QString> m_plainTranslatedText;
    QVector<CueTranslation> m_cueTranslations;
};

class TranslationCueProjection final
{
public:
    const TimedTextCueId &cueId() const noexcept { return m_cueId; }
    const QString &originalText() const noexcept { return m_originalText; }
    TranslationCueState state() const noexcept { return m_state; }
    const std::optional<QString> &translatedText() const noexcept
    {
        return m_translatedText;
    }
    qint64 startMs() const noexcept { return m_startMs; }
    const std::optional<qint64> &effectiveEndMs() const noexcept
    {
        return m_effectiveEndMs;
    }

private:
    friend TranslationResult<TranslationCueProjection> projectTranslationCue(
        const TimedTextDocument &original,
        const TimedTextTranslationDocument &translation,
        qsizetype cueIndex);

    TranslationCueProjection(TimedTextCueId cueId,
                             QString originalText,
                             TranslationCueState state,
                             std::optional<QString> translatedText,
                             qint64 startMs,
                             std::optional<qint64> effectiveEndMs)
        : m_cueId(std::move(cueId))
        , m_originalText(std::move(originalText))
        , m_state(state)
        , m_translatedText(std::move(translatedText))
        , m_startMs(startMs)
        , m_effectiveEndMs(effectiveEndMs)
    {
    }

    TimedTextCueId m_cueId;
    QString m_originalText;
    TranslationCueState m_state = TranslationCueState::Missing;
    std::optional<QString> m_translatedText;
    qint64 m_startMs = 0;
    std::optional<qint64> m_effectiveEndMs;
};

TranslationResult<TranslationCueProjection> projectTranslationCue(
    const TimedTextDocument &original,
    const TimedTextTranslationDocument &translation,
    qsizetype cueIndex);

class TranslationRequestKey final
{
public:
    TranslationRequestKey(TimedTextRequestKey sourceRequest,
                          TimedTextDocumentId sourceDocumentId,
                          TranslationLanguage targetLanguage,
                          quint64 requestGeneration)
        : m_sourceRequest(std::move(sourceRequest))
        , m_sourceDocumentId(std::move(sourceDocumentId))
        , m_targetLanguage(std::move(targetLanguage))
        , m_requestGeneration(requestGeneration)
    {
    }

    const TimedTextRequestKey &sourceRequest() const noexcept
    {
        return m_sourceRequest;
    }
    const TimedTextDocumentId &sourceDocumentId() const noexcept
    {
        return m_sourceDocumentId;
    }
    const TranslationLanguage &targetLanguage() const noexcept
    {
        return m_targetLanguage;
    }
    quint64 requestGeneration() const noexcept { return m_requestGeneration; }

    bool operator==(const TranslationRequestKey &other) const noexcept
    {
        return m_sourceRequest == other.m_sourceRequest
            && m_sourceDocumentId == other.m_sourceDocumentId
            && m_targetLanguage == other.m_targetLanguage
            && m_requestGeneration == other.m_requestGeneration;
    }

private:
    TimedTextRequestKey m_sourceRequest;
    TimedTextDocumentId m_sourceDocumentId;
    TranslationLanguage m_targetLanguage;
    quint64 m_requestGeneration = 0;
};

bool canApplyTranslationResult(const TranslationRequestKey &requested,
                               const TranslationRequestKey &current,
                               const TimedTextTranslationDocument &document) noexcept;

} // namespace vinyl
