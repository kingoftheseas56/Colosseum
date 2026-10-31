package org.colosseum.player;

import android.graphics.SurfaceTexture;
import android.media.MediaCodec;
import android.media.MediaFormat;
import android.view.Surface;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.Arrays;
import org.json.JSONArray;
import org.json.JSONObject;

/** Device regression: preserve AVC data while making odd container sizes configurable. */
public final class Media3CodecFormatHarness {
    // W3C Bunny: MP4 width 853, AVC SPS width 854 (54 macroblocks minus 5*2 crop).
    private static final byte[] SPS = hex("00000001274d401ea9181b07bcde00d4040406db0ad7bdf010");
    private static final byte[] PPS = hex("0000000128de09c8");

    public static void main(String[] args) throws Exception {
        Method normalize = null;
        try {
            normalize = Class.forName("org.colosseum.player.Media3CodecFormat")
                    .getMethod("normalize", String.class, MediaFormat.class);
        } catch (ClassNotFoundException absentBeforeFix) { /* Exercise the unmodified format. */ }
        JSONArray cases = new JSONArray();
        int failures = 0;
        for (String name : new String[]{"c2.unisoc.avc.decoder", "c2.android.avc.decoder"}) {
            for (int width : new int[]{853, 854}) {
                JSONObject row = new JSONObject().put("codec", name).put("containerWidth", width);
                MediaFormat format = MediaFormat.createVideoFormat("video/avc", width, 480);
                format.setInteger(MediaFormat.KEY_MAX_WIDTH, width);
                format.setInteger(MediaFormat.KEY_MAX_HEIGHT, 480);
                format.setByteBuffer("csd-0", ByteBuffer.wrap(SPS.clone()));
                format.setByteBuffer("csd-1", ByteBuffer.wrap(PPS.clone()));
                MediaCodec codec = null;
                SurfaceTexture texture = null;
                Surface surface = null;
                try {
                    boolean changed = normalize != null && (Boolean) normalize.invoke(null, name, format);
                    require(format.getInteger(MediaFormat.KEY_WIDTH) == 854, "codec width must be 854");
                    require(format.getInteger(MediaFormat.KEY_HEIGHT) == 480, "height must be preserved");
                    require(format.getInteger(MediaFormat.KEY_MAX_WIDTH) == 854, "adaptive maximum must fit");
                    require(format.getInteger(MediaFormat.KEY_MAX_HEIGHT) == 480, "maximum height must be preserved");
                    require(Arrays.equals(bytes(format.getByteBuffer("csd-0")), SPS), "SPS changed");
                    require(Arrays.equals(bytes(format.getByteBuffer("csd-1")), PPS), "PPS changed");
                    require(changed == (width == 853), "aligned input must remain untouched");
                    texture = new SurfaceTexture(false);
                    surface = new Surface(texture);
                    codec = MediaCodec.createByCodecName(name);
                    codec.configure(format, surface, null, 0);
                    row.put("passed", true).put("codecWidth", format.getInteger(MediaFormat.KEY_WIDTH));
                } catch (Throwable failure) {
                    failures++;
                    row.put("passed", false).put("error", failure.toString());
                } finally {
                    if (codec != null) codec.release();
                    if (surface != null) surface.release();
                    if (texture != null) texture.release();
                }
                cases.put(row);
            }
        }
        System.out.println(new JSONObject().put("normalizerLoaded", normalize != null)
                .put("passed", failures == 0).put("failed", failures).put("cases", cases));
        System.exit(failures == 0 ? 0 : 1);
    }

    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static byte[] bytes(ByteBuffer buffer) {
        ByteBuffer copy = buffer.duplicate();
        byte[] out = new byte[copy.remaining()];
        copy.get(out);
        return out;
    }

    private static byte[] hex(String value) {
        byte[] out = new byte[value.length() / 2];
        for (int i = 0; i < out.length; i++)
            out[i] = (byte) Integer.parseInt(value.substring(i * 2, i * 2 + 2), 16);
        return out;
    }
}
