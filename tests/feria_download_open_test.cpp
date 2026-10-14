#include "feria/FeriaAccountStore.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

class DownloadOpenTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    FeriaAccountStore store;
    int opens = 0;
    QString file;
    QString openedPath, openedSource;
    void bytes(const QByteArray &value) {
        QFile output(file); QVERIFY(output.open(QIODevice::WriteOnly)); QCOMPARE(output.write(value), value.size());
    }
    void record(QString state = "completed", QString name = "notes.txt", QString path = {}) {
        const auto token = store.beginSessionVisit("fixture", {{"pk", "youtube"}});
        QVERIFY(store.recordBrowserDownload({{"id", "owned"}, {"url", "https://example.org/notes"},
            {"fileName", name}, {"path", path.isEmpty() ? file : path}, {"state", state},
            {"sessionId", "fixture"}, {"visitToken", token}}));
    }
private slots:
    void init() {
        QVERIFY(dir.isValid()); file = dir.filePath("notes.txt"); bytes("harmless text");
        store.setStoragePath(dir.filePath("account.json")); store.clearBrowserDownloads(); opens = 0;
        store.setDownloadOpenerForTesting([&](const QString &path, const QString &source) {
            ++opens; openedPath = path; openedSource = source;
            return QVariantMap{{"ok", true}, {"reason", "request-started"}};
        });
    }
    void legacyFileNeedsExplicitConfirmationAndCancelIsFinal() {
        record(); auto prepared = store.prepareBrowserDownloadOpen("owned", store.browserGeneration());
        QVERIFY(prepared.value("ok").toBool()); QVERIFY(prepared.value("requiresConfirmation").toBool());
        QVERIFY(prepared.value("warning").toString().contains("record", Qt::CaseInsensitive));
        const auto ticket = prepared.value("ticket").toString(); QVERIFY(!ticket.isEmpty());
        QVERIFY(!store.finishBrowserDownloadOpen(ticket, false).value("ok").toBool());
        QVERIFY(!store.finishBrowserDownloadOpen(ticket, true).value("ok").toBool()); QCOMPARE(opens, 0);
    }
    void confirmedCompletedRecordedFileOpensOnce() {
        record(); auto prepared = store.prepareBrowserDownloadOpen("owned", store.browserGeneration());
        QVERIFY(prepared.value("ok").toBool()); const auto ticket = prepared.value("ticket").toString();
        QVERIFY(store.finishBrowserDownloadOpen(ticket, true).value("ok").toBool()); QCOMPARE(opens, 1);
        QCOMPARE(openedPath, QFileInfo(file).canonicalFilePath()); QCOMPARE(openedSource, QString("https://example.org/notes"));
        QVERIFY(!store.finishBrowserDownloadOpen(ticket, true).value("ok").toBool()); QCOMPARE(opens, 1);
    }
    void incompleteMissingUnknownAndSubstitutedPathsRefuse() {
        record("downloading"); QVERIFY(!store.prepareBrowserDownloadOpen("owned", store.browserGeneration()).value("ok").toBool());
        record(); QVERIFY(!store.prepareBrowserDownloadOpen(file, store.browserGeneration()).value("ok").toBool());
        QVERIFY(!store.prepareBrowserDownloadOpen("unknown", store.browserGeneration()).value("ok").toBool());
        record("completed", "different.exe"); QVERIFY(!store.prepareBrowserDownloadOpen("owned", store.browserGeneration()).value("ok").toBool());
        record(); QVERIFY(QFile::remove(file)); QVERIFY(!store.prepareBrowserDownloadOpen("owned", store.browserGeneration()).value("ok").toBool()); QCOMPARE(opens, 0);
    }
    void fileChangeAccountChangeAndHistoryRemovalCancelPreparedAuthority() {
        record(); auto prepared = store.prepareBrowserDownloadOpen("owned", store.browserGeneration()); QVERIFY(prepared.value("ok").toBool());
        bytes("changed harmless bytes"); QVERIFY(!store.finishBrowserDownloadOpen(prepared.value("ticket").toString(), true).value("ok").toBool());
        prepared = store.prepareBrowserDownloadOpen("owned", store.browserGeneration()); QVERIFY(prepared.value("ok").toBool());
        store.clearBrowserDownloads(); QVERIFY(!store.finishBrowserDownloadOpen(prepared.value("ticket").toString(), true).value("ok").toBool());
        record(); prepared = store.prepareBrowserDownloadOpen("owned", store.browserGeneration()); QVERIFY(prepared.value("ok").toBool());
        const int oldGeneration = store.browserGeneration(); store.setStoragePath(dir.filePath("another.json"));
        QVERIFY(!store.finishBrowserDownloadOpen(prepared.value("ticket").toString(), true).value("ok").toBool());
        QVERIFY(!store.prepareBrowserDownloadOpen("owned", oldGeneration).value("ok").toBool()); QCOMPARE(opens, 0);
    }
    void misleadingExecutableAndOsCancellationAreDisclosedAndPreserved() {
        file = dir.filePath("invoice.pdf.exe"); bytes("harmless fixture, not executable"); record("completed", "invoice.pdf.exe");
        auto prepared = store.prepareBrowserDownloadOpen("owned", store.browserGeneration()); QVERIFY(prepared.value("ok").toBool());
        QVERIFY(prepared.value("warning").toString().contains("program", Qt::CaseInsensitive));
        store.setDownloadOpenerForTesting([&](const QString &, const QString &) { ++opens; return QVariantMap{{"ok", false}, {"reason", "os-canceled"}}; });
        QCOMPARE(store.finishBrowserDownloadOpen(prepared.value("ticket").toString(), true).value("reason").toString(), "os-canceled"); QCOMPARE(opens, 1);
    }
    void callerObjectsDoNotBecomeEngineAuthority() {
        QObject untrusted; untrusted.setProperty("state", 2); untrusted.setProperty("path", file);
        QVERIFY(!store.watchBrowserDownloads(&untrusted)); QVERIFY(store.browserDownloadAuthority(&untrusted).isEmpty());
    }
    void disguisedProgramAndPolicyRefusalDoNotFallBack() {
        bytes("MZ harmless signature fixture"); record();
        const auto prepared = store.prepareBrowserDownloadOpen("owned", store.browserGeneration());
        QVERIFY(prepared.value("ok").toBool());
        QVERIFY(prepared.value("warning").toString().contains("program"));
        store.setDownloadOpenerForTesting([&](const QString &, const QString &) {
            ++opens; return QVariantMap{{"ok", false}, {"reason", "os-blocked"}};
        });
        QCOMPARE(store.finishBrowserDownloadOpen(prepared.value("ticket").toString(), true).value("reason").toString(), "os-blocked");
        QCOMPARE(opens, 1);
        QVERIFY(!store.finishBrowserDownloadOpen(prepared.value("ticket").toString(), true).value("ok").toBool());
        QCOMPARE(opens, 1);
    }
    void provenanceChangeAndCallerSuppliedFingerprintCannotAuthorize() {
        record(); const auto prepared = store.prepareBrowserDownloadOpen("owned", store.browserGeneration());
        const auto token = store.beginSessionVisit("fixture", {{"pk", "youtube"}});
        QVERIFY(store.recordBrowserDownload({{"id", "owned"}, {"url", "https://other.example/changed"},
            {"path", file}, {"fileName", "notes.txt"}, {"state", "completed"},
            {"downloadSnapshot", QVariantMap{{"path", file}}}, {"sessionId", "fixture"}, {"visitToken", token}}));
        QVERIFY(store.browserDownloadHistory().first().toMap().value("downloadSnapshot").toMap().isEmpty());
        QCOMPARE(store.finishBrowserDownloadOpen(prepared.value("ticket").toString(), true).value("reason").toString(), "provenance-changed");
        QCOMPARE(opens, 0);
    }
};
QTEST_GUILESS_MAIN(DownloadOpenTest)
#include "feria_download_open_test.moc"
