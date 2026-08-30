#include "reader/BookStores.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QJsonObject>
#include <cstdio>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    int fails = 0;
    auto check = [&](bool ok, const char* what){ if(!ok){std::printf("FAIL %s\n",what);++fails;} else std::printf("ok   %s\n",what); };
    QJsonObject p{{"cfi","epubcfi(/6/4!/4/2)"},{"percent",42}};
    BookStores::save(QStringLiteral("progress.json"), QStringLiteral("bk1"), p);
    check(BookStores::get(QStringLiteral("progress.json"), QStringLiteral("bk1")).value("percent").toInt()==42, "progress roundtrip");
    QJsonObject p2{{"cfi","epubcfi(/6/8!/4/2)"},{"percent",84}};
    BookStores::save(QStringLiteral("progress.json"), QStringLiteral("bk1"), p2);
    const QJsonObject replaced = BookStores::get(QStringLiteral("progress.json"), QStringLiteral("bk1"));
    check(replaced.value("percent").toInt()==84, "progress replacement roundtrip");
    check(replaced.value("cfi").toString()==QStringLiteral("epubcfi(/6/8!/4/2)"), "progress replacement remains valid JSON object");
    QJsonObject bm{{"id","b1"},{"cfi","epubcfi(/6/4!/4/8)"},{"snippet","damp, drizzly"}};
    BookStores::listSave(QStringLiteral("bookmarks.json"), QStringLiteral("bk1"), bm);
    check(BookStores::listGet(QStringLiteral("bookmarks.json"), QStringLiteral("bk1")).size()==1, "bookmark listSave/listGet");
    BookStores::listDelete(QStringLiteral("bookmarks.json"), QStringLiteral("bk1"), QStringLiteral("b1"));
    check(BookStores::listGet(QStringLiteral("bookmarks.json"), QStringLiteral("bk1")).isEmpty(), "bookmark listDelete");
    std::printf(fails ? "VERDICT: FAIL\n" : "VERDICT: PASS\n");
    return fails ? 1 : 0;
}
