package org.colosseum.player;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;

import androidx.media3.common.AudioAttributes;
import androidx.media3.common.C;
import androidx.media3.common.CueGroup;
import androidx.media3.common.Format;
import androidx.media3.common.MediaItem;
import androidx.media3.common.Metadata;
import androidx.media3.common.Label;
import androidx.media3.common.MediaMetadata;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.Timeline;
import androidx.media3.common.Tracks;
import androidx.media3.common.TrackSelectionOverride;
import androidx.media3.common.TrackSelectionParameters;
import androidx.media3.common.VideoSize;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.extractor.metadata.Chapter;
import androidx.media3.datasource.DefaultDataSource;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;
import androidx.media3.exoplayer.source.MediaSource;

import java.net.Inet4Address;
import java.net.Inet6Address;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

/**
 * AndroidX Media3 owner used by the native Android PlayerItem facade.
 * Every ExoPlayer mutation and listener callback is bound to Android's main looper.
 */
@UnstableApi
public final class Media3PlayerHost {
    private final Context appContext;
    private final long nativeHandle;
    private final Handler mainHandler;
    private final ExoPlayer player;
    private final Player.Listener listener;

    // Main-looper state only.
    private static final long TELEMETRY_INTERVAL_MS = 350L;

    private long activeGeneration;
    private float requestedVolume = 1.0f;
    private boolean muted;
    private boolean userWantsPlay;
    private boolean released;
    private final TreeMap<Long, String> chaptersByStartMs = new TreeMap<>();

    private final Runnable telemetryRunnable = new Runnable() {
        @Override
        public void run() {
            if (!isTelemetryUseful())
                return;
            emitPlaybackSnapshot();
            mainHandler.postDelayed(this, TELEMETRY_INTERVAL_MS);
        }
    };

    public Media3PlayerHost(Context context, long nativeHandle) {
        if (context == null)
            throw new IllegalArgumentException("context is required");
        this.appContext = context.getApplicationContext();
        this.nativeHandle = nativeHandle;
        this.mainHandler = new Handler(Looper.getMainLooper());
        this.player = createPlayerOnMain();
        this.listener = createListener();
        runOnMainSync(() -> player.addListener(listener));
    }

    private ExoPlayer createPlayerOnMain() {
        return callOnMain(() -> {
            DefaultRenderersFactory renderersFactory =
                new DefaultRenderersFactory(appContext)
                    .setEnableDecoderFallback(true);
            ExoPlayer created = new ExoPlayer.Builder(appContext)
                .setRenderersFactory(renderersFactory)
                .setLooper(Looper.getMainLooper())
                .build();
            AudioAttributes audioAttributes = new AudioAttributes.Builder()
                .setUsage(C.USAGE_MEDIA)
                .setContentType(C.AUDIO_CONTENT_TYPE_MOVIE)
                .build();
            created.setAudioAttributes(audioAttributes, true);
            created.setHandleAudioBecomingNoisy(true);
            return created;
        });
    }

    public void load(long generation, String sourceUrl, Map<String, String> headers) {
        final String url = sourceUrl == null ? "" : sourceUrl.trim();
        final Map<String, String> safeHeaders = copySafeHeaders(headers);
        final boolean sourceAllowed = isAllowedSource(url);

        runOnMain(() -> {
            if (released)
                return;
            stopTelemetry();
            activeGeneration = generation;
            chaptersByStartMs.clear();
            if (!sourceAllowed) {
                player.stop();
                player.clearMediaItems();
                emitError(generation, "source", "source_not_allowed", "Playback source is not permitted");
                return;
            }

            DefaultHttpDataSource.Factory httpFactory = new DefaultHttpDataSource.Factory()
                .setAllowCrossProtocolRedirects(false)
                .setDefaultRequestProperties(safeHeaders);
            DefaultDataSource.Factory dataSourceFactory =
                new DefaultDataSource.Factory(appContext, httpFactory);
            DefaultMediaSourceFactory mediaSourceFactory =
                new DefaultMediaSourceFactory(dataSourceFactory);
            MediaItem mediaItem = MediaItem.fromUri(url);
            MediaSource mediaSource = mediaSourceFactory.createMediaSource(mediaItem);
            player.setMediaSource(mediaSource);
            player.prepare();
            startTelemetry();
        });
    }

    public void play() {
        runOnMain(() -> {
            if (released)
                return;
            userWantsPlay = true;
            player.play();
            startTelemetry();
        });
    }

    public void pause() {
        runOnMain(() -> {
            if (released)
                return;
            userWantsPlay = false;
            player.pause();
            stopTelemetry();
            emitPlaybackSnapshot();
        });
    }

    public void stopAndClear() {
        runOnMain(() -> {
            if (released)
                return;
            userWantsPlay = false;
            stopTelemetry();
            player.stop();
            player.clearMediaItems();
            chaptersByStartMs.clear();
            activeGeneration = 0;
        });
    }

    public void stopForLifecycle() {
        runOnMain(() -> {
            if (released)
                return;
            stopTelemetry();
            player.setPlayWhenReady(false);
            player.stop();
        });
    }

    public void prepareForLifecycleRestore() {
        runOnMain(() -> {
            if (released || activeGeneration == 0)
                return;
            player.prepare();
            player.setPlayWhenReady(userWantsPlay);
            if (userWantsPlay) {
                startTelemetry();
            } else {
                stopTelemetry();
                emitPlaybackSnapshot();
            }
        });
    }

    public void seekTo(long positionMs) {
        final long clamped = Math.max(0L, positionMs);
        runOnMain(() -> {
            if (!released)
                player.seekTo(clamped);
        });
    }

    public void setVolume(float volume) {
        final float clamped = clamp(volume, 0.0f, 1.0f);
        runOnMain(() -> {
            if (released)
                return;
            requestedVolume = clamped;
            if (!muted)
                player.setVolume(requestedVolume);
        });
    }

    public void setMuted(boolean muted) {
        runOnMain(() -> {
            if (released)
                return;
            this.muted = muted;
            player.setVolume(muted ? 0.0f : requestedVolume);
        });
    }

    public void setSpeed(float speed) {
        final float clamped = clamp(speed, 0.25f, 4.0f);
        runOnMain(() -> {
            if (!released)
                player.setPlaybackSpeed(clamped);
        });
    }

    public void selectAudioTrack(String encodedId) {
        runOnMain(() -> applyTrackSelection(C.TRACK_TYPE_AUDIO, encodedId));
    }

    public void selectSubtitleTrack(String encodedId) {
        runOnMain(() -> {
            if (released)
                return;
            if (encodedId == null || encodedId.isEmpty()) {
                TrackSelectionParameters.Builder builder =
                    player.getTrackSelectionParameters().buildUpon();
                builder.clearOverridesOfType(C.TRACK_TYPE_TEXT);
                builder.setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true);
                player.setTrackSelectionParameters(builder.build());
                return;
            }
            applyTrackSelection(C.TRACK_TYPE_TEXT, encodedId);
        });
    }

    private void applyTrackSelection(int trackType, String encodedId) {
        if (released || encodedId == null)
            return;
        String[] parts = encodedId.split(":", -1);
        if (parts.length != 3)
            return;
        String expectedPrefix = trackType == C.TRACK_TYPE_AUDIO ? "a" : "s";
        if (!expectedPrefix.equals(parts[0]))
            return;
        final int groupIndex;
        final int trackIndex;
        try {
            groupIndex = Integer.parseInt(parts[1]);
            trackIndex = Integer.parseInt(parts[2]);
        } catch (NumberFormatException error) {
            return;
        }
        List<Tracks.Group> groups = player.getCurrentTracks().getGroups();
        if (groupIndex < 0 || groupIndex >= groups.size())
            return;
        Tracks.Group group = groups.get(groupIndex);
        if (group.getType() != trackType || trackIndex < 0 || trackIndex >= group.length)
            return;
        TrackSelectionParameters.Builder builder =
            player.getTrackSelectionParameters().buildUpon();
        builder.clearOverridesOfType(trackType);
        builder.setTrackTypeDisabled(trackType, false);
        builder.addOverride(new TrackSelectionOverride(group.getMediaTrackGroup(), trackIndex));
        player.setTrackSelectionParameters(builder.build());
    }

    public void setVideoSurface(Surface surface) {
        runOnMain(() -> {
            if (!released)
                player.setVideoSurface(surface);
        });
    }

    public void clearVideoSurface(Surface surface) {
        runOnMainSync(() -> {
            if (!released && surface != null)
                player.clearVideoSurface(surface);
        });
    }

    public void clearVideoSurface() {
        runOnMain(() -> {
            if (!released)
                player.clearVideoSurface();
        });
    }

    public void release() {
        runOnMainSync(() -> {
            if (released)
                return;
            stopTelemetry();
            released = true;
            userWantsPlay = false;
            activeGeneration = 0;
            chaptersByStartMs.clear();
            player.removeListener(listener);
            player.clearVideoSurface();
            player.stop();
            player.clearMediaItems();
            player.release();
        });
    }

    private boolean isTelemetryUseful() {
        if (released || activeGeneration == 0 || player.getCurrentMediaItem() == null)
            return false;
        int state = player.getPlaybackState();
        return userWantsPlay || player.isLoading() || state == Player.STATE_BUFFERING;
    }

    private void startTelemetry() {
        mainHandler.removeCallbacks(telemetryRunnable);
        if (!isTelemetryUseful())
            return;
        emitPlaybackSnapshot();
        mainHandler.postDelayed(telemetryRunnable, TELEMETRY_INTERVAL_MS);
    }

    private void stopTelemetry() {
        mainHandler.removeCallbacks(telemetryRunnable);
    }

    private void emitPlaybackSnapshot() {
        if (released || activeGeneration == 0)
            return;
        long durationMs = player.getDuration();
        if (durationMs == C.TIME_UNSET || durationMs < 0)
            durationMs = 0;
        long positionMs = Math.max(0L, player.getCurrentPosition());
        long bufferedPositionMs = Math.max(positionMs, player.getBufferedPosition());
        nativeOnPlaybackSnapshot(nativeHandle, activeGeneration, positionMs, durationMs,
            bufferedPositionMs, player.getBufferedPercentage(), !userWantsPlay,
            requestedVolume, muted, player.getPlaybackParameters().speed);
    }

    private static float clamp(float value, float minimum, float maximum) {
        if (Float.isNaN(value))
            return minimum;
        return Math.max(minimum, Math.min(maximum, value));
    }

    private static Map<String, String> copySafeHeaders(Map<String, String> headers) {
        Map<String, String> copy = new LinkedHashMap<>();
        if (headers == null || headers.isEmpty())
            return Collections.unmodifiableMap(copy);

        for (Map.Entry<String, String> entry : headers.entrySet()) {
            String name = entry.getKey();
            String value = entry.getValue();
            if (name == null || value == null || name.isEmpty())
                continue;
            if (name.contains("\r") || name.contains("\n")
                    || value.contains("\r") || value.contains("\n"))
                continue;
            if (containsControlCharacter(name))
                continue;
            copy.put(name, value);
        }
        return Collections.unmodifiableMap(copy);
    }

    private static boolean containsControlCharacter(String value) {
        for (int i = 0; i < value.length(); ++i) {
            char ch = value.charAt(i);
            if (ch <= 0x1f || ch == 0x7f)
                return true;
        }
        return false;
    }

    private static boolean isAllowedSource(String sourceUrl) {
        if (sourceUrl == null || sourceUrl.isEmpty())
            return false;

        final Uri uri;
        try {
            uri = Uri.parse(sourceUrl);
        } catch (RuntimeException ignored) {
            return false;
        }
        String scheme = uri.getScheme();
        if (scheme == null)
            return false;
        scheme = scheme.toLowerCase(Locale.ROOT);

        if ("https".equals(scheme))
            return isAllowedHttps(uri);
        if ("http".equals(scheme))
            return isAllowedLoopbackHttp(uri);
        if ("content".equals(scheme))
            return uri.getAuthority() != null && !uri.getAuthority().isEmpty();
        if ("file".equals(scheme))
            return isAllowedFileUri(uri);
        return false;
    }

    private static boolean isAllowedHttps(Uri uri) {
        String host = uri.getHost();
        if (host == null || host.isEmpty() || uri.getUserInfo() != null)
            return false;
        int port = uri.getPort();
        if (port == 0 || port > 65535)
            return false;
        return !isForbiddenPrivateHost(host);
    }

    private static boolean isAllowedLoopbackHttp(Uri uri) {
        String host = uri.getHost();
        int port = uri.getPort();
        return host != null
            && host.equals("127.0.0.1")
            && uri.getUserInfo() == null
            && port >= 1
            && port <= 65535;
    }

    private static boolean isAllowedFileUri(Uri uri) {
        String authority = uri.getAuthority();
        String path = uri.getPath();
        return (authority == null || authority.isEmpty())
            && path != null
            && path.startsWith("/");
    }

    private static boolean isForbiddenPrivateHost(String host) {
        String normalized = host.toLowerCase(Locale.ROOT);
        while (normalized.endsWith("."))
            normalized = normalized.substring(0, normalized.length() - 1);
        if (normalized.isEmpty())
            return true;
        if (normalized.equals("localhost")
                || normalized.endsWith(".localhost")
                || normalized.endsWith(".local"))
            return true;
        if (looksLikeIpv4Literal(normalized))
            return isPrivateIpv4Literal(normalized);
        if (normalized.indexOf(':') >= 0)
            return isPrivateIpv6Literal(normalized);
        return resolvesToPrivateAddress(normalized);
    }

    private static boolean looksLikeIpv4Literal(String host) {
        if (host.isEmpty())
            return false;
        for (int i = 0; i < host.length(); ++i) {
            char ch = host.charAt(i);
            if ((ch < '0' || ch > '9') && ch != '.')
                return false;
        }
        return true;
    }

    private static boolean isPrivateIpv4Literal(String host) {
        String[] parts = host.split("\\.", -1);
        if (parts.length != 4)
            return true;
        int[] octets = new int[4];
        try {
            for (int i = 0; i < 4; ++i) {
                if (parts[i].isEmpty() || parts[i].length() > 3)
                    return true;
                octets[i] = Integer.parseInt(parts[i]);
                if (octets[i] < 0 || octets[i] > 255)
                    return true;
            }
        } catch (NumberFormatException ignored) {
            return true;
        }

        int a = octets[0];
        int b = octets[1];
        if (a == 0 || a == 10 || a == 127)
            return true;
        if (a == 100 && b >= 64 && b <= 127)
            return true;
        if (a == 169 && b == 254)
            return true;
        if (a == 172 && b >= 16 && b <= 31)
            return true;
        if (a == 192 && b == 168)
            return true;
        if (a == 198 && (b == 18 || b == 19))
            return true;
        if (a >= 224)
            return true;
        return false;
    }

    private static boolean isPrivateIpv6Literal(String host) {
        try {
            InetAddress address = InetAddress.getByName(host);
            return !(address instanceof Inet6Address) || isNonPublicAddress(address);
        } catch (UnknownHostException ignored) {
            return true;
        }
    }

    private static boolean resolvesToPrivateAddress(String host) {
        try {
            InetAddress[] addresses = InetAddress.getAllByName(host);
            if (addresses.length == 0)
                return false;
            for (InetAddress address : addresses) {
                if (isNonPublicAddress(address))
                    return true;
            }
        } catch (UnknownHostException ignored) {
            // Let Media3 surface transient DNS failures as bounded network errors.
        }
        return false;
    }

    private static boolean isNonPublicAddress(InetAddress address) {
        if (address.isAnyLocalAddress()
                || address.isLoopbackAddress()
                || address.isLinkLocalAddress()
                || address.isSiteLocalAddress()
                || address.isMulticastAddress())
            return true;
        if (address instanceof Inet4Address)
            return isPrivateIpv4Literal(address.getHostAddress());
        if (address instanceof Inet6Address) {
            byte[] bytes = address.getAddress();
            return bytes != null
                && bytes.length == 16
                && (bytes[0] & 0xfe) == 0xfc;
        }
        return true;
    }

    private void runOnMain(Runnable command) {
        if (Looper.myLooper() == Looper.getMainLooper())
            command.run();
        else
            mainHandler.post(command);
    }

    private void runOnMainSync(Runnable command) {
        callOnMain(() -> {
            command.run();
            return null;
        });
    }

    private interface MainCallable<T> {
        T call();
    }

    private <T> T callOnMain(MainCallable<T> callable) {
        if (Looper.myLooper() == Looper.getMainLooper())
            return callable.call();

        CountDownLatch latch = new CountDownLatch(1);
        AtomicReference<T> result = new AtomicReference<>();
        AtomicReference<RuntimeException> failure = new AtomicReference<>();
        mainHandler.post(() -> {
            try {
                result.set(callable.call());
            } catch (RuntimeException error) {
                failure.set(error);
            } finally {
                latch.countDown();
            }
        });
        try {
            latch.await();
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("Interrupted while waiting for Android main looper", error);
        }
        if (failure.get() != null)
            throw failure.get();
        return result.get();
    }

    private Player.Listener createListener() {
        return new Player.Listener() {
            @Override
            public void onPlaybackStateChanged(int playbackState) {
                if (released)
                    return;
                if (playbackState == Player.STATE_READY) {
                    nativeOnReady(nativeHandle, activeGeneration,
                        player.getDuration(), player.getCurrentPosition(), player.getPlayWhenReady());
                    startTelemetry();
                } else if (playbackState == Player.STATE_ENDED) {
                    emitPlaybackSnapshot();
                    stopTelemetry();
                    nativeOnEnded(nativeHandle, activeGeneration);
                } else if (playbackState == Player.STATE_BUFFERING) {
                    startTelemetry();
                }
            }

            @Override
            public void onPlayerError(PlaybackException error) {
                if (released)
                    return;
                stopTelemetry();
                String family = classifyErrorFamily(error.errorCode);
                emitError(activeGeneration, family, family + "_error", "Playback failed");
            }

            @Override
            public void onTimelineChanged(Timeline timeline, int reason) {
                if (released)
                    return;
                nativeOnTimeline(nativeHandle, activeGeneration,
                    player.getDuration(), player.isCurrentMediaItemSeekable(),
                    player.isCurrentMediaItemLive());
            }

            @Override
            public void onVideoSizeChanged(VideoSize videoSize) {
                if (!released) {
                    nativeOnVideoSize(nativeHandle, activeGeneration,
                        videoSize.width, videoSize.height, videoSize.pixelWidthHeightRatio);
                }
            }

            @Override
            public void onRenderedFirstFrame() {
                if (!released)
                    nativeOnFirstFrame(nativeHandle, activeGeneration);
            }

            @Override
            public void onPositionDiscontinuity(
                    Player.PositionInfo oldPosition,
                    Player.PositionInfo newPosition,
                    int reason) {
                if (!released && reason == Player.DISCONTINUITY_REASON_SEEK) {
                    nativeOnSeekDiscontinuity(nativeHandle, activeGeneration,
                        oldPosition.positionMs, newPosition.positionMs);
                }
            }

            @Override
            public void onTracksChanged(Tracks tracks) {
                if (released)
                    return;
                nativeOnTracks(nativeHandle, activeGeneration, tracksToJson(tracks));
                accumulateChaptersFromTracks(tracks);
            }

            @Override
            public void onMetadata(Metadata metadata) {
                if (released)
                    return;
                if (accumulateChapters(metadata))
                    emitChapters();
            }

            @Override
            public void onMediaMetadataChanged(MediaMetadata mediaMetadata) {
                if (!released) {
                    nativeOnMetadata(nativeHandle, activeGeneration,
                        metadataToJson(mediaMetadata));
                }
            }

            @Override
            public void onCues(CueGroup cueGroup) {
                if (!released)
                    nativeOnCues(nativeHandle, activeGeneration, cuesToJson(cueGroup));
            }
        };
    }

    private void emitError(long generation, String family, String code, String message) {
        nativeOnError(nativeHandle, generation, family, code, message);
    }

    private static String classifyErrorFamily(int errorCode) {
        if (errorCode >= 2000 && errorCode < 3000) {
            if (errorCode >= 2001 && errorCode <= 2004)
                return "network";
            return "source";
        }
        if (errorCode >= 3000 && errorCode < 4000)
            return "source";
        if (errorCode >= 4000 && errorCode < 5000)
            return "decoder";
        if (errorCode >= 5000 && errorCode < 6000)
            return "audio";
        if (errorCode >= 6000 && errorCode < 7000)
            return "drm";
        return "unexpected";
    }

    private void accumulateChaptersFromTracks(Tracks tracks) {
        boolean changed = false;
        for (Tracks.Group group : tracks.getGroups()) {
            for (int trackIndex = 0; trackIndex < group.length; ++trackIndex) {
                Metadata metadata = group.getTrackFormat(trackIndex).metadata;
                changed |= accumulateChapters(metadata);
            }
        }
        if (changed)
            emitChapters();
    }

    private boolean accumulateChapters(Metadata metadata) {
        if (metadata == null)
            return false;
        boolean changed = false;
        long windowOffsetMs = currentWindowOffsetMs();
        for (Chapter chapter : metadata.getEntriesOfType(Chapter.class)) {
            if (chapter.isHidden())
                continue;
            long rawStartMs = chapter.getStartTimeMs();
            if (rawStartMs == C.TIME_UNSET || rawStartMs < 0)
                continue;
            long startMs = Math.max(0L, rawStartMs - windowOffsetMs);
            Label label = chapter.getTitle();
            String title = label == null ? "" : label.value;
            String prior = chaptersByStartMs.put(startMs, title == null ? "" : title);
            if (prior == null || !prior.equals(title))
                changed = true;
        }
        return changed;
    }

    private long currentWindowOffsetMs() {
        Timeline timeline = player.getCurrentTimeline();
        int index = player.getCurrentMediaItemIndex();
        if (timeline.isEmpty() || index < 0 || index >= timeline.getWindowCount())
            return 0L;
        Timeline.Window window = timeline.getWindow(index, new Timeline.Window());
        return window.positionInFirstPeriodUs / 1000L;
    }

    private void emitChapters() {
        nativeOnChapters(nativeHandle, activeGeneration, chaptersToJson());
    }

    private String chaptersToJson() {
        StringBuilder out = new StringBuilder("{\"chapters\":[");
        boolean first = true;
        for (Map.Entry<Long, String> entry : chaptersByStartMs.entrySet()) {
            if (!first)
                out.append(',');
            first = false;
            out.append("{\"title\":");
            appendJsonValue(out, entry.getValue());
            out.append(",\"startMs\":").append(entry.getKey()).append('}');
        }
        return out.append("]}").toString();
    }

    private static String tracksToJson(Tracks tracks) {
        StringBuilder out = new StringBuilder("{\"groups\":[");
        List<Tracks.Group> groups = tracks.getGroups();
        for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            if (groupIndex > 0)
                out.append(',');
            Tracks.Group group = groups.get(groupIndex);
            out.append("{\"type\":").append(group.getType())
                .append(",\"selected\":").append(group.isSelected())
                .append(",\"tracks\":[");
            for (int trackIndex = 0; trackIndex < group.length; ++trackIndex) {
                if (trackIndex > 0)
                    out.append(',');
                Format format = group.getTrackFormat(trackIndex);
                out.append('{');
                appendJsonString(out, "id", format.id, false);
                appendJsonString(out, "label", format.label, true);
                appendJsonString(out, "language", format.language, true);
                appendJsonString(out, "mimeType", format.sampleMimeType, true);
                out.append(",\"selected\":").append(group.isTrackSelected(trackIndex));
                out.append('}');
            }
            out.append("]}");
        }
        return out.append("]}").toString();
    }

    private static String metadataToJson(MediaMetadata metadata) {
        StringBuilder out = new StringBuilder("{");
        appendJsonString(out, "title", charSequence(metadata.title), false);
        appendJsonString(out, "artist", charSequence(metadata.artist), true);
        appendJsonString(out, "albumTitle", charSequence(metadata.albumTitle), true);
        return out.append('}').toString();
    }

    private static String cuesToJson(CueGroup cueGroup) {
        StringBuilder out = new StringBuilder("{\"cues\":[");
        for (int i = 0; i < cueGroup.cues.size(); ++i) {
            if (i > 0)
                out.append(',');
            CharSequence text = cueGroup.cues.get(i).text;
            out.append("{\"text\":");
            appendJsonValue(out, charSequence(text));
            out.append('}');
        }
        return out.append("]}").toString();
    }

    private static String charSequence(CharSequence value) {
        return value == null ? null : value.toString();
    }

    private static void appendJsonString(
            StringBuilder out, String key, String value, boolean leadingComma) {
        if (leadingComma)
            out.append(',');
        out.append('"').append(key).append("\":");
        appendJsonValue(out, value);
    }

    private static void appendJsonValue(StringBuilder out, String value) {
        if (value == null) {
            out.append("null");
            return;
        }
        out.append('"');
        for (int i = 0; i < value.length(); ++i) {
            char ch = value.charAt(i);
            switch (ch) {
            case '"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (ch < 0x20)
                    out.append(String.format(Locale.ROOT, "\\u%04x", (int) ch));
                else
                    out.append(ch);
            }
        }
        out.append('"');
    }

    private static native void nativeOnPlaybackSnapshot(
        long nativeHandle, long generation, long positionMs, long durationMs,
        long bufferedPositionMs, double bufferedPercentage, boolean paused,
        float requestedVolume, boolean muted, double speed);
    private static native void nativeOnReady(
        long nativeHandle, long generation, long durationMs, long positionMs, boolean playWhenReady);
    private static native void nativeOnEnded(long nativeHandle, long generation);
    private static native void nativeOnError(
        long nativeHandle, long generation, String family, String code, String message);
    private static native void nativeOnTimeline(
        long nativeHandle, long generation, long durationMs, boolean seekable, boolean live);
    private static native void nativeOnVideoSize(
        long nativeHandle, long generation, int width, int height, float pixelWidthHeightRatio);
    private static native void nativeOnFirstFrame(long nativeHandle, long generation);
    private static native void nativeOnSeekDiscontinuity(
        long nativeHandle, long generation, long oldPositionMs, long newPositionMs);
    private static native void nativeOnTracks(
        long nativeHandle, long generation, String tracksJson);
    private static native void nativeOnMetadata(
        long nativeHandle, long generation, String metadataJson);
    private static native void nativeOnChapters(
        long nativeHandle, long generation, String chaptersJson);
    private static native void nativeOnCues(
        long nativeHandle, long generation, String cuesJson);
}
