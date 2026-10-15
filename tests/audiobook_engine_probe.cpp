// audiobook_engine_probe — LIVE end-to-end triage for the audiobook delivery chain
// (2026-07-18: book-page downloads fail with 'retry' every time; each stage passes in
// isolation, so this drives the REAL AudiobookDownloader + StreamServer assembly and
// prints every signal). NETWORK-DEPENDENT — a triage tool, not part of the suite.
//
// PASS = the first progress byte arrives (the chain works; we cancel and exit — no need
// to pull 592 MB). FAIL prints the downloader's own failure reason — the string the GUI
// app logs to its (invisible) console.
//
// Stores are sandboxed (QStandardPaths test mode): the live audiobooks/index.json is
// never touched. Run: native\build-target.bat audiobook_engine_probe → run the exe.
#include "engine/AudiobookDownloader.h"
#include "player/streamserver.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Brotherhood"));
    app.setApplicationName(QStringLiteral("Colosseum"));
    QStandardPaths::setTestModeEnabled(true);   // sandbox the audiobook index/files

    QNetworkAccessManager nam;
    StreamServer stream;
    AudiobookDownloader dl(&nam, &stream);

    // The verified-good Joe Country torrent (hash matches the ABB detail page).
    const QString pairKey  = QStringLiteral("probe joe country|mick herron");
    const QString infoHash = QStringLiteral("e983595122553ecafc7e8101ade5b297bdc1e022");

    QObject::connect(&dl, &AudiobookDownloader::resolving, [](const QString& k) {
        std::printf("[probe] resolving %s\n", qUtf8Printable(k)); std::fflush(stdout);
    });
    QObject::connect(&dl, &AudiobookDownloader::progress,
                     [&](const QString&, double rcv, double tot) {
        std::printf("[probe] progress %.0f / %.0f — BYTES ARE FLOWING\n", rcv, tot);
        std::printf("VERDICT: PASS (chain delivers; cancelling)\n"); std::fflush(stdout);
        dl.cancelDownload(pairKey);
        QCoreApplication::exit(0);
    });
    QObject::connect(&dl, &AudiobookDownloader::finished, [](const QString&, const QString& dir) {
        std::printf("[probe] finished -> %s\nVERDICT: PASS\n", qUtf8Printable(dir)); std::fflush(stdout);
        QCoreApplication::exit(0);
    });
    QObject::connect(&dl, &AudiobookDownloader::failed, [](const QString&, const QString& why) {
        std::printf("[probe] FAILED: %s\nVERDICT: FAIL\n", qUtf8Printable(why)); std::fflush(stdout);
        QCoreApplication::exit(1);
    });
    QObject::connect(&stream, &StreamServer::streamError, [](const QString& msg) {
        std::printf("[probe] stream error: %s\n", qUtf8Printable(msg)); std::fflush(stdout);
    });

    dl.downloadAudiobook(pairKey, infoHash,
                         QStringLiteral("Joe Country (probe)"), QStringLiteral("Mick Herron"));

    QTimer::singleShot(180000, [] {   // 3-minute hard cap (cold engine + manifest polls)
        std::printf("VERDICT: FAIL (180s cap — no terminal signal)\n"); std::fflush(stdout);
        QCoreApplication::exit(2);
    });
    return app.exec();
}
