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
    bool regressionsPass = true;
    auto regression = [&](bool ok, const char *message) {
        ++checks;
        if (!ok) { regressionsPass = false; qCritical() << "REGRESSION:" << message; }
    };
    FeriaAccountStore recovery;
    const auto recoveryPath = temp.filePath("restore/account.json");
    recovery.setStoragePath(recoveryPath);
    const QVariantMap firstOffer{{"providerId", "youtube"}, {"url", "https://example.com/same"}, {"restoreId", "first"}};
    auto secondOffer = firstOffer; secondOffer.insert("restoreId", "second");
    const auto capture = [&](FeriaAccountStore &owner) {
        bool captured = false;
        return QMetaObject::invokeMethod(&owner, "captureBrowserRestoreOffers", Q_RETURN_ARG(bool, captured)) && captured;
    };
    const auto saveOffers = [&](FeriaAccountStore &owner, const QVariantList &rows) {
        bool saved = false;
        return QMetaObject::invokeMethod(&owner, "saveBrowserRestoreOffers", Q_RETURN_ARG(bool, saved), Q_ARG(QVariantList, rows)) && saved;
    };
    regression(recovery.saveBrowserTabs({firstOffer, secondOffer}) && capture(recovery)
        && recovery.property("browserRestoreOffers").toList().size() == 2,
        "Distinct same-URL restore offers must be durable and individually identified");
    regression(saveOffers(recovery, {secondOffer}) && recovery.saveBrowserTabs({firstOffer}) && capture(recovery)
        && recovery.property("browserRestoreOffers").toList().size() == 1,
        "Current-session snapshots cannot replace pending restore offers or recapture within the loaded account");
    FeriaAccountStore restartedRecovery; restartedRecovery.setStoragePath(recoveryPath);
    regression(capture(restartedRecovery) && restartedRecovery.property("browserRestoreOffers").toList().size() == 2,
        "A changed latest snapshot merges with previous pending offers on next process load");
    regression(saveOffers(restartedRecovery, {}) && capture(restartedRecovery), "Dismiss durable restore offers");
    FeriaAccountStore dismissedRecovery; dismissedRecovery.setStoragePath(recoveryPath);
    regression(capture(dismissedRecovery) && dismissedRecovery.property("browserRestoreOffers").toList().isEmpty(),
        "An identical captured snapshot cannot resurrect dismissed offers after restart");
    auto newerOffer = firstOffer; newerOffer.insert("restoreId", "newer"); newerOffer.insert("url", "https://example.com/newer");
    regression(dismissedRecovery.saveBrowserTabs({newerOffer}), "Save a new current session snapshot");
    FeriaAccountStore newerRecovery; newerRecovery.setStoragePath(recoveryPath);
    regression(capture(newerRecovery) && newerRecovery.property("browserRestoreOffers").toList().size() == 1
        && newerRecovery.property("browserRestoreOffers").toList().first().toMap().value("restoreId") == "newer",
        "A newly changed snapshot becomes offered at the next account load");
    regression(saveOffers(newerRecovery, {}) && newerRecovery.saveBrowserTabs({secondOffer}),
        "Save a changed snapshot before leaving account A");
    newerRecovery.setStoragePath(temp.filePath("restore-b/account.json"));
    regression(capture(newerRecovery) && newerRecovery.property("browserRestoreOffers").toList().isEmpty(),
        "Account B must not inherit account A restore offers");
    newerRecovery.setStoragePath(recoveryPath);
    regression(capture(newerRecovery) && newerRecovery.property("browserRestoreOffers").toList().size() == 1
        && newerRecovery.property("browserRestoreOffers").toList().first().toMap().value("restoreId") == "second",
        "Returning A to B to A captures A's newly changed snapshot");
    const auto validationPath = temp.filePath("browser-validation.json");
    QFile validationFile(validationPath);
    auto privateOffer = firstOffer; privateOffer.insert("privateBrowsing", true);
    auto authOffer = firstOffer; authOffer.insert("url", "https://example.com/login");
    auto unknownOffer = firstOffer; unknownOffer.insert("providerId", "unknown-provider");
    auto oversizedOffer = firstOffer; oversizedOffer.insert("title", QString(2049, 'x'));
    auto malformedOffer = firstOffer; malformedOffer.insert("url", QVariantList{"https://example.com/a"});
    auto hiddenStateOffer = firstOffer; hiddenStateOffer.insert("opener", "secret"); hiddenStateOffer.insert("pageGeneration", 55);
    const QVariantList invalidOffers{privateOffer, authOffer, unknownOffer, oversizedOffer, malformedOffer, 17};
    auto allOffers = invalidOffers; allOffers.append(hiddenStateOffer);
    regression(validationFile.open(QIODevice::WriteOnly), "Open persisted validation fixture");
    validationFile.write(QJsonDocument::fromVariant(QVariantMap{{"version", 1}, {"browserTabs", allOffers},
        {"browserRestoreOffers", allOffers}, {"browserPreferences", QVariantMap{{"restoreTabs", "true"},
        {"freezeBackground", 0}, {"searchEngine", "unapproved"}, {"unknown", "ignored"}}}}).toJson());
    validationFile.close();
    FeriaAccountStore validated; validated.setStoragePath(validationPath);
    const auto cleanTabs = validated.browserTabs();
    regression(cleanTabs.size() == 1 && !cleanTabs.first().toMap().contains("opener")
        && !cleanTabs.first().toMap().contains("pageGeneration"),
        "Persisted tabs reject private/auth/unknown/oversized/malformed records and strip live state");
    regression(validated.browserPreferences() == QVariantMap{{"restoreTabs", false}, {"freezeBackground", true}, {"searchEngine", "duckduckgo"}},
        "Loaded preferences must enforce the same typed allowlist as writes");
    regression(validated.property("browserRestoreOffers").toList().size() == 1, "Loaded offers use the same descriptor validation");
    regression(validated.saveBrowserTabs(allOffers) && validated.browserTabs().size() == 1,
        "Written descriptors reject malformed and oversized rows as well as loaded descriptors");
    regression(!validated.recordBrowserDownload({{"url", "https://example.com/file"}})
        && validated.browserDownloadHistory().isEmpty(), "Tokenless native download metadata must be rejected");
    auto legacyOffer = firstOffer; legacyOffer.remove("restoreId");
    regression(validationFile.open(QIODevice::WriteOnly), "Open legacy persisted descriptor fixture");
    validationFile.write(QJsonDocument::fromVariant(QVariantMap{{"version", 1},
        {"browserTabs", QVariantList{legacyOffer, legacyOffer}}}).toJson());
    validationFile.close();
    validated.setStoragePath(validationPath);
    regression(capture(validated)
        && validated.browserTabs().size() == 2
        && !validated.browserTabs().first().toMap().value("restoreId").toString().isEmpty()
        && validated.browserTabs()[0].toMap().value("restoreId") != validated.browserTabs()[1].toMap().value("restoreId"),
        "Legacy duplicate URLs receive separate stable IDs when captured");
    FeriaAccountStore legacyRestart; legacyRestart.setStoragePath(validationPath);
    regression(legacyRestart.browserTabs() == validated.browserTabs(), "Legacy IDs survive restart without regeneration");
    check(regressionsPass, "Browser native validation and restore regressions must pass");
    const auto interruptedPath = temp.filePath("interrupted/account.json");
    FeriaAccountStore seed; seed.setStoragePath(interruptedPath);
    check(seed.saveBrowserTabs({firstOffer, secondOffer}), "Seed previous snapshot before capture obstruction");
    const auto announcementPath = temp.filePath("announcement/account.json");
    FeriaAccountStore publisherSeed; publisherSeed.setStoragePath(announcementPath);
    check(publisherSeed.saveBrowserTabs({firstOffer}), "Seed an account whose profile signal captures restore offers");
    FeriaAccountStore published;
    bool capturedOnProfile = false;
    QObject::connect(&published, &FeriaAccountStore::profileChanged, &published, [&] {
        capturedOnProfile = capture(published);
    });
    published.setStoragePath(announcementPath);
    regression(capturedOnProfile && published.error().isEmpty() && published.browserRestoreOffers().size() == 1,
        "Profile observers must be able to atomically capture the loaded account without its read handle blocking replacement");
    FeriaAccountStore interrupted; interrupted.setStoragePath(interruptedPath);
    check(QFile::rename(interruptedPath, interruptedPath + ".snapshot") && QDir().mkpath(interruptedPath),
        "Obstruct capture after loading the original snapshot");
    check(!capture(interrupted) && interrupted.browserRestoreOffers().isEmpty()
        && interrupted.browserTabs().size() == 2, "Failed capture must retain the loaded snapshot for retry");
    const auto blockedVisit = interrupted.beginSessionVisit("blocked-write", {{"pk", "youtube"}});
    regression(!blockedVisit.isEmpty(), "A transient metadata write failure must not prevent volatile browser admission");
    const auto blockedGrant = interrupted.authorizeBrowserDownload("blocked-write", blockedVisit);
    regression(!blockedGrant.isEmpty(), "Transient write failure must retain provenance for a new download's later retry");
    check(!interrupted.recording(), "Metadata recording remains disabled while its write failure is unresolved");
    const auto blockedPrivate = interrupted.beginSessionVisit("blocked-private", {{"pk", "youtube"}, {"privateBrowsing", true}});
    check(!blockedPrivate.isEmpty() && interrupted.authorizeBrowserDownload("blocked-private", blockedPrivate).isEmpty(),
        "Transient write failure does not authorize private download history");
    check(QDir().rmdir(interruptedPath) && QFile::rename(interruptedPath + ".snapshot", interruptedPath),
        "Remove obstruction and restore the original file");
    check(capture(interrupted) && interrupted.browserRestoreOffers().size() == 2 && interrupted.error().isEmpty(),
        "Capture retries successfully without replacing the previous snapshot");
    FeriaAccountStore capturedRestart; capturedRestart.setStoragePath(interruptedPath);
    check(capturedRestart.browserRestoreOffers() == interrupted.browserRestoreOffers(),
        "Retried offer capture is durable after disk reload");
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
    check(!reopened.saveBrowserTabs({firstOffer}) && broken.open(QIODevice::ReadOnly)
        && broken.readAll() == QByteArray("broken"), "Malformed loaded data remains sealed against retry writes");
    broken.close();
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
    FeriaAccountStore browser;
    const auto browserPath = temp.filePath("browser-account.json");
    browser.setStoragePath(browserPath);
    check(browser.metaObject()->indexOfProperty("browserBookmarks") >= 0,
          "Browser persistence API must exist");
    auto bookmark = [&](const QString &url, const QString &title, bool saved) {
        bool result = false;
        return QMetaObject::invokeMethod(&browser, "setBrowserBookmark", Q_RETURN_ARG(bool, result),
            Q_ARG(QString, url), Q_ARG(QString, title), Q_ARG(bool, saved)) && result;
    };
    auto visit = [&](const QString &id, const QString &token, const QString &url) {
        bool result = false;
        return QMetaObject::invokeMethod(&browser, "recordBrowserVisit", Q_RETURN_ARG(bool, result),
            Q_ARG(QString, id), Q_ARG(QString, token), Q_ARG(QString, url), Q_ARG(QString, QStringLiteral("Page"))) && result;
    };
    auto preference = [&](const QString &key, const QVariant &value) {
        bool result = false;
        return QMetaObject::invokeMethod(&browser, "setBrowserPreference", Q_RETURN_ARG(bool, result),
            Q_ARG(QString, key), Q_ARG(QVariant, value)) && result;
    };
    auto tabs = [&](const QVariantList &descriptors) {
        bool result = false;
        return QMetaObject::invokeMethod(&browser, "saveBrowserTabs", Q_RETURN_ARG(bool, result),
            Q_ARG(QVariantList, descriptors)) && result;
    };
    auto download = [&](const QVariantMap &descriptor) {
        bool result = false;
        return QMetaObject::invokeMethod(&browser, "recordBrowserDownload", Q_RETURN_ARG(bool, result),
            Q_ARG(QVariantMap, descriptor)) && result;
    };
    const QString webUrl = "https://example.com/search?q=Beacon#results";
    check(bookmark("https://user:password@example.com/search?q=Beacon#results", "Beacon", true),
          "Bookmark preserves ordinary query and fragment and strips credentials");
    check(browser.property("browserBookmarks").toList().first().toMap().value("url") == webUrl,
          "Bookmark URL is sanitized without collapsing web search identity");
    check(bookmark(webUrl, "Renamed", true) && browser.property("browserBookmarks").toList().size() == 1,
          "Repeated bookmark updates instead of duplicating");
    check(!bookmark("javascript:alert(1)", "Bad", true), "Bookmark rejects executable scheme");
    const auto normalToken = browser.beginSessionVisit("normal", {{"pk", "youtube"}});
    const auto privateToken = browser.beginSessionVisit("private", {{"pk", "youtube"}, {"privateBrowsing", true}});
    check(!normalToken.isEmpty() && !privateToken.isEmpty(), "Private service views receive loader tokens");
    check(visit("normal", normalToken, webUrl), "Record ordinary successful browser visit");
    check(!visit("private", privateToken, webUrl) && !browser.observeSession("private", privateToken, sample)
          && !browser.saveSessionReadingPlace("private", privateToken, webUrl, "Private"),
          "Private token rejects browser, media, and explicit reading capture");
    check(!visit("normal", privateToken, webUrl) && !visit("normal", normalToken, "https://example.com/oauth/callback?code=secret")
          && !visit("normal", normalToken, "https://example.com/?access_token=secret")
          && !visit("normal", normalToken, "https://example.com/#access_token=secret"),
          "History rejects wrong tokens and authentication destinations");
    check(browser.property("browserHistory").toList().size() == 1 && browser.sessions().isEmpty(),
          "Browser history stays separate from media activity and Continue");
    check(preference("restoreTabs", true) && preference("freezeBackground", false)
          && preference("searchEngine", QStringLiteral("bing")), "Persist validated browser preferences");
    check(!preference("restoreTabs", QStringLiteral("false")) && !preference("searchEngine", QStringLiteral("unknown"))
          && !preference("unknown", true), "Reject malformed and unknown preferences");
    QVariantMap regularTab{{"providerId", "youtube"}, {"url", webUrl}, {"title", "Beacon"}};
    auto privateTab = regularTab; privateTab.insert("privateBrowsing", true);
    auto authTab = regularTab; authTab.insert("url", "https://example.com/login");
    auto localTab = regularTab; localTab.insert("url", "file:///secret.txt");
    check(tabs({regularTab, privateTab, authTab, localTab}) && browser.property("browserTabs").toList().size() == 1,
          "Restore snapshots exclude private, authentication, and local pages");
    check(download({{"id", "download-1"}, {"url", webUrl}, {"fileName", "beacon.txt"}, {"state", "completed"},
                    {"sessionId", "normal"}, {"visitToken", normalToken}})
          && !download({{"id", "private-download"}, {"url", webUrl}, {"privateBrowsing", true}}),
          "Download history excludes private transfers");
    FeriaAccountStore browserReopened; browserReopened.setStoragePath(browserPath);
    for (const char *property : {"browserBookmarks", "browserHistory", "browserTabs", "browserPreferences", "browserDownloadHistory"})
        check(browserReopened.property(property) == browser.property(property), "Browser state survives restart");
    check(bookmark(webUrl, "", false) && browser.property("browserBookmarks").toList().isEmpty(),
          "Remove bookmark independently from history");
    const auto replacementToken = browser.beginSessionVisit("normal", {{"pk", "youtube"}});
    check(!visit("normal", normalToken, webUrl) && visit("normal", replacementToken, webUrl),
          "Replaced browser token cannot append history");
    browser.setStoragePath(temp.filePath("browser-other.json"));
    check(!visit("normal", replacementToken, webUrl) && browser.property("browserHistory").toList().isEmpty()
          && browser.property("browserBookmarks").toList().isEmpty()
          && browser.property("browserPreferences").toMap().value("restoreTabs") == false,
          "Profile switch fences old callbacks and resets browser preferences");
    browser.setStoragePath({});
    check(!bookmark(webUrl, "Sealed", true) && !preference("restoreTabs", true), "Sealed profile cannot persist browser state");
    browser.setStoragePath(broken.fileName());
    check(!bookmark(webUrl, "Corrupt", true), "Corrupt browser state is preserved without writes");
    QFile capsFile(temp.filePath("browser-caps.json"));
    QVariantList manyBookmarks, manyHistory, manyDownloads, manyTabs;
    for (int i = 0; i < 1100; ++i) {
        const QVariantMap row{{"url", QStringLiteral("https://example.com/%1").arg(i)}, {"id", QString::number(i)}};
        manyBookmarks.append(row); manyHistory.append(row); manyDownloads.append(row);
        manyTabs.append(regularTab);
    }
    check(capsFile.open(QIODevice::WriteOnly), "Create bounded persistence fixture");
    capsFile.write(QJsonDocument::fromVariant(QVariantMap{{"version", 1}, {"browserBookmarks", manyBookmarks},
        {"browserHistory", manyHistory}, {"browserDownloadHistory", manyDownloads}}).toJson());
    capsFile.close(); browser.setStoragePath(capsFile.fileName());
    const auto cappedToken = browser.beginSessionVisit("cap", {{"pk", "youtube"}});
    check(bookmark(webUrl, "Capped", true) && browser.property("browserBookmarks").toList().size() == 500,
          "Bookmarks are capped at 500");
    check(visit("cap", cappedToken, webUrl) && browser.property("browserHistory").toList().size() == 1000,
          "Browser history is capped at 1000");
    check(tabs(manyTabs) && browser.property("browserTabs").toList().size() == 50, "Restored tabs are capped at 50");
    check(download({{"id", "latest"}, {"url", webUrl}, {"sessionId", "cap"}, {"visitToken", cappedToken}})
          && browser.property("browserDownloadHistory").toList().size() == 100, "Downloads are capped at 100");
    check(download({{"id", "latest"}, {"url", webUrl}, {"state", "completed"}, {"sessionId", "cap"}, {"visitToken", cappedToken}})
          && browser.property("browserDownloadHistory").toList().size() == 100,
          "Download progress updates one record rather than creating duplicates");
    check(!download({{"url", webUrl}, {"sessionId", "cap"}, {"visitToken", "stale"}}),
          "Download callbacks with stale tokens cannot write");
    const auto bookmarksBeforeClear = browser.property("browserBookmarks");
    bool cleared = false;
    check(QMetaObject::invokeMethod(&browser, "clearBrowserHistory", Q_RETURN_ARG(bool, cleared)) && cleared
          && browser.property("browserHistory").toList().isEmpty()
          && browser.property("browserBookmarks") == bookmarksBeforeClear,
          "Clearing browser history preserves bookmarks");
    cleared = false;
    check(QMetaObject::invokeMethod(&browser, "clearBrowserDownloads", Q_RETURN_ARG(bool, cleared)) && cleared
          && browser.property("browserDownloadHistory").toList().isEmpty(), "Clear download records independently");
    const auto beforeFailedBookmark = browser.property("browserBookmarks");
    const auto bookmarkBackup = capsFile.fileName() + ".preserved";
    check(QFile::rename(capsFile.fileName(), bookmarkBackup) && QDir().mkdir(capsFile.fileName()),
          "Block browser atomic write with directory");
    check(!bookmark("https://example.com/new", "Failed", true)
          && browser.property("browserBookmarks") == beforeFailedBookmark,
          "Failed browser write preserves in-memory bookmarks");
    FeriaAccountStore preservedBookmarks; preservedBookmarks.setStoragePath(bookmarkBackup);
    check(preservedBookmarks.property("browserBookmarks") == beforeFailedBookmark,
          "Failed browser write preserves persisted bookmarks");
    FeriaAccountStore transfers;
    transfers.setStoragePath(temp.filePath("transfer-account.json"));
    check(transfers.metaObject()->indexOfMethod("authorizeBrowserDownload(QString,QString)") >= 0,
          "Transient download authorization API must exist");
    auto authorize = [&](const QString &id, const QString &visitToken) {
        QString token;
        QMetaObject::invokeMethod(&transfers, "authorizeBrowserDownload", Q_RETURN_ARG(QString, token),
            Q_ARG(QString, id), Q_ARG(QString, visitToken));
        return token;
    };
    auto complete = [&](const QVariantMap &descriptor) {
        bool result = false;
        return QMetaObject::invokeMethod(&transfers, "recordBrowserDownload", Q_RETURN_ARG(bool, result),
            Q_ARG(QVariantMap, descriptor)) && result;
    };
    const auto sourceVisit = transfers.beginSessionVisit("source", {{"pk", "youtube"}});
    const auto transferToken = authorize("source", sourceVisit);
    check(!transferToken.isEmpty(), "Normal accepted download receives authorization");
    transfers.endSessionVisit("source");
    const QVariantMap closedCompletion{{"downloadToken", transferToken}, {"sessionId", "source"},
        {"visitToken", sourceVisit}, {"url", webUrl}, {"state", "completed"}};
    check(complete(closedCompletion) && transfers.browserDownloadHistory().size() == 1,
          "Accepted download completion remains authorized after its source tab closes");
    check(!complete(closedCompletion), "Successful download authorization is consumed once");
    const auto completedRow = transfers.browserDownloadHistory().first().toMap();
    check(completedRow.value("providerId") == "youtube" && !completedRow.contains("downloadToken")
          && !completedRow.contains("visitToken"), "Download authorization stays transient with captured provider identity");
    const auto beforeNavigation = transfers.beginSessionVisit("source", {{"pk", "youtube"}});
    const auto navigationToken = authorize("source", beforeNavigation);
    transfers.beginSessionVisit("source", {{"pk", "youtube"}});
    check(authorize("source", beforeNavigation).isEmpty(), "Stale source Visit cannot mint authorization");
    check(complete({{"downloadToken", navigationToken}, {"url", webUrl}, {"state", "completed"}}),
          "Accepted download completion remains authorized after source navigation replaces its Visit");
    const auto privateVisit = transfers.beginSessionVisit("private", {{"pk", "youtube"}, {"privateBrowsing", true}});
    check(authorize("private", privateVisit).isEmpty() && authorize("missing", sourceVisit).isEmpty(),
          "Private and unknown visits cannot authorize persistence");
    check(!complete({{"downloadToken", "stale"}, {"url", webUrl}}), "Unknown download authorization is rejected");
    const auto currentVisit = transfers.beginSessionVisit("source", {{"pk", "youtube"}});
    check(!complete({{"downloadToken", QString()}, {"sessionId", "source"}, {"visitToken", currentVisit}, {"url", webUrl}}),
          "An empty authorization cannot fall back to current Visit validation");
    const auto privateMarked = authorize("source", currentVisit);
    check(!complete({{"downloadToken", privateMarked}, {"url", webUrl}, {"privateBrowsing", true}}),
          "Private-marked completion cannot use a normal download authorization");
    check(!complete({{"downloadToken", privateMarked}, {"url", webUrl}, {"receivedBytes", -1}})
          && !complete({{"downloadToken", privateMarked}, {"url", webUrl}, {"providerId", "netflix"}})
          && complete({{"downloadToken", privateMarked}, {"url", webUrl}}),
          "Failed completion validation does not consume authorization");
    const auto reentrantToken = authorize("source", currentVisit);
    const QVariantMap reentrantCompletion{{"downloadToken", reentrantToken}, {"url", webUrl}};
    bool reentrantRejected = false;
    const auto changedConnection = QObject::connect(&transfers, &FeriaAccountStore::changed, &transfers, [&] {
        reentrantRejected = !complete(reentrantCompletion);
    });
    check(complete(reentrantCompletion) && reentrantRejected, "Synchronous changed callbacks cannot replay a committing authorization");
    QObject::disconnect(changedConnection);
    const auto cancelled = authorize("source", currentVisit);
    bool cancelledResult = false;
    check(QMetaObject::invokeMethod(&transfers, "cancelBrowserDownload", Q_RETURN_ARG(bool, cancelledResult),
            Q_ARG(QString, cancelled)) && cancelledResult
          && !complete({{"downloadToken", cancelled}, {"url", webUrl}}),
          "Cancelled download authorization cannot be reused");
    const auto oldProfileToken = authorize("source", currentVisit);
    transfers.setStoragePath(temp.filePath("transfer-other.json"));
    check(!complete({{"downloadToken", oldProfileToken}, {"url", webUrl}})
          && transfers.browserDownloadHistory().isEmpty(), "Profile switch rejects old download completions");
    const auto switchedVisit = transfers.beginSessionVisit("source", {{"pk", "youtube"}});
    const auto switchedToken = authorize("source", switchedVisit);
    transfers.setStoragePath({});
    check(!complete({{"downloadToken", switchedToken}, {"url", webUrl}})
          && authorize("source", switchedVisit).isEmpty(), "Signout clears grants and prevents new authorizations");
    const auto failedTransferPath = temp.filePath("failed-transfer/account.json");
    transfers.setStoragePath(failedTransferPath);
    const auto failedVisit = transfers.beginSessionVisit("source", {{"pk", "youtube"}});
    const auto failedTransferToken = authorize("source", failedVisit);
    check(QDir().mkpath(failedTransferPath), "Block download metadata save with directory");
    check(!complete({{"downloadToken", failedTransferToken}, {"url", webUrl}})
          && transfers.browserDownloadHistory().isEmpty(), "Failed metadata commit preserves download history");
    check(!transfers.error().isEmpty() && QDir().rmdir(failedTransferPath), "Unblock transient metadata save failure");
    regression(complete({{"downloadToken", failedTransferToken}, {"url", webUrl}})
        && transfers.browserDownloadHistory().size() == 1 && transfers.error().isEmpty(),
        "Unblocked metadata save must retry successfully with the same unconsumed grant");
    regression(!complete({{"downloadToken", failedTransferToken}, {"url", webUrl}}),
        "Successful metadata retry consumes its grant once");
    auto encodedOversizedOffer = firstOffer;
    encodedOversizedOffer.insert("url", QString("https://example.com/") + QString(3000, QChar(0x4e00)));
    regression(validated.saveBrowserTabs({encodedOversizedOffer}) && validated.browserTabs().isEmpty(),
        "Descriptor size bound applies to the canonical encoded URL written to disk");
    // ---- Browser set 2: history and bookmark management.
    {
        FeriaAccountStore manage;
        const auto managePath = temp.filePath("manage/account.json");
        manage.setStoragePath(managePath);
        const int generation = manage.browserGeneration();
        const auto urls = [](const QVariantList &rows) {
            QStringList result;
            for (const auto &row : rows) result.append(row.toMap().value("url").toString());
            return result;
        };
        const auto titles = [](const QVariantList &rows) {
            QStringList result;
            for (const auto &row : rows) result.append(row.toMap().value("title").toString());
            return result;
        };
        const QString a = "https://example.com/a", b = "https://example.com/b", c = "https://reader.example.org/caf%C3%A9";
        const auto tab = manage.beginSessionVisit("tab", {{"pk", "youtube"}});
        const auto other = manage.beginSessionVisit("other", {{"pk", "youtube"}});
        const auto hidden = manage.beginSessionVisit("hidden", {{"pk", "youtube"}, {"privateBrowsing", true}});

        // Recording: one row per address, newest visit first.
        check(manage.recordBrowserVisit("tab", tab, a, "Alpha", 1) && manage.recordBrowserVisit("tab", tab, b, "Beta", 2)
              && urls(manage.browserHistory()) == QStringList({b, a}), "Visits are listed newest first");
        const auto idOfA = manage.browserHistory().at(1).toMap().value("id").toString();
        const auto timeOfA = manage.browserHistory().at(1).toMap().value("at").toLongLong();
        QThread::msleep(5);
        check(manage.recordBrowserVisit("tab", tab, b, "Beta", 3) && urls(manage.browserHistory()) == QStringList({b, a}),
              "A reload refreshes its row instead of adding one");
        check(manage.recordBrowserVisit("tab", tab, a, "Alpha", 4) && urls(manage.browserHistory()) == QStringList({a, b})
              && manage.browserHistory().first().toMap().value("id").toString() == idOfA
              && manage.browserHistory().first().toMap().value("at").toLongLong() > timeOfA,
              "Back to an earlier page moves its one row to the top with a new time");
        check(manage.recordBrowserVisit("other", other, b, "Beta", 1) && urls(manage.browserHistory()) == QStringList({b, a}),
              "A second tab visiting the same address does not duplicate it");
        check(!manage.recordBrowserVisit("tab", tab, c, "Stale", 3) && manage.browserHistory().size() == 2,
              "An observation older than the tab's recorded document is refused");
        check(!manage.recordBrowserVisit("hidden", hidden, c, "Private", 1)
              && !manage.updateBrowserVisitTitle("hidden", hidden, 1, "Private") && manage.browserHistory().size() == 2,
              "Private tabs record no visit and no title");

        // Late titles amend only the visit their document recorded.
        check(manage.recordBrowserVisit("tab", tab, c, "", 5) && titles(manage.browserHistory()).first().isEmpty()
              && manage.updateBrowserVisitTitle("tab", tab, 5, " Cafe reading ")
              && titles(manage.browserHistory()) == QStringList({"Cafe reading", "Beta", "Alpha"})
              && urls(manage.browserHistory()) == QStringList({c, b, a}),
              "A title arriving after the load names its own visit and adds no row");
        check(!manage.updateBrowserVisitTitle("tab", tab, 4, "Old document")
              && !manage.updateBrowserVisitTitle("tab", tab, 6, "Next document")
              && !manage.updateBrowserVisitTitle("tab", other, 5, "Wrong token")
              && !manage.updateBrowserVisitTitle("tab", tab, 5, "  ")
              && titles(manage.browserHistory()).first() == "Cafe reading",
              "Titles from another document, another visit or with no text are refused");
        check(!manage.updateBrowserVisitTitle("tab", tab, 4, "Alpha late")
              && manage.recordBrowserVisit("other", other, c, "Cafe twin", 2)
              && !manage.updateBrowserVisitTitle("tab", tab, 5, "Lost race")
              && titles(manage.browserHistory()).first() == "Cafe twin",
              "A title cannot rename a row a later visit now owns");

        // Search by title or address.
        check(urls(manage.searchBrowserHistory("")) == QStringList({c, b, a})
              && urls(manage.searchBrowserHistory("   ")) == QStringList({c, b, a}), "An empty search lists everything");
        check(urls(manage.searchBrowserHistory("BETA")) == QStringList({b})
              && urls(manage.searchBrowserHistory("reader.example")) == QStringList({c})
              && urls(manage.searchBrowserHistory(QString::fromUtf8("caf\xc3\xa9"))) == QStringList({c})
              && urls(manage.searchBrowserHistory("caf%C3%A9")) == QStringList({c}),
              "History search matches title or address, in either case and as read or as stored");
        check(urls(manage.searchBrowserHistory("example alpha")) == QStringList({a})
              && manage.searchBrowserHistory("alpha beta").isEmpty() && manage.searchBrowserHistory("zebra").isEmpty(),
              "Every word must match; no match is an empty list");

        // Bookmarks: save, search, edit, duplicates, delete.
        check(manage.setBrowserBookmark(a, "Alpha", true, generation) && manage.setBrowserBookmark(b, "Beta", true, generation)
              && manage.setBrowserBookmark(c, "Cafe", true, generation)
              && urls(manage.browserBookmarks()) == QStringList({c, b, a}), "Save three bookmarks");
        check(urls(manage.searchBrowserBookmarks("be")) == QStringList({b})
              && urls(manage.searchBrowserBookmarks("example.com")) == QStringList({b, a})
              && manage.searchBrowserBookmarks("zebra").isEmpty(), "Bookmark search matches title or address");
        check(manage.isBrowserBookmarked(a) && manage.isBrowserBookmarked("https://user:pw@example.com/a")
              && !manage.isBrowserBookmarked("https://example.com/none") && !manage.isBrowserBookmarked("javascript:1"),
              "Bookmarked state is read against the stored address form");
        const auto savedAt = manage.browserBookmarks().at(1).toMap().value("at");
        auto edit = manage.editBrowserBookmark(generation, b, b, "  Beta renamed  ");
        check(edit.value("ok").toBool() && titles(manage.browserBookmarks()) == QStringList({"Cafe", "Beta renamed", "Alpha"})
              && manage.browserBookmarks().at(1).toMap().value("at") == savedAt,
              "Renaming a bookmark keeps its place and saved time");
        edit = manage.editBrowserBookmark(generation, b, "example.com/moved?q=1#top", "Beta renamed");
        check(edit.value("ok").toBool() && edit.value("url").toString() == "https://example.com/moved?q=1#top"
              && urls(manage.browserBookmarks()) == QStringList({c, "https://example.com/moved?q=1#top", a}),
              "Editing an address accepts one typed without a scheme and keeps the place");
        const QString moved = "https://example.com/moved?q=1#top";
        const auto beforeRefusals = manage.browserBookmarks();
        check(manage.editBrowserBookmark(generation, moved, a, "Clash").value("reason").toString() == "duplicate"
              && manage.editBrowserBookmark(generation, moved, "https://user:pw@example.com/a", "Clash").value("reason").toString() == "duplicate"
              && manage.editBrowserBookmark(generation, moved, "javascript:alert(1)", "Bad").value("reason").toString() == "invalid-url"
              && manage.editBrowserBookmark(generation, moved, "file:///secret.txt", "Bad").value("reason").toString() == "invalid-url"
              && manage.editBrowserBookmark(generation, moved, "not a web address", "Bad").value("reason").toString() == "invalid-url"
              && manage.editBrowserBookmark(generation, moved, "   ", "Bad").value("reason").toString() == "invalid-url"
              && manage.editBrowserBookmark(generation, moved, "mailto:someone@example.com", "Bad").value("reason").toString() == "invalid-url"
              && manage.editBrowserBookmark(generation, b, a, "Gone").value("reason").toString() == "not-found"
              && manage.browserBookmarks() == beforeRefusals,
              "A duplicate, invalid or missing bookmark edit is refused and changes nothing");
        check(manage.editBrowserBookmark(generation, moved, moved, "Beta renamed").value("ok").toBool()
              && manage.browserBookmarks() == beforeRefusals, "An unchanged edit succeeds without rewriting");
        check(manage.setBrowserBookmark(a, "Alpha again", true, generation)
              && urls(manage.browserBookmarks()) == QStringList({a, c, moved}),
              "Saving an address that is already bookmarked renames and lifts the one bookmark");
        check(manage.removeBrowserBookmark(generation, c) && urls(manage.browserBookmarks()) == QStringList({a, moved})
              && !manage.removeBrowserBookmark(generation, c) && !manage.removeBrowserBookmark(generation, "javascript:1"),
              "A bookmark is removed once; a second removal reports nothing removed");
        check(manage.removeBrowserHistoryEntry(generation, b) && urls(manage.browserHistory()) == QStringList({c, a})
              && !manage.removeBrowserHistoryEntry(generation, b) && !manage.removeBrowserHistoryEntry(generation, QString())
              && urls(manage.browserBookmarks()) == QStringList({a, moved}),
              "One history entry is removed without touching the rest or the bookmarks");
        check(manage.removeBrowserHistoryEntry(generation, c) && !manage.updateBrowserVisitTitle("other", other, 2, "Back again")
              && urls(manage.browserHistory()) == QStringList({a}),
              "A late title cannot bring back a removed history entry");

        // Restart: a new store over the same file.
        {
            FeriaAccountStore restarted; restarted.setStoragePath(managePath);
            check(restarted.browserBookmarks() == manage.browserBookmarks()
                  && restarted.browserHistory() == manage.browserHistory()
                  && urls(restarted.searchBrowserBookmarks("moved")) == QStringList({moved}),
                  "Edits and removals survive a restart");
            check(!restarted.recordBrowserVisit("tab", tab, b, "Beta", 9) && !restarted.updateBrowserVisitTitle("tab", tab, 5, "X"),
                  "Visit tokens do not survive a restart");
        }

        // Account isolation: everything captured under the first account is
        // refused once another is active, and the first is untouched on return.
        const auto firstBookmarks = manage.browserBookmarks();
        const auto firstHistory = manage.browserHistory();
        const auto secondPath = temp.filePath("manage-second/account.json");
        manage.setStoragePath(secondPath);
        const int second = manage.browserGeneration();
        check(second != generation && manage.browserBookmarks().isEmpty() && manage.browserHistory().isEmpty()
              && manage.searchBrowserBookmarks("alpha").isEmpty() && !manage.isBrowserBookmarked(a),
              "A new account starts with its own empty lists");
        const auto secondTab = manage.beginSessionVisit("tab", {{"pk", "youtube"}});
        check(manage.setBrowserBookmark(a, "Second account", true, second) && manage.recordBrowserVisit("tab", secondTab, a, "Second", 1),
              "The new account records its own rows");
        const auto secondBookmarks = manage.browserBookmarks();
        const auto secondHistory = manage.browserHistory();
        check(manage.editBrowserBookmark(generation, a, b, "Stale edit").value("reason").toString() == "stale-account"
              && !manage.removeBrowserBookmark(generation, a) && !manage.setBrowserBookmark(a, "Stale save", true, generation)
              && !manage.setBrowserBookmark(a, "", false, generation)
              && !manage.removeBrowserHistoryEntry(generation, a) && !manage.clearBrowserHistory(generation)
              && !manage.recordBrowserVisit("tab", tab, b, "Stale visit", 9)
              && !manage.updateBrowserVisitTitle("tab", tab, 5, "Stale title")
              && manage.browserBookmarks() == secondBookmarks && manage.browserHistory() == secondHistory,
              "Actions that began in the old account cannot change the new one");
        manage.setStoragePath(managePath);
        check(manage.browserGeneration() != generation && manage.browserGeneration() != second
              && manage.browserBookmarks() == firstBookmarks && manage.browserHistory() == firstHistory,
              "Returning to the first account finds it as it was left");
        check(!manage.removeBrowserBookmark(second, a) && !manage.clearBrowserHistory(second)
              && manage.clearBrowserHistory(manage.browserGeneration()) && manage.browserHistory().isEmpty()
              && manage.browserBookmarks() == firstBookmarks,
              "Clearing history is guarded the same way and leaves bookmarks alone");

        // Files written before visits were merged by address.
        QFile legacy(temp.filePath("manage-legacy.json"));
        check(legacy.open(QIODevice::WriteOnly), "Create repeated-address history fixture");
        legacy.write(QJsonDocument::fromVariant(QVariantMap{{"version", 1}, {"browserHistory", QVariantList{
            QVariantMap{{"id", "3"}, {"url", a}, {"title", "Newest A"}}, QVariantMap{{"id", "2"}, {"url", b}, {"title", "B"}},
            QVariantMap{{"id", "1"}, {"url", a}, {"title", "Oldest A"}}, QVariantMap{{"id", "0"}, {"title", "No address"}}}}}).toJson());
        legacy.close();
        manage.setStoragePath(legacy.fileName());
        check(titles(manage.browserHistory()) == QStringList({"Newest A", "B"}),
              "A stored history with repeated addresses loads as one row each, newest kept");
        check(manage.removeBrowserHistoryEntry(manage.browserGeneration(), a) && titles(manage.browserHistory()) == QStringList({"B"}),
              "Removing a page from such a history leaves no older copy behind");

        // Failed writes change nothing.
        const auto blockedPath = temp.filePath("manage-blocked/account.json");
        manage.setStoragePath(blockedPath);
        const int blocked = manage.browserGeneration();
        check(manage.setBrowserBookmark(a, "Alpha", true, blocked), "Save before blocking writes");
        const auto beforeBlock = manage.browserBookmarks();
        check(QFile::rename(blockedPath, blockedPath + ".kept") && QDir().mkpath(blockedPath), "Block management writes with a directory");
        check(manage.editBrowserBookmark(blocked, a, b, "Moved").value("reason").toString() == "save-failed"
              && !manage.removeBrowserBookmark(blocked, a) && manage.browserBookmarks() == beforeBlock,
              "A failed save reports the failure and keeps the bookmarks in memory");
    }
    check(regressionsPass, "Native retry and encoded descriptor regressions must pass");
    qInfo() << "FERIA_ACCOUNT_STORE_PASS" << checks;
}
