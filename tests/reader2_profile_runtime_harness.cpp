// reader2_profile_runtime_harness — F19 proof against the real
// ProfileStoreRuntime lifecycle. The sibling native harness exercises the
// explicit route controls; this one keeps the production binding enabled so
// storesAboutToChange/storesChanged ordering and runtime profile paths are
// covered as well.
#include "reader2/Reader2Bridge.h"
#include "account/LegacyPersonalStateStorage.h"
#include "account/ProfilePaths.h"
#include "account/ProfileStoreRuntime.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

namespace {

QString bookOwner(const QJsonObject &object)
{
    return object.value(QStringLiteral("owner")).toString();
}

QJsonObject firstItem(const QJsonArray &items)
{
    return items.isEmpty() ? QJsonObject{} : items.first().toObject();
}

bool prepareAccountRoot(const QString &appDataRoot,
                        const QString &accountId,
                        QString *profileRoot)
{
    const auto paths = ProfilePaths::account(accountId, appDataRoot);
    if (!paths.has_value())
        return false;
    if (!QDir().mkpath(paths->profileRoot()))
        return false;
    if (profileRoot)
        *profileRoot = paths->profileRoot();
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    int fails = 0;
    auto check = [&](bool ok, const char *what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++fails;
        } else {
            std::printf("ok   %s\n", what);
        }
    };

    QTemporaryDir tmp;
    check(tmp.isValid(), "disposable runtime root created");
    if (!tmp.isValid()) {
        std::printf("VERDICT: FAIL\n");
        return 1;
    }

    const QString accountA =
        QStringLiteral("11111111-1111-4111-8111-111111111111");
    const QString accountB =
        QStringLiteral("22222222-2222-4222-8222-222222222222");
    QString profileA;
    QString profileB;
    check(prepareAccountRoot(tmp.path(), accountA, &profileA),
          "account A profile root prepared");
    check(prepareAccountRoot(tmp.path(), accountB, &profileB),
          "account B profile root prepared");
    const QString localRoot =
        QDir(tmp.path()).filePath(QStringLiteral("profiles/local"));

    const QString bookId = QStringLiteral("same-book-key");
    const QString bookPath =
        QDir(tmp.path()).filePath(QStringLiteral("same-book.epub"));
    QFile book(bookPath);
    check(book.open(QIODevice::WriteOnly),
          "same physical book opened for runtime auth probe");
    if (book.isOpen())
        book.write(QByteArray("F19 runtime book bytes"));
    book.close();

    const LegacyPersonalStateStorage legacyStorage =
        LegacyPersonalStateStorage::isolated(
            QDir(tmp.path()).filePath(QStringLiteral("legacy")));
    ProfileStoreRuntime runtime(legacyStorage, tmp.path());
    Reader2Bridge bridge;

    QString expectedOldRoot;
    QString expectedOldOwner;
    bool oldRouteVisibleDuringAbout = false;
    int aboutCount = 0;
    int changedCount = 0;
    QObject::connect(&bridge, &Reader2Bridge::personalStateAboutToChange,
                     [&] {
        ++aboutCount;
        const bool routeStillOpen = !bridge.personalStateSealed()
            && bridge.personalStateRoot() == expectedOldRoot;
        const bool oldStateStillReadable =
            expectedOldOwner.isEmpty()
            || bookOwner(bridge.progressGet(bookId)) == expectedOldOwner;
        oldRouteVisibleDuringAbout = oldRouteVisibleDuringAbout
            || (routeStillOpen && oldStateStillReadable);
    });
    QObject::connect(&bridge, &Reader2Bridge::personalStateChanged,
                     [&] { ++changedCount; });

    bridge.bindProfileStoreRuntime(&runtime);
    check(bridge.personalStateSealed(),
          "production runtime binding starts Reader2 sealed");
    check(bridge.personalStateGeneration() > 0,
          "production runtime binding seeds a route generation");
    check(runtime.activeProfile().kind() == ProfilePaths::Kind::Sealed,
          "runtime starts in the sealed profile");

    QString error;
    check(runtime.activateAccountProfile(accountA, &error),
          "runtime activates account A");
    check(!bridge.personalStateSealed()
              && bridge.personalStateRoot()
                     == QDir(profileA).filePath(QStringLiteral("book_reader")),
          "bridge follows runtime account A profile root");
    const quint64 generationA = bridge.personalStateGeneration();
    const QJsonObject progressA{{QStringLiteral("owner"), QStringLiteral("A")},
                                {QStringLiteral("percent"), 41}};
    const QJsonObject settingsA{
        {QStringLiteral("reader2"),
         QJsonObject{{QStringLiteral("owner"), QStringLiteral("A")}}}};
    bridge.progressSaveForGeneration(bookId, progressA, generationA);
    bridge.settingsSaveForGeneration(settingsA, generationA);
    bridge.bookmarksSaveForGeneration(
        bookId,
        QJsonObject{{QStringLiteral("id"), QStringLiteral("bookmark-a")},
                    {QStringLiteral("cfi"), QStringLiteral("a-cfi")}},
        generationA);
    bridge.annotationsSaveForGeneration(
        bookId,
        QJsonObject{{QStringLiteral("id"), QStringLiteral("annotation-a")},
                    {QStringLiteral("cfi"), QStringLiteral("a-cfi")}},
        generationA);
    check(bridge.progressGet(bookId) == progressA,
          "runtime account A owns progress at the physical book path");

    expectedOldRoot = bridge.personalStateRoot();
    expectedOldOwner = QStringLiteral("A");
    bridge.setAuthorizedBook(bookPath);
    check(!bridge.filesRead(bookPath).isEmpty(),
          "account A authorizes the physical book");
    check(runtime.sealAccountProfile(accountA, &error),
          "runtime emits a sealed transition for account A");
    check(oldRouteVisibleDuringAbout,
          "runtime about-to-change exposes the old route before bridge sealing");
    check(bridge.personalStateSealed() && bridge.filesRead(bookPath).isEmpty(),
          "runtime sealing clears paper authorization and private access");
    const quint64 sealedGeneration = bridge.personalStateGeneration();
    check(sealedGeneration > generationA,
          "runtime sealing advances the reader route generation");

    oldRouteVisibleDuringAbout = false;
    expectedOldRoot.clear();
    expectedOldOwner.clear();
    check(runtime.activateAccountProfile(accountB, &error),
          "runtime activates account B after account A is sealed");
    check(!bridge.personalStateSealed()
              && bridge.personalStateRoot()
                     == QDir(profileB).filePath(QStringLiteral("book_reader")),
          "bridge follows runtime account B profile root");
    const quint64 generationB = bridge.personalStateGeneration();
    check(generationB > sealedGeneration,
          "account B receives a fresh reader route generation");
    check(bridge.progressGet(bookId).isEmpty()
              && bridge.settingsGet().isEmpty()
              && bridge.bookmarksGet(bookId).isEmpty()
              && bridge.annotationsGet(bookId).isEmpty(),
          "account B cannot read account A state at the same book path");

    const QJsonObject progressB{{QStringLiteral("owner"), QStringLiteral("B")},
                                {QStringLiteral("percent"), 72}};
    const QJsonObject settingsB{
        {QStringLiteral("reader2"),
         QJsonObject{{QStringLiteral("owner"), QStringLiteral("B")}}}};
    bridge.progressSaveForGeneration(bookId, progressB, generationB);
    bridge.settingsSaveForGeneration(settingsB, generationB);
    bridge.bookmarksSaveForGeneration(
        bookId,
        QJsonObject{{QStringLiteral("id"), QStringLiteral("bookmark-b")},
                    {QStringLiteral("cfi"), QStringLiteral("b-cfi")}},
        generationB);
    bridge.annotationsSaveForGeneration(
        bookId,
        QJsonObject{{QStringLiteral("id"), QStringLiteral("annotation-b")},
                    {QStringLiteral("cfi"), QStringLiteral("b-cfi")}},
        generationB);

    check(!bridge.personalStateGenerationIsCurrent(generationA),
          "account A generation is stale after account B activates");
    bridge.progressSaveForGeneration(
        bookId,
        QJsonObject{{QStringLiteral("owner"), QStringLiteral("late-A")}},
        generationA);
    bridge.settingsSaveForGeneration(
        QJsonObject{{QStringLiteral("reader2"),
                     QJsonObject{{QStringLiteral("owner"), QStringLiteral("late-A")}}}},
        generationA);
    check(bridge.bookmarksSaveForGeneration(
              bookId,
              QJsonObject{{QStringLiteral("id"), QStringLiteral("bookmark-b")},
                          {QStringLiteral("cfi"), QStringLiteral("late-a")}},
              generationA)
              .isEmpty(),
          "late account A bookmark write is refused after runtime account B switch");
    check(bridge.annotationsSaveForGeneration(
              bookId,
              QJsonObject{{QStringLiteral("id"), QStringLiteral("annotation-b")},
                          {QStringLiteral("cfi"), QStringLiteral("late-a")}},
              generationA)
              .isEmpty(),
          "late account A annotation write is refused after runtime account B switch");
    bridge.bookmarksDeleteForGeneration(bookId, QStringLiteral("bookmark-b"), generationA);
    bridge.annotationsDeleteForGeneration(bookId, QStringLiteral("annotation-b"), generationA);
    check(bridge.progressGet(bookId) == progressB
              && bridge.settingsGet() == settingsB,
          "late account A progress/settings cannot mutate runtime account B");
    check(firstItem(bridge.bookmarksGet(bookId)).value(QStringLiteral("cfi")).toString()
              == QStringLiteral("b-cfi")
              && firstItem(bridge.annotationsGet(bookId))
                     .value(QStringLiteral("cfi")).toString()
                     == QStringLiteral("b-cfi"),
          "late account A bookmark/annotation callbacks cannot mutate runtime account B");

    bridge.setAuthorizedBook(bookPath);
    oldRouteVisibleDuringAbout = false;
    expectedOldRoot = bridge.personalStateRoot();
    expectedOldOwner = QStringLiteral("B");
    check(runtime.sealAccountProfile(accountB, &error),
          "runtime seals account B before local-only route");
    check(runtime.activateLocalOnlyProfile(&error),
          "runtime activates local-only profile");
    check(oldRouteVisibleDuringAbout,
          "runtime local-only transition preserves old B route through about signal");
    check(bridge.personalStateRoot()
              == QDir(localRoot).filePath(QStringLiteral("book_reader")),
          "bridge follows runtime local-only profile root");
    check(bridge.progressGet(bookId).isEmpty()
              && bridge.settingsGet().isEmpty()
              && bridge.bookmarksGet(bookId).isEmpty()
              && bridge.annotationsGet(bookId).isEmpty(),
          "local-only runtime route is isolated from account B");
    check(bridge.filesRead(bookPath).isEmpty(),
          "local-only runtime transition clears account B paper authorization");

    check(runtime.activateAccountProfile(accountA, &error),
          "runtime returns to account A after local-only route");
    check(bridge.progressGet(bookId) == progressA
              && bridge.settingsGet() == settingsA
              && firstItem(bridge.bookmarksGet(bookId))
                     .value(QStringLiteral("id")).toString()
                     == QStringLiteral("bookmark-a")
              && firstItem(bridge.annotationsGet(bookId))
                     .value(QStringLiteral("id")).toString()
                     == QStringLiteral("annotation-a"),
          "runtime return to account A restores only account A reader state");

    bridge.setAuthorizedBook(bookPath);
    bridge.bindProfileStoreRuntime(nullptr);
    check(bridge.personalStateSealed(),
          "losing the runtime seals Reader2 instead of reopening legacy state");
    check(bridge.filesRead(bookPath).isEmpty()
              && bridge.progressGet(bookId).isEmpty(),
          "runtime loss refuses paper and private store access");
    check(aboutCount > 0 && changedCount > 0,
          "production runtime binding delivered lifecycle signals");

    std::printf(fails ? "VERDICT: FAIL\n" : "VERDICT: PASS\n");
    return fails ? 1 : 0;
}
