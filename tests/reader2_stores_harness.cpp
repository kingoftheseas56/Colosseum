#include "reader/BookStores.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QJsonObject>
#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    int fails = 0;
    auto check = [&](bool ok, const char* what){
        if(!ok){std::printf("FAIL %s\n",what);++fails;}
        else std::printf("ok   %s\n",what);
    };

    const QString bookPath = QStringLiteral("C:/x/y.epub");
    const QString bookId = BookStores::keyFor(bookPath);
    check(bookId == QStringLiteral("c6c28cd2ca56ec13e016"),
          "canonical Reader2 bookId remains SHA1[:20] of normalized path");

    QJsonObject p{{"cfi","epubcfi(/6/4!/4/2)"},{"percent",42}};
    BookStores::save(QStringLiteral("progress.json"), bookId, p);
    check(BookStores::get(QStringLiteral("progress.json"), bookId).value("percent").toInt()==42,
          "progress roundtrip under canonical bookId");

    QJsonObject p2{{"cfi","epubcfi(/6/8!/4/2)"},{"percent",84}};
    BookStores::save(QStringLiteral("progress.json"), bookId, p2);
    const QJsonObject replaced = BookStores::get(QStringLiteral("progress.json"), bookId);
    check(replaced.value("percent").toInt()==84, "progress replacement roundtrip");
    check(replaced.value("cfi").toString()==QStringLiteral("epubcfi(/6/8!/4/2)"),
          "progress replacement remains valid JSON object");

    QJsonObject bm{{"id","b1"},{"cfi","epubcfi(/6/4!/4/8)"},{"snippet","shared bookmark"}};
    BookStores::listSave(QStringLiteral("bookmarks.json"), bookId, bm);
    check(BookStores::listGet(QStringLiteral("bookmarks.json"), bookId).size()==1,
          "bookmark listSave/listGet under canonical bookId");
    BookStores::listDelete(QStringLiteral("bookmarks.json"), bookId, QStringLiteral("b1"));
    check(BookStores::listGet(QStringLiteral("bookmarks.json"), bookId).isEmpty(),
          "bookmark listDelete under canonical bookId");

    QJsonObject hl{{"id","h1"},{"cfi","epubcfi(/6/4!/4/12)"},
                   {"text","shared highlight"},{"color","#F0C24A"}};
    BookStores::listSave(QStringLiteral("annotations.json"), bookId, hl);
    check(BookStores::listGet(QStringLiteral("annotations.json"), bookId).size()==1,
          "annotation listSave/listGet under canonical bookId");

    const QJsonObject settings{{"reader2", QJsonObject{{"theme","night"},{"sizePct",115}}}};
    BookStores::writeStore(QStringLiteral("settings.json"), settings);
    check(BookStores::readStore(QStringLiteral("settings.json")) == settings,
          "Reader2 settings whole-store roundtrip unchanged");

    std::printf(fails ? "VERDICT: FAIL\n" : "VERDICT: PASS\n");
    return fails ? 1 : 0;
}
