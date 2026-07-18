// reader2_bridge_harness — deterministic, offline proof of Reader2Bridge (TASK 4):
//   (a) filesRead round-trips exact bytes through base64
//   (b) progressGet/progressSave delegate to BookStores (same as the old bridge)
//   (c) paperEvent relays into paperEventReceived with the same name+payload
// dictLookup needs a real event loop + the network — deliberately NOT exercised
// here so the harness stays deterministic and offline (see Task 4 spec).
#include "reader2/Reader2Bridge.h"
#include "reader/BookStores.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL %s\n", what); ++fails; }
        else       std::printf("ok   %s\n", what);
    };

    Reader2Bridge bridge;

    // (a) filesRead — write known bytes, read back, decode base64, compare exact.
    QTemporaryDir tmp;
    check(tmp.isValid(), "tempdir created");
    const QByteArray original("\x00\x01\x02Hello, Reader2!\xff\xfe", 21);
    const QString path = tmp.path() + QStringLiteral("/probe.bin");
    {
        QFile f(path);
        check(f.open(QIODevice::WriteOnly), "probe file opened for write");
        f.write(original);
    }
    // Authorize THIS book first (hardening): filesRead serves ONLY the currently-open book —
    // an untrusted paper can no longer pull arbitrary files off disk through the bridge.
    bridge.setAuthorizedBook(path);
    const QString b64 = bridge.filesRead(path);
    const QByteArray decoded = QByteArray::fromBase64(b64.toLatin1());
    check(decoded == original, "filesRead byte-exact base64 roundtrip (authorized book)");
    check(bridge.filesRead(tmp.path() + QStringLiteral("/does-not-exist.bin")).isEmpty(),
          "filesRead returns empty string for a missing / non-authorized path");

    // (a2) filesRead AUTHORIZATION — a REAL file that is NOT the authorized book must be refused.
    const QString other = tmp.path() + QStringLiteral("/other.bin");
    {
        QFile f(other);
        check(f.open(QIODevice::WriteOnly), "second probe file opened for write");
        f.write(original);
    }
    check(bridge.filesRead(other).isEmpty(),
          "filesRead refuses a real but NON-authorized path (paper can't read arbitrary files)");
    // Re-authorizing switches which single book is served (the per-open contract).
    bridge.setAuthorizedBook(other);
    check(!bridge.filesRead(other).isEmpty(), "filesRead serves the newly-authorized book");
    check(bridge.filesRead(path).isEmpty(),   "filesRead now refuses the previously-authorized book");
    bridge.setAuthorizedBook(path);           // restore for any later cases

    // (b) progress store roundtrip (delegates to BookStores — same file as old bridge)
    QJsonObject p{{"cfi", "epubcfi(/6/4!/4/2)"}, {"percent", 42}};
    bridge.progressSave(QStringLiteral("bk1"), p);
    const QJsonObject got = bridge.progressGet(QStringLiteral("bk1"));
    check(got.value("cfi").toString() == QStringLiteral("epubcfi(/6/4!/4/2)"), "progress cfi roundtrip");
    check(got.value("percent").toInt() == 42, "progress percent roundtrip");

    // (c) paperEvent -> paperEventReceived relay
    QString gotName, gotJson;
    int signalCount = 0;
    QObject::connect(&bridge, &Reader2Bridge::paperEventReceived,
                      [&](const QString& name, const QString& json) {
                          ++signalCount; gotName = name; gotJson = json;
                      });
    bridge.paperEvent(QStringLiteral("relocated"), QStringLiteral("{\"percent\":7}"));
    check(signalCount == 1, "paperEvent fired paperEventReceived exactly once");
    check(gotName == QStringLiteral("relocated"), "paperEventReceived name matches");
    check(gotJson == QStringLiteral("{\"percent\":7}"), "paperEventReceived payload matches");

    // (d) bookKey — the zero-migration store key. It must (1) delegate to the ONE
    //     shared derivation BookStores::keyFor (so old + fresh readers can never drift),
    //     and (2) equal a KNOWN-GOOD fingerprint for a fixed path (a hardcoded expected
    //     value, not a recomputation — that's what catches a formula change, whereas a
    //     recompute-here would be a tautology that drifts in lockstep).
    const QString p1 = QStringLiteral("C:/x/y.epub");
    const QString p2 = QStringLiteral("C:/Users/Suprabha/Desktop/book with spaces.epub");
    check(bridge.bookKey(p1) == BookStores::keyFor(p1), "bookKey delegates to BookStores::keyFor (p1)");
    check(bridge.bookKey(p2) == BookStores::keyFor(p2), "bookKey delegates to BookStores::keyFor (p2)");
    // Known-good: SHA1[:20] of the UTF-8 bytes of "C:/x/y.epub" (drift tripwire).
    check(bridge.bookKey(p1) == QStringLiteral("c6c28cd2ca56ec13e016"),
          "bookKey matches known-good SHA1[:20] for C:/x/y.epub");
    check(bridge.bookKey(p1).size() == 20, "bookKey is 20 hex chars");
    // Native separators normalize to the SAME key (keyFor does QDir::fromNativeSeparators).
    check(bridge.bookKey(QStringLiteral("C:\\x\\y.epub")) == bridge.bookKey(p1),
          "bookKey normalizes backslashes to forward slashes");

    std::printf(fails ? "VERDICT: FAIL\n" : "VERDICT: PASS\n");
    return fails ? 1 : 0;
}
