#include "webui/feeds/TheatreDetailProjection.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class TheatreDetailFeedTest : public QObject {
    Q_OBJECT
private:
    QVariantMap fixture(const QString &name) const {
        QFile file(QStringLiteral(COLOSSEUM_WEBUI_FIXTURE_DIR) + QLatin1Char('/') + name + QStringLiteral(".json"));
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("meta")).toObject().toVariantMap();
    }
    QVariantMap section(const QVariantList &sections, const QString &id) const {
        for (const QVariant &value : sections) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("id")) == id) return row;
        }
        return {};
    }
private slots:
    void movie() {
        const QVariantMap meta = fixture(QStringLiteral("movie"));
        QVERIFY(!meta.isEmpty());
        FeedContext ctx;
        ctx.visibleCount = 0;
        ctx.params = {{QStringLiteral("id"), QStringLiteral("tt0133093")},
                      {QStringLiteral("type"), QStringLiteral("movie")}};
        const QVariantList sections = TheatreDetailProjection::build(ctx, meta, {}, {});
        QCOMPARE(sections.size(), 7);
        QStringList ids;
        for (const QVariant &value : sections) ids.append(value.toMap().value(QStringLiteral("id")).toString());
        QCOMPARE(ids, QStringList({QStringLiteral("hero"), QStringLiteral("seasons"), QStringLiteral("episodes"),
                                   QStringLiteral("cast"), QStringLiteral("related"), QStringLiteral("facts"),
                                   QStringLiteral("sources")}));
        const QVariantMap hero = section(sections, QStringLiteral("hero"));
        QCOMPARE(hero.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        const QVariantMap data = hero.value(QStringLiteral("data")).toMap();
        QCOMPARE(data.value(QStringLiteral("schema")).toString(), QStringLiteral("theatre.hero"));
        QCOMPARE(data.value(QStringLiteral("title")).toString(), QStringLiteral("The Matrix"));
        QCOMPARE(data.value(QStringLiteral("rating")).toString(), QStringLiteral("8.7"));
        const QVariantList scores = data.value(QStringLiteral("scores")).toList();
        QCOMPARE(scores.size(), 1);
        QCOMPARE(scores.first().toMap().value(QStringLiteral("provider")).toString(), QStringLiteral("imdb"));
        QCOMPARE(scores.first().toMap().value(QStringLiteral("value")).toDouble(), 8.7);
        QCOMPARE(data.value(QStringLiteral("watchedMark")).toInt(), 0);
        QCOMPARE(data.value(QStringLiteral("primaryLabel")).toString(), QStringLiteral("Play"));
        QCOMPARE(data.value(QStringLiteral("primaryTargetId")).toString(), QStringLiteral("tt0133093"));
        QVERIFY(!data.value(QStringLiteral("synopsis")).toString().isEmpty());
        QCOMPARE(section(sections, QStringLiteral("seasons")).value(QStringLiteral("state")).toString(), QStringLiteral("empty"));
        QCOMPARE(section(sections, QStringLiteral("related")).value(QStringLiteral("state")).toString(), QStringLiteral("empty"));
    }
    void series() {
        const QVariantMap meta = fixture(QStringLiteral("series"));
        QVERIFY(!meta.isEmpty());
        FeedContext ctx;
        ctx.visibleCount = 0;
        ctx.params = {{QStringLiteral("id"), QStringLiteral("tt0944947")},
                      {QStringLiteral("type"), QStringLiteral("series")}};
        ctx.nativeSnapshot.insert(QStringLiteral("downloadStates"), QVariantMap{
            {QStringLiteral("tt0944947:2:1"), QVariantMap{
                {QStringLiteral("state"), QStringLiteral("downloading")},
                {QStringLiteral("progress"), 0.35}}}});
        const QVariantList related{{QVariantMap{{QStringLiteral("id"), QStringLiteral("tt0000001")}}}};
        const QVariantList sections = TheatreDetailProjection::build(ctx, meta, related, {});
        QCOMPARE(section(sections, QStringLiteral("hero")).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QCOMPARE(section(sections, QStringLiteral("seasons")).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QCOMPARE(section(sections, QStringLiteral("seasons")).value(QStringLiteral("data")).toMap()
                     .value(QStringLiteral("absoluteAvailable")).toBool(), false);
        const QVariantMap episodes = section(sections, QStringLiteral("episodes")).value(QStringLiteral("data")).toMap();
        QVERIFY(!episodes.value(QStringLiteral("rows")).toList().isEmpty());
        QVERIFY(!episodes.value(QStringLiteral("nextUpId")).toString().isEmpty());
        const QVariantMap firstEpisode = episodes.value(QStringLiteral("rows")).toList().first().toMap();
        QCOMPARE(firstEpisode.value(QStringLiteral("downloadState")).toString(), QStringLiteral("downloading"));
        QCOMPARE(firstEpisode.value(QStringLiteral("downloadProgress")).toDouble(), 0.35);
        const QVariantMap hero = section(sections, QStringLiteral("hero")).value(QStringLiteral("data")).toMap();
        QCOMPARE(hero.value(QStringLiteral("primaryLabel")).toString(), QStringLiteral("Play S2 E1"));
        QCOMPARE(hero.value(QStringLiteral("primaryTargetId")).toString(), episodes.value(QStringLiteral("nextUpId")).toString());
        QCOMPARE(section(sections, QStringLiteral("related")).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        ctx.params.insert(QStringLiteral("view"), QVariantMap{{QStringLiteral("order"), QStringLiteral("absolute")}});
        QCOMPARE(section(TheatreDetailProjection::build(ctx, meta, related, {}), QStringLiteral("seasons"))
                     .value(QStringLiteral("data")).toMap().value(QStringLiteral("order")).toString(),
                 QStringLiteral("seasons"));
    }
    void animeMalIdentity() {
        const QVariantMap meta = fixture(QStringLiteral("anime"));
        QVERIFY(!meta.isEmpty());
        FeedContext ctx;
        ctx.visibleCount = 0;
        ctx.params = {{QStringLiteral("id"), QStringLiteral("mal:21")},
                      {QStringLiteral("type"), QStringLiteral("series")}};
        ctx.nativeSnapshot.insert(QStringLiteral("malScore"),
            QVariantMap{{QStringLiteral("score"), 8.73}, {QStringLiteral("scored_by"), 1730000}});
        ctx.nativeSnapshot.insert(QStringLiteral("watchedMark"), 1);
        const QVariantList sections = TheatreDetailProjection::build(ctx, meta, {}, {});
        const QVariantMap hero = section(sections, QStringLiteral("hero")).value(QStringLiteral("data")).toMap();
        QCOMPARE(hero.value(QStringLiteral("id")).toString(), QStringLiteral("mal:21"));
        QCOMPARE(hero.value(QStringLiteral("title")).toString(), QStringLiteral("One Piece"));
        QCOMPARE(hero.value(QStringLiteral("kind")).toString(), QStringLiteral("anime"));
        QCOMPARE(section(sections, QStringLiteral("seasons")).value(QStringLiteral("data")).toMap()
                     .value(QStringLiteral("absoluteAvailable")).toBool(), false);
        const QVariantList scores = hero.value(QStringLiteral("scores")).toList();
        QCOMPARE(scores.size(), 2);
        QCOMPARE(scores.at(0).toMap().value(QStringLiteral("provider")).toString(), QStringLiteral("mal"));
        QCOMPARE(scores.at(0).toMap().value(QStringLiteral("value")).toDouble(), 8.73);
        QCOMPARE(scores.at(0).toMap().value(QStringLiteral("votes")).toInt(), 1730000);
        QCOMPARE(scores.at(1).toMap().value(QStringLiteral("provider")).toString(), QStringLiteral("imdb"));
        QCOMPARE(hero.value(QStringLiteral("watchedMark")).toInt(), 1);
        QVERIFY(!section(sections, QStringLiteral("episodes")).value(QStringLiteral("data")).toMap()
                     .value(QStringLiteral("rows")).toList().isEmpty());
    }
    void animeAbsoluteRequiresCompleteMapping() {
        const QVariantMap meta = fixture(QStringLiteral("anime"));
        QVERIFY(!meta.isEmpty());
        QVariantList mapped = meta.value(QStringLiteral("videos")).toList();
        QVERIFY(!mapped.isEmpty());
        for (int i = 0; i < mapped.size(); ++i) {
            QVariantMap episode = mapped.at(i).toMap();
            episode.insert(QStringLiteral("absoluteNumber"), i + 1);
            mapped[i] = episode;
        }
        FeedContext ctx;
        ctx.params = {{QStringLiteral("id"), QStringLiteral("mal:21")},
                      {QStringLiteral("type"), QStringLiteral("series")},
                      {QStringLiteral("view"), QVariantMap{{QStringLiteral("order"), QStringLiteral("absolute")}}}};
        ctx.nativeSnapshot.insert(QStringLiteral("animeOrder"),
            QVariantMap{{QStringLiteral("absoluteComplete"), true},
                        {QStringLiteral("defaultOrder"), QStringLiteral("seasons")},
                        {QStringLiteral("episodes"), mapped}});
        const QVariantList sections = TheatreDetailProjection::build(ctx, meta, {}, {});
        const QVariantMap seasons = section(sections, QStringLiteral("seasons")).value(QStringLiteral("data")).toMap();
        QVERIFY(seasons.value(QStringLiteral("absoluteAvailable")).toBool());
        QCOMPARE(seasons.value(QStringLiteral("order")).toString(), QStringLiteral("absolute"));
        const QVariantList rows = section(sections, QStringLiteral("episodes")).value(QStringLiteral("data"))
                                      .toMap().value(QStringLiteral("rows")).toList();
        QVERIFY(!rows.isEmpty());
        QCOMPARE(rows.first().toMap().value(QStringLiteral("displayNumber")).toInt(), 1);
    }
    void episodeWindowReaches250() {
        QVariantMap meta = fixture(QStringLiteral("series"));
        QVERIFY(!meta.isEmpty());
        const QVariantMap sample = meta.value(QStringLiteral("videos")).toList().first().toMap();
        QVariantList videos;
        for (int i = 1; i <= 250; ++i) {
            QVariantMap row = sample;
            row.insert(QStringLiteral("id"), QStringLiteral("tt-test:1:%1").arg(i));
            row.insert(QStringLiteral("season"), 1);
            row.insert(QStringLiteral("episode"), i);
            videos.append(row);
        }
        meta.insert(QStringLiteral("videos"), videos);
        FeedContext ctx;
        ctx.params = {{QStringLiteral("id"), QStringLiteral("tt-test")},
                      {QStringLiteral("type"), QStringLiteral("series")}};
        for (int start : {0, 100, 200}) {
            ctx.visibleCount = start;
            const QVariantMap section = this->section(TheatreDetailProjection::build(ctx, meta, {}, {}),
                                                       QStringLiteral("episodes"));
            const QVariantMap data = section.value(QStringLiteral("data")).toMap();
            QCOMPARE(data.value(QStringLiteral("windowStart")).toInt(), start);
            QCOMPARE(data.value(QStringLiteral("rows")).toList().size(), start == 200 ? 50 : 100);
            QCOMPARE(section.value(QStringLiteral("hasMore")).toBool(), start != 200);
        }
        const QVariantMap last = this->section(TheatreDetailProjection::build(ctx, meta, {}, {}),
                                               QStringLiteral("episodes")).value(QStringLiteral("data"))
                                               .toMap().value(QStringLiteral("rows")).toList().last().toMap();
        QCOMPARE(last.value(QStringLiteral("id")).toString(), QStringLiteral("tt-test:1:250"));
    }
    void seriesResumeLabel() {
        const QVariantMap meta = fixture(QStringLiteral("series"));
        QVERIFY(!meta.isEmpty());
        FeedContext ctx;
        ctx.params = {{QStringLiteral("id"), QStringLiteral("tt0944947")},
                      {QStringLiteral("type"), QStringLiteral("series")}};
        ctx.nativeSnapshot.insert(QStringLiteral("recent"), QVariantList{
            QVariantMap{{QStringLiteral("id"), QStringLiteral("tt0944947:2:1")},
                        {QStringLiteral("progress"), 0.42}}});
        const QVariantMap hero = section(TheatreDetailProjection::build(ctx, meta, {}, {}),
                                         QStringLiteral("hero")).value(QStringLiteral("data")).toMap();
        QCOMPARE(hero.value(QStringLiteral("primaryLabel")).toString(), QStringLiteral("Resume S2 E1"));
        QCOMPARE(hero.value(QStringLiteral("primaryTargetId")).toString(), QStringLiteral("tt0944947:2:1"));
    }
    void missingScores() {
        QVariantMap meta = fixture(QStringLiteral("movie"));
        QVERIFY(!meta.isEmpty());
        meta.remove(QStringLiteral("imdbRating"));
        FeedContext ctx;
        ctx.params = {{QStringLiteral("id"), QStringLiteral("tt0133093")},
                      {QStringLiteral("type"), QStringLiteral("movie")}};
        const QVariantMap hero = section(TheatreDetailProjection::build(ctx, meta, {}, {}),
                                         QStringLiteral("hero")).value(QStringLiteral("data")).toMap();
        QVERIFY(hero.value(QStringLiteral("scores")).toList().isEmpty());
    }
    void informationOmitsMissingFields() {
        QVariantMap meta = fixture(QStringLiteral("movie"));
        QVERIFY(!meta.isEmpty());
        meta.remove(QStringLiteral("year"));
        meta.remove(QStringLiteral("runtime"));
        meta.remove(QStringLiteral("genres"));
        meta.remove(QStringLiteral("imdbRating"));
        FeedContext ctx;
        ctx.params = {{QStringLiteral("id"), QStringLiteral("tt0133093")},
                      {QStringLiteral("type"), QStringLiteral("movie")}};
        const QVariantMap empty = section(TheatreDetailProjection::build(ctx, meta, {}, {}),
                                          QStringLiteral("facts"));
        QCOMPARE(empty.value(QStringLiteral("state")).toString(), QStringLiteral("empty"));
        QVERIFY(empty.value(QStringLiteral("data")).toMap().value(QStringLiteral("rows")).toList().isEmpty());
        meta.insert(QStringLiteral("status"), QStringLiteral("Released"));
        const QVariantList rows = section(TheatreDetailProjection::build(ctx, meta, {}, {}),
                                          QStringLiteral("facts")).value(QStringLiteral("data"))
                                          .toMap().value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Status"));
        QCOMPARE(rows.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("Released"));
    }
};

QTEST_GUILESS_MAIN(TheatreDetailFeedTest)
#include "tst_theatre_detail_feed.moc"
