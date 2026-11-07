#!/usr/bin/env python3
"""Compile real Nuvio Java against small Android stubs and test lifecycle ordering.

This is a deterministic stub lifecycle test, NOT Android/emulator runtime evidence.
Requires java and javac on PATH. For local use only, --ecj accepts an existing
Eclipse compiler JAR; this test never downloads tools or dependencies.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

STUB_SOURCES = {
    'android/content/ComponentName.java': r"""
package android.content;

public class ComponentName {}
""",
    'android/content/Intent.java': r"""
package android.content;

public class Intent {
  public Intent(Context c, Class<?> k) {}
}
""",
    'android/content/Context.java': r"""
package android.content;

public abstract class Context {
  public static final int BIND_AUTO_CREATE = 1;

  public Context getApplicationContext() {
    return this;
  }

  public abstract boolean bindService(Intent i, ServiceConnection c, int f);

  public abstract void unbindService(ServiceConnection c);
}
""",
    'android/content/ServiceConnection.java': r"""
package android.content;

import android.os.IBinder;

public interface ServiceConnection {
  void onServiceConnected(ComponentName n, IBinder b);

  void onServiceDisconnected(ComponentName n);

  default void onBindingDied(ComponentName n) {}

  default void onNullBinding(ComponentName n) {}
}
""",
    'android/os/IBinder.java': r"""
package android.os;

public interface IBinder {
  int FIRST_CALL_TRANSACTION = 1, FLAG_ONEWAY = 1, INTERFACE_TRANSACTION = 1598968902;

  interface DeathRecipient {
    void binderDied();
  }

  boolean transact(int c, Parcel d, Parcel r, int f) throws RemoteException;

  void linkToDeath(DeathRecipient r, int f) throws RemoteException;

  boolean unlinkToDeath(DeathRecipient r, int f);

  boolean isBinderAlive();
}
""",
    'android/os/Binder.java': r"""
package android.os;

public class Binder implements IBinder {
  public static int getCallingUid() {
    return 1;
  }

  public static int getCallingPid() {
    return 1;
  }

  protected boolean onTransact(int c, Parcel d, Parcel r, int f) throws RemoteException {
    return false;
  }

  public boolean transact(int c, Parcel d, Parcel r, int f) throws RemoteException {
    return onTransact(c, d, r, f);
  }

  public void linkToDeath(DeathRecipient r, int f) throws RemoteException {}

  public boolean unlinkToDeath(DeathRecipient r, int f) {
    return true;
  }

  public boolean isBinderAlive() {
    return true;
  }
}
""",
    'android/os/RemoteException.java': r"""
package android.os;

public class RemoteException extends Exception {
  private static final long serialVersionUID = 1L;
}
""",
    'android/os/Process.java': r"""
package android.os;

public class Process {
  public static int myPid() {
    return 1;
  }

  public static int myUid() {
    return 1;
  }

  public static void killProcess(int p) {}
}
""",
    'android/os/Looper.java': r"""
package android.os;

public class Looper {
  public static Looper getMainLooper() {
    return new Looper();
  }
}
""",
    'android/os/Handler.java': r"""
package android.os;

import java.util.ArrayDeque;

public class Handler {
  static final ArrayDeque<Runnable> queue = new ArrayDeque<>();
  static final ArrayDeque<Runnable> delayed = new ArrayDeque<>();

  public Handler(Looper looper) {}

  public boolean post(Runnable runnable) {
    queue.add(runnable);
    return true;
  }

  public boolean postDelayed(Runnable runnable, long delay) {
    delayed.add(runnable);
    return true;
  }

  public static void drain() {
    int count = 0;
    while (!queue.isEmpty()) {
      if (++count > 1000) throw new AssertionError("Main-thread loop");
      queue.remove().run();
    }
  }

  public static void advanceGracePeriod() {
    while (!delayed.isEmpty()) queue.add(delayed.remove());
    drain();
  }
}
""",
    'android/os/Parcel.java': r"""
package android.os;

import java.util.*;

public class Parcel {
  ArrayList<Object> a = new ArrayList<>();
  int p;

  public static Parcel obtain() {
    return new Parcel();
  }

  public void writeString(String s) {
    a.add(s);
  }

  public void writeInterfaceToken(String s) {
    writeString(s);
  }

  public void enforceInterface(String s) {
    if (!s.equals(a.get(p++))) throw new Error();
  }

  public void writeInt(int x) {
    a.add(x);
  }

  public int readInt() {
    return (Integer) a.get(p++);
  }

  public void writeStringArray(String[] s) {
    a.add(s);
  }

  public String[] createStringArray() {
    return (String[]) a.get(p++);
  }

  public void writeNoException() {}

  public void readException() {}

  public void recycle() {}

  public void setDataSize(int n) {
    a.clear();
  }

  public void setDataPosition(int n) {
    p = n;
  }
}
""",
    'android/os/ParcelFileDescriptor.java': r"""
package android.os;

import java.io.*;

public class ParcelFileDescriptor {
  static int next = 10;
  int fd = next++;

  public static ParcelFileDescriptor[] createPipe() throws IOException {
    return new ParcelFileDescriptor[] {new ParcelFileDescriptor(), new ParcelFileDescriptor()};
  }

  public void close() throws IOException {}

  public int detachFd() {
    return fd;
  }

  public void writeToParcel(Parcel p, int f) {
    p.writeInt(fd);
  }

  public static final Creator CREATOR = new Creator();

  public static class Creator {
    public ParcelFileDescriptor createFromParcel(Parcel p) {
      p.readInt();
      return new ParcelFileDescriptor();
    }
  }
}
""",
    'org/qtproject/qt/android/bindings/QtService.java': r"""
package org.qtproject.qt.android.bindings;

import android.content.Intent;
import android.os.IBinder;

public class QtService {
  public IBinder onBind(Intent i) {
    return null;
  }
}
""",
    'org/colosseum/nuvio/RaceTest.java': r"""
package org.colosseum.nuvio;

import android.content.*;
import android.os.*;
import java.util.*;

public class RaceTest {

  static class C extends Context {
    List<ServiceConnection> binds = new ArrayList<>();
    int unbound;

    public boolean bindService(Intent i, ServiceConnection c, int f) {
      binds.add(c);
      return true;
    }

    public void unbindService(ServiceConnection c) {
      unbound++;
    }
  }

  static class B extends Binder {
    int pid, starts, kills;
    boolean alive = true;
    boolean throwUnlink;
    DeathRecipient d;

    B(int p) {
      pid = p;
    }

    public void linkToDeath(DeathRecipient x, int f) throws RemoteException {
      if (!alive) throw new RemoteException();
      d = x;
    }

    public boolean unlinkToDeath(DeathRecipient x, int f) {
      d = null;
      if (throwUnlink) throw new NoSuchElementException();
      return true;
    }

    public boolean isBinderAlive() {
      return alive;
    }

    public boolean transact(int c, Parcel p, Parcel r, int f) throws RemoteException {
      if (!alive) throw new RemoteException();
      p.enforceInterface(NuvioService.DESCRIPTOR);
      if (c == NuvioService.PROCESS_ID) r.writeInt(pid);
      else if (c == NuvioService.START) {
        starts++;
        r.writeInt(pid);

      } else if (c == NuvioService.CANCEL) {
        if (p.readInt() != pid) throw new Error();
        kills++;
      }
      return true;
    }

    void die() {
      alive = false;
      if (d != null) d.binderDied();
    }
  }

  static int start(C c, int slot) {
    int[] r = NuvioProcessBridge.start(c, new String[] {"--proof"}, slot);
    if (r[0] < 0) throw new Error("rejected queued start");
    return r[1];
  }

  static void check(boolean b, String s) {
    if (!b) throw new Error(s);
  }

  public static void main(String[] args) {
    C c = new C();
    check(NuvioProcessBridge.processId(0) == -1, "no process before a request");
    Handler.drain();
    check(c.binds.isEmpty(), "loading the bridge must not prewarm any service");
    int first = start(c, 0);
    Handler.drain();
    B a = new B(101);
    c.binds.get(0).onServiceConnected(null, a);
    check(a.starts == 1, "start");
    NuvioProcessBridge.finish(first);
    int second = start(c, 0);
    Handler.drain();
    check(a.kills == 1 && c.unbound == 1, "finish must kill and unbind");
    check(c.binds.size() == 1, "must not reuse retiring runtime");
    a.throwUnlink = true;
    a.die();
    Handler.drain();
    check(c.binds.size() == 2, "death releases slot");
    check(NuvioProcessBridge.processId(first) == 101, "PID retained");
    B b = new B(102);
    c.binds.get(1).onServiceConnected(null, b);
    b.die();
    Handler.drain();
    check(NuvioProcessBridge.processId(second) == 102, "fast crash PID retained");
    int third = start(c, 0);
    Handler.drain();
    NuvioProcessBridge.cancel(third);
    int fourth = start(c, 0);
    Handler.drain();
    check(c.binds.size() == 3, "late cancellation holds slot");
    B late = new B(103);
    c.binds.get(2).onServiceConnected(null, late);
    check(late.starts == 0 && late.kills == 1, "late connection killed without START");
    late.die();
    Handler.drain();
    check(c.binds.size() == 4, "late death releases successor");
    B last = new B(104);
    c.binds.get(3).onServiceConnected(null, last);
    NuvioProcessBridge.cancel(fourth);
    Handler.drain();
    last.die();
    Handler.drain();
    check(last.kills == 1, "cancel current");
    check(!NuvioService.validArguments(new String[] {"x".repeat(8193)}), "argument limit");
    int stale = start(c, 1);
    Handler.drain();
    B dead = new B(105);
    dead.alive = false;
    c.binds.get(4).onServiceConnected(null, dead);
    Handler.drain();
    check(c.binds.size() == 6, "dead delivered Binder retries fresh bind");
    B fresh = new B(106);
    c.binds.get(5).onServiceConnected(null, fresh);
    check(fresh.starts == 1, "retry submits once");
    int queued = start(c, 1);
    Handler.drain();
    NuvioProcessBridge.cancel(queued);
    Handler.drain();
    check(c.binds.size() == 6, "queued cancellation never binds");
    NuvioProcessBridge.finish(stale);
    Handler.drain();
    fresh.die();
    Handler.drain();
    int[] ids = new int[4];
    int baseline = c.binds.size();
    for (int i = 0; i < 4; i++) ids[i] = start(c, i);
    Handler.drain();
    check(c.binds.size() == baseline + 4, "all four slots bind independently");
    for (int i = 0; i < 4; i++) {
      B worker = new B(200 + i);
      c.binds.get(baseline + i).onServiceConnected(null, worker);
      NuvioProcessBridge.cancel(ids[i]);
      Handler.drain();
      worker.die();
      Handler.drain();
    }
    check(c.unbound == c.binds.size(), "completed slots retain no idle service bindings");
    Handler.drain();
    check(c.binds.size() == baseline + 4, "idle slots must not automatically restart");
    int neverConnected = start(c, 3);

    Handler.drain();

    NuvioProcessBridge.cancel(neverConnected);

    Handler.drain();

    int waitingBehindCancelled = start(c, 3);

    Handler.drain();

    int beforeQuarantine = c.unbound;

    Handler.advanceGracePeriod();

    check(c.unbound == beforeQuarantine + 1, "stalled cancelled bind unbound");

    check(
        NuvioProcessBridge.start(c, new String[] {"--proof"}, 3)[0] < 0,
        "unknown process state quarantines slot instead of reusing runtime");

    check(
        NuvioProcessBridge.processId(waitingBehindCancelled) == -1,
        "quarantine discards waiting job without launching it");

    System.out.println(
        "PASS: stub lifecycle; lazy start, idle disposal, queued restart, death barrier, finish cancellation, late bind"
            + " cancellation, fast-crash PID history, argument limits");
  }
}
""",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ecj", type=Path, help="Use an already available ECJ compiler JAR")
    args = parser.parse_args()
    java = shutil.which("java")
    javac = shutil.which("javac")
    if not java or (not javac and args.ecj is None):
        print("SKIP: Nuvio stub lifecycle test needs java and javac on PATH (or --ecj).")
        return 0
    if args.ecj is not None and not args.ecj.is_file():
        parser.error("--ecj must name an existing compiler JAR")
    repository = Path(__file__).resolve().parents[1]
    production = repository / "native/platform/android/src/org/colosseum/nuvio"
    with tempfile.TemporaryDirectory(prefix="nuvio-java-stubs-") as temporary:
        root = Path(temporary)
        for name, source in STUB_SOURCES.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
        classes = root / "classes"
        classes.mkdir()
        sources = [str(path) for path in sorted(root.rglob("*.java"))]
        sources.extend(str(production / name) for name in ("NuvioProcessBridge.java", "NuvioService.java"))
        compiler = ([java, "-jar", str(args.ecj.resolve()), "-17"] if args.ecj is not None
                    else [javac, "--release", "17"])
        subprocess.run(compiler + ["-d", str(classes)] + sources, check=True)
        subprocess.run([java, "-cp", str(classes), "org.colosseum.nuvio.RaceTest"], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
