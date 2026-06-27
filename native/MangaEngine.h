#pragma once

// QML bridge over the native WeebCentral scraper (lifted from Tankoban 2's proven
// engine). QML calls the Q_INVOKABLEs and receives results as plain JS arrays/objects
// via the signals — so the QML side never sees a C++ struct. Uses its OWN fresh
// QNetworkAccessManager (no PreferCache) so scrape responses are never served stale.

#include "WeebCentralScraper.h"
#include "MangaSeriesDetail.h"
#include "MangaFireCatalogClient.h"

#include <QObject>
#include <QNetworkAccessManager>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

class MangaEngine : public QObject {
    Q_OBJECT
public:
    explicit MangaEngine(QObject *parent = nullptr) : QObject(parent) {
        m_nam = new QNetworkAccessManager(this);
        m_wc = new WeebCentralScraper(m_nam, this);
        m_mf = new tankoban::manga::mangafire::MangaFireCatalogClient(m_nam, this);

        // MangaFire is the VOLUME-structure source: clean per-volume covers + chapter
        // ranges, handed straight to QML (no MangaDex-style reconstruction). Best-effort:
        // any failure → empty list, and the volume selector hides.
        connect(m_mf, &tankoban::manga::mangafire::MangaFireCatalogClient::catalogReady,
                this, [this](const QString &, const QVariantList &volumes) {
                    emit volumesResult(QVariantMap{{"volumes", volumes}});
                });
        connect(m_mf, &tankoban::manga::mangafire::MangaFireCatalogClient::catalogFailed,
                this, [this](const QString &title, const QString &reason) {
                    qInfo("[mangafire] volumes failed for '%s': %s",
                          qUtf8Printable(title), qUtf8Printable(reason));
                    emit volumesResult(QVariantMap{{"volumes", QVariantList{}}});
                });

        connect(m_wc, &MangaScraper::searchFinished, this, [this](const QList<MangaResult> &r) {
            QVariantList out;
            for (const auto &m : r)
                out.append(QVariantMap{{"id", m.id}, {"url", m.url}, {"title", m.title},
                                       {"author", m.author}, {"cover", m.thumbnailUrl},
                                       {"status", m.status}});
            emit searchResults(out);
        });
        connect(m_wc, &MangaScraper::chaptersReady, this, [this](const QList<ChapterInfo> &r) {
            QVariantList out;
            for (const auto &c : r)
                out.append(QVariantMap{{"id", c.id}, {"name", c.name}, {"number", c.chapterNumber},
                                       {"date", QVariant::fromValue<qlonglong>(c.dateUpload)},
                                       {"volumeScanned", c.isVolumeScanned}});
            emit chaptersResults(out);
        });
        connect(m_wc, &MangaScraper::detailReady, this, [this](const MangaSeriesDetail &d) {
            emit detailResult(QVariantMap{{"synopsis", d.synopsis}, {"genres", QVariant(d.genres)},
                                          {"year", d.year}, {"status", d.status},
                                          {"author", d.author}, {"cover", d.heroCoverUrl}});
        });
        connect(m_wc, &MangaScraper::errorOccurred, this,
                [this](const QString &e) { emit engineError(e); });
    }

    // QML entry points. Results arrive on the matching signal (async).
    Q_INVOKABLE void search(const QString &query) { m_wc->search(query); }
    Q_INVOKABLE void chapters(const QString &seriesId) { m_wc->fetchChapters(seriesId); }
    Q_INVOKABLE void detail(const QString &id, const QString &url, const QString &title,
                            const QString &cover) {
        MangaResult p;
        p.id = id; p.url = url; p.title = title; p.thumbnailUrl = cover;
        p.source = "weebcentral"; p.type = "manga";
        m_wc->fetchDetail(p);
    }

    // AniList GraphQL — the rich metadata layer: banner, hi-res cover, synopsis, genres, score.
    // ALWAYS emits artResult exactly once (empty map on miss/error) so QML can gate the page
    // reveal on art being resolved — the page must never reveal half-loaded and then reflow.
    Q_INVOKABLE void art(const QString &title) {
        QNetworkRequest req{QUrl(QStringLiteral("https://graphql.anilist.co"))};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Accept", "application/json");
        const QString query =
            QStringLiteral("query($s:String){Media(search:$s,type:MANGA){bannerImage "
                           "coverImage{extraLarge large} description(asHtml:false) genres "
                           "averageScore status startDate{year}}}");
        const QJsonObject body{{"query", query}, {"variables", QJsonObject{{"s", title}}}};
        auto *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) { emit artResult(QVariantMap{}); return; }
            const QJsonObject m = QJsonDocument::fromJson(reply->readAll())
                                      .object().value("data").toObject().value("Media").toObject();
            if (m.isEmpty()) { emit artResult(QVariantMap{}); return; }
            QVariantList genres;
            const QJsonArray ga = m.value("genres").toArray();
            for (const auto &g : ga) genres.append(g.toString());
            QString desc = m.value("description").toString();
            desc.remove(QRegularExpression("<[^>]*>"));   // AniList wraps in <i>/<br>
            emit artResult(QVariantMap{
                {"banner", m.value("bannerImage").toString()},
                {"cover", m.value("coverImage").toObject().value("extraLarge").toString()},
                {"description", desc.trimmed()},
                {"genres", genres},
                {"score", m.value("averageScore").toInt()},
                {"year", m.value("startDate").toObject().value("year").toInt()}});
        });
    }

    // VOLUME structure — delegated to the MangaFire client (sitemap → /ajax volume + chapter).
    // Result arrives on volumesResult as {volumes: [{number, cover, chapterStart, chapterEnd}]}.
    Q_INVOKABLE void volumes(const QString &title) { m_mf->fetchByTitle(title); }

    // Dev self-test: resolve a title end-to-end and log the chapter count, so we can
    // confirm the in-app C++ path matches the curl de-risk.
    void selfTest(const QString &title) {
        auto *probe = new WeebCentralScraper(m_nam, this);
        connect(probe, &MangaScraper::searchFinished, this,
                [this, probe, title](const QList<MangaResult> &r) {
                    if (r.isEmpty()) { qInfo("[manga] self-test '%s': 0 results", qUtf8Printable(title)); return; }
                    const QString sid = r.first().id;
                    connect(probe, &MangaScraper::chaptersReady, this,
                            [title, sid](const QList<ChapterInfo> &ch) {
                                qInfo("[manga] self-test '%s' (%s): %d chapters",
                                      qUtf8Printable(title), qUtf8Printable(sid), int(ch.size()));
                            });
                    probe->fetchChapters(sid);
                });
        probe->search(title);
    }

signals:
    void searchResults(const QVariantList &results);
    void chaptersResults(const QVariantList &chapters);
    void detailResult(const QVariantMap &detail);
    void artResult(const QVariantMap &art);
    void volumesResult(const QVariantMap &result);
    void engineError(const QString &message);

private:
    QNetworkAccessManager *m_nam;
    WeebCentralScraper *m_wc;
    tankoban::manga::mangafire::MangaFireCatalogClient *m_mf;
};
