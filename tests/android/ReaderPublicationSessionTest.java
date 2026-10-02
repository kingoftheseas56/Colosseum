package org.colosseum.reader;

public final class ReaderPublicationSessionTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        ReaderPublicationSession first = new ReaderPublicationSession();
        ReaderPublicationSession second = new ReaderPublicationSession();
        String source = "content://provider/document/opaque%3A123";
        ReaderPublicationSession.Publication a = first.open(source, 7);
        check(a.url.startsWith(ReaderPublicationSession.ORIGIN + "/publication/"), "HTTPS asset origin");
        check(!a.url.contains("provider") && !a.url.contains("opaque"), "source is not exposed in URL");
        check(first.resolve(a.path) == a && a.source.equals(source) && a.generation == 7,
              "immutable source and generation binding");
        check(second.resolve(a.path) == null, "another renderer cannot resolve the token");
        check(first.resolve(a.path + "?other=1") == null, "only the exact publication path resolves");
        check(first.resolve("current/book.epub") == null, "no mutable current alias");
        check(first.resolve("../" + a.path) == null, "no traversal");
        check(first.acceptsEvent(ReaderPublicationSession.ORIGIN, true,
              ReaderPublicationSession.SHELL_URL, "relocated", 7), "current main-frame event accepted");
        check(first.acceptsEvent(ReaderPublicationSession.ORIGIN, true,
              ReaderPublicationSession.SHELL_URL + "?style=reader-settings", "relocated", 7),
              "common Foliate glue may replace the shell query with appearance settings");
        check(!first.acceptsEvent(ReaderPublicationSession.ORIGIN, false,
              ReaderPublicationSession.SHELL_URL, "relocated", 7), "publication subframe rejected");
        check(!first.acceptsEvent(ReaderPublicationSession.ORIGIN + ".evil.test", true,
              ReaderPublicationSession.SHELL_URL, "relocated", 7), "lookalike origin rejected");
        check(!first.acceptsEvent(ReaderPublicationSession.ORIGIN, true,
              a.url, "relocated", 7), "unexpected top-level context rejected");
        check(!first.acceptsEvent(ReaderPublicationSession.ORIGIN, true,
              ReaderPublicationSession.SHELL_URL, "authorizeBook", 7), "unknown event rejected");
        ReaderPublicationSession.Publication b = first.open("file:///private/next.epub", 8);
        check(!b.path.equals(a.path) && first.resolve(a.path) == null, "supersession revokes old token");
        check(!first.acceptsEvent(ReaderPublicationSession.ORIGIN, true,
              ReaderPublicationSession.SHELL_URL, "ready", 7), "late old-book event rejected");
        first.close();
        check(first.resolve(b.path) == null, "close revokes publication");
        check(!first.acceptsEvent(ReaderPublicationSession.ORIGIN, true,
              ReaderPublicationSession.SHELL_URL, "ready", 8), "close rejects late callbacks");
        check(first.acceptsEvent(ReaderPublicationSession.ORIGIN, true,
              ReaderPublicationSession.SHELL_URL, "glueLoaded", 0), "shell readiness is book independent");
        ReaderPublicationSession.Publication beforeInvalid = first.open(source, 9);
        for (String bad : new String[]{"https://example.com/book.epub", "javascript:alert(1)", ""}) {
            try { first.open(bad, 9); throw new AssertionError("unapproved source accepted"); }
            catch (IllegalArgumentException expected) { }
        }
        try { first.open(source, 0); throw new AssertionError("invalid generation accepted"); }
        catch (IllegalArgumentException expected) { }
        check(first.resolve(beforeInvalid.path) == null, "failed open also revokes prior publication");
        System.out.println("READER_PUBLICATION_SESSION_OK");
    }
}
