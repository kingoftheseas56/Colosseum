#include "TimedTextTranslation.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;

    qCritical().noquote() << "FAIL:" << message;
    std::exit(1);
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustTimed(const vinyl::TimedTextResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustTranslation(const vinyl::TranslationResult<T> &result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::RecordingMbid recording(const char *text)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(QString::fromLatin1(text)),
        "recording MBID fixture should parse");
}

vinyl::ProviderId provider(const char *text)
{
    return mustDomain(
        vinyl::ProviderId::parse(QString::fromLatin1(text)),
        "provider fixture should parse");
}

vinyl::ProviderItemId providerItem(const char *text)
{
    return mustDomain(
        vinyl::ProviderItemId::parse(QString::fromLatin1(text)),
        "provider item fixture should parse");
}

vinyl::QueueOccurrenceId occurrence(const char *text)
{
    const auto parsed = vinyl::QueueOccurrenceId::parse(QString::fromLatin1(text));
    require(parsed.ok(), "queue occurrence fixture should parse");
    return *parsed.value;
}

vinyl::TimedTextCueId cueId(const char *text)
{
    return mustTimed(
        vinyl::TimedTextCueId::create(QString::fromLatin1(text)),
        "cue ID fixture should parse");
}

vinyl::TranslationLanguage language(const char *text)
{
    return mustTranslation(
        vinyl::TranslationLanguage::create(QString::fromLatin1(text)),
        "translation language fixture should parse");
}

vinyl::TimedTextProvenance lyricProvenance()
{
    return vinyl::TimedTextProvenance(
        provider("lyrics.fixture"),
        std::optional<vinyl::ProviderItemId>{providerItem("lyrics-A")});
}

vinyl::TranslationProvenance translationProvenance(
    const char *providerText = "translate.fixture",
    const char *itemText = "translation-A")
{
    return vinyl::TranslationProvenance(
        provider(providerText),
        std::optional<vinyl::ProviderItemId>{providerItem(itemText)});
}

vinyl::TimedTextDocument synchronizedOriginal(
    const vinyl::RecordingMbid &recordingMbid)
{
    using namespace vinyl;
    QVector<TimedTextCue> cues{
        TimedTextCue(cueId("line-1"), QStringLiteral("first original"), 1000, 2000),
        TimedTextCue(cueId("line-2"), QStringLiteral("second original"), 2000),
        TimedTextCue(cueId("line-3"), QStringLiteral("third original"), 3000, 4000),
    };
    return mustTimed(
        TimedTextDocument::synchronized(
            recordingMbid,
            lyricProvenance(),
            TimedTextKind::LineSynced,
            std::move(cues)),
        "synchronized original fixture should validate");
}

vinyl::TimedTextDocument plainOriginal(const vinyl::RecordingMbid &recordingMbid)
{
    return mustTimed(
        vinyl::TimedTextDocument::plain(
            recordingMbid,
            lyricProvenance(),
            QStringLiteral("first plain line\nsecond plain line")),
        "plain original fixture should validate");
}

vinyl::CueTranslation translated(const char *id, const char *text)
{
    return mustTranslation(
        vinyl::CueTranslation::translated(
            cueId(id),
            QString::fromUtf8(text)),
        "translated cue fixture should validate");
}

} // namespace

static_assert(
    !std::is_constructible<vinyl::CueTranslation,
                           vinyl::TimedTextCueId,
                           QString,
                           qint64>::value,
    "BC2 cue translation must not accept an independent timestamp");

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    const RecordingMbid firstRecording =
        recording("11111111-1111-4111-8111-111111111111");
    const RecordingMbid secondRecording =
        recording("22222222-2222-4222-8222-222222222222");
    const TranslationLanguage spanish = language("es-MX");
    const TimedTextDocument original = synchronizedOriginal(firstRecording);

    // 1. Synchronized translations reuse BC1 cue identity and BC1 timing.
    QVector<CueTranslation> translatedCues{
        translated("line-1", "primero"),
        translated("line-3", "tercero"),
    };
    const TimedTextTranslationDocument translation = mustTranslation(
        TimedTextTranslationDocument::synchronized(
            original,
            spanish,
            translationProvenance(),
            std::move(translatedCues)),
        "synchronized translation should validate");
    const TranslationCueProjection firstProjection = mustTranslation(
        projectTranslationCue(original, translation, 0),
        "first translated cue should project");
    require(firstProjection.cueId() == original.cues().at(0).id(),
            "translation must preserve BC1 cue identity");
    require(firstProjection.startMs() == original.cues().at(0).startMs(),
            "translation must preserve the original BC1 cue start");
    require(firstProjection.effectiveEndMs() == original.effectiveCueEndMs(0),
            "translation must preserve the original BC1 effective cue end");
    require(firstProjection.state() == TranslationCueState::Translated,
            "translated cue should remain explicitly translated");
    require(firstProjection.translatedText().has_value()
                && *firstProjection.translatedText() == QStringLiteral("primero"),
            "translated cue text should survive projection");

    // 2. Partial translation is a valid document.
    require(translation.cueTranslations().size() == 2,
            "a translation document may cover only part of the original cue grid");
    const TranslationCueProjection secondProjection = mustTranslation(
        projectTranslationCue(original, translation, 1),
        "missing middle cue should still project");
    require(secondProjection.state() == TranslationCueState::Missing,
            "an unreturned cue must remain explicitly missing");
    require(!secondProjection.translatedText().has_value(),
            "partial translation must not fabricate translated text");
    require(secondProjection.startMs() == 2000,
            "a missing translation must still ride the original BC1 cue grid");
    require(secondProjection.effectiveEndMs().has_value()
                && *secondProjection.effectiveEndMs() == 3000,
            "derived BC1 cue end must survive translation projection");

    // 3. A completely missing translation is valid and fabricates nothing.
    const TimedTextTranslationDocument emptyTranslation = mustTranslation(
        TimedTextTranslationDocument::synchronized(
            original,
            spanish,
            translationProvenance(),
            {}),
        "empty synchronized translation should represent a valid missing result");
    const TranslationCueProjection emptyProjection = mustTranslation(
        projectTranslationCue(original, emptyTranslation, 0),
        "missing translation should project against the original");
    require(emptyProjection.state() == TranslationCueState::Missing,
            "missing translation must remain missing");
    require(!emptyProjection.translatedText().has_value(),
            "missing translation must never manufacture text");

    // Explicit untranslated is distinguishable from a provider never returning a cue.
    QVector<CueTranslation> untranslatedCue{
        CueTranslation::untranslated(cueId("line-2")),
    };
    const TimedTextTranslationDocument untranslatedDocument = mustTranslation(
        TimedTextTranslationDocument::synchronized(
            original,
            spanish,
            translationProvenance(),
            std::move(untranslatedCue)),
        "explicit untranslated state should validate");
    const TranslationCueProjection untranslatedProjection = mustTranslation(
        projectTranslationCue(original, untranslatedDocument, 1),
        "explicit untranslated cue should project");
    require(untranslatedProjection.state() == TranslationCueState::Untranslated,
            "explicit untranslated state must remain distinct from missing");
    require(!untranslatedProjection.translatedText().has_value(),
            "untranslated cue must not invent replacement text");

    // 4. Translated text cannot introduce a new clock or alter original timing.
    QVector<CueTranslation> reverseOrder{
        translated("line-3", "[999999:99] tercero"),
        translated("line-1", "[00:00.01] primero"),
    };
    const TimedTextTranslationDocument clocklessTranslation = mustTranslation(
        TimedTextTranslationDocument::synchronized(
            original,
            spanish,
            translationProvenance(),
            std::move(reverseOrder)),
        "translation text that resembles timestamps is still only text");
    const TranslationCueProjection thirdProjection = mustTranslation(
        projectTranslationCue(original, clocklessTranslation, 2),
        "third cue should project from BC1 timing");
    require(thirdProjection.startMs() == 3000,
            "timestamp-like translated text must not alter BC1 cue start");
    require(thirdProjection.effectiveEndMs().has_value()
                && *thirdProjection.effectiveEndMs() == 4000,
            "timestamp-like translated text must not alter BC1 cue end");
    require(thirdProjection.translatedText().has_value()
                && *thirdProjection.translatedText()
                    == QStringLiteral("[999999:99] tercero"),
            "timestamp-looking payload remains inert translated text");

    // 5. Translation provider provenance survives without replacing Recording identity.
    const TranslationProvenance providerEvidence = translationProvenance(
        "Translate.Provider",
        "Model-v2:Run-A");
    QVector<CueTranslation> provenanceCues{
        translated("line-1", "primero"),
    };
    const TimedTextTranslationDocument provenanceDocument = mustTranslation(
        TimedTextTranslationDocument::synchronized(
            original,
            spanish,
            providerEvidence,
            std::move(provenanceCues)),
        "provider provenance translation should validate");
    require(provenanceDocument.provenance().providerId().value()
                == QStringLiteral("translate.provider"),
            "translation provider ID normalization must survive");
    require(provenanceDocument.provenance().resultItemId().has_value()
                && provenanceDocument.provenance().resultItemId()->value()
                    == QStringLiteral("Model-v2:Run-A"),
            "opaque translation result provenance must preserve case and punctuation");
    require(provenanceDocument.recordingMbid() == firstRecording,
            "translation provenance must never replace canonical Recording identity");

    // 6. Stale translation work is fenced by occurrence, source document, and translation generation.
    const QueueOccurrenceId occurrenceA = occurrence("queue-occurrence-a");
    const QueueOccurrenceId occurrenceB = occurrence("queue-occurrence-b");
    const TranslationRequestKey requested(
        TimedTextRequestKey(firstRecording, occurrenceA),
        original.documentId(),
        spanish,
        7);
    const TranslationRequestKey sameCurrent(
        TimedTextRequestKey(firstRecording, occurrenceA),
        original.documentId(),
        spanish,
        7);
    const TranslationRequestKey newerTranslationGeneration(
        TimedTextRequestKey(firstRecording, occurrenceA),
        original.documentId(),
        spanish,
        8);
    const TranslationRequestKey newerOccurrence(
        TimedTextRequestKey(firstRecording, occurrenceB),
        original.documentId(),
        spanish,
        7);
    const TranslationRequestKey otherRecording(
        TimedTextRequestKey(secondRecording, occurrenceA),
        original.documentId(),
        spanish,
        7);
    const TranslationRequestKey otherLanguage(
        TimedTextRequestKey(firstRecording, occurrenceA),
        original.documentId(),
        language("fr"),
        7);
    require(canApplyTranslationResult(requested, sameCurrent, translation),
            "matching translation request should be applicable");
    require(!canApplyTranslationResult(
                requested, newerTranslationGeneration, translation),
            "stale translation request generation must be rejected");
    require(!canApplyTranslationResult(requested, newerOccurrence, translation),
            "translation from an old occurrence must be rejected");
    require(!canApplyTranslationResult(requested, otherRecording, translation),
            "translation must not attach to another Recording");
    require(!canApplyTranslationResult(requested, otherLanguage, translation),
            "translation must not attach to another target-language request");

    const TimedTextDocument secondOriginal = synchronizedOriginal(secondRecording);
    QVector<CueTranslation> wrongRecordingCues{
        translated("line-1", "otro"),
    };
    const TimedTextTranslationDocument wrongRecordingDocument = mustTranslation(
        TimedTextTranslationDocument::synchronized(
            secondOriginal,
            spanish,
            translationProvenance(),
            std::move(wrongRecordingCues)),
        "wrong-recording fixture should validate by itself");
    require(!canApplyTranslationResult(
                requested, sameCurrent, wrongRecordingDocument),
            "document Recording mismatch must fail even when request tokens match");

    QVector<TimedTextCue> swappedCues{
        TimedTextCue(cueId("line-1"), QStringLiteral("different first"), 1000, 2000),
        TimedTextCue(cueId("line-2"), QStringLiteral("different second"), 2000),
        TimedTextCue(cueId("line-3"), QStringLiteral("different third"), 3000, 4000),
    };
    const TimedTextDocument swappedOriginal = mustTimed(
        TimedTextDocument::synchronized(
            firstRecording,
            TimedTextProvenance(
                provider("lyrics.other"),
                std::optional<ProviderItemId>{providerItem("lyrics-B")}),
            TimedTextKind::LineSynced,
            std::move(swappedCues)),
        "swapped source document should validate");
    require(swappedOriginal.documentId() != original.documentId(),
            "provider/content swap must create a new source document identity");
    const auto swappedProjection =
        projectTranslationCue(swappedOriginal, translation, 0);
    require(!swappedProjection.ok()
                && swappedProjection.error.code
                    == TranslationErrorCode::SourceDocumentMismatch,
            "translation must not migrate to same-named cue in another lyric document");
    const TranslationRequestKey swappedSourceRequest(
        TimedTextRequestKey(firstRecording, occurrenceA),
        swappedOriginal.documentId(),
        spanish,
        7);
    require(!canApplyTranslationResult(
                 requested, swappedSourceRequest, translation),
            "request ownership must reject a different timed-text document revision");

    // 7. Original lyrics remain independent and available after translation.
    require(original.cues().at(0).text() == QStringLiteral("first original"),
            "BC1 original lyrics must remain unchanged");
    require(firstProjection.originalText() == QStringLiteral("first original"),
            "translation projection must retain original text independently");
    require(firstProjection.translatedText().has_value()
                && *firstProjection.translatedText() != firstProjection.originalText(),
            "translated text must remain a separate enrichment field");

    // 8. Plain lyrics remain untimed after translation.
    const TimedTextDocument plain = plainOriginal(firstRecording);
    const TimedTextTranslationDocument plainTranslation = mustTranslation(
        TimedTextTranslationDocument::plain(
            plain,
            spanish,
            translationProvenance(),
            std::optional<QString>{
                QStringLiteral("primera linea\nsegunda linea")}),
        "plain translation should validate");
    require(plainTranslation.sourceKind() == TimedTextKind::Plain,
            "plain translation must retain plain source kind");
    require(plainTranslation.cueTranslations().isEmpty(),
            "plain translation must not synthesize cue timing");
    require(plainTranslation.plainTranslatedText().has_value(),
            "plain translation may carry translated text");
    require(plain.cues().isEmpty() && plain.providerOffsetMs() == 0,
            "BC1 plain source must remain genuinely untimed");
    require(!currentTimedTextCueIndex(plain, 1500).has_value(),
            "translated plain lyrics must not become synchronized");
    const auto plainProjection = projectTranslationCue(
        plain, plainTranslation, 0);
    require(!plainProjection.ok()
                && plainProjection.error.code
                    == TranslationErrorCode::SourceKindMismatch,
            "plain translation must not expose a timed cue projection");

    const TimedTextTranslationDocument missingPlainTranslation = mustTranslation(
        TimedTextTranslationDocument::plain(
            plain,
            spanish,
            translationProvenance(),
            std::nullopt),
        "missing plain translation should be valid");
    require(!missingPlainTranslation.plainTranslatedText().has_value(),
            "missing plain translation must not fabricate text");

    // Typed validation failures.
    const auto invalidLanguage = TranslationLanguage::create(
        QStringLiteral("es MX"));
    require(!invalidLanguage.ok()
                && invalidLanguage.error.code
                    == TranslationErrorCode::InvalidTargetLanguage,
            "invalid target language must fail with a typed error");

    QVector<CueTranslation> unknownCue{
        translated("not-a-source-cue", "invalido"),
    };
    const auto unknownCueResult = TimedTextTranslationDocument::synchronized(
        original,
        spanish,
        translationProvenance(),
        std::move(unknownCue));
    require(!unknownCueResult.ok()
                && unknownCueResult.error.code
                    == TranslationErrorCode::UnknownCueId,
            "translation may not attach to a cue outside the BC1 source grid");

    QVector<CueTranslation> duplicateCue{
        translated("line-1", "uno"),
        translated("line-1", "primero"),
    };
    const auto duplicateCueResult = TimedTextTranslationDocument::synchronized(
        original,
        spanish,
        translationProvenance(),
        std::move(duplicateCue));
    require(!duplicateCueResult.ok()
                && duplicateCueResult.error.code
                    == TranslationErrorCode::DuplicateCueId,
            "duplicate translated cue identity must fail validation");

    const auto blankTranslation = CueTranslation::translated(
        cueId("line-1"), QStringLiteral("   "));
    require(!blankTranslation.ok()
                && blankTranslation.error.code
                    == TranslationErrorCode::InvalidTranslationText,
            "whitespace-only translated text must fail validation");

    QVector<CueTranslation> invalidKindCues{
        translated("line-1", "primero"),
    };
    const auto invalidKind = TimedTextTranslationDocument::synchronized(
        plain,
        spanish,
        translationProvenance(),
        std::move(invalidKindCues));
    require(!invalidKind.ok()
                && invalidKind.error.code
                    == TranslationErrorCode::SourceKindMismatch,
            "plain BC1 source may not be normalized as synchronized translation");

    require(translationErrorCodeName(TranslationErrorCode::UnknownCueId)
                == QStringLiteral("unknown_cue_id"),
            "translation failures should expose stable names");
    require(translationCueStateName(TranslationCueState::Untranslated)
                == QStringLiteral("untranslated"),
            "translation cue states should expose stable names");

    qInfo().noquote() << "VINYL_BC2_TRANSLATION_OK checks=" << g_checks;
    return 0;
}
