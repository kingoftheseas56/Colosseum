package org.colosseum.vault;

import android.content.Context;
import android.graphics.Bitmap;
import android.media.MediaMetadataRetriever;
import android.net.Uri;

import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public final class VaultMediaProbe {
    private static final int MIN_TIMEOUT_MS = 100;

    private VaultMediaProbe() {}

    public static String probe(Context context, String source, int timeoutMs) {
        ExecutorService executor = Executors.newSingleThreadExecutor(r -> {
            Thread thread = new Thread(r, "ColosseumVaultProbe");
            thread.setDaemon(true);
            return thread;
        });
        Future<String> future = executor.submit(() -> probeNow(context, source));
        try {
            return future.get(Math.max(MIN_TIMEOUT_MS, timeoutMs), TimeUnit.MILLISECONDS);
        } catch (TimeoutException timeout) {
            return result("RejectedTimeout", 0, 0, "timeout");
        } catch (Exception error) {
            return result("RejectedError", 0, 0, safeMessage(error));
        } finally {
            future.cancel(true);
            executor.shutdownNow();
        }
    }

    private static String probeNow(Context context, String source) {
        MediaMetadataRetriever retriever = new MediaMetadataRetriever();
        Bitmap frame = null;
        try {
            setDataSource(retriever, context, source);
            String metaWidth = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH);
            String metaHeight = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT);
            frame = retriever.getFrameAtTime(0L, MediaMetadataRetriever.OPTION_CLOSEST_SYNC);
            if (frame == null) {
                boolean hasVideoMetadata = !isBlank(metaWidth) || !isBlank(metaHeight);
                return result(hasVideoMetadata ? "RejectedError" : "RejectedNoVideo",
                              0, 0, hasVideoMetadata ? "frame decode failed" : "no video frame");
            }
            int width = frame.getWidth();
            int height = frame.getHeight();
            return width > 0 && height > 0
                ? result("Admitted", width, height, "")
                : result("RejectedError", 0, 0, "decoded frame has no dimensions");
        } catch (Exception error) {
            return result("RejectedError", 0, 0, safeMessage(error));
        } finally {
            if (frame != null)
                frame.recycle();
            try {
                retriever.release();
            } catch (Exception ignored) {
            }
        }
    }

    public static boolean writeThumbnail(Context context, String source,
                                         String outputPath, long timeUs) {
        MediaMetadataRetriever retriever = new MediaMetadataRetriever();
        Bitmap frame = null;
        Bitmap scaled = null;
        File output = new File(outputPath);
        try {
            setDataSource(retriever, context, source);
            frame = retriever.getFrameAtTime(Math.max(0L, timeUs),
                                             MediaMetadataRetriever.OPTION_CLOSEST_SYNC);
            if (frame == null)
                return false;
            Bitmap toWrite = frame;
            if (frame.getWidth() > 320 && frame.getHeight() > 0) {
                int height = Math.max(2, (int) Math.round(frame.getHeight() * (320.0 / frame.getWidth())));
                scaled = Bitmap.createScaledBitmap(frame, 320, height, true);
                toWrite = scaled;
            }
            File parent = output.getParentFile();
            if (parent != null && !parent.exists() && !parent.mkdirs())
                return false;
            try (FileOutputStream stream = new FileOutputStream(output)) {
                if (!toWrite.compress(Bitmap.CompressFormat.JPEG, 86, stream)) {
                    output.delete();
                    return false;
                }
            }
            return output.isFile() && output.length() > 0L;
        } catch (Exception error) {
            output.delete();
            return false;
        } finally {
            if (scaled != null && scaled != frame)
                scaled.recycle();
            if (frame != null)
                frame.recycle();
            try {
                retriever.release();
            } catch (Exception ignored) {
            }
        }
    }

    private static void setDataSource(MediaMetadataRetriever retriever,
                                      Context context, String source) {
        String value = source == null ? "" : source.trim();
        if (value.startsWith("content://") || value.startsWith("file://")) {
            retriever.setDataSource(context, Uri.parse(value));
        } else {
            retriever.setDataSource(value);
        }
    }

    private static boolean isBlank(String value) {
        return value == null || value.trim().isEmpty();
    }

    private static String safeMessage(Throwable error) {
        String message = error == null ? "" : error.getMessage();
        if (message == null || message.trim().isEmpty())
            return error == null ? "media probe failed" : error.getClass().getSimpleName();
        return message;
    }

    private static String result(String verdict, int width, int height, String detail) {
        try {
            JSONObject json = new JSONObject();
            json.put("verdict", verdict);
            json.put("width", width);
            json.put("height", height);
            json.put("detail", detail == null ? "" : detail);
            return json.toString();
        } catch (Exception ignored) {
            return "{\"verdict\":\"RejectedError\",\"width\":0,\"height\":0,\"detail\":\"json failure\"}";
        }
    }
}
