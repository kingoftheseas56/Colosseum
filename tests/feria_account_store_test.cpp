#include "FeriaAccountStore.h"
#include "FeriaBrowserPolicy.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QFile>
#include <QDebug>
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
    qInfo() << "FERIA_ACCOUNT_STORE_PASS" << checks;
}
