#include "AnnotationMeaningPlane.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <cstdio>
#include <optional>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;

    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

template <typename T>
T mustDomain(const vinyl::DomainResult<T> &result,
             const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustTimed(const vinyl::TimedTextResult<T> &result,
            const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustAnnotation(const vinyl::AnnotationResult<T> &result,
                 const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

vinyl::RecordingMbid recording(const char *text)
{
    return mustDomain(
        vinyl::RecordingMbid::parse(
            QString::fromLatin1(text)),
        "recording MBID fixture should parse");
}

vinyl::ProviderId provider(const char *text)
{
    return mustDomain(
        vinyl::ProviderId::parse(
            QString::fromLatin1(text)),
        "provider fixture should parse");
}

vinyl::ProviderItemId providerItem(const char *text)
{
    return mustDomain(
        vinyl::ProviderItemId::parse(
            QString::fromLatin1(text)),
        "provider item fixture should parse");
}

vinyl::TimedTextCueId cueId(const char *text)
{
    return mustTimed(
        vinyl::TimedTextCueId::create(
            QString::fromLatin1(text)),
        "cue ID fixture should parse");
}

vinyl::AnnotationId annotationId(const char *text)
{
    return mustAnnotation(
        vinyl::AnnotationId::create(
            QString::fromLatin1(text)),
        "annotation ID fixture should parse");
}

vinyl::AnnotationSummary summary(const char *header,
                                 const char *text = "")
{
    return mustAnnotation(
        vinyl::AnnotationSummary::create(
            QString::fromLatin1(header),
            QString::fromLatin1(text)),
        "annotation summary fixture should validate");
}

vinyl::AnnotationProvider geniusProvider(
    const char *providerText = "Genius.Live",
    const char *itemText = "Referent-42")
{
    return mustAnnotation(
        vinyl::AnnotationProvider::create(
            provider(providerText),
            std::optional<vinyl::ProviderItemId>{
                providerItem(itemText)},
            std::optional<QUrl>{
                QUrl(QStringLiteral(
                    "https://genius.com/annotations/42"))}),
        "annotation provider fixture should validate");
}

vinyl::TimedTextDocument lyricDocument(
    const vinyl::RecordingMbid &recordingMbid,
    const char *providerText = "lyrics.fixture",
    const char *itemText = "lyrics-A",
    const char *chorusText = "chorus text")
{
    using namespace vinyl;
    QVector<TimedTextCue> cues{
        TimedTextCue(
            cueId("verse-1"),
            QStringLiteral("verse text"),
            1000,
            2000),
        TimedTextCue(
            cueId("chorus-1"),
            QString::fromUtf8(chorusText),
            2000,
            3000),
    };
    return mustTimed(
        TimedTextDocument::synchronized(
            recordingMbid,
            TimedTextProvenance(
                provider(providerText),
                std::optional<ProviderItemId>{providerItem(itemText)}),
            TimedTextKind::LineSynced,
            std::move(cues)),
        "lyric document fixture should validate");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    const RecordingMbid firstRecording =
        recording(
            "11111111-1111-4111-8111-111111111111");
    const RecordingMbid otherRecording =
        recording(
            "22222222-2222-4222-8222-222222222222");

    // 1. Provider text normalization preserves a stable referent anchor.
    const AnnotationTextAnchor noisyAnchor =
        mustAnnotation(
            AnnotationTextAnchor::create(
                QStringLiteral(
                    "  We   shine\nbright tonight  "),
                QStringLiteral("shine   bright")),
            "noisy provider text should normalize");
    const AnnotationTextAnchor normalizedAnchor =
        mustAnnotation(
            AnnotationTextAnchor::create(
                QStringLiteral(
                    "We shine bright tonight"),
                QStringLiteral("shine bright")),
            "already-normalized provider text should validate");

    const AnnotationReferent noisyReferent =
        mustAnnotation(
            AnnotationReferent::untimed(
                firstRecording,
                noisyAnchor),
            "noisy untimed referent should validate");
    const AnnotationReferent normalizedReferent =
        mustAnnotation(
            AnnotationReferent::untimed(
                firstRecording,
                normalizedAnchor),
            "normalized untimed referent should validate");

    require(
        noisyReferent.id() == normalizedReferent.id(),
        "provider text normalization must preserve deterministic referent identity");
    require(
        noisyReferent.textAnchor()->fragment()
            == QStringLiteral("shine bright"),
        "normalized referent must retain the target fragment");

    // 2. Timed annotations bind to exact BC1 document + cue identity, not BC3 timestamps.
    const TimedTextDocument sourceLyrics = lyricDocument(firstRecording);
    const AnnotationReferent cueReferent =
        mustAnnotation(
            AnnotationReferent::cueLinked(
                sourceLyrics,
                cueId("chorus-1")),
            "cue-linked referent should validate");

    require(
        cueReferent.cueId().has_value()
            && cueReferent.cueId()->value()
                == QStringLiteral("chorus-1"),
        "cue-linked annotation must preserve BC1 TimedTextCueId");
    require(
        cueReferent.sourceDocumentId().has_value()
            && *cueReferent.sourceDocumentId() == sourceLyrics.documentId(),
        "cue-linked annotation must preserve the exact timed-text document identity");
    require(
        !cueReferent.textAnchor().has_value(),
        "whole-cue referent should not require invented text or timing data");
    require(
        cueReferent.resolveCueIndexIn(sourceLyrics).value
            == std::optional<qsizetype>{1},
        "cue-linked annotation should resolve inside its exact source document");

    const TimedTextDocument swappedLyrics = lyricDocument(
        firstRecording,
        "lyrics.other",
        "lyrics-B",
        "different chorus text");
    require(
        swappedLyrics.documentId() != sourceLyrics.documentId(),
        "provider/document swap must produce a distinct timed-text document identity");
    const auto mismatchedCue = cueReferent.resolveCueIndexIn(swappedLyrics);
    require(
        !mismatchedCue.ok()
            && mismatchedCue.error.code
                == AnnotationErrorCode::SourceDocumentMismatch,
        "same cue ID in another lyric document must not inherit annotations");

    const auto unknownCueReferent = AnnotationReferent::cueLinked(
        sourceLyrics,
        cueId("not-in-source"));
    require(
        !unknownCueReferent.ok()
            && unknownCueReferent.error.code
                == AnnotationErrorCode::UnknownTimedTextCue,
        "cue-linked annotation must fail closed for a cue absent from its source document");

    // 3. Multiple annotations may target one referent.
    const AnnotationProvider genius = geniusProvider();
    require(
        genius.providerId().value()
            == QStringLiteral("genius.live"),
        "BA0 provider normalization must remain visible as provenance");
    require(
        genius.sourceItemId().has_value()
            && genius.sourceItemId()->value()
                == QStringLiteral("Referent-42"),
        "provider source identity must remain opaque and case-preserving");
    require(
        genius.sourceUrl().has_value()
            && genius.sourceUrl()->host()
                == QStringLiteral("genius.com"),
        "source URL should remain explicit provenance");

    const Annotation first(
        annotationId("annotation-1"),
        firstRecording,
        cueReferent.id(),
        genius,
        summary(
            "Author note",
            "First interpretation"),
        AnnotationBody::notLoaded());

    const Annotation second(
        annotationId("annotation-2"),
        firstRecording,
        cueReferent.id(),
        geniusProvider(
            "genius.live",
            "Referent-43"),
        summary(
            "Community note",
            "Second interpretation"),
        mustAnnotation(
            AnnotationBody::loadedPlainText(
                QStringLiteral(
                    "  A normalized plain-text explanation.  ")),
            "loaded plain body should validate"));

    const AnnotationDocument multi =
        mustAnnotation(
            AnnotationDocument::create(
                firstRecording,
                QVector<AnnotationReferent>{
                    cueReferent},
                QVector<Annotation>{
                    first,
                    second}),
            "multiple annotations on one referent should validate");

    require(
        multi.annotationsFor(
            cueReferent.id()).size() == 2,
        "one referent must permit multiple annotations");

    // 4. Lazy, unavailable, and removed bodies are explicit states.
    require(
        first.body().state()
                == AnnotationBodyState::NotLoaded
            && first.body().plainText().isEmpty(),
        "lazy body state must be explicit and carry no fake text");
    require(
        AnnotationBody::unavailable().state()
            == AnnotationBodyState::Unavailable,
        "unavailable body must remain distinguishable");
    require(
        AnnotationBody::removed().state()
            == AnnotationBodyState::Removed,
        "removed body must remain distinguishable");
    require(
        annotationBodyStateName(
            AnnotationBodyState::NotLoaded)
            == QStringLiteral("not_loaded"),
        "body states must expose stable names");

    // 5. Provider identity never replaces canonical Recording identity.
    require(
        second.recordingMbid() == firstRecording,
        "annotation must preserve canonical RecordingMbid");
    require(
        second.provider().providerId().value()
            == QStringLiteral("genius.live"),
        "provider identity is separate provenance");
    require(
        second.recordingMbid().toString()
            != second.provider().providerId().value(),
        "provider identity must never substitute for RecordingMbid");

    // 6. The normalized domain needs plain text, not raw provider HTML.
    require(
        second.body().state()
            == AnnotationBodyState::Loaded,
        "plain normalized annotation body should be loadable");
    require(
        second.body().plainText()
            == QStringLiteral(
                "A normalized plain-text explanation."),
        "loaded body should expose normalized plain text only");
    require(
        second.summary().header()
            == QStringLiteral("Community note"),
        "annotation header should be normalized plain text");

    // 7. Untimed text has deterministic referents without invented timing.
    const AnnotationTextAnchor repeatedAnchor =
        mustAnnotation(
            AnnotationTextAnchor::create(
                QStringLiteral("echo echo echo"),
                QStringLiteral("echo"),
                1),
            "second repeated fragment should resolve deterministically");

    const AnnotationReferent repeatedA =
        mustAnnotation(
            AnnotationReferent::untimed(
                firstRecording,
                repeatedAnchor),
            "repeated untimed referent should validate");

    const AnnotationTextAnchor repeatedAnchorAgain =
        mustAnnotation(
            AnnotationTextAnchor::create(
                QStringLiteral(
                    " echo   echo\necho "),
                QStringLiteral("echo"),
                1),
            "equivalent normalized repeated fragment should validate");

    const AnnotationReferent repeatedB =
        mustAnnotation(
            AnnotationReferent::untimed(
                firstRecording,
                repeatedAnchorAgain),
            "equivalent repeated referent should validate");

    require(
        repeatedA.id() == repeatedB.id(),
        "untimed referent identity must be deterministic after normalization");
    require(
        !repeatedA.cueId().has_value(),
        "untimed referent must not fabricate BC1 cue identity");
    require(
        repeatedA.textAnchor()->rangeStart() == 5
            && repeatedA.textAnchor()->rangeLength() == 4,
        "untimed repeated fragment must retain deterministic normalized range");

    // 8. Annotation absence is valid for empty documents and bare referents.
    const AnnotationDocument empty =
        mustAnnotation(
            AnnotationDocument::create(
                firstRecording,
                {},
                {}),
            "empty annotation document should be valid");

    require(
        empty.referents().isEmpty()
            && empty.annotations().isEmpty(),
        "annotation absence must be represented as valid empty state");

    const AnnotationDocument bareReferent =
        mustAnnotation(
            AnnotationDocument::create(
                firstRecording,
                QVector<AnnotationReferent>{
                    repeatedA},
                {}),
            "referent with zero annotations should be valid");

    require(
        bareReferent.annotationsFor(
            repeatedA.id()).isEmpty(),
        "one lyric fragment may legitimately have zero annotations");

    // Additional negative controls for typed failures.
    const Annotation duplicateId(
        annotationId("annotation-1"),
        firstRecording,
        cueReferent.id(),
        genius,
        summary("Duplicate"),
        AnnotationBody::unavailable());

    const auto duplicateResult =
        AnnotationDocument::create(
            firstRecording,
            QVector<AnnotationReferent>{
                cueReferent},
            QVector<Annotation>{
                first,
                duplicateId});

    require(
        !duplicateResult.ok()
            && duplicateResult.error.code
                == AnnotationErrorCode::DuplicateAnnotationId,
        "duplicate annotation identity must fail with a typed error");

    const Annotation unknownReferent(
        annotationId("annotation-unknown"),
        firstRecording,
        repeatedA.id(),
        genius,
        summary("Unknown referent"),
        AnnotationBody::unavailable());

    const auto unknownResult =
        AnnotationDocument::create(
            firstRecording,
            QVector<AnnotationReferent>{
                cueReferent},
            QVector<Annotation>{
                unknownReferent});

    require(
        !unknownResult.ok()
            && unknownResult.error.code
                == AnnotationErrorCode::UnknownReferent,
        "annotation may not silently target a missing referent");

    const Annotation wrongRecording(
        annotationId(
            "annotation-wrong-recording"),
        otherRecording,
        cueReferent.id(),
        genius,
        summary("Wrong recording"),
        AnnotationBody::unavailable());

    const auto recordingMismatch =
        AnnotationDocument::create(
            firstRecording,
            QVector<AnnotationReferent>{
                cueReferent},
            QVector<Annotation>{
                wrongRecording});

    require(
        !recordingMismatch.ok()
            && recordingMismatch.error.code
                == AnnotationErrorCode::RecordingMismatch,
        "provider annotation cannot replace or change document RecordingMbid");

    require(
        annotationErrorCodeName(
            AnnotationErrorCode::UnknownReferent)
            == QStringLiteral("unknown_referent"),
        "annotation errors should expose stable names");

    const QString regression = app.arguments().value(1);
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-hash")) {
        const auto collisionLyrics = mustTimed(TimedTextDocument::synchronized(
            firstRecording, sourceLyrics.provenance(), TimedTextKind::LineSynced,
            {TimedTextCue(cueId("x"), QStringLiteral("A"), 0, 1000),
             TimedTextCue(cueId("x|text=A|start=0|length=1"), QStringLiteral("B"), 1000, 2000)}),
            "collision fixture must be valid lyrics");
        const auto anchor = mustAnnotation(AnnotationTextAnchor::create(QStringLiteral("A"), QStringLiteral("A")),
                                           "cue fragment fixture must resolve");
        const auto whole = mustAnnotation(AnnotationReferent::cueLinked(
            collisionLyrics, cueId("x|text=A|start=0|length=1")), "whole cue must bind");
        const auto fragment = mustAnnotation(AnnotationReferent::cueLinked(collisionLyrics, cueId("x"), anchor),
                                             "fragment must bind");
        require(!(whole.id() == fragment.id()), "M0R structurally different referents must not hash to one identity");
        if (!regression.isEmpty()) return 0;
    }
    if (regression.isEmpty() || regression == QStringLiteral("--m0r-anchor")) {
        const auto unrelated = mustAnnotation(AnnotationTextAnchor::create(
            QStringLiteral("unrelated text"), QStringLiteral("text")), "unrelated anchor fixture must resolve");
        const auto rejected = AnnotationReferent::cueLinked(sourceLyrics, cueId("chorus-1"), unrelated);
        require(!rejected.ok(), "M0R cue fragment must belong to the selected cue text");
        if (!regression.isEmpty()) return 0;
    }
    qInfo().noquote()
        << "VINYL_BC3_ANNOTATION_MEANING_OK checks="
        << g_checks;
    return 0;
}
