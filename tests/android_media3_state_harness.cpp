#include "../native/player/androidmedia3state.h"

#include <QCoreApplication>
#include <QDebug>

#include <cmath>

using Colosseum::Player::AndroidMedia3State;
using Colosseum::Player::HostPlaybackAction;
using Colosseum::Player::Media3ChapterRow;
using Colosseum::Player::Media3CueRow;
using Colosseum::Player::Media3TrackRow;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        qFatal("android_media3_state_harness: %s", message);
}

bool near(double a, double b, double epsilon = 0.0001)
{
    return std::abs(a - b) <= epsilon;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    AndroidMedia3State state;

    QVariantMap firstHeaders;
    firstHeaders.insert(QStringLiteral("Referer"), QStringLiteral("https://a.invalid/"));
    const quint64 g1 = state.beginLoad(QStringLiteral("https://example.invalid/a.mp4"), firstHeaders);
    require(g1 > 0, "first load must allocate a non-zero generation");
    require(state.accepts(g1), "current generation must be accepted");
    require(!state.accepts(0), "generation zero must never be accepted");
    require(state.snapshot().headers == firstHeaders, "load headers must be generation scoped");
    require(state.snapshot().decodedWidth == 0 && state.snapshot().decodedHeight == 0,
            "new load must start without decoded-frame truth");

    require(state.markReady(g1), "first READY must transition once");
    require(!state.markReady(g1), "duplicate READY must be rejected");
    require(state.noteVideoSize(g1, 1920, 1080), "video size must accept current generation");
    require(state.snapshot().decodedWidth == 0, "video size alone must not publish decoded dimensions");
    require(state.markFirstFrame(g1), "first frame must transition once per surface");
    require(state.snapshot().decodedWidth == 1920 && state.snapshot().decodedHeight == 1080,
            "first frame must publish the latest positive video size");
    require(!state.markFirstFrame(g1), "duplicate first frame on one surface must be one-shot");

    const quint64 identityBeforeSurfaceLoss = state.generation();
    const QString urlBeforeSurfaceLoss = state.snapshot().currentUrl;
    require(state.resetVideoSurface(), "surface loss must reset first-frame truth");
    require(state.generation() == identityBeforeSurfaceLoss && state.snapshot().currentUrl == urlBeforeSurfaceLoss,
            "surface loss must not change semantic source identity");
    require(state.snapshot().decodedWidth == 0 && state.snapshot().decodedHeight == 0,
            "surface loss must clear exported decoded dimensions");
    require(state.markFirstFrame(g1), "restored surface must require a fresh first-frame transition");

    require(state.updateTimeline(g1, 12'500, 100'000, 25'000, 25.0, false, 90, false, 1.25),
            "timeline must accept current generation");
    require(near(state.snapshot().positionSec, 12.5), "position must normalize milliseconds to seconds");
    require(near(state.snapshot().durationSec, 100.0), "duration must normalize milliseconds to seconds");
    require(near(state.snapshot().bufferedSec, 25.0), "buffered position must normalize to seconds");
    require(near(state.snapshot().bufferingPercent, 25.0), "known buffering percentage must survive");
    require(!state.snapshot().paused && state.snapshot().volume == 90 && !state.snapshot().muted,
            "pause/volume/mute timeline truth must normalize together");
    require(near(state.snapshot().speed, 1.25), "playback speed must normalize without losing valid values");

    require(state.updateTimeline(g1, -50, -1, -100, 150.0, true, 250, true, -4.0),
            "timeline normalization must tolerate invalid host values");
    require(state.snapshot().positionSec == 0.0 && state.snapshot().durationSec == 0.0,
            "negative position/duration must fail closed to zero");
    require(state.snapshot().bufferedSec == 0.0 && state.snapshot().bufferingPercent == 100.0,
            "buffer values must clamp to the portable range");
    require(state.snapshot().volume == 100 && state.snapshot().muted && state.snapshot().paused,
            "volume and booleans must normalize deterministically");
    require(state.snapshot().speed == 1.0, "invalid speed must fall back to normal speed");

    require(state.beginSeek(g1, 42'000, 1'000), "seek must start for the active generation");
    require(state.snapshot().seeking, "seek start must expose coreSeeking truth");
    require(state.noteSeekDiscontinuity(g1, 41'900), "seek discontinuity must be generation guarded");
    require(state.snapshot().seeking, "discontinuity alone must not claim seek arrival");
    require(state.notePlayerReady(g1, 41'950), "READY after discontinuity must settle the seek");
    require(!state.snapshot().seeking, "READY settlement must clear coreSeeking");

    require(state.beginSeek(g1, 80'000, 2'000), "second seek must replace prior seek state cleanly");
    require(!state.expireSeek(4'999), "seek timeout must not fire before exactly three seconds");
    require(state.expireSeek(5'000), "seek timeout must fire at exactly three seconds");
    require(!state.snapshot().seeking, "timed-out seek must clear only the seeking flag");

    require(state.beginSeek(g1, 60'000, 6'000), "seek must restart after timeout");
    require(state.noteSeekDiscontinuity(g1, 59'800), "accepted post-seek position must be recorded");
    require(state.updateTimeline(g1, 60'100, 100'000, 70'000, 70.0, false, 90, false, 1.0),
            "timeline sample must remain accepted during seek");
    require(!state.snapshot().seeking, "sample within 500ms of accepted discontinuity must settle seek");

    Media3TrackRow audio;
    audio.id = QStringLiteral("a:0:0");
    audio.title = QStringLiteral("Japanese");
    audio.lang = QStringLiteral("ja");
    audio.codec = QStringLiteral("aac");
    audio.channels = QStringLiteral("2");
    audio.selected = true;
    audio.isDefault = true;

    Media3TrackRow subtitle;
    subtitle.id = QStringLiteral("s:1:0");
    subtitle.title = QStringLiteral("English SDH");
    subtitle.lang = QStringLiteral("en");
    subtitle.codec = QStringLiteral("ass");
    subtitle.hearingImpaired = true;
    subtitle.forced = false;

    require(state.replaceTracks(g1, {audio, subtitle}), "track replacement must accept current generation");
    require(state.audioTracks().size() == 1 && state.subtitleTracks().size() == 1,
            "track rows must split into plain audio/subtitle lists");
    const QVariantMap audioMap = state.audioTracks().constFirst().toMap();
    require(audioMap.value(QStringLiteral("id")).toString() == QStringLiteral("a:0:0"),
            "audio opaque id must survive normalization");
    require(audioMap.value(QStringLiteral("default")).toBool(), "audio default flag must survive normalization");
    const QVariantMap subtitleMap = state.subtitleTracks().constFirst().toMap();
    require(subtitleMap.value(QStringLiteral("hearingImpaired")).toBool(),
            "subtitle accessibility flag must survive normalization");

    Media3ChapterRow later{QStringLiteral("Part A"), 120.0};
    Media3ChapterRow duplicate{QStringLiteral("Opening"), 90.0};
    Media3ChapterRow opening{QStringLiteral("Opening"), 90.0};
    Media3ChapterRow unnamed{QString(), -5.0};
    require(state.replaceChapters(g1, {later, duplicate, opening, unnamed}),
            "chapter replacement must accept current generation");
    require(state.chapters().size() == 3, "chapters must sort and dedupe equal start/title rows");
    require(state.chapters().at(0).toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Chapter"),
            "blank chapter names must normalize honestly");
    require(state.chapters().at(0).toMap().value(QStringLiteral("startSec")).toDouble() == 0.0,
            "negative chapter times must clamp to zero");
    require(state.chapters().at(1).toMap().value(QStringLiteral("startSec")).toDouble() == 90.0,
            "chapters must sort by start time");

    Media3CueRow cue;
    cue.text = QStringLiteral("Hello");
    cue.position = 0.5;
    cue.line = 0.9;
    cue.size = 0.8;
    cue.alignment = QStringLiteral("center");
    cue.positionAnchor = QStringLiteral("middle");
    cue.lineAnchor = QStringLiteral("end");
    cue.zIndex = 3;
    QVariantMap span;
    span.insert(QStringLiteral("bold"), true);
    cue.spans = {span};
    require(state.replaceSubtitleCues(g1, {cue}), "cue replacement must accept current generation");
    require(state.subtitleCues().size() == 1, "cue rows must publish as a plain Qt list");
    const QVariantMap cueMap = state.subtitleCues().constFirst().toMap();
    require(cueMap.value(QStringLiteral("text")).toString() == QStringLiteral("Hello"),
            "cue text must survive normalization");
    require(cueMap.value(QStringLiteral("zIndex")).toInt() == 3,
            "cue ordering must survive normalization");
    require(cueMap.value(QStringLiteral("spans")).toList().size() == 1,
            "cue style runs must remain plain Qt data");

    const QVariantMap caps = state.capabilities();
    require(caps.value(QStringLiteral("subtitleCueOverlay")).toBool(),
            "Media3 subtitle cue overlay capability must be truthful");
    require(!caps.value(QStringLiteral("frameCapture")).toBool()
                && !caps.value(QStringLiteral("gifCapture")).toBool()
                && !caps.value(QStringLiteral("videoTransform")).toBool(),
            "unsupported mpv-only capabilities must stay false");

    require(state.noteUserPlay(g1), "explicit user play intent must be generation guarded");
    require(state.snapshot().userWantsPlay, "user play intent must be observable");
    require(state.beginLifecycleHostStop(g1) == HostPlaybackAction::Stop,
            "background lifecycle must request transport stop, not semantic user pause");
    const quint64 lifecycle1 = state.snapshot().lifecycleEpoch;
    require(lifecycle1 > 0 && state.snapshot().hostStopped,
            "host stop must own a non-zero lifecycle epoch");
    require(state.snapshot().userWantsPlay,
            "host stop must preserve playing user intent independently");
    require(state.shouldSuppressLifecycleError(g1, lifecycle1, QStringLiteral("network"))
                && state.shouldSuppressLifecycleError(g1, lifecycle1, QStringLiteral("http"))
                && state.shouldSuppressLifecycleError(g1, lifecycle1, QStringLiteral("source")),
            "matching teardown transport errors must be suppressible while the epoch is armed");
    require(!state.shouldSuppressLifecycleError(g1, lifecycle1, QStringLiteral("decoder")),
            "decoder failures must never be hidden by lifecycle transport suppression");
    require(state.noteLifecyclePrepareSubmitted(g1, lifecycle1),
            "foreground prepare submission must retire teardown error suppression");
    require(!state.shouldSuppressLifecycleError(g1, lifecycle1, QStringLiteral("network")),
            "suppression must end once restore prepare is submitted");
    require(state.noteLifecycleReady(g1, lifecycle1) == HostPlaybackAction::Resume,
            "READY after restore may resume only preserved playing intent");

    require(state.noteUserPause(g1), "user pause must remain authoritative");
    require(state.beginLifecycleHostStop(g1) == HostPlaybackAction::Stop,
            "user-paused prepared media must still stop transport on background");
    const quint64 lifecycle2 = state.snapshot().lifecycleEpoch;
    require(lifecycle2 > lifecycle1, "lifecycle epochs must increase monotonically");
    require(!state.snapshot().userWantsPlay, "host stop must not rewrite a user pause into play intent");
    require(!state.noteLifecyclePrepareSubmitted(g1, lifecycle1),
            "old lifecycle epochs must be rejected after a newer host stop");
    require(state.noteLifecyclePrepareSubmitted(g1, lifecycle2), "current restore prepare must be accepted");
    require(state.noteLifecycleReady(g1, lifecycle2) == HostPlaybackAction::None,
            "user-paused content must remain paused after lifecycle restore");

    require(state.noteUserPlay(g1), "user may establish fresh play intent");
    require(state.beginLifecycleHostStop(g1) == HostPlaybackAction::Stop,
            "fresh playing intent may enter another host-stop epoch");
    const quint64 lifecycle3 = state.snapshot().lifecycleEpoch;
    require(state.noteTerminalAudioFocusLoss(g1), "terminal focus loss must invalidate lifecycle resume");
    require(state.noteLifecyclePrepareSubmitted(g1, lifecycle3), "restore prepare may still clear suppression");
    require(state.noteLifecycleReady(g1, lifecycle3) == HostPlaybackAction::None,
            "terminal focus loss must prevent lifecycle auto-resume");

    QVariantMap staleHeaders;
    staleHeaders.insert(QStringLiteral("Authorization"), QStringLiteral("stale"));
    const quint64 g2 = state.beginLoad(QStringLiteral("https://example.invalid/a.mp4"), {});
    require(g2 > g1, "same-URL reload must allocate a strictly newer generation");
    require(!state.accepts(g1) && state.accepts(g2), "old generation callbacks must become stale immediately");
    require(state.snapshot().headers.isEmpty(), "same-URL headerless reload must clear prior headers");
    require(!state.snapshot().ready && !state.snapshot().ended && !state.snapshot().errored,
            "same-URL reload must reset READY/end/error truth");
    require(state.audioTracks().isEmpty() && state.subtitleTracks().isEmpty()
                && state.chapters().isEmpty() && state.subtitleCues().isEmpty(),
            "same-URL reload must clear all source-scoped normalized rows");
    require(!state.snapshot().seeking && state.snapshot().decodedWidth == 0,
            "same-URL reload must clear seek and decoded-frame state");
    require(!state.snapshot().userWantsPlay && !state.snapshot().hostStopped
                && !state.snapshot().lifecycleErrorSuppressionArmed,
            "source replacement must invalidate prior lifecycle restoration and play intent");
    require(!state.noteLifecyclePrepareSubmitted(g1, lifecycle3)
                && state.noteLifecycleReady(g1, lifecycle3) == HostPlaybackAction::None,
            "old generation/lifecycle callbacks must be ignored after source replacement");

    require(!state.updateTimeline(g1, 99'000, 100'000, 100'000, 100.0, false, 1, true, 3.0),
            "stale timeline callbacks must be rejected");
    require(!state.markReady(g1) && !state.markFirstFrame(g1),
            "stale readiness/frame callbacks must be rejected");
    require(!state.replaceTracks(g1, {audio}) && state.audioTracks().isEmpty(),
            "stale track callbacks must not repopulate a replacement source");

    require(state.markError(g2, QStringLiteral("decoder"), QStringLiteral("unsupported sample")),
            "first error must transition once");
    require(!state.markError(g2, QStringLiteral("source"), QStringLiteral("duplicate")),
            "duplicate error must not overwrite terminal truth");
    require(state.snapshot().errored && state.snapshot().errorCode == QStringLiteral("decoder"),
            "first typed error must remain the one-shot source truth");
    require(!state.markEnded(g2), "EOF must not fire after the generation already terminated with error");
    require(!state.markReady(g2), "READY must not resurrect a terminal generation");

    const quint64 g3 = state.beginLoad(QStringLiteral("https://example.invalid/b.mp4"), staleHeaders);
    require(g3 > g2, "every replacement must keep generation monotonic");
    require(state.noteUserPlay(g3), "terminal-stop fixture must begin with playing intent");
    require(state.beginLifecycleHostStop(g3) == HostPlaybackAction::Stop,
            "terminal-stop fixture must enter lifecycle host-stop state");
    const quint64 lifecycle4 = state.snapshot().lifecycleEpoch;
    require(state.noteStopped(g3), "explicit stop/back must cancel lifecycle restoration");
    require(!state.snapshot().hostStopped && !state.snapshot().userWantsPlay,
            "terminal stop must clear lifecycle state and play intent");
    require(!state.noteLifecyclePrepareSubmitted(g3, lifecycle4)
                && state.noteLifecycleReady(g3, lifecycle4) == HostPlaybackAction::None,
            "foreground return must not resurrect an explicitly stopped source");
    require(state.markEnded(g3), "first EOF must transition once");
    require(!state.markEnded(g3), "duplicate EOF must be one-shot");
    require(!state.markError(g3, QStringLiteral("unknown"), QStringLiteral("late")),
            "late error must not replace EOF truth");

    qInfo().noquote() << QStringLiteral("ANDROID_MEDIA3_STATE_OK");
    return 0;
}
