#include "FeriaAccountStore.h"
#include "FeriaBrowserPolicy.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    int checks = 0;
    auto check = [&](bool ok, const char *message) { ++checks; if (!ok) { qCritical() << message; std::exit(1); } };
    FeriaAccountStore store;
    const auto path = temp.filePath("a/account.json");
    store.setStoragePath(path);
    check(store.sessions().isEmpty(), "Empty history must not contain samples");
    check(FeriaAccountStore::safeUrl("https://youtube.com/watch?v=abc&access_token=secret#token=secret") == "https://youtube.com/watch?v=abc", "Strip sensitive query and fragment");
    check(FeriaAccountStore::safeUrl("https://example.com/oauth/callback?code=secret").isEmpty(), "Reject sign-in callback");
    check(FeriaAccountStore::safeUrl("file:///private").isEmpty(), "Reject local URL");
    check(FeriaAccountStore::safeUrl("https://read.amazon.com/?asin=ABC").contains("asin=ABC"), "Keep reader identity");
    store.beginVisit({{"pk", "youtube"}});
    QVariantMap sample{{"href", "https://youtube.com/watch?v=abc"}, {"title", "An actual video"},
        {"position", 10.0}, {"duration", 100.0}, {"paused", false}, {"rate", 1.0}};
    auto ad = sample; ad.insert("ad", true);
    check(!store.observe(ad) && store.sessions().isEmpty(), "Ads excluded");
    check(store.observe(sample), "Record playing media");
    check(store.sessions().size() == 1 && store.continueItems().size() == 1, "One history and Continue item");
    QThread::msleep(80); sample.insert("position", 10.08);
    check(store.observe(sample), "Record playback delta");
    double time = store.sessions().first().toMap().value("mins").toDouble();
    check(time > 0 && time < 0.01, "Only elapsed playback counted");
    sample.insert("position", 80.0); store.observe(sample);
    check(store.sessions().first().toMap().value("mins").toDouble() == time, "Seek must not count as watching");
    sample.insert("paused", true); store.observe(sample);
    QThread::msleep(80); store.observe(sample);
    check(store.sessions().first().toMap().value("mins").toDouble() == time, "Pause must not count");
    sample.insert("ended", true); sample.insert("position", 100.0); store.observe(sample);
    check(store.continueItems().isEmpty(), "Completed media leaves Continue");
    store.beginVisit({{"pk", "kindle"}});
    check(store.saveReadingPlace("https://read.amazon.com/?asin=BOOK", "My book"), "Explicit reading bookmark");
    check(store.continueItems().size() == 1, "Reading place available");
    check(store.continueItems().first().toMap().value("mins").toDouble() == 0, "Do not invent reading time");
    FeriaAccountStore reopened; reopened.setStoragePath(path);
    check(reopened.sessions().size() == 2, "Persist across restart");
    check(reopened.dismissContinue(reopened.continueItems().first().toMap().value("id").toString()), "Dismiss Continue");
    check(reopened.continueItems().isEmpty() && reopened.sessions().size() == 2, "Dismiss preserves history");
    reopened.setRecording(false); reopened.beginVisit({{"pk", "kindle"}});
    check(!reopened.saveReadingPlace("https://example.com/book", "Paused"), "Recording preference enforced");
    reopened.setStoragePath(temp.filePath("b/account.json"));
    check(reopened.sessions().isEmpty(), "Profiles isolated");
    reopened.setStoragePath({});
    check(!reopened.recording() && !reopened.setRecording(true), "Sealed profile cannot persist");
    store.clearHistory();
    check(store.sessions().isEmpty() && store.continueItems().isEmpty(), "Clear removes history and Continue");
    store.beginVisit({{"pk", "webtoon"}});
    QVariantMap reading{{"href", "https://www.webtoons.com/en/reader?episode_no=12"}, {"title", "Chapter 12"},
        {"kind", "book"}, {"paused", false}, {"locator", QVariantMap{{"type", "scroll"}, {"selector", "#reader"}, {"fraction", 0.4}}}};
    check(store.observe(reading), "Automatic reading place");
    auto saved = store.continueItems().first().toMap();
    check(saved.value("position").toDouble() == 40 && saved.value("duration").toDouble() == 100, "Reading percentage stored");
    check(saved.value("locator").toMap().value("selector") == "#reader", "Nested reader locator retained");
    QThread::msleep(80); store.observe(reading);
    check(store.sessions().first().toMap().value("mins").toDouble() > 0, "Active reading time counted");
    reading.insert("paused", true); store.observe(reading);
    time = store.sessions().first().toMap().value("mins").toDouble();
    QThread::msleep(80); store.observe(reading);
    check(store.sessions().first().toMap().value("mins").toDouble() == time, "Idle reading excluded");
    check(store.saveReadingPlace(reading.value("href").toString(), reading.value("title").toString()), "Save automatic reading place manually");
    check(store.continueItems().first().toMap().value("position").toDouble() == 40, "Manual save preserves automatic position");
    reopened.setStoragePath(path);
    check(reopened.continueItems().first().toMap().value("locator").toMap().value("fraction").toDouble() == 0.4, "Reading locator persists across restart");
    reading.insert("locator", QVariantMap{{"type", "scroll"}, {"fraction", 0.99}});
    check(store.observe(reading) && store.continueItems().isEmpty(), "Completed reading leaves Continue");
    reading.insert("locator", QVariantMap{{"type", "scroll"}, {"fraction", std::nan("")}});
    check(!store.observe(reading), "Invalid reading locator rejected");
    store.beginVisit({{"pk", "netflix"}});
    check(!store.observe(reading), "A video provider cannot submit reading observations");
    FeriaBrowserPolicy policy;
    policy.setStorageRoot(temp.filePath("browser-a"));
    policy.rememberOrigin("netflix", QUrl("https://user:secret@player.netflix.com/watch/1?token=secret#secret"));
    const auto scope = policy.sessionScope("netflix");
    check(scope.value("origins").toStringList().contains("https://player.netflix.com"), "Visited provider origin retained without credentials");
    check(!scope.value("origins").toStringList().join(',').contains("secret"), "Session scope excludes credentials");
    check(FeriaBrowserPolicy::cookieInScope(".netflix.com", scope.value("domains").toStringList()), "Parent provider cookie matched");
    check(!FeriaBrowserPolicy::cookieInScope("evilnetflix.com", scope.value("domains").toStringList()), "Domain suffix boundary enforced");
    check(policy.sessionScope("unknown").isEmpty(), "Unknown provider scope rejected");
    policy.setStorageRoot(temp.filePath("browser-b"));
    check(!policy.sessionScope("netflix").value("origins").toStringList().contains("https://player.netflix.com"), "Origin history isolated by profile");
    policy.setStorageRoot({});
    check(policy.sessionScope("netflix").isEmpty(), "Sealed profile cannot clear another browser profile");
    QFile broken(temp.filePath("broken.json")); check(broken.open(QIODevice::WriteOnly), "Create corrupt fixture"); broken.write("broken"); broken.close();
    reopened.setStoragePath(broken.fileName());
    check(!reopened.error().isEmpty() && !reopened.clearHistory(), "Preserve unreadable store");
    FeriaAccountStore hotstar;
    hotstar.setStoragePath(temp.filePath("hotstar.json"));
    hotstar.beginVisit({{"pk", "hotstar"}});
    check(hotstar.observe({{"href", "https://www.hotstar.com/in/shows/example/1260000001/watch"},
        {"title", "JioHotstar episode"}, {"position", 42}, {"duration", 1200}, {"paused", false}}),
        "JioHotstar playback accepted by provider registry");
    check(hotstar.continueItems().first().toMap().value("pk") == "hotstar",
        "JioHotstar Continue retains its own provider");
    policy.setStorageRoot(temp.filePath("browser-hotstar"));
    check(FeriaBrowserPolicy::cookieInScope(".hotstar.com", policy.sessionScope("hotstar").value("domains").toStringList()),
        "JioHotstar sign-out scope includes its service domain");
    FeriaAccountStore privacy;
    const auto privacyPath = temp.filePath("privacy/account.json");
    privacy.setStoragePath(privacyPath);
    privacy.beginVisit({{"pk", "webtoon"}});
    reading.insert("locator", QVariantMap{{"type", "scroll"}, {"fraction", 0.4}, {"selector", "#reader"}});
    reading.insert("paused", false);
    check(privacy.observe(reading), "Create saved reading place for clear regression");
    privacy.beginVisit({{"pk", "youtube"}});
    sample.insert("position", 20.0); sample.insert("paused", false); sample.insert("ended", false);
    check(privacy.observe(sample), "Create playback place for clear regression");
    QThread::msleep(80); sample.insert("position", 20.08); privacy.observe(sample);
    const auto continuing = privacy.continueItems();
    const auto videoId = continuing.first().toMap().value("id").toString();
    check(continuing.size() == 2 && privacy.clearActivity() && privacy.sessions().isEmpty(),
          "Feria activity clear removes only activity sessions");
    const auto retained = privacy.continueItems();
    check(retained.size() == 2
          && retained.first().toMap().value("position") == continuing.first().toMap().value("position")
          && retained.last().toMap().value("locator") == continuing.last().toMap().value("locator"),
          "Clear preserves video position and reading locator");
    check(!retained.first().toMap().contains("mins") && !retained.first().toMap().contains("sessionId"),
          "Retained progress does not preserve cleared activity measurements");
    FeriaAccountStore privacyReopened;
    privacyReopened.setStoragePath(privacyPath);
    check(privacyReopened.sessions().isEmpty()
          && QJsonDocument::fromVariant(privacyReopened.continueItems()).toJson()
              == QJsonDocument::fromVariant(retained).toJson(),
          "Clear survives restart without restoring activity");
    check(privacyReopened.dismissContinue(videoId), "Dismiss retained playback place");
    FeriaAccountStore afterDismiss;
    afterDismiss.setStoragePath(privacyPath);
    check(afterDismiss.continueItems().size() == 1, "Retained dismissal survives restart");
    sample.insert("position", 21.0);
    check(privacy.observe(sample) && privacy.sessions().first().toMap().value("mins").toDouble() == 0,
          "Existing host can record after clear without inheriting old activity time");
    QThread::msleep(80); sample.insert("position", 21.08); privacy.observe(sample);
    check(privacy.sessions().first().toMap().value("mins").toDouble() > 0,
          "New playback time is recorded after clearing");
    check(!reopened.clearActivity(), "Unreadable activity store rejects clear");
    QFile preserved(broken.fileName());
    check(preserved.open(QIODevice::ReadOnly), "Read rejected clear source file");
    check(preserved.readAll() == "broken", "Rejected clear preserves unreadable source file");
    FeriaAccountStore failedClear;
    const auto blockedPath = temp.filePath("write-failure/account.json");
    failedClear.setStoragePath(blockedPath);
    failedClear.beginVisit({{"pk", "youtube"}});
    check(failedClear.observe(sample), "Create activity before write-failure clear");
    const auto beforeFailedClear = failedClear.sessions();
    const auto backupPath = blockedPath + ".preserved";
    check(QFile::rename(blockedPath, backupPath) && QDir().mkdir(blockedPath), "Block atomic save with a directory");
    check(!failedClear.clearActivity() && failedClear.sessions() == beforeFailedClear,
          "Failed save leaves activity and Continue available");
    FeriaAccountStore preservedActivity;
    preservedActivity.setStoragePath(backupPath);
    check(QJsonDocument::fromVariant(preservedActivity.sessions()).toJson()
              == QJsonDocument::fromVariant(beforeFailedClear).toJson(),
          "Failed clear preserves persisted activity");
    check(privacyReopened.clearHistory() && privacyReopened.continueItems().isEmpty(),
          "Internal full-reset API can still reset isolated fixtures");
    FeriaAccountStore manga;
    const auto mangaPath = temp.filePath("manga.json");
    manga.setStoragePath(mangaPath);
    manga.beginVisit({{"pk", "mangaplus"}});
    QVariantMap chapter{{"href", "https://mangaplus.shueisha.co.jp/viewer/1000486"},
        {"title", "MANGA Plus by SHUEISHA"}, {"kind", "book"}, {"paused", true},
        {"locator", QVariantMap{{"type", "url"}}}};
    check(manga.observe(chapter), "Record chapter while its title loads");
    const auto chapterId = manga.continueItems().first().toMap().value("id");
    chapter.insert("title", "[#001] One Piece | MANGA Plus");
    check(manga.observe(chapter) && manga.continueItems().size() == 1
          && manga.continueItems().first().toMap().value("id") == chapterId
          && manga.sessions().size() == 1,
          "Loading title changes must not duplicate a chapter or split its session");
    const QVariantMap pagePlace{{"type", "mangaplus-page"}, {"page", 3}, {"offset", 0.5}, {"total", 58}};
    chapter.insert("locator", pagePlace);
    check(manga.observe(chapter), "Accept MANGA Plus page position");
    FeriaAccountStore mangaReopened;
    mangaReopened.setStoragePath(mangaPath);
    check(mangaReopened.continueItems().first().toMap().value("locator").toMap() == pagePlace,
          "MANGA Plus page and within-page offset survive restart");
    check(std::abs(mangaReopened.continueItems().first().toMap().value("position").toDouble()
          - 100.0 * 3.5 / 58) < 0.0001, "Page progress uses chapter length, not lazy scroll extent");
    check(manga.saveReadingPlace(chapter.value("href").toString(), "One Piece")
          && manga.continueItems().first().toMap().value("locator").toMap() == pagePlace,
          "Manual save preserves the captured MANGA Plus page");
    for (const QVariantMap &bad : {
            QVariantMap{{"type", "mangaplus-page"}, {"page", 3.5}, {"offset", 0.5}, {"total", 58}},
            QVariantMap{{"type", "mangaplus-page"}, {"page", 58}, {"offset", 0.5}, {"total", 58}},
            QVariantMap{{"type", "mangaplus-page"}, {"page", 3}, {"offset", std::nan("")}, {"total", 58}},
            QVariantMap{{"type", "mangaplus-page"}, {"page", 3}, {"offset", 0.5}, {"total", 0}}}) {
        chapter.insert("locator", bad);
        check(!manga.observe(chapter), "Reject malformed MANGA Plus page position");
    }
    chapter.insert("locator", pagePlace);
    chapter.insert("href", "https://example.com/viewer/1000486");
    check(!manga.observe(chapter), "MANGA Plus locator is scoped to its reader origin");
    chapter.insert("href", "https://mangaplus.shueisha.co.jp/viewer/1000486");
    manga.beginVisit({{"pk", "webtoon"}});
    check(!manga.observe(chapter), "Other providers cannot submit a MANGA Plus locator");
    manga.beginVisit({{"pk", "mangaplus"}});
    chapter.insert("href", "https://mangaplus.shueisha.co.jp/viewer/1000487");
    check(manga.observe(chapter) && manga.continueItems().size() == 2, "Distinct chapters retain separate Continue entries");

    // Existing title-derived IDs must deduplicate without erasing activity history.
    QVariantMap oldChapter{{"pk", "mangaplus"}, {"url", "https://mangaplus.shueisha.co.jp/viewer/1000486"},
        {"kind", "book"}, {"id", "old-title"}, {"title", "Loading"}, {"locator", QVariantMap{{"type", "url"}}}};
    auto latestChapter = oldChapter;
    latestChapter.insert("id", "final-title"); latestChapter.insert("title", "One Piece");
    latestChapter.insert("locator", pagePlace);
    QFile legacyFile(temp.filePath("legacy-manga.json"));
    auto writeLegacy = [&](bool completed, bool dismissed) {
        latestChapter.insert("completed", completed); latestChapter.insert("dismissed", dismissed);
        check(legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "Open legacy reading fixture");
        legacyFile.write(QJsonDocument::fromVariant(QVariantMap{{"version", 1},
            {"sessions", QVariantList{latestChapter, oldChapter}}, {"retainedContinue", QVariantList{oldChapter}}}).toJson());
        legacyFile.close();
    };
    writeLegacy(false, false);
    mangaReopened.setStoragePath(legacyFile.fileName());
    check(mangaReopened.continueItems().size() == 1 && mangaReopened.sessions().size() == 2
          && mangaReopened.continueItems().first().toMap().value("locator").toMap() == pagePlace,
          "Legacy duplicate chapters show the newest place and preserve history");
    check(mangaReopened.dismissContinue("final-title") && mangaReopened.continueItems().isEmpty(),
          "Dismissing a legacy chapter must not reveal another title copy");
    mangaReopened.setStoragePath(legacyFile.fileName());
    check(mangaReopened.continueItems().isEmpty(), "Legacy chapter dismissal survives restart");
    writeLegacy(true, false); mangaReopened.setStoragePath(legacyFile.fileName());
    check(mangaReopened.continueItems().isEmpty(), "Completed latest chapter hides old title copies");
    writeLegacy(false, true); mangaReopened.setStoragePath(legacyFile.fileName());
    check(mangaReopened.continueItems().isEmpty(), "Dismissed latest chapter hides old title copies");
    qInfo() << "FERIA_ACCOUNT_STORE_PASS" << checks;
}
