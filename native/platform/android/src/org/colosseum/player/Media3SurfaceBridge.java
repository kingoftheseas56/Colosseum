package org.colosseum.player;

import android.graphics.SurfaceTexture;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Owns the Android objects that wrap a Qt-created GL_TEXTURE_EXTERNAL_OES name.
 * The texture is created, updated and released by native code on the Qt render thread.
 * Frame callbacks only schedule another scene-graph render.
 */
public final class Media3SurfaceBridge {
    private final long nativeHandle;
    private final long surfaceGeneration;
    private final SurfaceTexture surfaceTexture;
    private final Surface surface;
    private final AtomicBoolean released = new AtomicBoolean(false);

    public Media3SurfaceBridge(int textureId, long nativeHandle, long surfaceGeneration) {
        if (textureId <= 0)
            throw new IllegalArgumentException("textureId must be positive");
        if (nativeHandle == 0 || surfaceGeneration <= 0)
            throw new IllegalArgumentException("native handle and surface generation are required");

        this.nativeHandle = nativeHandle;
        this.surfaceGeneration = surfaceGeneration;
        surfaceTexture = new SurfaceTexture(textureId);
        surface = new Surface(surfaceTexture);
        surfaceTexture.setOnFrameAvailableListener(
                ignored -> {
                    if (!released.get())
                        nativeOnFrameAvailable(nativeHandle, surfaceGeneration);
                },
                new Handler(Looper.getMainLooper()));
    }

    public Surface surface() {
        return surface;
    }

    public long surfaceGeneration() {
        return surfaceGeneration;
    }

    public void updateTexImage(long expectedGeneration) {
        if (!isCurrent(expectedGeneration))
            return;
        surfaceTexture.updateTexImage();
    }

    public void getTransformMatrix(long expectedGeneration, float[] out16) {
        if (out16 == null || out16.length < 16)
            throw new IllegalArgumentException("matrix must contain 16 floats");
        if (!isCurrent(expectedGeneration))
            return;
        surfaceTexture.getTransformMatrix(out16);
    }

    public void release() {
        if (!released.compareAndSet(false, true))
            return;
        surfaceTexture.setOnFrameAvailableListener(null);
        surface.release();
        surfaceTexture.release();
    }

    private boolean isCurrent(long expectedGeneration) {
        return !released.get() && expectedGeneration == surfaceGeneration;
    }

    private static native void nativeOnFrameAvailable(long nativeHandle, long surfaceGeneration);
}
