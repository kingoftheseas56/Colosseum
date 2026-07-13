// Phase 1 exit gate — tankorent engine import (spec 2026-07-13).
// PROVES over live DHT/trackers, on the IMPORTED engine:
//   fresh lane:  magnet -> metadata -> per-file pick (smallest file only,
//                setFilePriorities) -> that file completes -> resume blob
//                persisted -> exit 0
//   resume lane (--resume): the .fastresume from the fresh lane parses with
//                progress intact (kill-and-restart survival, offline) -> exit 0
// Usage:
//   torrent_engine_download_harness.exe <workDir> [magnetUri]
//   torrent_engine_download_harness.exe --resume <workDir>
// Exit: 0 PASS · 1 FAIL · 2 TIMEOUT. Verdict rides the exit code; the watchdog
// guarantees exit (house law: a hung harness is a failed harness).
// Default torrent: Blender's Sintel (open movie, legal, seeded for a decade).
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QVector>
#include <cstdio>
#include "torrent/engine/TorrentEngine.h"

namespace {
const char* kDefaultMagnet =
    "magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10&dn=Sintel"
    "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"
    "&tr=udp%3A%2F%2Fopen.demonii.com%3A1337%2Fannounce"
    "&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce";
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    // Engine reads/writes QSettings (tankorent/bannedPeers); without these a bare
    // harness pollutes HKCU\Software\Unknown Organization (review M2, 2026-07-13).
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("ColosseumEngineHarness"));
    const QStringList args = app.arguments();
    const bool resumeLane = args.contains(QStringLiteral("--resume"));
    QString workDir;
    QString magnet = QString::fromLatin1(kDefaultMagnet);
    if (resumeLane) {
        if (args.size() < 3) { std::printf("FAIL: --resume needs <workDir>\n"); return 1; }
        workDir = args.at(2);
    } else {
        if (args.size() < 2) { std::printf("FAIL: need <workDir> [magnet]\n"); return 1; }
        workDir = args.at(1);
        if (args.size() >= 3) magnet = args.at(2);
    }
    const QString cacheDir = workDir + QStringLiteral("/cache");
    const QString saveDir  = workDir + QStringLiteral("/files");
    QDir().mkpath(cacheDir);
    QDir().mkpath(saveDir);

    TorrentEngine engine(cacheDir);
    engine.setGlobalSeedingRules(0.f, 1);   // download-and-stop: pause 1 s after seeding starts
    engine.start();

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&app]() {
        std::printf("TIMEOUT — watchdog fired\n");
        app.exit(2);
    });

    if (resumeLane) {
        // ---- kill-and-restart lane: fully offline ----
        watchdog.start(30000);
        const QStringList blobs = QDir(cacheDir).entryList({QStringLiteral("*.fastresume")}, QDir::Files);
        if (blobs.isEmpty()) { std::printf("FAIL: no .fastresume in %s\n", qPrintable(cacheDir)); return 1; }
        const QString resumePath = cacheDir + QLatin1Char('/') + blobs.first();
        const auto st = engine.resumeDataDiskState(resumePath, saveDir);
        if (!st.parsed || !st.hadProgress || !st.anyFilePresent) {
            std::printf("FAIL: resume state parsed=%d hadProgress=%d anyFilePresent=%d\n",
                        st.parsed, st.hadProgress, st.anyFilePresent);
            return 1;
        }
        const QString hash = engine.addFromResume(resumePath, saveDir, /*paused=*/true);
        if (hash.isEmpty()) { std::printf("FAIL: addFromResume returned empty hash\n"); return 1; }
        std::printf("PASS resume: parsed + progress intact + files on disk + re-added %s\n",
                    qPrintable(hash));
        engine.stop();
        return 0;
    }

    // ---- fresh lane: live network ----
    watchdog.start(240000);  // metadata over DHT measured at 93-245 s in TB2
    QString ourHash;
    bool finished = false;

    QObject::connect(&engine, &TorrentEngine::torrentAddFailed, &app,
        [&app](const QString&, const QString& err) {
            std::printf("FAIL: add failed: %s\n", qPrintable(err));
            app.exit(1);
        });

    QObject::connect(&engine, &TorrentEngine::metadataReady, &app,
        [&](const QString& infoHash, const QString& name, qint64, const QJsonArray& files) {
            std::printf("metadata: %s (%d files)\n", qPrintable(name), int(files.size()));
            // per-file pick: keep ONLY the smallest nonzero file (proves the
            // Phase-2/3 mechanism — one book out of a 5-book pack, one volume
            // out of a nyaa pack)
            int pickIdx = -1; qint64 pickSize = -1;
            for (const auto& v : files) {
                const auto o = v.toObject();
                const qint64 sz = qint64(o.value(QStringLiteral("size")).toDouble());
                if (sz > 0 && (pickSize < 0 || sz < pickSize)) {
                    pickSize = sz;
                    pickIdx = o.value(QStringLiteral("index")).toInt();
                }
            }
            if (pickIdx < 0) { std::printf("FAIL: no files in metadata\n"); app.exit(1); return; }
            QVector<int> prio(int(files.size()), 0);  // 0 = skip
            prio[pickIdx] = 4;                        // libtorrent default priority
            engine.setFilePriorities(infoHash, prio);
            std::printf("picked file %d (%lld bytes), all others skipped\n",
                        pickIdx, static_cast<long long>(pickSize));
        });

    QObject::connect(&engine, &TorrentEngine::torrentFinished, &app,
        [&](const QString& infoHash) {
            finished = true;
            std::printf("finished: %s — waiting for resume blob\n", qPrintable(infoHash));
        });

    QObject::connect(&engine, &TorrentEngine::resumeDataAvailable, &app,
        [&](const QString& infoHash, const QByteArray& blob) {
            if (!finished) return;  // only persist the post-completion blob
            const QString path = cacheDir + QLatin1Char('/') + infoHash
                                 + QStringLiteral(".fastresume");
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                std::printf("FAIL: cannot write %s\n", qPrintable(path));
                app.exit(1);
                return;
            }
            f.write(blob);
            f.close();
            std::printf("PASS fresh: downloaded picked file + resume blob at %s\n",
                        qPrintable(path));
            app.exit(0);
        });

    ourHash = engine.addMagnet(magnet, saveDir, /*paused=*/false);
    if (ourHash.isEmpty()) { std::printf("FAIL: addMagnet returned empty hash\n"); return 1; }
    std::printf("added %s — waiting on swarm...\n", qPrintable(ourHash));

    const int code = app.exec();
    engine.stop();
    return code;
}
