#include "TimedTextCore.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdlib>
#include <cstdio>
#include <optional>
#include <utility>

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

vinyl::TimedTextProvenance provenance(const char *providerText = "lyrics.fixture",
                                      const char *itemText = "source-A")
{
    return vinyl::TimedTextProvenance(
        provider(providerText),
        std::optional<vinyl::ProviderItemId>{providerItem(itemText)});
}

vinyl::TimedTextDocument lineDocument(qint64 providerOffsetMs = 0)
{
    using namespace vinyl;
    QVector<TimedTextCue> cues{
        TimedTextCue(cueId("line-1"), QStringLiteral("first"), 1000, 2000),
        TimedTextCue(cueId("line-2"), QStringLiteral("second"), 2000, 3000),
        TimedTextCue(cueId("line-3"), QStringLiteral("third"), 3000, 4000),
    };
    return mustTimed(
        TimedTextDocument::synchronized(
            recording("11111111-1111-4111-8111-111111111111"),
            provenance(),
            TimedTextKind::LineSynced,
            std::move(cues),
            providerOffsetMs),
        "line document should validate");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    const RecordingMbid firstRecording =
        recording("11111111-1111-4111-8111-111111111111");

    // 1. Plain lyrics stay explicitly untimed.
    const TimedTextDocument plain = mustTimed(
        TimedTextDocument::plain(
            firstRecording,
            provenance(),
            QStringLiteral("first plain line\nsecond plain line")),
        "plain lyrics should be valid");
    require(plain.kind() == TimedTextKind::Plain,
            "plain lyrics must remain plain");
    require(plain.cues().isEmpty(),
            "plain lyrics must not receive synthetic timed cues");
    require(plain.providerOffsetMs() == 0,
            "plain lyrics must not carry synthetic timing offset");
    require(!currentTimedTextCueIndex(plain, 1200).has_value(),
            "plain lyrics must not participate in current-cue lookup");

    // 2. Line-synced lookup uses half-open cue intervals at boundaries.
    const TimedTextDocument lines = lineDocument();
    require(!currentTimedTextCueIndex(lines, 999).has_value(),
            "time before first cue should have no current cue");
    require(currentTimedTextCueIndex(lines, 1000) == std::optional<qsizetype>{0},
            "cue start boundary should select that cue");
    require(currentTimedTextCueIndex(lines, 1999) == std::optional<qsizetype>{0},
            "time before cue end should remain on current cue");
    require(currentTimedTextCueIndex(lines, 2000) == std::optional<qsizetype>{1},
            "shared end/start boundary should select the next cue");
    require(!currentTimedTextCueIndex(lines, 4000).has_value(),
            "explicit final end boundary should have no cue");

    // 3. Word timing is nested and preserves the parent line interval.
    QVector<TimedTextCue> wordCues{
        TimedTextCue(
            cueId("word-line-1"),
            QStringLiteral("hello world"),
            5000,
            6500,
            QVector<TimedTextWord>{
                TimedTextWord(QStringLiteral("hello"), 5000, 5600),
                TimedTextWord(QStringLiteral("world"), 5700, 6400),
            }),
        TimedTextCue(
            cueId("word-line-2"),
            QStringLiteral("again"),
            6500,
            7200),
    };
    const TimedTextDocument words = mustTimed(
        TimedTextDocument::synchronized(
            firstRecording,
            provenance(),
            TimedTextKind::WordSynced,
            std::move(wordCues)),
        "word-synced document should validate");
    require(words.kind() == TimedTextKind::WordSynced,
            "word timing must remain distinguishable from line timing");
    require(words.cues().at(0).startMs() == 5000
                && words.cues().at(0).endMs() == std::optional<qint64>{6500},
            "nested word timing must not rewrite parent line timing");
    require(words.cues().at(0).words().size() == 2,
            "word timing should remain nested under its parent cue");

    // 4. Positive and negative provider/global offsets use one deterministic sign.
    const TimedTextDocument positiveProvider = lineDocument(500);
    require(currentTimedTextCueIndex(positiveProvider, 1500)
                == std::optional<qsizetype>{1},
            "positive offset should advance the lyric clock");
    const TimedTextDocument negativeProvider = lineDocument(-500);
    require(currentTimedTextCueIndex(negativeProvider, 2500)
                == std::optional<qsizetype>{1},
            "negative offset should delay the lyric clock");
    const TimedTextDocument combinedOffsets = lineDocument(300);
    require(currentTimedTextCueIndex(combinedOffsets, 1500, 200)
                == std::optional<qsizetype>{1},
            "provider and global offsets should add deterministically");
    require(TimedTextOffsets(300, -100).effectiveOffsetMs() == 200,
            "effective offset should equal provider plus global offset");

    // 5. Click-to-seek inverts the effective offset and clamps at zero.
    const auto seekRoundTrip = timedTextCueSeekTargetMs(combinedOffsets, 1, 200);
    require(seekRoundTrip == std::optional<qint64>{1500},
            "click seek should subtract the effective offset");
    require(currentTimedTextCueIndex(combinedOffsets, *seekRoundTrip, 200)
                == std::optional<qsizetype>{1},
            "click seek and cue lookup should round-trip");
    const TimedTextDocument largePositiveOffset = lineDocument(1500);
    require(timedTextCueSeekTargetMs(largePositiveOffset, 0)
                == std::optional<qint64>{0},
            "click seek target must clamp below zero to zero");

    // 6. A discontinuity performs a fresh lookup immediately.
    require(resynchronizeTimedTextCueIndex(lines, 3500)
                == std::optional<qsizetype>{2},
            "forward discontinuity should select the destination cue");
    require(resynchronizeTimedTextCueIndex(lines, 1200)
                == std::optional<qsizetype>{0},
            "backward discontinuity should immediately select the new cue");

    // 7. Backwards timing fails instead of being silently sorted.
    QVector<TimedTextCue> backwards{
        TimedTextCue(cueId("back-1"), QStringLiteral("later"), 2000),
        TimedTextCue(cueId("back-2"), QStringLiteral("earlier"), 1000),
    };
    const auto backwardsResult = TimedTextDocument::synchronized(
        firstRecording,
        provenance(),
        TimedTextKind::LineSynced,
        std::move(backwards));
    require(!backwardsResult.ok(),
            "backwards timing must fail validation");
    require(backwardsResult.error.code == TimedTextErrorCode::NonMonotonicTiming,
            "backwards timing must report non-monotonic timing");

    QVector<TimedTextCue> overlap{
        TimedTextCue(cueId("overlap-1"), QStringLiteral("one"), 1000, 2500),
        TimedTextCue(cueId("overlap-2"), QStringLiteral("two"), 2000, 3000),
    };
    const auto overlapResult = TimedTextDocument::synchronized(
        firstRecording,
        provenance(),
        TimedTextKind::LineSynced,
        std::move(overlap));
    require(!overlapResult.ok()
                && overlapResult.error.code == TimedTextErrorCode::OverlappingTiming,
            "overlapping line timing must fail validation");

    // 8. Async results are bound to both Recording and exact QueueOccurrenceId.
    const QueueOccurrenceId occurrenceA = occurrence("queue-occurrence-a");
    const QueueOccurrenceId occurrenceB = occurrence("queue-occurrence-b");
    const TimedTextRequestKey requested(firstRecording, occurrenceA);
    const TimedTextRequestKey sameCurrent(firstRecording, occurrenceA);
    const TimedTextRequestKey newerOccurrence(firstRecording, occurrenceB);
    const RecordingMbid otherRecording =
        recording("22222222-2222-4222-8222-222222222222");
    const TimedTextRequestKey otherCurrent(otherRecording, occurrenceA);
    const TimedTextDocument wrongRecordingDocument = mustTimed(
        TimedTextDocument::unavailable(otherRecording, provenance()),
        "wrong-recording stale-result fixture should validate");
    require(canApplyTimedTextResult(requested, sameCurrent, lines),
            "matching request identity should be applicable");
    require(!canApplyTimedTextResult(requested, newerOccurrence, lines),
            "stale result from an older occurrence must not become current");
    require(!canApplyTimedTextResult(requested, otherCurrent, lines),
            "result requested for another Recording must not become current");
    require(!canApplyTimedTextResult(requested, sameCurrent, wrongRecordingDocument),
            "document for another Recording must not pass a matching request token");

    // Same-occurrence transport restart does not invalidate a still-valid lyric request.
    const TimedTextRequestKey sameOccurrenceAfterSourceRestart(firstRecording, occurrenceA);
    require(canApplyTimedTextResult(
                requested, sameOccurrenceAfterSourceRestart, lines),
            "same QueueOccurrenceId must survive a transport/source restart");

    // 9. Document identity is revision-scoped to provider/item/content evidence.
    const TimedTextDocument sameLinesAgain = lineDocument();
    require(lines.documentId() == sameLinesAgain.documentId(),
            "identical provider/item/content evidence must reproduce one timed-text document ID");
    QVector<TimedTextCue> correctedCues{
        TimedTextCue(cueId("line-1"), QStringLiteral("first corrected"), 1000, 2000),
        TimedTextCue(cueId("line-2"), QStringLiteral("second"), 2000, 3000),
        TimedTextCue(cueId("line-3"), QStringLiteral("third"), 3000, 4000),
    };
    const TimedTextDocument correctedDocument = mustTimed(
        TimedTextDocument::synchronized(
            firstRecording,
            provenance(),
            TimedTextKind::LineSynced,
            std::move(correctedCues)),
        "corrected document should validate");
    require(lines.documentId() != correctedDocument.documentId(),
            "content correction must mint a different timed-text document revision ID");

    QVector<TimedTextCue> swappedProviderCues{
        TimedTextCue(cueId("line-1"), QStringLiteral("first"), 1000, 2000),
        TimedTextCue(cueId("line-2"), QStringLiteral("second"), 2000, 3000),
        TimedTextCue(cueId("line-3"), QStringLiteral("third"), 3000, 4000),
    };
    const TimedTextDocument providerSwapDocument = mustTimed(
        TimedTextDocument::synchronized(
            firstRecording,
            provenance("lyrics.other", "source-A"),
            TimedTextKind::LineSynced,
            std::move(swappedProviderCues)),
        "provider swap document should validate");
    require(lines.documentId() != providerSwapDocument.documentId(),
            "provider swap must not reuse the prior timed-text document identity");

    // 10. W4-A recording-time offset is required for source-aware synchronization.
    require(recordingPositionMsForPlayback(15000, qint64{-10000})
                == std::optional<qint64>{5000},
            "recording time must equal track-relative playback plus recordingTimeOffsetMs");
    require(!recordingPositionMsForPlayback(
                 15000, std::optional<qint64>{}).has_value(),
            "unknown recording-time offset must remain unknown");
    require(currentTimedTextCueIndexForPlayback(
                lines, 12000, qint64{-10000})
                == std::optional<qsizetype>{1},
            "source-aware lyric lookup must convert track-relative playback to recording time");
    require(!currentTimedTextCueIndexForPlayback(
                 lines, 12000, std::optional<qint64>{}).has_value(),
            "unknown recording-time offset must disable synchronized highlighting");
    require(timedTextCuePlaybackSeekTargetMs(
                lines, 1, qint64{-10000})
                == std::optional<qint64>{12000},
            "click-to-seek must invert recordingTimeOffsetMs back to track-relative playback");
    require(!timedTextCuePlaybackSeekTargetMs(
                 lines, 1, std::optional<qint64>{}).has_value(),
            "unknown recording-time offset must not invent a synchronized seek target");

    // 11. Provider provenance survives normalization without replacing Recording identity.
    const ProviderId normalizedProvider = provider("Lyrics-Provider");
    const ProviderItemId opaqueSource = providerItem("Source-ABC");
    const TimedTextProvenance normalizedProvenance(
        normalizedProvider,
        std::optional<ProviderItemId>{opaqueSource});
    QVector<TimedTextCue> provenanceCues{
        TimedTextCue(cueId("provenance-1"), QStringLiteral("line"), 0, 1000),
    };
    const TimedTextDocument provenanceDoc = mustTimed(
        TimedTextDocument::synchronized(
            firstRecording,
            normalizedProvenance,
            TimedTextKind::LineSynced,
            std::move(provenanceCues)),
        "provenance fixture should validate");
    require(provenanceDoc.provenance().providerId().value()
                == QStringLiteral("lyrics-provider"),
            "provider identity normalization must survive the timed-text boundary");
    require(provenanceDoc.provenance().sourceItemId().has_value()
                && provenanceDoc.provenance().sourceItemId()->value()
                    == QStringLiteral("Source-ABC"),
            "opaque provider source identity must preserve case and punctuation");
    require(provenanceDoc.recordingMbid() == firstRecording,
            "provider provenance must not replace canonical Recording identity");

    // 10. Absence is a valid, typed document result.
    const TimedTextDocument unavailable = mustTimed(
        TimedTextDocument::unavailable(firstRecording, provenance()),
        "unavailable lyrics should be a valid result");
    require(unavailable.kind() == TimedTextKind::Unavailable,
            "absence should remain distinguishable from plain text");
    require(unavailable.plainText().isEmpty() && unavailable.cues().isEmpty(),
            "unavailable result must not masquerade as text");
    require(!currentTimedTextCueIndex(unavailable, 5000).has_value(),
            "unavailable text must not participate in synchronization");

    require(timedTextKindName(TimedTextKind::WordSynced)
                == QStringLiteral("word_synced"),
            "timing kinds should expose stable names");
    require(timedTextErrorCodeName(TimedTextErrorCode::NonMonotonicTiming)
                == QStringLiteral("non_monotonic_timing"),
            "timed-text validation errors should expose stable names");

    const TimedTextDocument introLyrics = lineDocument(2000);
    if (!app.arguments().contains(QStringLiteral("--m0r-lookup"))) {
        require(timedTextCuePlaybackSeekTargetMs(introLyrics, 0, qint64{-10000})
                    == std::optional<qint64>{9000},
                "M0R inverse offsets must clamp only at final playback target");
    }
    require(currentTimedTextCueIndexForPlayback(introLyrics, 9000, qint64{-10000})
                == std::optional<qsizetype>{0},
            "M0R intro seek must round-trip through the calibrated lyric clock");
    qInfo().noquote() << "VINYL_BC1_TIMED_TEXT_OK checks=" << g_checks;
    return 0;
}
