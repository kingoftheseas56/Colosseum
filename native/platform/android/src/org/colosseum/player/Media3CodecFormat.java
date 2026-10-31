package org.colosseum.player;

import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.util.Log;

/** Keeps codec configuration within the decoder's declared AVC alignment. */
public final class Media3CodecFormat {
    private Media3CodecFormat() {}

    public static boolean normalize(String decoderName, MediaFormat format) {
        if (format == null || !"video/avc".equals(format.getString(MediaFormat.KEY_MIME))
                || !format.containsKey(MediaFormat.KEY_WIDTH)
                || !format.containsKey(MediaFormat.KEY_HEIGHT))
            return false;
        final int width = format.getInteger(MediaFormat.KEY_WIDTH);
        final int height = format.getInteger(MediaFormat.KEY_HEIGHT);
        if (width <= 0 || height <= 0)
            return false;

        for (MediaCodecInfo info : new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()) {
            if (!info.getName().equals(decoderName) || info.isEncoder())
                continue;
            MediaCodecInfo.VideoCapabilities caps =
                    info.getCapabilitiesForType("video/avc").getVideoCapabilities();
            if (caps == null || caps.isSizeSupported(width, height))
                return false;
            int alignedWidth = align(width, caps.getWidthAlignment());
            int alignedHeight = align(height, caps.getHeightAlignment());
            if (alignedWidth <= 0 || alignedHeight <= 0
                    || !caps.isSizeSupported(alignedWidth, alignedHeight))
                return false;

            // Only the MediaCodec configuration is changed. The source Format,
            // presentation metadata, AVC initialization data and fallback stay intact.
            format.setInteger(MediaFormat.KEY_WIDTH, alignedWidth);
            format.setInteger(MediaFormat.KEY_HEIGHT, alignedHeight);
            if (format.containsKey(MediaFormat.KEY_MAX_WIDTH)) {
                int maximum = align(Math.max(format.getInteger(MediaFormat.KEY_MAX_WIDTH), alignedWidth),
                        caps.getWidthAlignment());
                if (maximum > 0) format.setInteger(MediaFormat.KEY_MAX_WIDTH, maximum);
            }
            if (format.containsKey(MediaFormat.KEY_MAX_HEIGHT)) {
                int maximum = align(Math.max(format.getInteger(MediaFormat.KEY_MAX_HEIGHT), alignedHeight),
                        caps.getHeightAlignment());
                if (maximum > 0) format.setInteger(MediaFormat.KEY_MAX_HEIGHT, maximum);
            }
            Log.i("ColosseumCodec", decoderName + " AVC config " + width + "x" + height
                    + " -> " + alignedWidth + "x" + alignedHeight);
            return true;
        }
        return false;
    }

    private static int align(int size, int alignment) {
        if (alignment <= 0) return -1;
        long value = ((long) size + alignment - 1) / alignment * alignment;
        return value <= Integer.MAX_VALUE ? (int) value : -1;
    }
}
