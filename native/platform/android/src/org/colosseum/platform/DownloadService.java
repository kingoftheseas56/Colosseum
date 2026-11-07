package org.colosseum.platform;

import android.Manifest;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.text.format.Formatter;
import java.lang.ref.WeakReference;

// One same-process OS host for the existing native download owners. This is
// deliberately an Android Service, not a second Qt application or queue.
public final class DownloadService extends Service {
    private static final String CHANNEL = "colosseum-downloads";
    private static final String OPEN_DOWNLOADS = "org.colosseum.OPEN_DOWNLOADS";
    private static final int NOTIFICATION = 17020;
    private static final int PERMISSION_REQUEST = 17021;
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static final BackgroundDownloadHostState state = new BackgroundDownloadHostState();
    private static Context appContext;
    private static WeakReference<Activity> activity = new WeakReference<>(null);
    private static DownloadService instance;
    private static long nativeHandle;
    private static long nativeEpoch;
    private static String title = "";
    private static long received;
    private static long total;
    private static boolean visible;
    private static boolean pendingOpen;
    private PowerManager.WakeLock wakeLock;

    private static native void nativeHostState(long handle, long epoch, boolean ready, String error);
    private static native void nativeOpenDownloads(long handle);

    // Called by the native read-model bridge. Always use Android's main Looper;
    // Qt's runOnAndroidMainThread intentionally queues calls while hidden.
    public static void update(Context context, long handle, long epoch, int count, String jobTitle,
                              long bytes, long expected, boolean foreground) {
        main.post(() -> {
            appContext = context.getApplicationContext();
            if (context instanceof Activity) activity = new WeakReference<>((Activity) context);
            nativeHandle = handle;
            nativeEpoch = epoch;
            title = jobTitle;
            received = Math.max(0, bytes);
            total = Math.max(0, expected);
            visible = foreground;
            reconcile(count);
            if (pendingOpen && nativeHandle != 0) {
                pendingOpen = false;
                nativeOpenDownloads(nativeHandle);
            }
        });
    }

    static void handleIntent(Intent intent) {
        if (intent == null || !OPEN_DOWNLOADS.equals(intent.getAction())) return;
        intent.setAction(Intent.ACTION_MAIN);
        if (nativeHandle != 0) nativeOpenDownloads(nativeHandle);
        else pendingOpen = true;
    }

    static void permissionResult(int requestCode, int[] grants) {
        if (requestCode != PERMISSION_REQUEST) return;
        state.permissionResult(grants.length > 0 && grants[0] == PackageManager.PERMISSION_GRANTED);
        reconcile(state.jobs);
    }

    private static NotificationManager manager() {
        return appContext.getSystemService(NotificationManager.class);
    }

    private static BackgroundDownloadHostState.Gate notificationGate() {
        if (Build.VERSION.SDK_INT >= 33 && appContext.checkSelfPermission(
                Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            return BackgroundDownloadHostState.Gate.PERMISSION_REQUIRED;
        NotificationManager notifications = manager();
        if (notifications == null || !notifications.areNotificationsEnabled())
            return BackgroundDownloadHostState.Gate.DISABLED;
        NotificationChannel channel = notifications.getNotificationChannel(CHANNEL);
        if (channel != null && channel.getImportance() == NotificationManager.IMPORTANCE_NONE)
            return BackgroundDownloadHostState.Gate.DISABLED;
        return BackgroundDownloadHostState.Gate.ALLOWED;
    }

    private static void publishState() {
        if (nativeHandle != 0) nativeHostState(nativeHandle, nativeEpoch, state.ready, state.error);
    }

    private static void reconcile(int count) {
        if (appContext == null) return;
        Activity current = activity.get();
        boolean canStart = visible && current != null && !current.isFinishing();
        BackgroundDownloadHostState.Action action = state.update(count, canStart, notificationGate());
        try {
            switch (action) {
            case START:
                appContext.startForegroundService(new Intent(appContext, DownloadService.class));
                break;
            case UPDATE:
                if (instance != null) manager().notify(NOTIFICATION, instance.notification());
                break;
            case STOP:
                if (instance != null) instance.finishHost();
                else appContext.stopService(new Intent(appContext, DownloadService.class));
                break;
            case REQUEST_PERMISSION:
                current.requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, PERMISSION_REQUEST);
                break;
            default:
                break;
            }
        } catch (RuntimeException failure) {
            state.failed("Android could not keep this download running in the background. Keep Colosseum open to continue.");
            if (instance != null) instance.finishHost();
        }
        publishState();
    }

    private Notification notification() {
        String heading = state.jobs == 1 && !title.isEmpty() ? title : state.jobs + " downloads";
        String progress = Formatter.formatShortFileSize(this, received);
        if (total > 0) progress += " of " + Formatter.formatShortFileSize(this, total);
        Intent open = new Intent(this, ColosseumActivity.class).setAction(OPEN_DOWNLOADS)
                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent tap = PendingIntent.getActivity(this, NOTIFICATION, open,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder builder = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.stat_sys_download)
                .setContentTitle("Downloading · " + heading)
                .setContentText(progress).setContentIntent(tap)
                .setOngoing(true).setOnlyAlertOnce(true).setShowWhen(false)
                .setCategory(Notification.CATEGORY_PROGRESS);
        if (total > 0) builder.setProgress(1000, (int) Math.min(1000, received * 1000.0 / total), false);
        else builder.setProgress(0, 0, true);
        if (Build.VERSION.SDK_INT >= 31)
            builder.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        return builder.build();
    }

    @Override public void onCreate() {
        super.onCreate();
        instance = this;
        appContext = getApplicationContext();
        NotificationChannel channel = new NotificationChannel(CHANNEL, "Downloads",
                NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("Progress of downloads you start in Colosseum");
        manager().createNotificationChannel(channel);
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        // A killed process has no native owner: never restart an orphan queue.
        if (state.jobs == 0 || nativeHandle == 0 || !state.starting) {
            finishHost();
            return START_NOT_STICKY;
        }
        try {
            startForeground(NOTIFICATION, notification());
            PowerManager power = getSystemService(PowerManager.class);
            wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, getPackageName() + ":downloads");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire();
            if (!state.started()) finishHost();
        } catch (RuntimeException failure) {
            state.failed("Android could not start the download notification. Keep Colosseum open to continue.");
            finishHost();
        }
        publishState();
        return START_NOT_STICKY;
    }

    private void finishHost() {
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        wakeLock = null;
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
    }

    @Override public void onTimeout(int startId, int foregroundServiceType) {
        state.failed("Android's background download time limit was reached. Reopen Colosseum to continue.");
        publishState(); // Withdraw the native download lease before releasing OS lifetime.
        finishHost();
    }

    @Override public void onTaskRemoved(Intent rootIntent) {
        state.failed("The app was closed. Reopen Colosseum to continue downloads.");
        publishState();
        finishHost();
    }

    @Override public void onDestroy() {
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        wakeLock = null;
        instance = null;
        if (state.ready || state.starting)
            state.failed("Background downloading stopped. Reopen Colosseum to continue.");
        publishState();
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
