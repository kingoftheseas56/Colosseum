#include "FeriaAccountStore.h"
#include "FeriaBrowserPolicy.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDebug>
#include <QDesktopServices>
#include <QJSEngine>
#include <QThread>

// Only this owner keeps live/private entries. The policy stores no file rows.
class DownloadOwner final : public QObject {
    Q_OBJECT
public:
    QMap<QString, QVariantMap> files;
    QStringList lostIds;
    QStringList *externalLosses = nullptr;
    Q_INVOKABLE void nativeRequestDestroyed(const QVariant &id) {
        lostIds.append(id.toString());
        if (externalLosses) externalLosses->append(id.toString());
    }
    Q_INVOKABLE QVariant resolveDownloadFile(const QVariant &id) const {
        return files.value(id.toString());
    }
};

class UrlRecorder final : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
    Q_INVOKABLE void receive(const QUrl &url) { urls.append(url); }
};

// Chromium can report a reason that the receiving Qt enum does not expose.
// Its native text getter must only be called for reasons in that Qt enum.
class DownloadReasonFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(DownloadInterruptReason interruptReason READ interruptReason)
    Q_PROPERTY(QString interruptReasonString READ interruptReasonString)
public:
    enum DownloadInterruptReason { NoReason = 0, NetworkFailed = 20 };
    Q_ENUM(DownloadInterruptReason)
    int reason = 0;
    mutable int formatterCalls = 0;
    DownloadInterruptReason interruptReason() const {
        return static_cast<DownloadInterruptReason>(reason);
    }
    QString interruptReasonString() const {
        ++formatterCalls;
        return QStringLiteral("Native translated network failure");
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    int checks = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok) { qCritical() << "FAIL:" << message; std::exit(1); }
    };
    const auto write = [&](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        check(file.open(QIODevice::WriteOnly), "Open fixture file");
        check(file.write(bytes) == bytes.size(), "Write fixture file");
    };
    FeriaAccountStore store;
    const QString storePath = temp.filePath("account.json");
    store.setStoragePath(storePath);
    FeriaBrowserPolicy policy;
    policy.setStorageRoot(temp.filePath("profile"));
    policy.setDownloadAccountStore(&store);
    const auto interruptionText = [&](QObject *request) {
        QString result;
        check(QMetaObject::invokeMethod(&policy, "downloadInterruptionText", Q_RETURN_ARG(QString, result),
                                       Q_ARG(QObject *, request)), "Safe native interruption formatter exists");
        return result;
    };
    DownloadReasonFixture reasonFixture;
    reasonFixture.reason = 20;
    check(interruptionText(&reasonFixture) == QStringLiteral("Native translated network failure")
          && reasonFixture.formatterCalls == 1, "Known Qt reasons preserve native translated text");
    for (const int reason : {38, 41, 50, -1, 999}) {
        reasonFixture.reason = reason;
        check(interruptionText(&reasonFixture).contains(QString::number(reason))
              && reasonFixture.formatterCalls == 1, "Unknown reasons retain their code without reading the unsafe native formatter");
    }
    check(interruptionText(nullptr).isEmpty(), "Absent request has no interruption text");
    QObject missingEnum;
    missingEnum.setProperty("interruptReason", 38);
    missingEnum.setProperty("interruptReasonString", "Untrusted formatter text");
    check(interruptionText(&missingEnum).contains("38"), "Missing enum uses numeric fallback rather than native formatter");
    QObject missingReason;
    check(interruptionText(&missingReason).isEmpty(), "Missing reason does not invent an interruption");
    check(store.metaObject()->indexOfMethod("recordBrowserDownloadResult(QVariantMap)") >= 0,
          "Typed metadata outcome must exist, rather than treating all false results as retryable");
    QObject profileOne, profileTwo;
    QObject nativeRequest(&profileOne);
    const auto ownedRequest = [&](QObject *request, QObject *profile) {
        bool result = false;
        check(QMetaObject::invokeMethod(&policy, "requestBelongsToProfile", Q_RETURN_ARG(bool, result),
                                       Q_ARG(QObject *, request), Q_ARG(QObject *, profile)), "Check native QObject profile owner");
        return result;
    };
    check(ownedRequest(&nativeRequest, &profileOne) && !ownedRequest(&nativeRequest, &profileTwo)
          && !ownedRequest(nullptr, &profileOne), "Only exact native request parent profile is accepted");
    const auto observeLifetime = [&](QObject *owner, QObject *request, const QString &id) {
        bool observed = false;
        check(QMetaObject::invokeMethod(&policy, "observeDownloadRequestLifetime", Q_RETURN_ARG(bool, observed),
              Q_ARG(QObject *, owner), Q_ARG(QObject *, request), Q_ARG(QString, id)), "Connect native request lifetime observer");
        return observed;
    };
    DownloadOwner lifetimeOwner;
    lifetimeOwner.setProperty("profile", QVariant::fromValue<QObject *>(&profileOne));
    auto *lostRequest = new QObject(&profileOne);
    check(!observeLifetime(nullptr, lostRequest, "loss") && !observeLifetime(&lifetimeOwner, nullptr, "loss")
          && !observeLifetime(&lifetimeOwner, lostRequest, ""), "Lifetime observer rejects absent owner, request or ID");
    check(!observeLifetime(&lifetimeOwner, lostRequest, QString(101, 'x')), "Lifetime observer rejects an oversized opaque ID");
    QObject noCallback;
    noCallback.setProperty("profile", QVariant::fromValue<QObject *>(&profileOne));
    check(!observeLifetime(&noCallback, lostRequest, "unsupported"), "Lifetime observer requires its explicit cleanup callback");
    check(observeLifetime(&lifetimeOwner, lostRequest, "loss"), "Same-profile request lifetime observation is available");
    delete lostRequest;
    check(lifetimeOwner.lostIds == QStringList{"loss"}, "Actual QObject destruction dispatches only its opaque download ID");
    auto *foreignRequest = new QObject(&profileTwo);
    check(!observeLifetime(&lifetimeOwner, foreignRequest, "foreign"), "Lifetime observer rejects another profile's request");
    delete foreignRequest;
    check(lifetimeOwner.lostIds == QStringList{"loss"}, "Rejected observation cannot dispatch an unrelated loss");
    QJSEngine scriptEngine;
    QJSEngine::setObjectOwnership(&profileOne, QJSEngine::CppOwnership);
    lifetimeOwner.setProperty("profile", QVariant::fromValue(scriptEngine.newQObject(&profileOne)));
    auto *retiredScopeRequest = new QObject(&profileOne);
    check(observeLifetime(&lifetimeOwner, retiredScopeRequest, "retired-scope"), "QJSValue profile wrapper preserves exact native parent identity");
    policy.setDownloadAccountStore(nullptr);
    delete retiredScopeRequest;
    check(lifetimeOwner.lostIds == QStringList{"loss", "retired-scope"}, "Account invalidation cannot suppress dead-handle cleanup");
    policy.setDownloadAccountStore(&store);
    QThread foreignThread;
    auto *foreignProfile = new QObject;
    auto *crossThreadRequest = new QObject(foreignProfile);
    foreignProfile->moveToThread(&foreignThread);
    foreignThread.start();
    check(interruptionText(crossThreadRequest).isEmpty(), "Formatter rejects cross-thread native property access");
    lifetimeOwner.setProperty("profile", QVariant::fromValue<QObject *>(foreignProfile));
    check(!observeLifetime(&lifetimeOwner, crossThreadRequest, "thread"), "Lifetime observer rejects cross-thread object access");
    QMetaObject::invokeMethod(foreignProfile, [foreignProfile] { delete foreignProfile; }, Qt::BlockingQueuedConnection);
    foreignThread.quit(); foreignThread.wait();
    lifetimeOwner.setProperty("profile", QVariant::fromValue<QObject *>(&profileOne));
    QStringList abandonedLosses;
    auto *retiredOwner = new DownloadOwner;
    retiredOwner->externalLosses = &abandonedLosses;
    retiredOwner->setProperty("profile", QVariant::fromValue<QObject *>(&profileOne));
    auto *lateRequest = new QObject(&profileOne);
    check(observeLifetime(retiredOwner, lateRequest, "late"), "Observe request before owner retirement");
    delete retiredOwner;
    delete lateRequest;
    check(abandonedLosses.isEmpty(), "Destroyed owner automatically disconnects late native lifetime callbacks");
    const auto record = [&](FeriaAccountStore &owner, const QVariantMap &descriptor) {
        QVariantMap result;
        check(QMetaObject::invokeMethod(&owner, "recordBrowserDownloadResult", Q_RETURN_ARG(QVariantMap, result),
                                       Q_ARG(QVariantMap, descriptor)), "Call typed record outcome");
        return result;
    };
    const auto admit = [&](const QString &directory, const QString &name) {
        QVariantMap result;
        check(QMetaObject::invokeMethod(&policy, "admitDownloadDestination", Q_RETURN_ARG(QVariantMap, result),
                                       Q_ARG(QString, directory), Q_ARG(QString, name)), "Call native admission");
        return result;
    };
    const auto completed = [&](const QVariantMap &admission) {
        QVariantMap result;
        check(QMetaObject::invokeMethod(&policy, "completedDownloadFile", Q_RETURN_ARG(QVariantMap, result),
                                       Q_ARG(QVariantMap, admission)), "Call native completed snapshot");
        return result;
    };
    const auto status = [&](QObject *owner, const QString &id, FeriaBrowserPolicy *selectedPolicy = nullptr) {
        QVariantMap result;
        check(QMetaObject::invokeMethod(selectedPolicy ? selectedPolicy : &policy, "downloadFileStatus", Q_RETURN_ARG(QVariantMap, result),
                                       Q_ARG(QObject *, owner), Q_ARG(QString, id)), "Resolve file by owner and ID");
        return result.value("status").toString();
    };
    UrlRecorder launches;
    QDesktopServices::setUrlHandler("file", &launches, "receive");
    const auto action = [&](const char *method, QObject *owner, const QString &id) {
        QVariantMap result;
        check(QMetaObject::invokeMethod(&policy, method, Q_RETURN_ARG(QVariantMap, result),
                                       Q_ARG(QObject *, owner), Q_ARG(QString, id)), "Invoke native file action by ID");
        return result.value("status").toString();
    };
    const auto grant = [&](const QVariantMap &binding = {}) {
        const auto visit = store.beginSessionVisit("space", {{"pk", "youtube"}});
        QString result;
        check(QMetaObject::invokeMethod(&store, "authorizeBrowserDownload", Q_RETURN_ARG(QString, result),
              Q_ARG(QString, QStringLiteral("space")), Q_ARG(QString, visit), Q_ARG(QVariantMap, binding)),
              "Call transfer-bound metadata grant");
        check(!result.isEmpty(), "Ordinary valid source can grant metadata once");
        return result;
    };
    const auto descriptor = [&](const QString &token, const QString &id) {
        return QVariantMap{{"downloadToken", token}, {"id", id}, {"url", "https://example.com/file"},
                           {"state", "completed"}, {"receivedBytes", 0}, {"totalBytes", -1}};
    };

    // Unknown totals must remain unknown; successful metadata cannot replay.
    auto unknown = descriptor(grant(), "unknown-total");
    check(record(store, unknown).value("status") == "recorded", "Unknown total -1 is admitted as unknown");
    check(store.browserDownloadHistory().first().toMap().value("totalBytes").toLongLong() == -1,
          "Unknown total is not coerced to a known zero-byte total");
    check(record(store, unknown).value("status") == "invalidated", "A committed metadata grant cannot replay");

    // A real QSaveFile failure retains the prior bytes/state and the exact grant.
    const auto oldRows = store.browserDownloadHistory();
    check(QFile::rename(storePath, storePath + ".saved") && QDir().mkdir(storePath), "Obstruct atomic replacement");
    auto retry = descriptor(grant({{"id", "retry"}}), "retry");
    check(record(store, retry).value("status") == "retryable", "Only atomic write failure is retryable");
    check(store.browserDownloadHistory() == oldRows, "Write failure retains prior in-memory state");
    auto changedRetry = descriptor(grant({{"id", "changed-retry"}}), "changed-retry");
    check(record(store, changedRetry).value("status") == "retryable", "Second grant experiences same real write failure");
    check(QDir().rmdir(storePath) && QFile::rename(storePath + ".saved", storePath), "Unblock atomic replacement");
    changedRetry.insert("receivedBytes", 9);
    check(record(store, changedRetry).value("status") == "rejected", "Write retry cannot change its settled descriptor");
    bool reentered = false, insideReentry = false;
    auto connection = QObject::connect(&store, &FeriaAccountStore::changed, [&] {
        if (insideReentry) return;
        insideReentry = true;
        reentered = record(store, retry).value("status") == "invalidated";
        insideReentry = false;
    });
    check(record(store, retry).value("status") == "recorded" && reentered,
          "Retry keeps its exact grant and synchronous changed callbacks cannot replay it");
    QObject::disconnect(connection);
    check(record(store, retry).value("status") == "invalidated", "Retry success consumes grant exactly once");

    for (const QString &url : {QString("blob:https://example.com/id"), QString("data:text/plain,secret"),
                               QString("https://example.com/login"), QString("https://example.com/file?token=secret")}) {
        auto rejected = descriptor(grant(), "rejected-url");
        rejected.insert("url", url);
        check(record(store, rejected).value("status") == "rejected", "Inadmissible URL has terminal rejection");
        rejected.insert("url", "https://example.com/file");
        check(record(store, rejected).value("status") == "invalidated", "Rejected URL cannot reuse grant for another descriptor");
    }
    auto malformed = descriptor(grant(), "malformed");
    malformed.insert("receivedBytes", -1);
    check(record(store, malformed).value("status") == "rejected", "Negative received bytes are rejected");
    malformed.insert("receivedBytes", 0);
    check(record(store, malformed).value("status") == "invalidated", "Malformed descriptor invalidates its grant");
    auto wrongTransfer = descriptor(grant({{"id", "original"}}), "replacement");
    check(record(store, wrongTransfer).value("status") == "rejected", "Grant binds exact transfer ID");
    auto wrongBinding = descriptor(grant({{"id", "bound"}, {"profileId", "profile-one"},
                                           {"nativeId", "native-one"}, {"sourceUrl", "https://example.com/file"}}), "bound");
    wrongBinding.insert("profileId", "profile-two"); wrongBinding.insert("nativeId", "native-one");
    check(record(store, wrongBinding).value("status") == "rejected", "Same native ID in another profile cannot use grant");
    auto privateMarked = descriptor(grant(), "private-marked"); privateMarked.insert("privateBrowsing", true);
    check(record(store, privateMarked).value("status") == "rejected", "Private marked descriptor cannot enter ordinary history");
    const auto privateVisit = store.beginSessionVisit("private", {{"pk", "youtube"}, {"privateBrowsing", true}});
    check(store.authorizeBrowserDownload("private", privateVisit).isEmpty(), "Private visit cannot acquire ordinary history grant");

    // Safe files resolve from admitted completion identities, never raw UI paths.
    const QString directory = temp.filePath("downloads"); check(QDir().mkdir(directory), "Create admitted directory");
    for (const QString &name : {QString("../escape"), QString("nested/file"), QString("nested\\file"),
                               QString("/absolute"), QString("CON.txt"), QString("trailing."), QString("a:b"),
                               QString("."), QString(".."), QString("bad\nname")})
        check(admit(directory, name).value("status") != "admitted", "Unsafe destination leaf is not admitted");
    check(admit("relative", "file").value("status") != "admitted", "Relative destination rejected");
    check(admit("//server/share", "file").value("status") != "admitted", "UNC destination rejected");
    check(admit(temp.filePath("new-downloads"), "fresh.bin").value("status") == "admitted",
          "A missing default download directory can be prepared by native admission");
    auto admission = admit(directory, "file.bin");
    check(admission.value("status") == "admitted", "Fresh default destination is admitted");
    check(completed(admission).isEmpty(), "Missing file cannot become completed snapshot");
    const QString filePath = admission.value("path").toString();
    write(filePath, "original");
    check(admit(directory, "file.bin").value("status") != "admitted", "No implicit overwrite admission");
    const auto snapshot = completed(admission);
    check(!snapshot.isEmpty(), "Existing admitted regular file can snapshot completion identity");
    DownloadOwner live;
    live.files.insert("live", snapshot);
    check(status(&live, "live") == "invalidated", "Unregistered resolver cannot authorize a path");
    bool registered = false;
    check(QMetaObject::invokeMethod(&policy, "registerDownloadOwner", Q_RETURN_ARG(bool, registered),
                                   Q_ARG(QObject *, &live), Q_ARG(QObject *, &store)) && registered,
          "Register existing live owner for current account scope");
    check(status(&live, "live") == "available", "Private/transient completed file needs no ordinary history grant");
    check(status(&live, filePath) == "invalidated", "Caller path is not an owner ID");
    check(action("openDownloadedFile", &live, filePath) == "invalidated" && launches.urls.isEmpty(),
          "Caller path cannot dispatch a shell action");
    check(action("openDownloadedFile", &live, "live") == "opened" && launches.urls.size() == 1
          && launches.urls.last() == QUrl::fromLocalFile(filePath), "Open dispatches validated completed file");
    check(action("revealDownloadedFile", &live, "live") == "opened" && launches.urls.size() == 2
          && launches.urls.last() == QUrl::fromLocalFile(directory), "Reveal opens containing folder rather than claiming shell selection");
    check(status(&live, "missing-id") == "invalidated", "Unknown ID never resolves through selected item");
    auto cancelledSnapshot = snapshot; cancelledSnapshot.insert("state", "cancelled");
    live.files.insert("cancelled", cancelledSnapshot);
    check(status(&live, "cancelled") == "rejected", "Cancelled descriptor cannot open a file");

    auto history = descriptor(grant({{"id", "saved-file"}, {"admission", admission}}), "saved-file");
    history.insert("path", filePath); history.insert("fileName", "file.bin"); history.insert("fileSnapshot", snapshot);
    check(record(store, history).value("status") == "recorded", "Canonical history stores admitted completed file identity");
    check(status(&store, "saved-file") == "available", "History resolves its own completed file by ID");
    auto wrongPath = descriptor(grant({{"id", "wrong-path"}, {"admission", admission}}), "wrong-path");
    wrongPath.insert("path", temp.filePath("elsewhere")); wrongPath.insert("fileSnapshot", snapshot);
    check(record(store, wrongPath).value("status") == "rejected", "Completed snapshot must match admitted metadata path");
    FeriaAccountStore restarted; restarted.setStoragePath(storePath);
    FeriaBrowserPolicy restartPolicy;
    restartPolicy.setStorageRoot(temp.filePath("profile"));
    restartPolicy.setDownloadAccountStore(&store);
    restartPolicy.setDownloadAccountStore(&restarted);
    check(status(&store, "saved-file", &restartPolicy) == "invalidated",
          "A retained distinct old account store cannot authorize history in the new canonical scope");
    check(status(&restarted, "saved-file", &restartPolicy) == "available", "Restart resolves recorded file without inventing a native request");
    check(QFile::rename(filePath, filePath + ".original"), "Move original file aside");
    write(filePath, "original");
    check(status(&live, "live") == "replaced" && status(&restarted, "saved-file", &restartPolicy) == "replaced",
          "Same path and bytes but new native file identity is rejected");
    check(action("openDownloadedFile", &live, "live") == "replaced" && launches.urls.size() == 2,
          "Action-time replacement fence blocks shell dispatch");
    check(QFile::remove(filePath), "Delete replacement fixture");
    check(status(&live, "live") == "unavailable", "Missing completed file is unavailable");
    check(QFile::rename(filePath + ".original", filePath), "Restore original file");
#ifdef Q_OS_UNIX
    const auto linkAdmission = admit(directory, "link.bin");
    check(QFile::link(filePath, linkAdmission.value("path").toString()), "Create symlink fixture");
    check(completed(linkAdmission).isEmpty(), "Symlink cannot acquire completed-file authorization");
#endif
    const auto unsafeHistoryPath = temp.filePath("unsafe-history.json");
    auto unsafeHistoryRow = history;
    unsafeHistoryRow.insert("url", "data:text/plain,hidden");
    write(unsafeHistoryPath, QJsonDocument::fromVariant(QVariantMap{{"version", 1},
        {"browserDownloadHistory", QVariantList{unsafeHistoryRow}}}).toJson());
    FeriaAccountStore unsafeHistory; unsafeHistory.setStoragePath(unsafeHistoryPath);
    check(unsafeHistory.browserDownloadHistory().isEmpty() && status(&unsafeHistory, "saved-file") == "invalidated",
          "Persisted inadmissible or forged history cannot authorize a file");
    const auto corrupt = temp.filePath("corrupt.json"); write(corrupt, "{partial");
    FeriaAccountStore corruptStore; corruptStore.setStoragePath(corrupt);
    check(!corruptStore.error().isEmpty() && corruptStore.browserDownloadHistory().isEmpty(), "Corrupt data cannot authorize files");
    check(!corruptStore.clearBrowserDownloads(), "Corrupt data is preserved rather than overwritten by history cleanup");
    QFile corruptFile(corrupt); check(corruptFile.open(QIODevice::ReadOnly) && corruptFile.readAll() == "{partial", "Corrupt bytes preserved");
    const auto partialPath = temp.filePath("partial-account.json");
    check(QFile::copy(storePath, partialPath), "Copy known good account snapshot");
    FeriaAccountStore partialStore; partialStore.setStoragePath(partialPath);
    const auto lastGoodRows = partialStore.browserDownloadHistory();
    write(partialPath, "{partial");
    partialStore.setStoragePath(partialPath);
    check(!lastGoodRows.isEmpty() && partialStore.browserDownloadHistory() == lastGoodRows
          && !partialStore.error().isEmpty(), "A same-account partial reload preserves last good in-memory state");
    check(!partialStore.clearBrowserDownloads(), "Preserved last-good data cannot overwrite corrupt source");
    check(restarted.clearBrowserDownloads() && QFileInfo::exists(filePath), "Clearing history leaves downloaded file untouched");
    check(status(&restarted, "saved-file", &restartPolicy) == "invalidated", "Cleared history cannot continue to authorize old ID");

    // Different account and even A -> B -> A retire old live capabilities and grants.
    const auto oldToken = grant();
    store.setStoragePath(temp.filePath("other-account.json"));
    check(record(store, descriptor(oldToken, "old")).value("status") == "invalidated", "Old account grant cannot write new account");
    check(status(&live, "live") == "invalidated", "Account change retires prior live file scope");
    store.setStoragePath(storePath);
    check(status(&live, "live") == "invalidated", "Returning to same path cannot revive old live owner scope");
    policy.setDownloadAccountStore(&store);
    live.files.insert("live", completed(admission));
    check(QMetaObject::invokeMethod(&policy, "registerDownloadOwner", Q_RETURN_ARG(bool, registered),
                                   Q_ARG(QObject *, &live), Q_ARG(QObject *, &store)) && registered,
          "Explicit new account scope can register its own owner again");
    check(status(&live, "live") == "available", "Newly captured completed identity is current");
    policy.setStorageRoot(temp.filePath("replacement-profile"));
    check(status(&live, "live") == "invalidated", "Storage root replacement retires registered live scope");
    QDesktopServices::unsetUrlHandler("file");
    qInfo() << "FERIA_DOWNLOAD_POLICY_PASS" << checks;
}
#include "feria_download_policy_test.moc"
