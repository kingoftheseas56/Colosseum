#include "devtools/VinylTestProfileController.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        QTextStream(stdout) << (ok ? "PASS " : "FAIL ") << name << '\n';
        ok ? ++passed : ++failed;
    };
    QTemporaryDir fixture(QDir::tempPath() + "/vinyl075-fixture-XXXXXX");
    check(fixture.isValid(), "owned-temporary-fixture-created");
    QDir(fixture.path()).mkdir("data");
    const QString data = fixture.path() + "/data";
    QFile marker(fixture.path() + '/' + VinylTestProfileController::markerName);
    check(marker.open(QIODevice::WriteOnly), "owned-marker-opened");
    marker.write(VinylTestProfileController::markerContents); marker.close();
    QString alias = "local";
    int callbacks = 0;
    bool failActivation = false;
    bool pendingRequested = false;
    VinylTestProfileController c(data, [&](const QString &next, bool withPendingWrite) {
        pendingRequested = withPendingWrite;
        ++callbacks;
        if (failActivation) return false;
        alias = next; return true;
    }, [&]() { return VinylTestProfileController::Status{alias, true, true, true}; });
    qputenv("COLOSSEUM_VINYL_TEST_ROOT", fixture.path().toUtf8());
    qputenv("COLOSSEUM_APPDATA_TAG", "vinyl075-fixture-guard");
    for (const auto &entry : {qMakePair("HOME", "home"), qMakePair("XDG_DATA_HOME", "data"),
                             qMakePair("XDG_CONFIG_HOME", "config"), qMakePair("XDG_CACHE_HOME", "cache"),
                             qMakePair("XDG_RUNTIME_DIR", "runtime")}) {
        QDir(fixture.path()).mkpath(entry.second);
        qputenv(entry.first, (fixture.path() + '/' + entry.second).toUtf8());
    }

    qunsetenv("COLOSSEUM_VINYL_TEST_FIXTURES");
    check(!c.execute({{"action", "activate-a"}})["success"].toBool() && callbacks == 0,
          "launch-gate-absent-refuses-before-callback");
    qputenv("COLOSSEUM_VINYL_TEST_FIXTURES", "1");
    check(c.enabled(), "owned-root-both-gates-enable");
    qunsetenv("COLOSSEUM_APPDATA_TAG");
    check(!c.enabled(), "untagged-settings-refused");
    qputenv("COLOSSEUM_APPDATA_TAG", "normal-profile");
    check(!c.enabled(), "nonfixture-tag-refused");
    qputenv("COLOSSEUM_APPDATA_TAG", "vinyl075-fixture-guard");
    qputenv("XDG_CONFIG_HOME", QDir::tempPath().toUtf8());
    check(!c.enabled(), "configuration-outside-fixture-refused");
    qputenv("XDG_CONFIG_HOME", (fixture.path() + "/config").toUtf8());
    const QString descendantLink = data + "/profiles-link";
    check(QFile::link(QDir::tempPath(), descendantLink), "owned-descendant-symlink-created");
    check(!c.execute({{"action", "activate-a"}})["success"].toBool() && callbacks == 0,
          "descendant-profile-symlink-refused-before-callback");
    check(QFile::remove(descendantLink), "owned-descendant-link-removed");

    check(!c.execute({{"action", "activate-account"}})["success"].toBool() && callbacks == 0,
          "arbitrary-action-refused");
    check(!c.execute({{"action", "activate-a"}, {"accountId", "arbitrary"}})["success"].toBool() && callbacks == 0,
          "arbitrary-identities-refused");
    check(!c.execute({{"action", 1}})["success"].toBool(), "wrong-action-type-refused");
    check(!c.execute({})["success"].toBool(), "empty-payload-refused");
    auto a = c.execute({{"action", "activate-a"}});
    check(a["success"].toBool() && alias == "A" && a["generation"].toInt() == 1,
          "fixed-alias-callback-and-generation");
    a = c.execute({{"action", "activate-a"}});
    check(a["generation"].toInt() == 1, "same-alias-does-not-fabricate-generation");
    failActivation = true;
    auto b = c.execute({{"action", "activate-b"}});
    check(!b["success"].toBool() && b["profileAlias"] == "A" && b["generation"].toInt() == 1,
          "failed-activation-retains-observed-alias-generation");
    failActivation = false;
    b = c.execute({{"action", "activate-b"}});
    check(b["success"].toBool() && b["profileAlias"] == "B" && b["generation"].toInt() == 2,
          "second-fixed-alias-confirmed");
    auto local = c.execute({{"action", "activate-local"}});
    check(local["success"].toBool() && local["profileAlias"] == "local", "local-alias-confirmed");
    auto pending = c.execute({{"action", "activate-b-with-pending-write"}});
    check(pending["success"].toBool() && pendingRequested && alias == "B",
          "bounded-pending-write-action-dispatched-with-fixed-alias");
    const int pendingCallbacks = callbacks;
    auto pendingNoop = c.execute({{"action", "activate-b-with-pending-write"}});
    check(!pendingNoop["success"].toBool() && callbacks == pendingCallbacks,
          "pending-write-noop-on-B-refused-before-callback");
    const int previousCallbacks = callbacks;
    auto status = c.execute({{"action", "status"}});
    check(status["success"].toBool() && callbacks == previousCallbacks && status.size() == 12,
          "status-read-only-and-minimal");
    VinylTestProfileController retired(data, [](const QString &, bool) { return true; }, [] {
        VinylTestProfileController::Status observed{"B", true, true, true};
        observed.pendingWriteAccepted = true;
        observed.pendingBeforeSwitch = true;
        observed.pendingOwnerRetired = true;
        return observed;
    });
    const auto retiredStatus = retired.execute({{"action", "status"}});
    check(retiredStatus["pendingOwnerRetired"].toBool()
          && retiredStatus["pendingWriteAccepted"].toBool()
          && retiredStatus["pendingBeforeSwitch"].toBool()
          && !retiredStatus["pendingWriteCompleted"].toBool()
          && !retiredStatus["pendingWriteSucceeded"].toBool(),
          "owner-retirement-does-not-fabricate-durable-acknowledgement");
    alias = "real-profile";
    check(!c.execute({{"action", "activate-a"}})["success"].toBool() && callbacks == previousCallbacks,
          "unrecognized-profile-refused-before-callback");
    alias = "local";
    check(marker.open(QIODevice::WriteOnly | QIODevice::Truncate), "negative-marker-opened"); marker.write("wrong"); marker.close();
    check(!c.enabled(), "incorrect-marker-refused");
    check(marker.open(QIODevice::WriteOnly | QIODevice::Truncate), "marker-restored-opened"); marker.write(VinylTestProfileController::markerContents); marker.close();
    VinylTestProfileController foreign(QDir::tempPath(), {}, {});
    check(!foreign.enabled(), "appdata-outside-fixture-refused");
    const QString link = fixture.path() + "/linked-data";
    check(QFile::link(data, link), "owned-symlink-created-for-negative-case");
    VinylTestProfileController symlink(link, {}, {});
    check(!symlink.enabled(), "symlink-appdata-refused");
    VinylTestProfileController lexical(fixture.path() + "/linked-data/../data", {}, {});
    check(!lexical.enabled(), "lexical-symlink-parent-traversal-refused");
    check(QFile::remove(link), "owned-negative-link-removed");
    VinylTestProfileController unchanged(data, [](const QString &, bool) { return true; },
        []() { return VinylTestProfileController::Status{"local", true, true, true}; });
    auto unconfirmed = unchanged.execute({{"action", "activate-a"}});
    check(!unconfirmed["success"].toBool()
          && unconfirmed["errorCategory"] == "activation-unconfirmed",
          "successful-callback-without-actual-profile-change-refused");
    QString partialAlias = "A";
    VinylTestProfileController partial(data, [&](const QString &, bool) {
        partialAlias = "local"; return false;
    }, [&]() { return VinylTestProfileController::Status{partialAlias, true, true, true}; });
    auto partialResult = partial.execute({{"action", "activate-b"}});
    check(!partialResult["success"].toBool() && partialResult["profileAlias"] == "local"
          && partialResult["generation"].toInt() == 1,
          "partially-committed-local-boundary-reported-truthfully");
    VinylTestProfileController unbound(data, {}, {});
    check(!unbound.execute({{"action", "status"}})["success"].toBool(), "unbound-controller-refused");
    QTextStream(stdout) << "TOTAL pass=" << passed << " fail=" << failed
        << " kind=controller-guard-component; real-profile-store-and-native-UI-not-exercised\n";
    return failed ? 1 : 0;
}
