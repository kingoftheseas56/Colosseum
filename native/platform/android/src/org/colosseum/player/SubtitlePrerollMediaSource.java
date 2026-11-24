package org.colosseum.player;

import androidx.media3.common.MimeTypes;
import androidx.media3.common.StreamKey;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.exoplayer.LoadingInfo;
import androidx.media3.exoplayer.SeekParameters;
import androidx.media3.exoplayer.source.MediaPeriod;
import androidx.media3.exoplayer.source.MediaSource;
import androidx.media3.exoplayer.source.SampleStream;
import androidx.media3.exoplayer.source.TrackGroupArray;
import androidx.media3.exoplayer.source.WrappingMediaSource;
import androidx.media3.exoplayer.trackselection.ExoTrackSelection;
import androidx.media3.exoplayer.upstream.Allocator;

import java.io.IOException;
import java.util.List;

/** Keeps the packets needed by delayed subtitles when opening or seeking a source. */
@UnstableApi
final class SubtitlePrerollMediaSource extends WrappingMediaSource {
    private final EmbeddedSubtitleRenderer subtitles;

    SubtitlePrerollMediaSource(MediaSource source, EmbeddedSubtitleRenderer subtitles) {
        super(source);
        this.subtitles = subtitles;
    }

    @Override
    public MediaPeriod createPeriod(MediaPeriodId id, Allocator allocator, long startPositionUs) {
        MediaPeriod child = mediaSource.createPeriod(id, allocator, subtitles.prerollStartUs(startPositionUs));
        return new Period(child);
    }

    @Override
    public void releasePeriod(MediaPeriod mediaPeriod) {
        mediaSource.releasePeriod(((Period) mediaPeriod).child);
    }

    private final class Period implements MediaPeriod, MediaPeriod.Callback {
        private final MediaPeriod child;
        private Callback callback;
        private boolean timedTextSelected;

        Period(MediaPeriod child) {
            this.child = child;
        }

        @Override
        public void prepare(Callback callback, long positionUs) {
            this.callback = callback;
            child.prepare(this, subtitles.prerollStartUs(positionUs));
        }

        @Override
        public void onPrepared(MediaPeriod mediaPeriod) {
            callback.onPrepared(this);
        }

        @Override
        public void onContinueLoadingRequested(MediaPeriod mediaPeriod) {
            callback.onContinueLoadingRequested(this);
        }

        @Override
        public void maybeThrowPrepareError() throws IOException {
            child.maybeThrowPrepareError();
        }

        @Override
        public TrackGroupArray getTrackGroups() {
            return child.getTrackGroups();
        }

        @Override
        public List<StreamKey> getStreamKeys(List<ExoTrackSelection> trackSelections) {
            return child.getStreamKeys(trackSelections);
        }

        @Override
        public long selectTracks(ExoTrackSelection[] selections, boolean[] mayRetainStreamFlags,
                SampleStream[] streams, boolean[] streamResetFlags, long positionUs) {
            timedTextSelected = false;
            long loadPositionUs = positionUs;
            for (int i = 0; i < selections.length; i++) {
                ExoTrackSelection selection = selections[i];
                if (selection == null || !MimeTypes.APPLICATION_MEDIA3_CUES.equals(
                        selection.getSelectedFormat().sampleMimeType))
                    continue;
                timedTextSelected = true;
                if (streams[i] == null || !mayRetainStreamFlags[i])
                    loadPositionUs = subtitles.prerollStartUs(positionUs);
                break;
            }
            long adjustedUs = child.selectTracks(selections, mayRetainStreamFlags, streams,
                    streamResetFlags, loadPositionUs);
            return restorePlaybackPosition(positionUs, loadPositionUs, adjustedUs);
        }

        @Override
        public long seekToUs(long positionUs) {
            long loadPositionUs = timedTextSelected ? subtitles.prerollStartUs(positionUs) : positionUs;
            return restorePlaybackPosition(positionUs, loadPositionUs, child.seekToUs(loadPositionUs));
        }

        private long restorePlaybackPosition(long requestedUs, long loadUs, long adjustedUs) {
            // Decoders may consume earlier packets. ExoPlayer's renderer position stays
            // at the user's seek target, so they discard the pre-roll audio/video output.
            return loadUs < requestedUs ? Math.max(requestedUs, adjustedUs) : adjustedUs;
        }

        @Override
        public void discardBuffer(long positionUs, boolean toKeyframe) {
            child.discardBuffer(timedTextSelected ? subtitles.prerollStartUs(positionUs) : positionUs,
                    toKeyframe);
        }

        @Override
        public long readDiscontinuity() {
            return child.readDiscontinuity();
        }

        @Override
        public void setUsesStreamPrerollFlags() {
            child.setUsesStreamPrerollFlags();
        }

        @Override
        public long getAdjustedSeekPositionUs(long positionUs, SeekParameters seekParameters) {
            return child.getAdjustedSeekPositionUs(positionUs, seekParameters);
        }

        @Override
        public long getBufferedPositionUs() {
            return child.getBufferedPositionUs();
        }

        @Override
        public long getNextLoadPositionUs() {
            return child.getNextLoadPositionUs();
        }

        @Override
        public boolean continueLoading(LoadingInfo loadingInfo) {
            return child.continueLoading(loadingInfo);
        }

        @Override
        public boolean isLoading() {
            return child.isLoading();
        }

        @Override
        public void reevaluateBuffer(long positionUs) {
            child.reevaluateBuffer(positionUs);
        }

        @Override
        public long setEndPositionUs(long endPositionUs) {
            return child.setEndPositionUs(endPositionUs);
        }
    }
}
