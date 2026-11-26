package org.colosseum.player;

import android.os.Handler;
import android.os.Looper;

import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.text.Cue;
import androidx.media3.common.text.CueGroup;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.decoder.DecoderInputBuffer;
import androidx.media3.exoplayer.BaseRenderer;
import androidx.media3.exoplayer.ExoPlaybackException;
import androidx.media3.exoplayer.FormatHolder;
import androidx.media3.exoplayer.RendererCapabilities;
import androidx.media3.exoplayer.source.MediaSource;
import androidx.media3.exoplayer.source.SampleStream;
import androidx.media3.exoplayer.text.TextOutput;
import androidx.media3.extractor.text.CueDecoder;
import androidx.media3.extractor.text.CuesWithTiming;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Selects already-decoded subtitle cues on their own clock, without retiming audio or video. */
@UnstableApi
final class EmbeddedSubtitleRenderer extends BaseRenderer {
    static final int MSG_SET_DELAY = 10_000;
    private static final long LOOK_AHEAD_US = C.MICROS_PER_SECOND;
    private static final int MAX_CACHED_GROUPS = 512;

    private final TextOutput output;
    private final Handler outputHandler;
    private final CueDecoder decoder = new CueDecoder();
    private final FormatHolder formatHolder = new FormatHolder();
    private final DecoderInputBuffer buffer =
            new DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_NORMAL);
    private final ArrayList<CuesWithTiming> timeline = new ArrayList<>();
    private List<Cue> shown = Collections.emptyList();
    private boolean merge;
    private boolean inputEnded;
    private boolean outputEnded;
    private long delayUs;

    EmbeddedSubtitleRenderer(TextOutput output, Looper outputLooper) {
        super(C.TRACK_TYPE_TEXT);
        this.output = output;
        outputHandler = new Handler(outputLooper);
    }

    @Override
    public String getName() {
        return "ColosseumEmbeddedSubtitles";
    }

    @Override
    public int supportsFormat(Format format) {
        if (MimeTypes.APPLICATION_MEDIA3_CUES.equals(format.sampleMimeType))
            return RendererCapabilities.create(format.cryptoType == C.CRYPTO_TYPE_NONE
                    ? C.FORMAT_HANDLED : C.FORMAT_UNSUPPORTED_DRM);
        return RendererCapabilities.create(MimeTypes.isText(format.sampleMimeType)
                ? C.FORMAT_UNSUPPORTED_SUBTYPE : C.FORMAT_UNSUPPORTED_TYPE);
    }

    @Override
    public void handleMessage(int messageType, Object message) throws ExoPlaybackException {
        if (messageType == MSG_SET_DELAY) {
            delayUs = (Long) message;
            outputEnded = false;
        } else {
            super.handleMessage(messageType, message);
        }
    }

    long prerollStartUs(long positionUs) {
        return positionUs >= 0 ? Math.max(0, positionUs - Math.max(0, delayUs)) : positionUs;
    }

    @Override
    protected void onStreamChanged(Format[] formats, long startPositionUs, long offsetUs,
            MediaSource.MediaPeriodId mediaPeriodId) {
        merge = formats[0].cueReplacementBehavior == Format.CUE_REPLACEMENT_BEHAVIOR_MERGE;
        timeline.clear();
        resetOutput();
    }

    @Override
    protected void onPositionReset(long positionUs, boolean joining, boolean streamReset) {
        // Media3 can seek the sample stream past cues still needed by a positive delay.
        // Keep decoded cues for the current track until the renderer sees a new stream.
        resetOutput();
    }

    @Override
    protected void onDisabled() {
        timeline.clear();
        resetOutput();
    }

    private void resetOutput() {
        inputEnded = false;
        outputEnded = false;
        outputHandler.removeCallbacksAndMessages(null);
        publish(Collections.emptyList(), 0);
    }

    @Override
    public boolean isEnded() {
        return outputEnded;
    }

    @Override
    public boolean isReady() {
        try {
            maybeThrowStreamError();
            return true;
        } catch (IOException failure) {
            return false;
        }
    }

    @Override
    public void render(long positionUs, long elapsedRealtimeUs) {
        long cueTimeUs = (long) ((double) positionUs - delayUs);
        long readUntilUs = Math.min(Long.MAX_VALUE - LOOK_AHEAD_US,
                Math.max(positionUs, cueTimeUs)) + LOOK_AHEAD_US;
        if (!inputEnded)
            readCueAtOrBefore(readUntilUs, cueTimeUs);

        List<Cue> active = cuesAt(cueTimeUs);
        if (!shown.equals(active))
            publish(active, Math.max(0, positionUs - getStreamOffsetUs()));

        long streamEndUs = getStreamEndPositionUs();
        outputEnded = inputEnded && (timeline.isEmpty()
                || (streamEndUs != C.TIME_UNSET && positionUs - getStreamOffsetUs() >= streamEndUs)
                || noMoreChanges(cueTimeUs));
    }

    private void readCueAtOrBefore(long timeUs, long cueTimeUs) {
        buffer.clear();
        int result = readSource(formatHolder, buffer,
                SampleStream.FLAG_PEEK | SampleStream.FLAG_OMIT_SAMPLE_DATA);
        if (result != C.RESULT_BUFFER_READ || (!buffer.isEndOfStream() && buffer.timeUs > timeUs))
            return;

        buffer.clear();
        if (readSource(formatHolder, buffer, 0) != C.RESULT_BUFFER_READ)
            return;
        if (buffer.isEndOfStream()) {
            inputEnded = true;
            return;
        }
        buffer.flip();
        ByteBuffer data = buffer.data;
        if (data == null)
            return;
        CuesWithTiming decoded =
                decoder.decode(buffer.timeUs, data.array(), data.arrayOffset(), data.limit());
        // The shared QML overlay displays text. Keep cue timing without holding decoded bitmaps.
        List<Cue> text = new ArrayList<>();
        for (Cue cue : decoded.cues) {
            if (cue.text != null)
                text.add(cue);
        }
        CuesWithTiming sample = new CuesWithTiming(text, decoded.startTimeUs, decoded.durationUs);

        int index = timeline.size();
        while (index > 0 && timeline.get(index - 1).startTimeUs > sample.startTimeUs)
            index--;
        for (int i = index - 1; i >= 0 && timeline.get(i).startTimeUs == sample.startTimeUs; i--) {
            CuesWithTiming known = timeline.get(i);
            if (known.endTimeUs == sample.endTimeUs && known.cues.equals(sample.cues))
                return;
        }
        timeline.add(index, sample);
        if (timeline.size() > MAX_CACHED_GROUPS) {
            int last = timeline.size() - 1;
            double distanceToFirst = Math.abs((double) cueTimeUs - timeline.get(0).startTimeUs);
            double distanceToLast = Math.abs((double) cueTimeUs - timeline.get(last).startTimeUs);
            timeline.remove(distanceToFirst > distanceToLast ? 0 : last);
        }
    }

    private List<Cue> cuesAt(long timeUs) {
        List<Cue> active = Collections.emptyList();
        for (int i = timeline.size() - 1; i >= 0; i--) {
            CuesWithTiming sample = timeline.get(i);
            if (sample.startTimeUs > timeUs)
                continue;
            boolean visible = sample.endTimeUs == C.TIME_UNSET || timeUs < sample.endTimeUs;
            if (!merge)
                return visible ? sample.cues : Collections.emptyList();
            if (visible && !sample.cues.isEmpty()) {
                if (active.isEmpty())
                    active = new ArrayList<>();
                active.addAll(0, sample.cues);
            }
        }
        return active;
    }

    private boolean noMoreChanges(long timeUs) {
        for (CuesWithTiming sample : timeline) {
            if (sample.startTimeUs > timeUs
                    || (sample.endTimeUs != C.TIME_UNSET && sample.endTimeUs > timeUs))
                return false;
        }
        return true;
    }

    private void publish(List<Cue> cues, long presentationTimeUs) {
        shown = cues;
        CueGroup group = new CueGroup(cues, presentationTimeUs);
        outputHandler.post(() -> output.onCues(group));
    }
}
