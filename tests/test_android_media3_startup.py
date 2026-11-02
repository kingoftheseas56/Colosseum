"""Run the production startup/dispatch methods against a paused Android looper."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "native/platform/android/src/org/colosseum/player/Media3PlayerHost.java"


def method(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class Media3StartupOrdering(unittest.TestCase):
    def test_constructor_returns_without_android_main_and_commands_keep_fifo(self):
        text = HOST.read_text(encoding="utf8")
        methods = "\n".join(method(text, signature) for signature in (
            "public Media3PlayerHost(", "private void runOnMain(",
            "private void runOnMainSync(", "private <T> T callOnMain(",
            "public void play()", "public void pause()", "public void release()",
        ))
        player_field = re.search(r"private (?:final )?ExoPlayer player;", text)[0]
        harness = r'''
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
public final class Media3PlayerHost {
    private final Context appContext;
    private final long nativeHandle;
    private final Handler mainHandler;
    PLAYER_FIELD
    private final Player.Listener listener;
    private boolean released, userWantsPlay;
    private long activeGeneration;
    private final TreeMap<Long,String> chaptersByStartMs = new TreeMap<>();
    private interface MainCallable<T> { T call(); }
    private static final List<String> events = new ArrayList<>();
    private ExoPlayer createPlayerOnMain() {
        return callOnMain(() -> { requireMain(); events.add("create"); return new ExoPlayer(); });
    }
    private Player.Listener createListener() { return new Player.Listener() {}; }
    private void startTelemetry() {}
    private void stopTelemetry() {}
    private void emitPlaybackSnapshot() {}
    METHODS
    private static void requireMain() {
        if (Looper.myLooper() != Looper.getMainLooper()) throw new AssertionError("wrong player owner");
    }
    private static final class Context { Context getApplicationContext() { return this; } }
    private static final class Player { interface Listener {} }
    private static final class Looper {
        static final Looper MAIN = new Looper();
        static final Thread owner = Thread.currentThread();
        static Looper getMainLooper() { return MAIN; }
        static Looper myLooper() { return Thread.currentThread() == owner ? MAIN : null; }
    }
    private static final class Handler {
        static final Queue<Runnable> queue = new ConcurrentLinkedQueue<>();
        Handler(Looper ignored) {}
        boolean post(Runnable command) { return queue.add(command); }
    }
    private static final class ExoPlayer {
        void addListener(Player.Listener ignored) { requireMain(); events.add("listener"); }
        void removeListener(Player.Listener ignored) { requireMain(); }
        void play() { requireMain(); events.add("play"); }
        void pause() { requireMain(); events.add("pause"); }
        void clearVideoSurface() { requireMain(); }
        void stop() { requireMain(); }
        void clearMediaItems() { requireMain(); }
        void release() { requireMain(); events.add("release"); }
    }
    public static void main(String[] args) throws Exception {
        Looper.getMainLooper(); // Android main is occupied waiting for Qt, so do NOT pump it yet.
        AtomicReference<Media3PlayerHost> host = new AtomicReference<>();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread qt = new Thread(() -> {
            try {
                Media3PlayerHost created = new Media3PlayerHost(new Context(), 7);
                host.set(created);
                created.play();
                created.pause();
            } catch (Throwable error) { failure.set(error); }
        });
        qt.setDaemon(true);
        qt.start();
        qt.join(1500);
        if (qt.isAlive()) throw new AssertionError("Qt constructor waited for the occupied Android main thread");
        if (failure.get() != null) throw new AssertionError(failure.get());
        if (host.get() == null || !events.isEmpty()) throw new AssertionError("player ran before main dispatch");
        for (Runnable task; (task = Handler.queue.poll()) != null;) task.run();
        host.get().release();
        host.get().release();
        if (!events.equals(List.of("create", "listener", "play", "pause", "release")))
            throw new AssertionError("startup/command/shutdown ordering: " + events);
        events.clear();
        Media3PlayerHost onMain = new Media3PlayerHost(new Context(), 8);
        onMain.play(); onMain.pause(); onMain.release();
        if (!events.equals(List.of("create", "listener", "play", "pause", "release")))
            throw new AssertionError("main-thread construction failed: " + events);
        System.out.println("MEDIA3_STARTUP_ORDERING_OK");
    }
}
'''.replace("PLAYER_FIELD", player_field).replace("METHODS", methods)
        java_home = os.environ.get("JAVA_HOME")
        suffix = ".exe" if os.name == "nt" else ""
        def tool(name):
            found = shutil.which(name)
            if found:
                return found
            candidate = Path(java_home) / "bin" / (name + suffix) if java_home else None
            if candidate and candidate.is_file():
                return str(candidate)
            self.skipTest(f"JDK tool {name} is required for the executable startup regression")
        javac, java = tool("javac"), tool("java")
        with tempfile.TemporaryDirectory(prefix="media3-startup-") as directory:
            source = Path(directory) / "Media3PlayerHost.java"
            source.write_text(harness, encoding="utf8")
            compiled = subprocess.run([javac, "-d", directory, str(source)], capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            tested = subprocess.run([java, "-cp", directory, "Media3PlayerHost"], capture_output=True, text=True, timeout=15)
            self.assertEqual(tested.returncode, 0, tested.stderr)
            self.assertIn("MEDIA3_STARTUP_ORDERING_OK", tested.stdout)


if __name__ == "__main__":
    unittest.main()
