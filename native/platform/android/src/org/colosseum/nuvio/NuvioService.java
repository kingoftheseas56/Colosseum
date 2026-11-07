package org.colosseum.nuvio;

import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.os.RemoteException;

import org.qtproject.qt.android.bindings.QtService;

import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** One private Qt runtime per slot, with a fresh native host for each invocation. */
public class NuvioService extends QtService {
    static final String DESCRIPTOR = "org.colosseum.nuvio.NuvioService";
    static final int START = IBinder.FIRST_CALL_TRANSACTION;
    static final int CANCEL = START + 1;
    static final int PROCESS_ID = START + 2;
    private static final Object LOCK = new Object();
    private static boolean running;
    private static boolean stopping;
    private static String[] arguments;
    private static ParcelFileDescriptor output;

    static boolean validArguments(String[] args) {
        if (args == null || args.length == 0 || args.length > 16) return false;
        for (String arg : args) {
            if (arg == null || arg.indexOf('\0') >= 0
                    || arg.getBytes(StandardCharsets.UTF_8).length > 8192) return false;
        }
        return true;
    }

    // QtService.onCreate loads android.app.lib_name and starts the native main.
    // That main polls these nonblocking accessors until Binder delivers its job.
    public static String[] takeArguments() {
        synchronized (LOCK) {
            String[] result = arguments;
            arguments = null;
            return result;
        }
    }

    public static int takeOutputFd() {
        synchronized (LOCK) {
            if (output == null) return -1;
            int fd = output.detachFd();
            output = null;
            return fd;
        }
    }

    // The Qt service startup handshake has completed by the time the first job
    // is delivered. Keep the service thread asleep between subsequent jobs.
    public static String[] awaitArguments() {
        synchronized (LOCK) {
            while (!stopping && arguments == null) {
                try { LOCK.wait(); }
                catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    return null;
                }
            }
            return stopping ? null : takeArguments();
        }
    }

    public static void jobCompleted() {
        synchronized (LOCK) { running = false; }
    }

    private final Binder binder = new Binder() {
        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            if (code == INTERFACE_TRANSACTION) {
                reply.writeString(DESCRIPTOR);
                return true;
            }
            if (code != START && code != CANCEL && code != PROCESS_ID) return super.onTransact(code, data, reply, flags);
            if (Binder.getCallingUid() != Process.myUid()) throw new SecurityException();
            data.enforceInterface(DESCRIPTOR);
            if (code == PROCESS_ID) {
                reply.writeNoException();
                reply.writeInt(Process.myPid());
                return true;
            }
            if (code == CANCEL) {
                // The request names this exact process, not a possibly recycled PID.
                int expectedPid = data.readInt();
                if (expectedPid == Process.myPid() && Binder.getCallingPid() != Process.myPid()) {
                    Process.killProcess(Process.myPid());
                }
                return true;
            }
            String[] args = data.createStringArray();
            ParcelFileDescriptor fd = ParcelFileDescriptor.CREATOR.createFromParcel(data);
            boolean accepted = false;
            try {
                synchronized (LOCK) {
                    if (stopping || running || !validArguments(args)) {
                        throw new IllegalArgumentException("Invalid or overlapping Nuvio job");
                    }
                    output = fd;
                    arguments = args;
                    running = true;
                    accepted = true;
                    LOCK.notifyAll();
                }
                reply.writeNoException();
                reply.writeInt(Process.myPid());
                return true;
            } finally {
                if (!accepted) close(fd);
            }
        }
    };

    @Override public IBinder onBind(Intent intent) { return binder; }

    @Override public void onDestroy() {
        synchronized (LOCK) {
            stopping = true;
            close(output);
            output = null;
            arguments = null;
            LOCK.notifyAll();
        }
        super.onDestroy();
    }

    private static void close(ParcelFileDescriptor fd) {
        if (fd != null) try { fd.close(); } catch (IOException ignored) { }
    }

    public static final class Slot0 extends NuvioService { }
    public static final class Slot1 extends NuvioService { }
}
