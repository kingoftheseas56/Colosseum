#include <QtTest>

#include "engine/MangaPageTransport.h"
#include "engine/TankoyomiIdentity.h"

#include <QNetworkRequest>
#include <QVariantList>
#include <QVariantMap>

class tst_manga_page_transport final : public QObject
{
    Q_OBJECT

private slots:
    void qualifiedPagesDoNotInventRefererFromChapterLocator();
    void explicitPageRefererWins();
    void qualifiedPageWithoutLocatorNeverUsesWeebCentralReferer();
    void legacyPageKeepsWeebCentralRefererFallback();
};

void tst_manga_page_transport::qualifiedPagesDoNotInventRefererFromChapterLocator()
{
    const QVariantMap chapter{
        {QStringLiteral("id"), QStringLiteral("123")},
        {QStringLiteral("url"), QStringLiteral("https://zonatmo.org/view_uploads/123")}
    };
    const QString qualified = TankoyomiIdentity::qualifyChapter(
        QStringLiteral("es"), QStringLiteral("zonatmo"), chapter);
    const QVariantList rows{QVariantMap{
        {QStringLiteral("index"), 0},
        {QStringLiteral("url"), QStringLiteral("https://storage1.zonatmo.org/chapters/a/1.jpg")}
    }};

    const QList<PageInfo> pages = MangaPageTransport::normalizeTankoyomiPages(rows, qualified);
    QCOMPARE(pages.size(), 1);
    QVERIFY(pages.first().referer.isEmpty());

    const QNetworkRequest request = MangaPageTransport::requestForPage(pages.first(), qualified);
    QVERIFY(request.rawHeader("Referer").isEmpty());
}

void tst_manga_page_transport::explicitPageRefererWins()
{
    const QVariantMap chapter{
        {QStringLiteral("id"), QStringLiteral("123")},
        {QStringLiteral("url"), QStringLiteral("https://provider.example/chapter/123")}
    };
    const QString qualified = TankoyomiIdentity::qualifyChapter(
        QStringLiteral("fr"), QStringLiteral("fixture"), chapter);
    const QVariantList rows{QVariantMap{
        {QStringLiteral("url"), QStringLiteral("https://cdn.example/page.jpg")},
        {QStringLiteral("referer"), QStringLiteral("https://provider.example/reader/123")}
    }};

    const QList<PageInfo> pages = MangaPageTransport::normalizeTankoyomiPages(rows, qualified);
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.first().referer, QStringLiteral("https://provider.example/reader/123"));
}

void tst_manga_page_transport::qualifiedPageWithoutLocatorNeverUsesWeebCentralReferer()
{
    const QVariantMap chapter{{QStringLiteral("id"), QStringLiteral("no-locator")}};
    const QString qualified = TankoyomiIdentity::qualifyChapter(
        QStringLiteral("pt"), QStringLiteral("fixture"), chapter);
    PageInfo page;
    page.imageUrl = QStringLiteral("https://cdn.example/page.jpg");

    const QNetworkRequest request = MangaPageTransport::requestForPage(page, qualified);
    QVERIFY(request.rawHeader("Referer").isEmpty());
}

void tst_manga_page_transport::legacyPageKeepsWeebCentralRefererFallback()
{
    PageInfo page;
    page.imageUrl = QStringLiteral("https://hot.planeptune.us/manga/page.jpg");

    const QNetworkRequest request = MangaPageTransport::requestForPage(
        page, QStringLiteral("01LEGACYCHAPTER"));
    QCOMPARE(request.rawHeader("Referer"), QByteArray("https://weebcentral.com/"));
}

QTEST_GUILESS_MAIN(tst_manga_page_transport)
#include "tst_manga_page_transport.moc"
