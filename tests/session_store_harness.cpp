#include "SessionStore.h"

#include <QCoreApplication>
#include <QObject>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QVariantMap comicDesc(const QString &title,
                      const QString &seriesId,
                      const QString &chapterId,
                      const QString &entryKind,
                      const QString &marker = {})
{
    QVariantMap target{{QStringLiteral("title"), title},
                       {QStringLiteral("seriesId"), seriesId},
                       {QStringLiteral("chapterId"), chapterId},
                       {QStringLiteral("entryKind"), entryKind}};
    if (!marker.isEmpty())
        target.insert(QStringLiteral("marker"), marker);
    return {{QStringLiteral("appType"), QStringLiteral("tankoban")},
            {QStringLiteral("contentKind"), QStringLiteral("comic")},
            {QStringLiteral("title"), title},
            {QStringLiteral("target"), target}};
}

QVariantMap showDesc(const QString &showKey,
                     const QString &subId,
                     const QString &infoHash,
                     int fileIdx,
                     const QString &title,
                     const QString &url = {})
{
    QVariantMap target{{QStringLiteral("showKey"), showKey},
                       {QStringLiteral("subId"), subId},
                       {QStringLiteral("infoHash"), infoHash},
                       {QStringLiteral("fileIdx"), fileIdx}};
    if (!url.isEmpty())
        target.insert(QStringLiteral("streamUrl"), url);
    return {{QStringLiteral("appType"), QStringLiteral("theatre")},
            {QStringLiteral("contentKind"), QStringLiteral("movie")},
            {QStringLiteral("title"), title},
            {QStringLiteral("target"), target}};
}

QVariantMap rawTorrentDesc(const QString &hash, int fileIdx, const QString &title)
{
    return {{QStringLiteral("appType"), QStringLiteral("theatre")},
            {QStringLiteral("contentKind"), QStringLiteral("movie")},
            {QStringLiteral("title"), title},
            {QStringLiteral("target"), QVariantMap{{QStringLiteral("infoHash"), hash},
                                                   {QStringLiteral("fileIdx"), fileIdx}}}};
}

QVariantMap bookDesc(const QString &id, const QString &path, const QString &title)
{
    return {{QStringLiteral("appType"), QStringLiteral("biblio")},
            {QStringLiteral("contentKind"), QStringLiteral("book")},
            {QStringLiteral("title"), title},
            {QStringLiteral("target"), QVariantMap{{QStringLiteral("id"), id},
                                                   {QStringLiteral("path"), path}}}};
}

void runSuite()
{
    SessionStore store;
    int replacements = 0;
    QObject::connect(&store, &SessionStore::targetReplaced,
                     [&replacements](const QString &) { ++replacements; });

    const QString c1 = store.openOrSwitch(
        comicDesc(QStringLiteral("Shared Title"), QStringLiteral("series-a"),
                  QStringLiteral("1"), QString()));
    store.switchTo(QString());
    const QString c2 = store.openOrSwitch(
        comicDesc(QStringLiteral("Shared Title"), QStringLiteral("series-a"),
                  QStringLiteral("2"), QString()));
    require(c2 != c1, "two chapters of one series are distinct per-content sessions");

    store.switchTo(QString());
    const QString c3 = store.openOrSwitch(
        comicDesc(QStringLiteral("Shared Title"), QStringLiteral("series-b"),
                  QStringLiteral("1"), QString()));
    require(c3 != c1 && c3 != c2,
            "same title and chapter label in another series cannot collide");

    store.switchTo(QString());
    const QString c4 = store.openOrSwitch(
        comicDesc(QStringLiteral("Fallback Series"), QString(),
                  QStringLiteral("7"), QStringLiteral("tankoban")));
    store.switchTo(QString());
    const QString c5 = store.openOrSwitch(
        comicDesc(QStringLiteral("Fallback Series"), QString(),
                  QStringLiteral("8"), QStringLiteral("tankoban")));
    require(c5 != c4,
            "missing series id still retains chapter/volume identity instead of title-only collapse");

    store.saveState(c1, QVariantMap{{QStringLiteral("page"), 8}});
    store.switchTo(QString());
    const QString c1Again = store.openOrSwitch(
        comicDesc(QStringLiteral("Renamed Title"), QStringLiteral("series-a"),
                  QStringLiteral("1"), QString(), QStringLiteral("fresh")));
    require(c1Again == c1, "exact comic content reuses its original session");
    require(store.get(c1).value(QStringLiteral("title")).toString() == QStringLiteral("Renamed Title"),
            "exact-content reopen refreshes visible metadata");
    require(store.get(c1).value(QStringLiteral("target")).toMap()
                .value(QStringLiteral("marker")).toString() == QStringLiteral("fresh"),
            "exact-content reopen refreshes target payload");
    require(store.get(c1).value(QStringLiteral("savedState")).toMap()
                .value(QStringLiteral("page")).toInt() == 8,
            "exact-content descriptor refresh preserves captured state");

    store.switchTo(QString());
    const QString s1 = store.openOrSwitch(
        showDesc(QStringLiteral("tt-show"), QStringLiteral("tt-show:1:1"),
                 QStringLiteral("packhash"), 0, QStringLiteral("Episode 1")));
    store.saveState(s1, QVariantMap{{QStringLiteral("position"), 91}});
    const int beforeReplaceSignals = replacements;
    const QString s2 = store.openOrSwitch(
        showDesc(QStringLiteral("tt-show"), QStringLiteral("tt-show:1:2"),
                 QStringLiteral("packhash"), 1, QStringLiteral("Episode 2")));
    require(s2 == s1, "same show replaces in one taskbar session");
    require(replacements == beforeReplaceSignals + 1,
            "different episode in the same show emits targetReplaced");
    require(store.get(s1).value(QStringLiteral("savedState")).toMap().isEmpty(),
            "different episode clears stale playback state");
    require(store.get(s1).value(QStringLiteral("target")).toMap()
                .value(QStringLiteral("subId")).toString() == QStringLiteral("tt-show:1:2"),
            "same-pack episode replacement installs the new episode target");

    store.saveState(s1, QVariantMap{{QStringLiteral("position"), 37}});
    const int beforeRefreshSignals = replacements;
    const QString s2Refreshed = store.openOrSwitch(
        showDesc(QStringLiteral("tt-show"), QStringLiteral("tt-show:1:2"),
                 QStringLiteral("betterhash"), 7, QStringLiteral("Episode 2 remux"),
                 QStringLiteral("http://127.0.0.1/new")));
    require(s2Refreshed == s1, "same episode keeps the same session id after re-resolution");
    require(replacements == beforeRefreshSignals,
            "same episode transport refresh is not a content replacement");
    require(store.get(s1).value(QStringLiteral("savedState")).toMap()
                .value(QStringLiteral("position")).toInt() == 37,
            "same episode transport refresh preserves playback state");
    require(store.get(s1).value(QStringLiteral("target")).toMap()
                .value(QStringLiteral("infoHash")).toString() == QStringLiteral("betterhash"),
            "same episode transport refresh installs the newest source descriptor");

    store.switchTo(QString());
    const QString r1 = store.openOrSwitch(
        rawTorrentDesc(QStringLiteral("rawpack"), 0, QStringLiteral("Raw 0")));
    store.switchTo(QString());
    const QString r2 = store.openOrSwitch(
        rawTorrentDesc(QStringLiteral("rawpack"), 1, QStringLiteral("Raw 1")));
    require(r2 != r1, "raw files inside one torrent pack remain distinct sessions");
    store.switchTo(QString());
    const QString b1 = store.openOrSwitch(
        bookDesc(QStringLiteral("book-7"), QStringLiteral("C:/old/book.epub"), QStringLiteral("Book")));
    store.saveState(b1, QVariantMap{{QStringLiteral("cfi"), QStringLiteral("epubcfi(/6/8)") }});
    store.switchTo(QString());
    const QString b2 = store.openOrSwitch(
        bookDesc(QStringLiteral("book-7"), QStringLiteral("D:/new/book.epub"), QStringLiteral("Book Revised")));
    require(b2 == b1, "stable book id survives a path/metadata refresh");
    require(store.get(b1).value(QStringLiteral("target")).toMap()
                .value(QStringLiteral("path")).toString() == QStringLiteral("D:/new/book.epub"),
            "book reopen stores the latest path");
    require(store.get(b1).value(QStringLiteral("savedState")).toMap()
                .value(QStringLiteral("cfi")).toString() == QStringLiteral("epubcfi(/6/8)"),
            "book descriptor refresh preserves reader state");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    runSuite();
    std::cout << "SessionStore behavioral tests passed.\n";
    return 0;
}

