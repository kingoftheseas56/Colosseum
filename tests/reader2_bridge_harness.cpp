// reader2_bridge_harness — deterministic, offline proof of Reader2Bridge (TASK 4):
//   (a) filesRead round-trips exact bytes through base64
//   (b) progressGet/progressSave delegate to BookStores (same as the old bridge)
//   (c) paperEvent relays into paperEventReceived with the same name+payload
// dictLookup needs a real event loop + the network — deliberately NOT exercised
// here so the harness stays deterministic and offline (see Task 4 spec).
#include "reader2/Reader2Bridge.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
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
    const QString b64 = bridge.filesRead(path);
    const QByteArray decoded = QByteArray::fromBase64(b64.toLatin1());
    check(decoded == original, "filesRead byte-exact base64 roundtrip");
    check(bridge.filesRead(tmp.path() + QStringLiteral("/does-not-exist.bin")).isEmpty(),
          "filesRead returns empty string on open error");

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

    // (d) bookKey — the zero-migration store key. Must equal SHA1[:20] of the
    //     path-normalized absolute path, IDENTICAL to BookBridge::progressKey. Compute
    //     the expected fingerprint independently here and compare, for a couple of paths.
    auto expectedKey = [](const QString& absPath) {
        const QString norm = QDir::fromNativeSeparators(absPath);
        const QByteArray hex =
            QCryptographicHash::hash(norm.toUtf8(), QCryptographicHash::Sha1).toHex();
        return QString::fromLatin1(hex.left(20));
    };
    const QString p1 = QStringLiteral("C:/x/y.epub");
    const QString p2 = QStringLiteral("C:/Users/Suprabha/Desktop/book with spaces.epub");
    check(bridge.bookKey(p1) == expectedKey(p1), "bookKey matches SHA1[:20] formula (p1)");
    check(bridge.bookKey(p2) == expectedKey(p2), "bookKey matches SHA1[:20] formula (p2)");
    check(bridge.bookKey(p1).size() == 20, "bookKey is 20 hex chars");
    // Native separators normalize to the SAME key (BookBridge does QDir::fromNativeSeparators).
    check(bridge.bookKey(QStringLiteral("C:\\x\\y.epub")) == bridge.bookKey(p1),
          "bookKey normalizes backslashes to forward slashes");

    std::printf(fails ? "VERDICT: FAIL\n" : "VERDICT: PASS\n");
    return fails ? 1 : 0;
}
