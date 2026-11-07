package org.colosseum.nuvio;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.os.RemoteException;

import java.io.IOException;
import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;

/** JNI-facing owner of bindings; native code owns and closes the returned read descriptor. */
public final class NuvioProcessBridge {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final Map<Integer, Job> JOBS = new HashMap<>();
    // Evidence only. Never use this history to address or kill a process.
    private static final Map<Integer, Integer> FINISHED_PIDS = new LinkedHashMap<>();
    private static final Class<?>[] SERVICES = {
        NuvioService.Slot0.class, NuvioService.Slot1.class,
        NuvioService.Slot2.class, NuvioService.Slot3.class
    };
    // All slot, binding, and queue state is owned by the Android main thread.
    private static final Job[] OWNERS = new Job[4];
    private static final boolean[] QUARANTINED = new boolean[4];
    private static final long CANCEL_BIND_GRACE_MS = 22000;
    private static final ArrayDeque<Job> WAITING = new ArrayDeque<>();
    private static int nextId = 1;
    private NuvioProcessBridge() { }

    public static int[] start(Context context, String[] args, int slot) {
        if (context == null || slot < 0 || slot >= SERVICES.length
                || !NuvioService.validArguments(args)) return new int[] {-1, -1};
        synchronized (JOBS) {
            if (QUARANTINED[slot] || JOBS.size() >= 32) return new int[] {-1, -1};
            ParcelFileDescriptor[] pipe = null;
            try {
                pipe = ParcelFileDescriptor.createPipe();
                while (nextId <= 0 || JOBS.containsKey(nextId)) nextId = nextId <= 0 ? 1 : nextId + 1;
                Job job = new Job(context.getApplicationContext(), args.clone(), slot, nextId++, pipe[1]);
                JOBS.put(job.id, job);
                FINISHED_PIDS.remove(job.id);
                if (!MAIN.post(() -> { WAITING.addLast(job); pump(); })) {
                    JOBS.remove(job.id);
                    close(pipe[0]);
                    close(pipe[1]);
                    return new int[] {-1, -1};
                }
                return new int[] {pipe[0].detachFd(), job.id};
            } catch (IOException | RuntimeException failure) {
                if (pipe != null) { close(pipe[0]); close(pipe[1]); }
                return new int[] {-1, -1};
            }
        }
    }

    public static int processId(int id) {
        synchronized (JOBS) {
            Job job = JOBS.get(id);
            Integer previous = FINISHED_PIDS.get(id);
            return job != null ? job.pid : previous == null ? -1 : previous;
        }
    }

    public static void cancel(int id) { MAIN.post(() -> finishOnMain(id)); }
    public static void finish(int id) { MAIN.post(() -> finishOnMain(id)); }

    private static void pump() {
        // bind() can fail synchronously, so don't retain an iterator across it.
        for (Job job : WAITING.toArray(new Job[0])) {
            if (QUARANTINED[job.slot]) {
                job.dispose();
            } else if (OWNERS[job.slot] == null && WAITING.remove(job)) {
                OWNERS[job.slot] = job;
                job.bind();
            }
        }
    }

    private static void finishOnMain(int id) {
        Job job;
        synchronized (JOBS) { job = JOBS.get(id); }
        if (job == null) return;
        boolean firstCancellation = !job.cancelled;
        job.cancelled = true;
        close(job.write);
        job.write = null;
        if (job.remote != null) job.retire();
        else if (!job.bound) job.dispose();
        else if (firstCancellation) MAIN.postDelayed(() -> {
            if (!job.disposed && job.cancelled && job.remote == null) {
                // No Binder means no safe proof that a later bind would be fresh.
                // Release resources, but fail closed for this slot until app restart.
                synchronized (JOBS) { QUARANTINED[job.slot] = true; }
                job.dispose();
            }
        }, CANCEL_BIND_GRACE_MS);
        // A pending binding must deliver its Binder before we can retire its process.
        // It never receives START if cancelled. The parent/native startup deadlines
        // still apply. If no Binder arrives, the grace deadline quarantines the slot.
    }

    private static final class Job implements ServiceConnection {
        final Context context;
        final int slot;
        final int id;
        String[] args;
        ParcelFileDescriptor write;
        IBinder remote;
        IBinder.DeathRecipient death;
        volatile int pid = -1;
        boolean bound;
        boolean cancelled;
        boolean disposed;
        boolean retiring;
        Job(Context context, String[] args, int slot, int id, ParcelFileDescriptor write) {
            this.context = context;
            this.args = args;
            this.slot = slot;
            this.id = id;
            this.write = write;
        }
        void bind() {
            if (cancelled || disposed) { dispose(); return; }
            try {
                bound = context.bindService(new Intent(context, SERVICES[slot]), this, Context.BIND_AUTO_CREATE);
                if (!bound) dispose();
            } catch (RuntimeException failure) { dispose(); }
        }
        @Override public void onServiceConnected(ComponentName name, IBinder service) {
            if (disposed || retiring) return;
            remote = service;
            death = () -> MAIN.post(() -> { if (remote == service) dispose(); });
            try {
                service.linkToDeath(death, 0);
            } catch (RemoteException alreadyDead) {
                // AMS can briefly deliver the old Binder while processing its death.
                // No job was submitted: rebind, never reuse that Qt runtime.
                unbind();
                remote = null;
                death = null;
                MAIN.post(this::bind);
                return;
            }
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                // Capture PID before making arguments visible, even for instant crashes.
                data.writeInterfaceToken(NuvioService.DESCRIPTOR);
                if (!service.transact(NuvioService.PROCESS_ID, data, reply, 0)) throw new RemoteException();
                reply.readException();
                pid = reply.readInt();
                if (pid <= 0 || pid == Process.myPid()) throw new RemoteException();
                if (cancelled) { retire(); return; }
                data.setDataSize(0);
                data.setDataPosition(0);
                reply.setDataSize(0);
                reply.setDataPosition(0);
                data.writeInterfaceToken(NuvioService.DESCRIPTOR);
                data.writeStringArray(args);
                write.writeToParcel(data, 0);
                if (!service.transact(NuvioService.START, data, reply, 0)) throw new RemoteException();
                reply.readException();
                if (reply.readInt() != pid) throw new RemoteException();
            } catch (RuntimeException | RemoteException failure) {
                retire();
            } finally {
                args = null;
                close(write);
                write = null;
                data.recycle();
                reply.recycle();
            }
        }
        void retire() {
            if (disposed || retiring) return;
            retiring = true;
            // Remove AUTO_CREATE before terminating: no service restart loop.
            unbind();
            if (remote == null || !remote.isBinderAlive()) { dispose(); return; }
            if (pid > 0 && pid != Process.myPid()) {
                Parcel data = Parcel.obtain();
                try {
                    data.writeInterfaceToken(NuvioService.DESCRIPTOR);
                    data.writeInt(pid);
                    remote.transact(NuvioService.CANCEL, data, null, IBinder.FLAG_ONEWAY);
                } catch (RemoteException | RuntimeException ignored) {
                    // Keep ownership until death is observed, not merely requested.
                } finally { data.recycle(); }
            }
            // QtService.onDestroy also terminates its process after unbinding.
            // Binder death, not an arbitrary delay or PID poll, releases this slot.
        }
        @Override public void onServiceDisconnected(ComponentName name) {
            if (remote == null || !remote.isBinderAlive()) dispose();
            else retire();
        }
        @Override public void onBindingDied(ComponentName name) {
            if (remote == null || !remote.isBinderAlive()) dispose();
            else retire();
        }
        @Override public void onNullBinding(ComponentName name) { dispose(); }
        void unbind() {
            if (bound) {
                bound = false;
                try { context.unbindService(this); } catch (IllegalArgumentException ignored) { }
            }
        }
        void dispose() {
            if (disposed) return;
            disposed = true;
            close(write);
            write = null;
            args = null;
            unbind();
            if (remote != null && death != null) {
                try { remote.unlinkToDeath(death, 0); }
                catch (java.util.NoSuchElementException ignored) { }
            }
            remote = null;
            WAITING.remove(this);
            synchronized (JOBS) {
                JOBS.remove(id);
                if (pid > 0) {
                    FINISHED_PIDS.put(id, pid);
                    while (FINISHED_PIDS.size() > 128)
                        FINISHED_PIDS.remove(FINISHED_PIDS.keySet().iterator().next());
                }
            }
            if (OWNERS[slot] == this) OWNERS[slot] = null;
            MAIN.post(NuvioProcessBridge::pump);
        }
    }
    private static void close(ParcelFileDescriptor fd) {
        if (fd != null) try { fd.close(); } catch (IOException ignored) { }
    }
}
