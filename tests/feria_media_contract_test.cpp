#include "FeriaAccountStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>
#include <QElapsedTimer>
#include <QProcess>
#include <cstdlib>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--verify-reopen") {
        FeriaAccountStore saved;
        saved.setStoragePath(QString::fromLocal8Bit(argv[2]));
        bool qualified = false;
        for (const auto &value : saved.continueItems())
            qualified |= value.toMap().value("providerMediaId") == "abcdefghijk";
        return saved.error().isEmpty() && saved.continueItems().size() == 3 && qualified ? 0 : 1;
    }
    QTemporaryDir temp;
    int checks = 0;
    auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { qCritical() << "FAIL" << name; std::exit(1); }
    };
    FeriaAccountStore store;
    const auto path = temp.filePath("account.json");
    store.setStoragePath(path); store.beginVisit({{"pk", "ytmusic"}});
    QVariantMap sample{{"href", "https://music.youtube.com/watch?v=abcdefghijk&list=queue"},
        {"title", "Loading"}, {"providerMediaId", "abcdefghijk"}, {"identityProvider", "ytmusic"},
        {"position", 42.0}, {"duration", 240.0}, {"paused", false}, {"kind", "audio"}};
    check(store.observe(sample), "qualified observation accepted");
    const auto first = store.continueItems().first().toMap();
    sample["title"] = "Actual title"; sample["href"] = "https://music.youtube.com/watch?v=abcdefghijk&list=other";
    check(store.observe(sample), "title/queue churn accepted");
    check(store.continueItems().size() == 1 && store.continueItems().first().toMap()["id"] == first["id"],
          "title and queue do not change stable identity");
    check(store.continueItems().first().toMap()["url"] == "https://music.youtube.com/watch?v=abcdefghijk",
          "reopen URL is canonical");
    sample["providerMediaId"] = "0123456789_"; sample["href"] = "https://music.youtube.com/watch?v=0123456789_";
    check(store.observe(sample) && store.continueItems().size() == 2, "different IDs with same title remain distinct");
    sample["identityProvider"] = "youtube";
    check(!store.observe(sample), "payload cannot spoof provider");
    sample["identityProvider"] = "ytmusic"; store.beginVisit({{"pk", "youtube"}});
    check(!store.observe(sample), "native context qualifies ID");
    store.beginVisit({{"pk", "ytmusic"}}); sample.remove("providerMediaId"); sample.remove("identityProvider");
    sample["href"] = "https://music.youtube.com/legacy";
    check(store.observe(sample), "missing ID preserves legacy media path");
    FeriaAccountStore reopened; reopened.setStoragePath(path);
    check(reopened.continueItems().size() == 3, "qualified and legacy state survive process reopen");
    check(reopened.continueItems().last().toMap()["providerMediaId"] == "abcdefghijk",
          "qualified ID persisted, not discarded sidecar");
    QProcess child;
    child.setWorkingDirectory(temp.path());
    child.start(QCoreApplication::applicationFilePath(), {"--verify-reopen", path});
    check(child.waitForFinished(30000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
          "fresh process reloads persisted qualified identities");
    reopened.setStoragePath(temp.filePath("profile-b.json"));
    check(reopened.continueItems().isEmpty(), "profile remains isolated");
    store.beginVisit({{"pk", "ytmusic"}, {"resumePending", true}});
    const auto beforeProtected = store.sessions();
    check(!store.observe(sample) && store.sessions() == beforeProtected,
          "native boundary protects explicit intent before any readiness callback");
    store.setMediaCaptureBlocked(false);
    check(store.observe(sample), "trusted settlement can release native gate");
    auto held = sample; held["captureState"] = "hold";
    check(!store.observe(held), "hold envelope cannot persist at native boundary");
    const auto staleToken = store.visitToken();
    store.beginVisit({{"pk", "ytmusic"}, {"resumePending", true}});
    store.setMediaCaptureBlocked(false, staleToken);
    check(!store.observe(sample), "stale visit callback cannot release current gate");
    store.setMediaCaptureBlocked(false, store.visitToken());
    auto staleSample = sample; staleSample["visitToken"] = staleToken;
    check(!store.observe(staleSample), "stale visit cannot persist after new visit/profile scope");
    const auto liveVisit = store.visitToken();
    auto liveSample = sample; liveSample["visitToken"] = liveVisit;
    check(store.clearActivity() && store.visitToken() == liveVisit && store.observe(liveSample),
          "clearActivity rotates activity session but preserves current browser visit authority");
    QVariantMap legacy{{"id", "legacy-title"}, {"pk", "ytmusic"}, {"kind", "audio"},
        {"url", "https://music.youtube.com/watch?v=abcdefghijk"}, {"title", "Old title"},
        {"at", 100}, {"position", 60}, {"duration", 240}, {"mins", 2.0}};
    auto qualified = legacy;
    qualified["id"] = "stable-title"; qualified["providerMediaId"] = "abcdefghijk";
    qualified["at"] = 200; qualified["title"] = "New title"; qualified["position"] = 70;
    auto writeFixture = [&](const QVariantList &rows, const QVariantList &retained) {
        QFile file(path); check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "open fixture");
        file.write(QJsonDocument::fromVariant(QVariantMap{{"version", 1}, {"sessions", rows},
            {"retainedContinue", retained}}).toJson()); file.close(); store.setStoragePath(path);
    };
    writeFixture({qualified, legacy}, {legacy});
    check(store.continueItems().size() == 1 && store.sessions().size() == 2,
          "exact legacy alias collapses only in Continue, preserving activity");
    check(store.dismissContinue("legacy-title") && store.continueItems().isEmpty(),
          "dismiss through any exact alias suppresses all aliases including retained");
    store.setStoragePath(path); check(store.continueItems().isEmpty(), "dismissal survives reopen");
    qualified["completed"] = true;
    writeFixture({legacy}, {qualified});
    check(store.continueItems().isEmpty(), "newer retained completion suppresses older history");
    qualified["completed"] = false; qualified["dismissed"] = true;
    writeFixture({legacy}, {qualified});
    check(store.continueItems().isEmpty(), "newer retained dismissal suppresses older history");
    qualified["dismissed"] = false;
    writeFixture({qualified, legacy}, {legacy});
    check(store.clearActivity() && store.sessions().isEmpty() && store.continueItems().size() == 1,
          "clearActivity retains one logical current state");
    check(!store.continueItems().first().toMap().contains("mins"), "retention removes measurements");
    check(store.dismissContinue("stable-title") && store.continueItems().isEmpty(), "retained alias cannot resurrect");
    check(store.clearHistory() && store.continueItems().isEmpty(), "full clear still clears");
    auto distinct = qualified;
    distinct["id"] = "other"; distinct["providerMediaId"] = "0123456789_";
    distinct["url"] = "https://music.youtube.com/watch?v=0123456789_";
    writeFixture({qualified, distinct, legacy}, {});
    check(store.continueItems().size() == 2, "same title does not alias distinct provider IDs");
    QVariantList capacity;
    for (int i = 0; i < 4998; ++i) {
        auto row = legacy; row["id"] = QString("other-%1").arg(i);
        row["pk"] = i % 2 ? "youtube" : "netflix";
        row["url"] = QString("https://example.test/watch?id=%1").arg(i);
        capacity.append(row);
    }
    capacity.append(qualified); capacity.append(legacy);
    auto completeAtCapacity = qualified; completeAtCapacity["completed"] = true;
    completeAtCapacity["at"] = 300;
    writeFixture(capacity, {legacy, completeAtCapacity});
    check(store.continueItems().size() == 4998, "near-capacity newer retained completion suppresses aliases");
    writeFixture(capacity, {legacy, qualified});
    QElapsedTimer scale; scale.start();
    check(store.continueItems().size() == 4999, "5000 history + 2 retained, exact alias only");
    check(store.dismissContinue("stable-title") && store.continueItems().size() == 4998,
          "near-capacity dismissal suppresses aliases, preserves other providers");
    check(store.clearActivity() && store.continueItems().size() == 4998 && store.sessions().isEmpty(),
          "near-capacity clearActivity preserves logical state");
    qInfo() << "SCALE history=5000 retained=2 providers=3 projection+dismiss+clear ms=" << scale.elapsed();
    qInfo() << "FERIA_MEDIA_CONTRACT_PASS" << checks;
}
