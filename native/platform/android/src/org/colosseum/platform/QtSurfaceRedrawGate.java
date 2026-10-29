package org.colosseum.platform;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import java.util.ArrayList;

// Supplements Qt 6.11's Callback with Android's asynchronous redraw handshake.
// Qt still receives all its own create/change/destroy callbacks unchanged.
final class QtSurfaceRedrawGate implements SurfaceHolder.Callback2,
        ViewTreeObserver.OnGlobalLayoutListener {
    private static final String TAG = "ColosseumSurface";
    private final View root;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ArrayList<Runnable> completions = new ArrayList<>();
    private SurfaceView surfaceView;
    private long bridgeHandle;
    private long token;
    private Runnable timeout;
    private static native void nativeRequestRedraw(long handle, long token, int width, int height);

    QtSurfaceRedrawGate(Activity activity) {
        root = activity.getWindow().getDecorView();
        root.getViewTreeObserver().addOnGlobalLayoutListener(this);
        onGlobalLayout();
    }

    void setBridgeHandle(long handle) {
        if (handle == 0) finishPending();
        bridgeHandle = handle;
    }

    @Override public void onGlobalLayout() {
        if (surfaceView != null && !surfaceView.isAttachedToWindow()) {
            surfaceView.getHolder().removeCallback(this);
            surfaceView = null;
            finishPending();
        }
        if (surfaceView == null) findSurface(root);
    }

    private void findSurface(View view) {
        // Only Qt's primary render surface; native reader/player views keep their owner.
        if (view instanceof SurfaceView
                && "org.qtproject.qt.android.QtSurface".equals(view.getClass().getName())) {
            surfaceView = (SurfaceView) view;
            surfaceView.getHolder().addCallback(this);
            Log.i(TAG, "observing Qt redraws");
            return;
        }
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount() && surfaceView == null; ++i)
                findSurface(group.getChildAt(i));
        }
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {}
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {}
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        if (bridgeHandle != 0) nativeRequestRedraw(bridgeHandle, 0, 0, 0);
        finishPending();
    }

    @Override public void surfaceRedrawNeeded(SurfaceHolder holder) {
        // API 26+ always calls the async override; Colosseum requires API 28+.
        if (surfaceView != null) surfaceView.invalidate();
    }

    @Override public void surfaceRedrawNeededAsync(SurfaceHolder holder, Runnable drawingFinished) {
        if (bridgeHandle == 0 || !holder.getSurface().isValid()
                || holder.getSurfaceFrame().isEmpty()) {
            drawingFinished.run();
            return;
        }
        completions.add(drawingFinished);
        long requested = ++token;
        if (timeout != null) handler.removeCallbacks(timeout);
        timeout = () -> {
            if (requested != token) return;
            Log.w(TAG, "redraw acknowledgement timed out token=" + requested);
            nativeRequestRedraw(bridgeHandle, 0, 0, 0);
            finishPending();
        };
        // A teardown or failed renderer must not freeze Android's window transaction.
        handler.postDelayed(timeout, 2000);
        int width = holder.getSurfaceFrame().width();
        int height = holder.getSurfaceFrame().height();
        Log.i(TAG, "redraw requested token=" + requested + " size=" + width + "x" + height);
        nativeRequestRedraw(bridgeHandle, requested, width, height);
    }

    void framePresented(long completedToken) {
        if (completedToken != token || completions.isEmpty()) return;
        Log.i(TAG, "redraw presented token=" + completedToken);
        finishPending();
    }

    private void finishPending() {
        if (timeout != null) handler.removeCallbacks(timeout);
        timeout = null;
        ArrayList<Runnable> finished = new ArrayList<>(completions);
        completions.clear();
        for (Runnable completion : finished) completion.run();
    }

    void dispose() {
        if (root.getViewTreeObserver().isAlive())
            root.getViewTreeObserver().removeOnGlobalLayoutListener(this);
        if (surfaceView != null) surfaceView.getHolder().removeCallback(this);
        surfaceView = null;
        setBridgeHandle(0);
    }
}
