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
        QCOMPARE(ids, QStringList({QStringLiteral("hero"), QStringLiteral("facts"), QStringLiteral("seasons"),
                                   QStringLiteral("episodes"), QStringLiteral("cast"), QStringLiteral("sources"),
                                   QStringLiteral("related")}));
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
        QCOMPARE(data.value(QStringLiteral("primaryLabel")).toString(), QStringLiteral("Watch"));
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
        const QVariantList related{{QVariantMap{{QStringLiteral("id"), QStringLiteral("tt0000001")}}}};
        const QVariantList sections = TheatreDetailProjection::build(ctx, meta, related, {});
        QCOMPARE(section(sections, QStringLiteral("hero")).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QCOMPARE(section(sections, QStringLiteral("seasons")).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        const QVariantMap episodes = section(sections, QStringLiteral("episodes")).value(QStringLiteral("data")).toMap();
        QVERIFY(!episodes.value(QStringLiteral("rows")).toList().isEmpty());
        QVERIFY(!episodes.value(QStringLiteral("nextUpId")).toString().isEmpty());
        QCOMPARE(section(sections, QStringLiteral("related")).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
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
};

QTEST_GUILESS_MAIN(TheatreDetailFeedTest)
#include "tst_theatre_detail_feed.moc"
