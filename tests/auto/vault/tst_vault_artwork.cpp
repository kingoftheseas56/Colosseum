// tst_vault_artwork — Vault browse-artwork execution plan, Slice 1 (2026-08-13).
//
// Proves VaultThumbnailer, the persistent frame-grab producer: a still is written to
// its keyed cache file from a genuinely decodable fixture; a second request for the
// same (path,size,mtimeMs) key is an honest cache hit — the file is left untouched and
// no second ffmpeg is spawned; a missing or corrupt input yields no file and an honest
// empty/false result, never a hang. Drives the SAME tiny.mp4 fixture
// vault_admission_probe_harness already proves decodes a real frame (dwidth > 0) — no
// new fixture invented here. GUILESS; real ffmpeg (MpvItem::findFfmpeg), QTemporaryDir.

#include "engine/VaultThumbnailer.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class tst_vault_artwork : public QObject
{
    Q_OBJECT

private:
    static QString mediaFixture(const QString& name)
    {
        return QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/media/") + name;
    }
    // The exact fixture vault_admission_probe_harness proves ADMITS with a decoded
    // frame (dwidth > 0) via the real libmpv probe — reused here rather than inventing
    // a second "guaranteed decodable" fixture. It is intentionally tiny (1s, 64x64,
    // 8fps), which is exactly why VaultThumbnailer's offset-clamping (never seek past
    // a short clip's own duration) matters and is exercised by every case below.
    static QString tinyMp4() { return mediaFixture(QStringLiteral("tiny.mp4")); }
    static double tinyMp4DurationSec() { return 1.0; }

private slots:
    void still_is_produced_at_keyed_cache_path();
    void second_request_for_same_key_is_a_cache_hit();
    void missing_input_yields_no_file_and_no_hang();
    void corrupt_input_yields_no_file_and_no_hang();
};

void tst_vault_artwork::still_is_produced_at_keyed_cache_path()
{
    QVERIFY2(QFileInfo::exists(tinyMp4()), "fixture tiny.mp4 must exist (shared with vault_admission_probe_harness)");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Baseline (falsifiability): before any request, the thumbs cache holds nothing
    // for this fixture — "it now produces one" has something to be measured against.
    QCOMPARE(QDir(tmp.filePath(QStringLiteral("thumbs"))).entryList(QDir::Files).size(), 0);

    VaultThumbnailer thumbnailer(tmp.path());
    const qint64 size = QFileInfo(tinyMp4()).size();
    const qint64 mtimeMs = 1;

    QCOMPARE(thumbnailer.cachedThumbPath(tinyMp4(), size, mtimeMs), QString()); // miss before any grab

    QSignalSpy ready(&thumbnailer, &VaultThumbnailer::thumbReady);
    const QString immediate = thumbnailer.requestThumb(tinyMp4(), size, mtimeMs, tinyMp4DurationSec());
    QVERIFY2(immediate.isEmpty(), "a fresh grab must not return a path synchronously");

    QVERIFY2(ready.wait(15000), "thumbReady must fire once ffmpeg's grab completes");
    QCOMPARE(ready.count(), 1);
    const QString grabbedPath = ready.at(0).at(1).toString();
    QVERIFY2(QFileInfo::exists(grabbedPath), "the still must land on disk at the emitted path");
    QVERIFY(QFileInfo(grabbedPath).size() > 0);
    QVERIFY2(grabbedPath.startsWith(tmp.filePath(QStringLiteral("thumbs"))),
              "the still must live under the injected cacheDir's thumbs/ subdir");

    QCOMPARE(QDir(tmp.filePath(QStringLiteral("thumbs"))).entryList(QDir::Files).size(), 1);
    QCOMPARE(thumbnailer.cachedThumbPath(tinyMp4(), size, mtimeMs), grabbedPath); // now a sync hit
}

void tst_vault_artwork::second_request_for_same_key_is_a_cache_hit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultThumbnailer thumbnailer(tmp.path());
    const qint64 size = QFileInfo(tinyMp4()).size();
    const qint64 mtimeMs = 1;

    QSignalSpy ready(&thumbnailer, &VaultThumbnailer::thumbReady);
    QVERIFY(thumbnailer.requestThumb(tinyMp4(), size, mtimeMs, tinyMp4DurationSec()).isEmpty());
    QVERIFY(ready.wait(15000));
    const QString grabbedPath = ready.at(0).at(1).toString();
    QVERIFY(QFileInfo::exists(grabbedPath));
    const QDateTime firstMtime = QFileInfo(grabbedPath).lastModified();

    // Second call for the identical key: must be a synchronous hit that spawns
    // NOTHING — checked immediately, before the event loop even turns, so this
    // assertion can only pass if requestThumb never reached startJob().
    ready.clear();
    const QString hitPath = thumbnailer.requestThumb(tinyMp4(), size, mtimeMs, tinyMp4DurationSec());
    QCOMPARE(hitPath, grabbedPath);
    QCOMPARE(thumbnailer.inFlightCount(), 0); // <- negative control target: disable the hit
                                               //    short-circuit and this goes red (becomes 1)

    // No fresh signal, and the file itself is untouched (mtime unchanged) — a
    // wrongly-spawned second process would eventually rewrite the file.
    QCOMPARE(ready.count(), 0);
    QVERIFY2(!ready.wait(3000), "a cache hit must never trigger a second thumbReady");
    QCOMPARE(QFileInfo(grabbedPath).lastModified(), firstMtime);
}

void tst_vault_artwork::missing_input_yields_no_file_and_no_hang()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultThumbnailer thumbnailer(tmp.path());
    const QString missing = tmp.filePath(QStringLiteral("does-not-exist.mp4"));

    QSignalSpy ready(&thumbnailer, &VaultThumbnailer::thumbReady);
    QVERIFY(thumbnailer.requestThumb(missing, 123, 456).isEmpty());
    QVERIFY2(!ready.wait(5000), "a nonexistent input must never produce a still");

    QCOMPARE(thumbnailer.cachedThumbPath(missing, 123, 456), QString());
    QCOMPARE(thumbnailer.inFlightCount(), 0); // bounded-timeout cleanup left no zombie job
}

void tst_vault_artwork::corrupt_input_yields_no_file_and_no_hang()
{
    const QString corrupt = mediaFixture(QStringLiteral("not-a-video.mp4")); // junk bytes, real fixture
    QVERIFY2(QFileInfo::exists(corrupt), "fixture not-a-video.mp4 must exist (shared with the admission probe test)");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultThumbnailer thumbnailer(tmp.path());
    const qint64 size = QFileInfo(corrupt).size();

    QSignalSpy ready(&thumbnailer, &VaultThumbnailer::thumbReady);
    QVERIFY(thumbnailer.requestThumb(corrupt, size, 1).isEmpty());
    QVERIFY2(!ready.wait(5000), "corrupt/undecodable bytes must never produce a still");

    QCOMPARE(thumbnailer.cachedThumbPath(corrupt, size, 1), QString());
    QCOMPARE(thumbnailer.inFlightCount(), 0);
}

QTEST_GUILESS_MAIN(tst_vault_artwork)
#include "tst_vault_artwork.moc"
