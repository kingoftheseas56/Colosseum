// reader2_profile_privacy_harness — deterministic F19 proof of Reader2's
// profile-private JSON route. It uses disposable profile roots and the existing
// BookStores test-mode sandbox; no account service, network, or live reader files.
//
// The harness also submits writes with the OLD route generation after switching
// from account A to account B. The bridge must reject those writes, including
// settings, bookmarks, and annotations, so a late callback cannot mutate B.
#include "reader2/Reader2Bridge.h"
#include "reader/BookStores.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

namespace {

bool hasFile(const QString& root, const QString& name)
{
    return QFileInfo::exists(QDir(root).filePath(QStringLiteral("book_reader/") + name));
}

QJsonObject firstItem(const QJsonArray& items)
{
    return items.isEmpty() ? QJsonObject{} : items.first().toObject();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL %s\n", what); ++fails; }
        else       std::printf("ok   %s\n", what);
    };

    // The legacy directory is redirected by Qt test mode. Remove only that
    // disposable sandbox so the legacy-preservation checks start clean.
    const QString legacyRoot = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/book_reader"));
    QDir(legacyRoot).removeRecursively();

    const QString bookId = QStringLiteral("book-key");
    const QJsonObject legacyProgress{{QStringLiteral("owner"), QStringLiteral("legacy")},
                                     {QStringLiteral("percent"), 11}};
    const QJsonObject legacySettings{{QStringLiteral("reader2"),
                                      QJsonObject{{QStringLiteral("owner"), QStringLiteral("legacy")}}}};
    BookStores::save(QStringLiteral("progress.json"), bookId, legacyProgress);
    BookStores::writeStore(QStringLiteral("settings.json"), legacySettings);
    BookStores::listSave(QStringLiteral("bookmarks.json"), bookId,
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("legacy-bookmark")},
                                     {QStringLiteral("cfi"), QStringLiteral("legacy-cfi")}});
    BookStores::listSave(QStringLiteral("annotations.json"), bookId,
                         QJsonObject{{QStringLiteral("id"), QStringLiteral("legacy-annotation")},
                                     {QStringLiteral("cfi"), QStringLiteral("legacy-cfi")}});
    BookStores::writeStore(QStringLiteral("display_names.json"),
                           QJsonObject{{QStringLiteral("library-label"), QStringLiteral("shared")}});

    QTemporaryDir tmp;
    check(tmp.isValid(), "disposable profile root created");
    const QString profileA = QDir(tmp.path()).filePath(QStringLiteral("profiles/account-a"));
    const QString profileB = QDir(tmp.path()).filePath(QStringLiteral("profiles/account-b"));
    const QString localProfile = QDir(tmp.path()).filePath(QStringLiteral("profiles/local"));
    const QString bookPath = QDir(tmp.path()).filePath(QStringLiteral("same-book.epub"));
    {
        QFile book(bookPath);
        check(book.open(QIODevice::WriteOnly), "same physical book opened for auth probe");
        book.write(QByteArray("F19 book bytes"));
    }

    Reader2Bridge bridge;
    QStringList lifecycle;
    bool oldRouteVisibleDuringAbout = false;
    QObject::connect(&bridge, &Reader2Bridge::personalStateAboutToChange, [&] {
        lifecycle.append(QStringLiteral("about"));
        oldRouteVisibleDuringAbout = !bridge.personalStateSealed()
            && bridge.progressGet(bookId).value(QStringLiteral("owner")).toString()
                   == QStringLiteral("legacy");
    });
    QObject::connect(&bridge, &Reader2Bridge::personalStateChanged, [&] {
        lifecycle.append(QStringLiteral("changed"));
    });

    check(!bridge.personalStateSealed(), "unbound bridge starts on explicit legacy route");
    check(bridge.progressGet(bookId) == legacyProgress,
          "legacy route reads preserved progress JSON");
    check(bridge.settingsGet() == legacySettings,
          "legacy route reads preserved settings JSON");
    check(firstItem(bridge.bookmarksGet(bookId)).value(QStringLiteral("id")).toString()
              == QStringLiteral("legacy-bookmark"),
          "legacy route reads preserved bookmarks JSON");
    check(firstItem(bridge.annotationsGet(bookId)).value(QStringLiteral("id")).toString()
              == QStringLiteral("legacy-annotation"),
          "legacy route reads preserved annotations JSON");

    bridge.setAuthorizedBook(bookPath);
    check(!bridge.filesRead(bookPath).isEmpty(), "legacy route authorizes the same physical book");

    bridge.useProfilePersonalState(profileA);
    check(lifecycle == QStringList{QStringLiteral("about"), QStringLiteral("changed")},
          "profile route emits about-to-change before changed");
    check(oldRouteVisibleDuringAbout, "old route remains readable during about-to-change flush window");
    check(!bridge.personalStateSealed(), "account A route is open after changed");
    check(bridge.progressGet(bookId).isEmpty(),
          "account A starts empty instead of copying legacy reader progress");
    check(bridge.settingsGet().isEmpty(),
          "account A starts empty instead of copying legacy reader settings");
    check(bridge.bookmarksGet(bookId).isEmpty() && bridge.annotationsGet(bookId).isEmpty(),
          "account A starts without legacy bookmarks or annotations");
    check(bridge.filesRead(bookPath).isEmpty(),
          "profile switch clears old paper authorization");
    check(!hasFile(profileA, QStringLiteral("display_names.json")),
          "shared display label is not copied into account A private root");

    const quint64 generationA = bridge.personalStateGeneration();
    const QJsonObject progressA{{QStringLiteral("owner"), QStringLiteral("A")},
                                {QStringLiteral("percent"), 42}};
    const QJsonObject settingsA{{QStringLiteral("reader2"),
                                 QJsonObject{{QStringLiteral("owner"), QStringLiteral("A")}}}};
    bridge.progressSaveForGeneration(bookId, progressA, generationA);
    bridge.settingsSaveForGeneration(settingsA, generationA);
    const QJsonObject bookmarkA = bridge.bookmarksSaveForGeneration(
        bookId, QJsonObject{{QStringLiteral("id"), QStringLiteral("bookmark-a")},
                            {QStringLiteral("cfi"), QStringLiteral("a-cfi")}}, generationA);
    const QJsonObject annotationA = bridge.annotationsSaveForGeneration(
        bookId, QJsonObject{{QStringLiteral("id"), QStringLiteral("annotation-a")},
                            {QStringLiteral("cfi"), QStringLiteral("a-cfi")}}, generationA);
    check(bookmarkA.value(QStringLiteral("id")).toString() == QStringLiteral("bookmark-a"),
          "account A bookmark write accepts active generation");
    check(annotationA.value(QStringLiteral("id")).toString() == QStringLiteral("annotation-a"),
          "account A annotation write accepts active generation");
    check(bridge.progressGet(bookId) == progressA, "account A progress is private");
    check(bridge.settingsGet() == settingsA, "account A settings are private");

    bridge.useProfilePersonalState(profileB);
    const quint64 generationB = bridge.personalStateGeneration();
    check(bridge.progressGet(bookId).isEmpty() && bridge.settingsGet().isEmpty()
              && bridge.bookmarksGet(bookId).isEmpty() && bridge.annotationsGet(bookId).isEmpty(),
          "account B cannot read account A reader state at the same book path");

    const QJsonObject progressB{{QStringLiteral("owner"), QStringLiteral("B")},
                                {QStringLiteral("percent"), 73}};
    const QJsonObject settingsB{{QStringLiteral("reader2"),
                                 QJsonObject{{QStringLiteral("owner"), QStringLiteral("B")}}}};
    bridge.progressSaveForGeneration(bookId, progressB, generationB);
    bridge.settingsSaveForGeneration(settingsB, generationB);
    bridge.bookmarksSaveForGeneration(
        bookId, QJsonObject{{QStringLiteral("id"), QStringLiteral("bookmark-b")},
                            {QStringLiteral("cfi"), QStringLiteral("b-cfi")}}, generationB);
    bridge.annotationsSaveForGeneration(
        bookId, QJsonObject{{QStringLiteral("id"), QStringLiteral("annotation-b")},
                            {QStringLiteral("cfi"), QStringLiteral("b-cfi")}}, generationB);

    const QJsonObject staleSettings{{QStringLiteral("reader2"),
                                     QJsonObject{{QStringLiteral("owner"), QStringLiteral("late-A")}}}};
    check(!bridge.personalStateGenerationIsCurrent(generationA),
          "account A generation is stale after account B activates");
    bridge.progressSaveForGeneration(bookId,
                                     QJsonObject{{QStringLiteral("owner"), QStringLiteral("late-A")}},
                                     generationA);
    bridge.settingsSaveForGeneration(staleSettings, generationA);
    check(bridge.bookmarksSaveForGeneration(
              bookId, QJsonObject{{QStringLiteral("id"), QStringLiteral("bookmark-b")},
                                  {QStringLiteral("cfi"), QStringLiteral("late-a-cfi")}},
              generationA).isEmpty(),
          "late account A bookmark write is refused by bridge generation");
    check(bridge.annotationsSaveForGeneration(
              bookId, QJsonObject{{QStringLiteral("id"), QStringLiteral("annotation-b")},
                                  {QStringLiteral("cfi"), QStringLiteral("late-a-cfi")}},
              generationA).isEmpty(),
          "late account A annotation write is refused by bridge generation");
    bridge.bookmarksDeleteForGeneration(bookId, QStringLiteral("bookmark-b"), generationA);
    bridge.annotationsDeleteForGeneration(bookId, QStringLiteral("annotation-b"), generationA);
    check(bridge.progressGet(bookId) == progressB,
          "late account A progress write cannot mutate account B");
    check(bridge.settingsGet() == settingsB,
          "late account A settings write cannot mutate account B");
    check(firstItem(bridge.bookmarksGet(bookId)).value(QStringLiteral("cfi")).toString()
              == QStringLiteral("b-cfi"),
          "late account A bookmark write/delete cannot mutate account B");
    check(firstItem(bridge.annotationsGet(bookId)).value(QStringLiteral("cfi")).toString()
              == QStringLiteral("b-cfi"),
          "late account A annotation write/delete cannot mutate account B");

    bridge.setAuthorizedBook(bookPath);
    check(!bridge.filesRead(bookPath).isEmpty(), "account B can authorize the same book independently");
    bridge.useProfilePersonalState(localProfile);
    check(bridge.progressGet(bookId).isEmpty() && bridge.settingsGet().isEmpty()
              && bridge.bookmarksGet(bookId).isEmpty() && bridge.annotationsGet(bookId).isEmpty(),
          "local-only reader root is isolated from account B");
    check(bridge.filesRead(bookPath).isEmpty(),
          "local-only transition clears account B paper authorization");
    bridge.useProfilePersonalState(profileA);
    check(bridge.progressGet(bookId) == progressA && bridge.settingsGet() == settingsA,
          "return to account A restores only account A state");
    check(firstItem(bridge.bookmarksGet(bookId)).value(QStringLiteral("id")).toString()
              == QStringLiteral("bookmark-a"),
          "return to account A restores account A bookmarks");
    check(firstItem(bridge.annotationsGet(bookId)).value(QStringLiteral("id")).toString()
              == QStringLiteral("annotation-a"),
          "return to account A restores account A annotations");

    Reader2Bridge restarted;
    restarted.useProfilePersonalState(profileA);
    check(restarted.progressGet(bookId) == progressA && restarted.settingsGet() == settingsA,
          "same profile root survives a bridge restart without cross-profile copy");

    bridge.setAuthorizedBook(bookPath);
    bridge.sealPersonalState();
    const quint64 sealedGeneration = bridge.personalStateGeneration();
    check(bridge.personalStateSealed(), "sealed route reports sealed");
    check(bridge.progressGet(bookId).isEmpty() && bridge.settingsGet().isEmpty()
              && bridge.bookmarksGet(bookId).isEmpty() && bridge.annotationsGet(bookId).isEmpty(),
          "sealed route refuses all reader private reads");
    check(bridge.filesRead(bookPath).isEmpty(), "sealed route refuses paper file reads");
    bridge.progressSaveForGeneration(bookId,
                                     QJsonObject{{QStringLiteral("owner"), QStringLiteral("sealed")}},
                                     sealedGeneration);
    bridge.settingsSaveForGeneration(
        QJsonObject{{QStringLiteral("reader2"), QJsonObject{{QStringLiteral("owner"), QStringLiteral("sealed")}}}},
        sealedGeneration);
    check(bridge.progressGet(bookId).isEmpty() && bridge.settingsGet().isEmpty(),
          "sealed route refuses reader private writes");

    bridge.useLegacyPersonalState();
    check(bridge.progressGet(bookId) == legacyProgress && bridge.settingsGet() == legacySettings,
          "explicit legacy-local route still reads preserved legacy JSON");
    check(BookStores::readStore(QStringLiteral("display_names.json"))
              .value(QStringLiteral("library-label")).toString() == QStringLiteral("shared"),
          "display_names remains shared legacy library metadata");

    QDir(legacyRoot).removeRecursively();
    std::printf(fails ? "VERDICT: FAIL\n" : "VERDICT: PASS\n");
    return fails ? 1 : 0;
}
