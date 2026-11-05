package org.colosseum.platform;

import android.content.Intent;
import android.os.Bundle;
import android.os.Build;
import android.content.res.Configuration;
import org.qtproject.qt.android.bindings.QtActivity;

public final class ColosseumActivity extends QtActivity {
    private final PlayerOrientationRequest playerOrientation = new PlayerOrientationRequest();
    private long backBridgeHandle;
    private Object backCallback;
    private boolean playbackBrightnessOwned;
    private float previousWindowBrightness;
    private QtSurfaceRedrawGate surfaceRedrawGate;
    private static native void nativeBackGesture(long handle, int phase, float progress, int edge);
    private static native void nativePlaybackBrightness(long handle, float value);
    private static native void nativeFontScale(long handle, float value);

    public void setPlayerLandscape(boolean enabled) {
        final int current = getRequestedOrientation();
        final int requested = playerOrientation.update(enabled, current);
        if (requested != current) setRequestedOrientation(requested);
    }

    @Override public void onResume() {
        super.onResume();
        if (playerOrientation.active()) setPlayerLandscape(true);
    }

    public void beginPlaybackBrightness() {
        if (!playbackBrightnessOwned) {
            previousWindowBrightness = getWindow().getAttributes().screenBrightness;
            playbackBrightnessOwned = true;
        }
        float level = getWindow().getAttributes().screenBrightness;
        if (level < 0f) level = android.provider.Settings.System.getInt(getContentResolver(),
            android.provider.Settings.System.SCREEN_BRIGHTNESS, 128) / 255f;
        if (backBridgeHandle != 0) nativePlaybackBrightness(backBridgeHandle, level);
    }

    public void setPlaybackBrightness(float value) {
        if (!playbackBrightnessOwned) beginPlaybackBrightness();
        android.view.WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.screenBrightness = Math.max(0.01f, Math.min(1f, value));
        getWindow().setAttributes(attributes);
    }

    public void restorePlaybackBrightness() {
        if (!playbackBrightnessOwned) return;
        android.view.WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.screenBrightness = previousWindowBrightness;
        getWindow().setAttributes(attributes);
        playbackBrightnessOwned = false;
    }

    // Called on Android's UI thread only after the Qt Runtime exists. Do not synthesize a
    // KEYCODE_BACK: that would dispatch the same gesture again through Qt's legacy path.
    public void setBackBridgeHandle(long handle) {
        if (surfaceRedrawGate != null) surfaceRedrawGate.setBridgeHandle(handle);
        if (handle != 0) nativeFontScale(handle, getResources().getConfiguration().fontScale);
        if (Build.VERSION.SDK_INT < 33) { backBridgeHandle = handle; return; }
        if (backCallback != null) BackApi33.remove(this, backCallback);
        backCallback = null;
        backBridgeHandle = handle;
        if (handle != 0) backCallback = BackApi33.install(this);
    }

    private void sendBack(int phase, float progress, int edge) {
        if (backBridgeHandle != 0)
            nativeBackGesture(backBridgeHandle, phase, progress, edge);
    }

    private static class BackApi33 {
        static Object install(ColosseumActivity host) {
            android.window.OnBackInvokedCallback callback = Build.VERSION.SDK_INT >= 34
                ? BackApi34.create(host) : () -> host.sendBack(3, 1f, 0);
            // The IME has higher priority, so its Back continues to dismiss just the keyboard.
            host.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback);
            return callback;
        }
        static void remove(ColosseumActivity host, Object callback) {
            host.getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(
                (android.window.OnBackInvokedCallback) callback);
        }
    }

    private static class BackApi34 {
        static android.window.OnBackInvokedCallback create(ColosseumActivity host) {
            return new android.window.OnBackAnimationCallback() {
                @Override public void onBackStarted(android.window.BackEvent event) {
                    host.sendBack(0, event.getProgress(), event.getSwipeEdge());
                }
                @Override public void onBackProgressed(android.window.BackEvent event) {
                    host.sendBack(1, event.getProgress(), event.getSwipeEdge());
                }
                @Override public void onBackCancelled() { host.sendBack(2, 0f, 0); }
                @Override public void onBackInvoked() { host.sendBack(3, 1f, 0); }
            };
        }
    }

    @Override public void onCreate(Bundle state) {
        configureDesktopScale();
        super.onCreate(state);
        surfaceRedrawGate = new QtSurfaceRedrawGate(this);
        DownloadService.handleIntent(getIntent());
    }

    @SuppressWarnings("deprecation")
    private void configureDesktopScale() {
        android.util.DisplayMetrics metrics = new android.util.DisplayMetrics();
        getWindowManager().getDefaultDisplay().getMetrics(metrics);
        int[] drawable = Build.VERSION.SDK_INT >= 30
            ? DesktopBoundsApi30.read(this)
            : new int[] {metrics.widthPixels, metrics.heightPixels, metrics.widthPixels, metrics.heightPixels};
        int width = Math.max(drawable[0], drawable[1]);
        int height = Math.min(drawable[0], drawable[1]);
        // Installed Beta composition: whole-Qt visual zoom on cold launch (default 1.5, i.e. 50% over the fitted baseline),
        // optionally overridden by the non-persistent colosseum_visual_zoom extra (0.9-1.6).
        float baselineScale = DesktopPresentationScale.forBounds(width, height, metrics.density);
        float visualZoom = 1.5f;
        Intent launchIntent = getIntent();
        if (launchIntent != null) {
            float requestedZoom = launchIntent.getFloatExtra("colosseum_visual_zoom", 1.5f);
            if (Float.isFinite(requestedZoom) && requestedZoom >= 0.9f && requestedZoom <= 1.6f) {
                visualZoom = requestedZoom;
            }
        }
        float scale = baselineScale * visualZoom;
        try {
            android.system.Os.setenv("QT_SCALE_FACTOR", Float.toString(scale), true);
        } catch (android.system.ErrnoException error) {
            throw new IllegalStateException("Could not establish the desktop presentation scale", error);
        }
        android.util.Log.i("ColosseumPresentation", "startup window=" + drawable[2] + "x" + drawable[3]
            + " drawable=" + drawable[0] + "x" + drawable[1] + " landscape-reference=" + width + "x" + height
            + " density=" + metrics.density + " baselineScale=" + baselineScale
            + " visualZoom=" + visualZoom + " scale=" + scale
            + (Build.VERSION.SDK_INT >= 30 ? " cutout-aware" : " legacy-app-metrics; verify cutout geometry"));
    }

    private static final class DesktopBoundsApi30 {
        static int[] read(ColosseumActivity activity) {
            android.view.WindowMetrics metrics = activity.getWindowManager().getCurrentWindowMetrics();
            android.graphics.Rect bounds = metrics.getBounds();
            android.graphics.Insets cutout = metrics.getWindowInsets()
                .getInsetsIgnoringVisibility(android.view.WindowInsets.Type.displayCutout());
            return new int[] {Math.max(0, bounds.width() - cutout.left - cutout.right),
                Math.max(0, bounds.height() - cutout.top - cutout.bottom), bounds.width(), bounds.height()};
        }
    }

    @Override public void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        DownloadService.handleIntent(intent);
    }

    @Override public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        if (backBridgeHandle != 0) nativeFontScale(backBridgeHandle, configuration.fontScale);
    }

    @Override public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                                    int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        DownloadService.permissionResult(requestCode, grantResults);
    }

    @Override public void onDestroy() {
        setPlayerLandscape(false);
        if (surfaceRedrawGate != null) surfaceRedrawGate.dispose();
        restorePlaybackBrightness();
        setBackBridgeHandle(0);
        super.onDestroy();
    }

    public void onSurfaceRedrawComplete(long token) {
        if (surfaceRedrawGate != null) surfaceRedrawGate.framePresented(token);
    }
}
