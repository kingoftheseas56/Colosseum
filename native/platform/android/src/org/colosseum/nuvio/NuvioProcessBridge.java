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

/** JNI-facing owner of private service bindings and per-invocation output pipes. */
public final class NuvioProcessBridge {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final Class<?>[] SERVICES = {NuvioService.Slot0.class, NuvioService.Slot1.class};
    private static final Slot[] SLOTS = new Slot[SERVICES.length];
    private static final boolean[] QUARANTINED = new boolean[SERVICES.length];
    private static final ArrayDeque<Job> WAITING = new ArrayDeque<>();
    private static final long IDLE_MS = 60000;
    private static final long RETIRE_GRACE_MS = 22000;

    // Slots and queue belong to the Android main thread. The Qt thread uses this
    // synchronized registry to allocate IDs and read the job's service PID.
    private static final Map<Integer, Job> JOBS = new HashMap<>();
    private static final Map<Integer, Integer> FINISHED_PIDS = new LinkedHashMap<>();
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

    public static void cancel(int id) { MAIN.post(() -> finishOnMain(id, false)); }
    public static void finish(int id) { MAIN.post(() -> finishOnMain(id, true)); }

    private static void pump() {
        for (Job job : WAITING.toArray(new Job[0])) {
            Slot slot = SLOTS[job.slot];
            if (QUARANTINED[job.slot]) {
                job.dispose();
            } else if (slot == null && WAITING.remove(job)) {
                slot = new Slot(job);
                SLOTS[job.slot] = slot;
                slot.bind();
            } else if (slot != null && slot.job == null && !slot.retiring) {
                if (!slot.remote.isBinderAlive()) {
                    slot.dispose();
                } else if (WAITING.remove(job)) {
                    MAIN.removeCallbacks(slot.expireIdle);
                    slot.job = job;
                    slot.submit();
                }
            }
        }
    }

    private static void finishOnMain(int id, boolean reuse) {
        Job job;
        synchronized (JOBS) { job = JOBS.get(id); }
        if (job == null) return;
        Slot slot = SLOTS[job.slot];
        if (slot == null || slot.job != job) {
            job.dispose();
            return;
        }
        job.dispose();
        slot.job = null;
        if (reuse && slot.remote != null && slot.remote.isBinderAlive() && !slot.retiring) {
            MAIN.postDelayed(slot.expireIdle, IDLE_MS);
            pump();
        } else {
            slot.retire();
        }
    }

    private static final class Job {
        final Context context;
        final int slot;
        final int id;
        String[] args;
        ParcelFileDescriptor write;
        volatile int pid = -1;

        Job(Context context, String[] args, int slot, int id, ParcelFileDescriptor write) {
            this.context = context;
            this.args = args;
            this.slot = slot;
            this.id = id;
            this.write = write;
        }

        void closeInput() {
            args = null;
            close(write);
            write = null;
        }

        void dispose() {
            closeInput();
            WAITING.remove(this);
            synchronized (JOBS) {
                JOBS.remove(id);
                if (pid > 0) {
                    FINISHED_PIDS.put(id, pid);
                    while (FINISHED_PIDS.size() > 128)
                        FINISHED_PIDS.remove(FINISHED_PIDS.keySet().iterator().next());
                }
            }
        }
    }

    private static final class Slot implements ServiceConnection {
        final int index;
        final Context context;
        Job job;
        final Runnable expireIdle = () -> { if (job == null) retire(); };
        final Runnable retireGrace = this::quarantine;
        IBinder remote;
        IBinder.DeathRecipient death;
        int pid = -1;
        boolean bound;
        boolean retiring;
        boolean disposed;

        Slot(Job job) {
            this.index = job.slot;
            this.context = job.context;
            this.job = job;
        }

        void bind() {
            try {
                bound = context.bindService(new Intent(context, SERVICES[index]), this, Context.BIND_AUTO_CREATE);
                if (!bound) dispose();
            } catch (RuntimeException failure) { dispose(); }
        }

        @Override public void onServiceConnected(ComponentName name, IBinder service) {
            if (disposed || remote != null) return;
            remote = service;
            death = () -> MAIN.post(() -> { if (remote == service) dispose(); });
            try {
                service.linkToDeath(death, 0);
            } catch (RemoteException alreadyDead) {
                // AMS can deliver the dead Binder while the old process is being removed.
                // Requeue only the job that was never submitted.
                if (job != null) WAITING.addFirst(job);
                job = null;
                dispose();
                return;
            }
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                data.writeInterfaceToken(NuvioService.DESCRIPTOR);
                if (!service.transact(NuvioService.PROCESS_ID, data, reply, 0)) throw new RemoteException();
                reply.readException();
                pid = reply.readInt();
                if (pid <= 0 || pid == Process.myPid()) throw new RemoteException();
                if (retiring) stopProcess();
                else submit();
            } catch (RuntimeException | RemoteException failure) {
                retire();
            } finally {
                data.recycle();
                reply.recycle();
            }
        }

        void submit() {
            Job active = job;
            // Capture PID before making arguments visible, even for an instant crash.
            active.pid = pid;
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            try {
                data.writeInterfaceToken(NuvioService.DESCRIPTOR);
                data.writeStringArray(active.args);
                active.write.writeToParcel(data, 0);
                if (!remote.transact(NuvioService.START, data, reply, 0)) throw new RemoteException();
                reply.readException();
                if (reply.readInt() != pid) throw new RemoteException();
            } catch (RuntimeException | RemoteException failure) {
                retire();
            } finally {
                active.closeInput();
                data.recycle();
                reply.recycle();
            }
        }

        void retire() {
            if (disposed || retiring) return;
            retiring = true;
            MAIN.removeCallbacks(expireIdle);
            if (job != null) { job.dispose(); job = null; }
            // A cancelled bind must deliver its Binder before we can safely retire
            // that process. Keep ownership until death, even after an idle stop.
            MAIN.postDelayed(retireGrace, RETIRE_GRACE_MS);
            if (remote != null) stopProcess();
            else if (!bound) dispose();
        }

        void stopProcess() {
            unbind();
            if (!remote.isBinderAlive()) { dispose(); return; }
            if (pid <= 0 || pid == Process.myPid()) return;
            Parcel data = Parcel.obtain();
            try {
                data.writeInterfaceToken(NuvioService.DESCRIPTOR);
                data.writeInt(pid);
                remote.transact(NuvioService.CANCEL, data, null, IBinder.FLAG_ONEWAY);
            } catch (RemoteException | RuntimeException ignored) {
                // Release the slot only once this Binder is dead.
            } finally { data.recycle(); }
        }

        void quarantine() {
            if (disposed) return;
            if (remote != null && !remote.isBinderAlive()) { dispose(); return; }
            synchronized (JOBS) { QUARANTINED[index] = true; }
            dispose();
        }

        @Override public void onServiceDisconnected(ComponentName name) { dispose(); }
        @Override public void onBindingDied(ComponentName name) {
            if (remote != null && remote.isBinderAlive()) retire();
            else dispose();
        }
        @Override public void onNullBinding(ComponentName name) { quarantine(); }

        void unbind() {
            if (bound) {
                bound = false;
                try { context.unbindService(this); } catch (IllegalArgumentException ignored) { }
            }
        }

        void dispose() {
            if (disposed) return;
            disposed = true;
            MAIN.removeCallbacks(expireIdle);
            MAIN.removeCallbacks(retireGrace);
            if (job != null) { job.dispose(); job = null; }
            unbind();
            if (remote != null && death != null) {
                try { remote.unlinkToDeath(death, 0); }
                catch (java.util.NoSuchElementException ignored) { }
            }
            remote = null;
            if (SLOTS[index] == this) SLOTS[index] = null;
            MAIN.post(NuvioProcessBridge::pump);
        }
    }

    private static void close(ParcelFileDescriptor fd) {
        if (fd != null) try { fd.close(); } catch (IOException ignored) { }
    }
}
