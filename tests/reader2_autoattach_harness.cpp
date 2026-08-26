// reader2_autoattach_harness — deterministic, offline proof of TASK 12: an
// audiobook auto-attaches to its book (keyed by the reader's bookId) at download
// completion, with NO pairing UI.
//
// The load-bearing contract: the key the attach writes under MUST be the SAME key
// Task 13's Audio tab looks up — `BookStores::keyFor(<the ebook's file path>)`
// (= ReaderShell.bookId = Reader2Bridge.bookKey(bookPath)). So the harness builds
// its `bookId` through BookStores::keyFor and asserts the pairing lands under it.
//
// Offline drive: we don't run a real libtorrent/Stremio download. Instead we
// manufacture a prior on-disk download (index.json + a real audio file) in the
// test-mode sandbox, then call the REAL bookId-carrying downloadAudiobook(...)
// overload. Its idempotent path (isDownloaded → re-emit finished) is the exact
// production code path that fires the auto-attach — no test-only shim.
#include "engine/AudiobookDownloader.h"
#include "AudioPairingStore.h"
#include "reader/BookStores.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QVariantMap>

#include <cstdio>

namespace {

QString audiobooksBaseDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/audiobooks");
}

// Manufacture a "this audiobook is already downloaded" state on disk the way a
// prior real download would leave it: a directory with one real audio file, plus
// an index.json entry (which AudiobookDownloader::loadIndex reads at construction,
// keeping the entry only if files[0] actually exists). Returns the dir path.
QString seedDownloaded(QJsonObject& indexRoot, const QString& pairKey, const QString& folder,
                       const QString& title, const QString& author)
{
    // `folder` (not pairKey — a pairKey holds '|', illegal in a Windows path) names the
    // on-disk dir; loadIndex() stores whatever dir/files the index.json entry declares.
    const QString dir = audiobooksBaseDir() + QStringLiteral("/") + folder;
    QDir().mkpath(dir);
    const QString file = dir + QStringLiteral("/01 - track.mp3");
    { QFile f(file); if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write("id3fake"); }

    QJsonObject o;
    o[QStringLiteral("dir")]     = dir;
    o[QStringLiteral("title")]   = title;
    o[QStringLiteral("author")]  = author;
    o[QStringLiteral("bytes")]   = 7.0;
    o[QStringLiteral("addedAt")] = 1.0;
    QJsonArray files; files.append(file);
    o[QStringLiteral("files")]   = files;
    indexRoot[pairKey] = o;
    return dir;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    // TEST-SCOPED QSettings identity (2026-07-18 fix): QStandardPaths test mode redirects
    // FILE paths only — QSettings still resolves to the REAL registry hive of whatever
    // org/app names are set. This harness previously wiped Brotherhood/Colosseum's live
    // `audiobook/pairings` on EVERY run — silently destroying the user's real book↔audiobook
    // attachments (root cause of the dead Audio tab, 2026-07-18). Distinct names give
    // AudioPairingStore (default-constructed QSettings) its own disposable hive.
    app.setOrganizationName(QStringLiteral("BrotherhoodTest"));
    app.setApplicationName(QStringLiteral("ColosseumAutoattachHarness"));

    // Deterministic across repeated runs (the "verify the committed artifact" rerun
    // depends on this): wipe the sandboxed audiobook dir + the pairing QSettings this
    // harness writes, so a prior run's persisted pairing never leaks into this one.
    // (Test hive ONLY — never the live Brotherhood/Colosseum registry.)
    QDir(audiobooksBaseDir()).removeRecursively();
    {
        QSettings s(QStringLiteral("BrotherhoodTest"), QStringLiteral("ColosseumAutoattachHarness"));
        s.remove(QStringLiteral("audiobook/pairings"));
        s.sync();
    }

    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL %s\n", what); ++fails; }
        else       std::printf("ok   %s\n", what);
    };

    // ── seed two prior downloads on disk, THEN write index.json so loadIndex()
    //    picks them up when the downloader is constructed. ──
    const QString pkAttached = QStringLiteral("attached pair|author");
    const QString pkOrphan   = QStringLiteral("orphan pair|author");
    QJsonObject indexRoot;
    const QString dirAttached = seedDownloaded(indexRoot, pkAttached, "ab_attached", "The Book", "The Author");
    const QString dirOrphan   = seedDownloaded(indexRoot, pkOrphan,   "ab_orphan",  "No Book",  "No Author");
    {
        QDir().mkpath(audiobooksBaseDir());
        QFile f(audiobooksBaseDir() + QStringLiteral("/index.json"));
        check(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "index.json opened for seed");
        f.write(QJsonDocument(indexRoot).toJson(QJsonDocument::Compact));
    }

    // The bookId is the reader's key: keyFor(<ebook file path>). This is EXACTLY
    // what ReaderShell.bookId / Task 13's Audio tab will use to look the pairing up.
    const QString ebookPath = QStringLiteral("C:/Users/TestUser/Books/the-book.epub");
    const QString bookId    = BookStores::keyFor(ebookPath);

    QNetworkAccessManager nam;
    AudiobookDownloader downloader(&nam, /*stream=*/nullptr);
    AudioPairingStore store;
    downloader.setPairing(&store);

    check(downloader.isDownloaded(pkAttached), "seeded audiobook is seen as downloaded");
    const int revBefore = store.revision();
    check(store.getPairing(bookId).isEmpty(), "no pairing before download completion");

    // ── (1) attach case: bookId supplied → idempotent finished() writes the pairing ──
    //    A valid 40-char infoHash so we pass the format guard and reach the
    //    isDownloaded() re-emit (the real auto-attach code path).
    const QString hash40 = QString(40, QChar('a'));
    downloader.downloadAudiobook(pkAttached, hash40, QStringLiteral("The Book"),
                                 QStringLiteral("The Author"), bookId);

    const QVariantMap paired = store.getPairing(bookId);
    check(!paired.isEmpty(), "pairing written under keyFor(ebookPath)");
    check(paired.value(QStringLiteral("pairKey")).toString() == pkAttached,
          "pairing carries the audiobook pairKey");
    check(paired.value(QStringLiteral("dirPath")).toString() == dirAttached,
          "pairing carries the audiobook dirPath");
    check(paired.value(QStringLiteral("bookId")).toString() == bookId,
          "pairing is stamped with the reader bookId");
    check(store.revision() == revBefore + 1, "exactly one write for the attach case");

    // ── (2) empty-bookId case: no bookId → NO pairing written (no bogus key) ──
    downloader.downloadAudiobook(pkOrphan, hash40, QStringLiteral("No Book"),
                                 QStringLiteral("No Author"), QString());
    check(store.revision() == revBefore + 1, "empty bookId wrote NOTHING (no extra bump)");
    check(store.getPairing(pkOrphan).isEmpty(), "empty bookId did not key by pairKey");
    // keyFor("") is the SHA1 of the empty string (NOT ""), and the reader never
    // queries that key — assert we never wrote under it either.
    check(store.getPairing(BookStores::keyFor(QString())).isEmpty(),
          "no pairing written under keyFor(empty) either");

    std::printf(fails ? "VERDICT: FAIL\n" : "VERDICT: PASS\n");
    return fails ? 1 : 0;
}
