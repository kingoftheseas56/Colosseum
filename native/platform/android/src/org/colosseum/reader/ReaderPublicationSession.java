package org.colosseum.reader;

import java.net.URI;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;

/** Per-WebView capability registry. No Android types so its boundary is host-testable. */
public final class ReaderPublicationSession {
    public static final String ORIGIN = "https://appassets.androidplatform.net";
    public static final String SHELL_URL = ORIGIN + "/assets/reader2/android_paper.html";
    private static final Set<String> BOOK_EVENTS = new HashSet<>(Arrays.asList(
        "ready", "relocated", "error", "selection", "selectionCleared", "highlightTapped",
        "searchResults", "manualNavigation", "toggleChrome", "escape", "footnote",
        "readAlongRangeMissing"));

    public static final class Publication {
        public final String source;
        public final long generation;
        public final String path;
        public final String url;

        private Publication(String source, long generation) {
            this.source = source;
            this.generation = generation;
            path = UUID.randomUUID().toString() + "/book.epub";
            url = ORIGIN + "/publication/" + path;
        }
    }

    private Publication active;

    public synchronized Publication open(String source, long generation) {
        active = null;
        URI uri;
        try { uri = URI.create(source); }
        catch (RuntimeException error) { throw new IllegalArgumentException("Unsupported source"); }
        boolean content = "content".equals(uri.getScheme()) && uri.getAuthority() != null;
        boolean file = "file".equals(uri.getScheme()) && uri.getAuthority() == null
            && uri.getPath() != null && uri.getPath().startsWith("/");
        if ((!content && !file) || generation <= 0)
            throw new IllegalArgumentException("Unsupported source or generation");
        active = new Publication(source, generation);
        return active;
    }

    // Called on WebView's resource thread, concurrently with UI-thread open/close.
    public synchronized Publication resolve(String path) {
        return active != null && active.path.equals(path) ? active : null;
    }

    public synchronized long generation() { return active != null ? active.generation : 0; }
    public synchronized void close() { active = null; }

    public static boolean isShellUrl(String value) {
        if (value == null) return false;
        try {
            URI uri = URI.create(value);
            return "https".equals(uri.getScheme())
                && "appassets.androidplatform.net".equals(uri.getRawAuthority())
                && "/assets/reader2/android_paper.html".equals(uri.getRawPath());
        } catch (IllegalArgumentException error) { return false; }
    }

    public synchronized boolean acceptsEvent(String origin, boolean mainFrame, String topUrl,
                                              String event, long generation) {
        if (!ORIGIN.equals(origin) || !mainFrame || !isShellUrl(topUrl)) return false;
        if ("glueLoaded".equals(event)) return generation == 0 && active == null;
        if ("error".equals(event) && active == null) return generation == 0;
        return active != null && active.generation == generation && BOOK_EVENTS.contains(event);
    }
}
