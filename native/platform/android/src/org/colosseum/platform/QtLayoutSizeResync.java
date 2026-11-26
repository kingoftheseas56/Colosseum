package org.colosseum.platform;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import java.lang.reflect.Method;

// Qt 6.11 forwards QtRootLayout size changes to its native side once. When Colosseum
// starts behind the upright lock screen, the rotation to landscape on unlock arrives
// before Qt's native platform is running, the change is dropped, and Qt's window keeps
// the portrait geometry inside a landscape root (half the screen stays empty). This
// re-sends the root size whenever Qt's window disagrees with it, until they match.
final class QtLayoutSizeResync implements ViewTreeObserver.OnGlobalLayoutListener {
    private static final String TAG = "ColosseumLayout";
    private static final long RETRY_MS = 400;
    private static final int MAX_RETRIES = 25;
    private final View decor;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable check = this::check;
    private int retries;
    private Method handleLayoutSizeChanged;

    QtLayoutSizeResync(Activity activity) {
        decor = activity.getWindow().getDecorView();
        decor.getViewTreeObserver().addOnGlobalLayoutListener(this);
    }

    @Override public void onGlobalLayout() {
        retries = 0;
        handler.removeCallbacks(check);
        handler.post(check);
    }

    void onFocusOrResume() {
        onGlobalLayout();
    }

    private void check() {
        ViewGroup root = findRoot(decor);
        if (root == null || root.getWidth() <= 0 || root.getHeight() <= 0) return;
        View qtWindow = null;
        for (int i = 0; i < root.getChildCount(); ++i) {
            View child = root.getChildAt(i);
            if ("org.qtproject.qt.android.QtWindow".equals(child.getClass().getName())) {
                qtWindow = child;
                break;
            }
        }
        if (qtWindow == null || qtWindow.getWidth() <= 0) return;
        boolean rootLandscape = root.getWidth() > root.getHeight();
        boolean windowLandscape = qtWindow.getWidth() > qtWindow.getHeight();
        if (rootLandscape == windowLandscape) return;
        if (retries++ >= MAX_RETRIES) return;
        Log.i(TAG, "resync root=" + root.getWidth() + "x" + root.getHeight()
                + " qtWindow=" + qtWindow.getWidth() + "x" + qtWindow.getHeight());
        send(root.getWidth(), root.getHeight());
        handler.postDelayed(check, RETRY_MS);
    }

    private void send(int width, int height) {
        try {
            if (handleLayoutSizeChanged == null) {
                Class<?> manager = Class.forName("org.qtproject.qt.android.QtDisplayManager");
                handleLayoutSizeChanged =
                        manager.getDeclaredMethod("handleLayoutSizeChanged", int.class, int.class);
                handleLayoutSizeChanged.setAccessible(true);
            }
            handleLayoutSizeChanged.invoke(null, width, height);
        } catch (ReflectiveOperationException | UnsatisfiedLinkError error) {
            Log.w(TAG, "cannot resend layout size", error);
            retries = MAX_RETRIES;
        }
    }

    private static ViewGroup findRoot(View view) {
        if (view instanceof ViewGroup
                && "org.qtproject.qt.android.QtRootLayout".equals(view.getClass().getName()))
            return (ViewGroup) view;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i) {
                ViewGroup found = findRoot(group.getChildAt(i));
                if (found != null) return found;
            }
        }
        return null;
    }

    void dispose() {
        handler.removeCallbacks(check);
        if (decor.getViewTreeObserver().isAlive())
            decor.getViewTreeObserver().removeOnGlobalLayoutListener(this);
    }
}
