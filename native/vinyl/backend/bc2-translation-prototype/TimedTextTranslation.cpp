#include "TimedTextTranslation.h"

#include <QSet>

#include <algorithm>

namespace vinyl {
namespace {

bool isAsciiAlphaNumeric(QChar ch) noexcept
{
    const ushort value = ch.unicode();
    return (value >= 'a' && value <= 'z')
        || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9');
}

bool isSynchronizedKind(TimedTextKind kind) noexcept
{
    return kind == TimedTextKind::LineSynced
        || kind == TimedTextKind::WordSynced;
}

bool isMeaningfulText(const QString &text) noexcept
{
    return !text.trimmed().isEmpty();
}

template <typename T>
TranslationResult<T> failure(TranslationErrorCode code,
                             QString field,
                             QString detail)
{
    return TranslationResult<T>::failure(
        code, std::move(field), std::move(detail));
}

} // namespace

QString translationErrorCodeName(TranslationErrorCode code)
{
    switch (code) {
    case TranslationErrorCode::None:
        return QStringLiteral("none");
    case TranslationErrorCode::InvalidTargetLanguage:
        return QStringLiteral("invalid_target_language");
    case TranslationErrorCode::InvalidDocumentShape:
        return QStringLiteral("invalid_document_shape");
    case TranslationErrorCode::SourceKindMismatch:
        return QStringLiteral("source_kind_mismatch");
    case TranslationErrorCode::SourceDocumentMismatch:
        return QStringLiteral("source_document_mismatch");
    case TranslationErrorCode::RecordingMismatch:
        return QStringLiteral("recording_mismatch");
    case TranslationErrorCode::InvalidTranslationText:
        return QStringLiteral("invalid_translation_text");
    case TranslationErrorCode::DuplicateCueId:
        return QStringLiteral("duplicate_cue_id");
    case TranslationErrorCode::UnknownCueId:
        return QStringLiteral("unknown_cue_id");
    }
    return QStringLiteral("unknown");
}

QString translationCueStateName(TranslationCueState state)
{
    switch (state) {
    case TranslationCueState::Missing:
        return QStringLiteral("missing");
    case TranslationCueState::Untranslated:
        return QStringLiteral("untranslated");
    case TranslationCueState::Translated:
        return QStringLiteral("translated");
    }
    return QStringLiteral("unknown");
}

TranslationResult<TranslationLanguage> TranslationLanguage::create(
    const QString &value)
{
    if (value.isEmpty()
        || value.size() > 63
        || value.trimmed() != value) {
        return failure<TranslationLanguage>(
            TranslationErrorCode::InvalidTargetLanguage,
            QStringLiteral("targetLanguage"),
            QStringLiteral("Target language must be non-empty, bounded, and have no surrounding whitespace."));
    }

    const QStringList subtags = value.split(QLatin1Char('-'), Qt::KeepEmptyParts);
    for (const QString &subtag : subtags) {
        if (subtag.isEmpty() || subtag.size() > 8) {
            return failure<TranslationLanguage>(
                TranslationErrorCode::InvalidTargetLanguage,
                QStringLiteral("targetLanguage"),
                QStringLiteral("Target language must use non-empty BCP47-shaped subtags of at most eight ASCII alphanumeric characters."));
        }
        for (const QChar ch : subtag) {
            if (!isAsciiAlphaNumeric(ch)) {
                return failure<TranslationLanguage>(
                    TranslationErrorCode::InvalidTargetLanguage,
                    QStringLiteral("targetLanguage"),
                    QStringLiteral("Target language may contain only ASCII alphanumeric subtags separated by hyphens."));
            }
        }
    }

    return TranslationResult<TranslationLanguage>::success(
        TranslationLanguage(value.toLower()));
}

TranslationResult<CueTranslation> CueTranslation::translated(
    TimedTextCueId cueId,
    QString text)
{
    if (!isMeaningfulText(text)) {
        return failure<CueTranslation>(
            TranslationErrorCode::InvalidTranslationText,
            QStringLiteral("translatedText"),
            QStringLiteral("Translated cue text must contain non-whitespace content."));
    }

    return TranslationResult<CueTranslation>::success(
        CueTranslation(std::move(cueId),
                       TranslationCueState::Translated,
                       std::optional<QString>{std::move(text)}));
}

CueTranslation CueTranslation::untranslated(TimedTextCueId cueId)
{
    return CueTranslation(std::move(cueId),
                          TranslationCueState::Untranslated,
                          std::nullopt);
}

TranslationResult<TimedTextTranslationDocument>
TimedTextTranslationDocument::plain(
    const TimedTextDocument &original,
    TranslationLanguage targetLanguage,
    TranslationProvenance provenance,
    std::optional<QString> translatedText)
{
    if (!original.recordingMbid() || original.kind() != TimedTextKind::Plain) {
        return failure<TimedTextTranslationDocument>(
            TranslationErrorCode::SourceKindMismatch,
            QStringLiteral("sourceKind"),
            QStringLiteral("Plain translation requires a BC1 plain-text source document."));
    }
    if (translatedText.has_value() && !isMeaningfulText(*translatedText)) {
        return failure<TimedTextTranslationDocument>(
            TranslationErrorCode::InvalidTranslationText,
            QStringLiteral("plainTranslatedText"),
            QStringLiteral("A present plain translation must contain non-whitespace content."));
    }

    return TranslationResult<TimedTextTranslationDocument>::success(
        TimedTextTranslationDocument(
            *original.recordingMbid(),
            original.documentId(),
            original.kind(),
            std::move(targetLanguage),
            std::move(provenance),
            std::move(translatedText),
            {}));
}

TranslationResult<TimedTextTranslationDocument>
TimedTextTranslationDocument::synchronized(
    const TimedTextDocument &original,
    TranslationLanguage targetLanguage,
    TranslationProvenance provenance,
    QVector<CueTranslation> cueTranslations)
{
    if (!original.recordingMbid() || !isSynchronizedKind(original.kind())) {
        return failure<TimedTextTranslationDocument>(
            TranslationErrorCode::SourceKindMismatch,
            QStringLiteral("sourceKind"),
            QStringLiteral("Synchronized translation requires a BC1 line-synced or word-synced source document."));
    }

    QSet<QString> knownCueIds;
    for (const TimedTextCue &cue : original.cues())
        knownCueIds.insert(cue.id().value());

    QSet<QString> seenCueIds;
    for (qsizetype index = 0; index < cueTranslations.size(); ++index) {
        const CueTranslation &entry = cueTranslations.at(index);
        const QString id = entry.cueId().value();
        if (!knownCueIds.contains(id)) {
            return failure<TimedTextTranslationDocument>(
                TranslationErrorCode::UnknownCueId,
                QStringLiteral("cueTranslations[%1].cueId").arg(index),
                QStringLiteral("Translation entries may reference only cue IDs owned by the BC1 source document."));
        }
        if (seenCueIds.contains(id)) {
            return failure<TimedTextTranslationDocument>(
                TranslationErrorCode::DuplicateCueId,
                QStringLiteral("cueTranslations[%1].cueId").arg(index),
                QStringLiteral("One translation document may contain at most one entry for each BC1 cue ID."));
        }
        seenCueIds.insert(id);
    }

    return TranslationResult<TimedTextTranslationDocument>::success(
        TimedTextTranslationDocument(
            *original.recordingMbid(),
            original.documentId(),
            original.kind(),
            std::move(targetLanguage),
            std::move(provenance),
            std::nullopt,
            std::move(cueTranslations)));
}

TranslationResult<TranslationCueProjection> projectTranslationCue(
    const TimedTextDocument &original,
    const TimedTextTranslationDocument &translation,
    qsizetype cueIndex)
{
    if (original.recordingMbid() != translation.recordingMbid()) {
        return failure<TranslationCueProjection>(
            TranslationErrorCode::RecordingMismatch,
            QStringLiteral("recordingMbid"),
            QStringLiteral("Translation and original timed text must refer to the same Recording MBID."));
    }
    if (original.documentId() != translation.sourceDocumentId()) {
        return failure<TranslationCueProjection>(
            TranslationErrorCode::SourceDocumentMismatch,
            QStringLiteral("sourceDocumentId"),
            QStringLiteral("Translation belongs to a different timed-text document revision."));
    }
    if (original.kind() != translation.sourceKind()
        || !isSynchronizedKind(original.kind())) {
        return failure<TranslationCueProjection>(
            TranslationErrorCode::SourceKindMismatch,
            QStringLiteral("sourceKind"),
            QStringLiteral("Cue projection requires the synchronized BC1 source kind used to create the translation."));
    }
    if (cueIndex < 0 || cueIndex >= original.cues().size()) {
        return failure<TranslationCueProjection>(
            TranslationErrorCode::InvalidDocumentShape,
            QStringLiteral("cueIndex"),
            QStringLiteral("Cue projection index is outside the BC1 source document."));
    }

    const TimedTextCue &sourceCue = original.cues().at(cueIndex);
    TranslationCueState state = TranslationCueState::Missing;
    std::optional<QString> translatedText;

    const auto it = std::find_if(
        translation.cueTranslations().cbegin(),
        translation.cueTranslations().cend(),
        [&sourceCue](const CueTranslation &candidate) {
            return candidate.cueId() == sourceCue.id();
        });
    if (it != translation.cueTranslations().cend()) {
        state = it->state();
        translatedText = it->translatedText();
    }

    return TranslationResult<TranslationCueProjection>::success(
        TranslationCueProjection(
            sourceCue.id(),
            sourceCue.text(),
            state,
            std::move(translatedText),
            sourceCue.startMs(),
            original.effectiveCueEndMs(cueIndex)));
}

bool canApplyTranslationResult(
    const TranslationRequestKey &requested,
    const TranslationRequestKey &current,
    const TimedTextTranslationDocument &document) noexcept
{
    return requested == current
        && document.recordingMbid() == requested.sourceRequest().recordingMbid()
        && document.sourceDocumentId() == requested.sourceDocumentId()
        && document.targetLanguage() == requested.targetLanguage();
}

} // namespace vinyl
