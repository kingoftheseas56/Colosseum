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
        super.onCreate(state);
        surfaceRedrawGate = new QtSurfaceRedrawGate(this);
        DownloadService.handleIntent(getIntent());
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
