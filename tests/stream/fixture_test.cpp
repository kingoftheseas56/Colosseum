#include "SourceScorer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTextStream>
#include <cmath>
using namespace colosseum::stream;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            qFatal("line %d: %s", __LINE__, #x);                                                   \
    } while (false)
QJsonObject readObject(const QString &path) {
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    CHECK(error.error == QJsonParseError::NoError);
    CHECK(doc.isObject());
    return doc.object();
}
QJsonObject scoredJson(const ScoredStream &s) {
    QJsonArray reasons;
    for (const auto &r : s.reasons)
        reasons.append(QJsonObject{{"signal", r.signal}, {"delta", r.delta}});
    return {{"rowIndex", s.parsed.stream.input_index},
            {"filename", s.parsed.filename},
            {"parsedTitle", s.parsed.parsed_title},
            {"score", s.score},
            {"reasons", reasons}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QDir fixtures(QString::fromUtf8(STREAM_FIXTURE_DIR));
    const auto manifest = readObject(fixtures.filePath("manifest.json"));
    const auto cases = manifest.value("fixtures").toArray();
    CHECK(cases.size() >= 20);
    int positive = 0, total = 0, movies = 0, anime = 0;
    QJsonArray report;
    for (const auto &v : cases) {
        const auto c = v.toObject();
        const auto path = c.value("responseFile").toString();
        if (path.isEmpty())
            continue; // Explicit unsuccessful transport probes are not fixtures.
        const auto data = readObject(fixtures.filePath(path));
        CHECK(data.value("streams").isArray());
        const auto rows = data.value("streams").toArray();
        QList<Stream> streams;
        for (int i = 0; i < rows.size(); ++i) {
            CHECK(rows[i].isObject());
            streams.append(
                stream_from_json(rows[i].toObject(), i, c.value("provider").toString(), 0));
        }
        TrustOptions trust;
        trust.kind = c.value("kind").toString();
        trust.expected_title = c.value("title").toString();
        trust.imdb_id = c.value("imdbId").toString();
        trust.is_anime = c.value("isAnime").toBool();
        trust.today = QDate(2026, 11, 8);
        trust.release_date =
            QDate::fromString(c.value("releaseDate").toString().left(10), Qt::ISODate);
        const auto context = c.value("context").toObject();
        if (context.value("expected_year").isDouble())
            trust.expected_year = context.value("expected_year").toInt();
        else if (trust.release_date.isValid())
            trust.expected_year = trust.release_date.year();
        if (c.value("season").isDouble())
            trust.expected_season = c.value("season").toInt();
        if (c.value("episode").isDouble())
            trust.expected_episode = c.value("episode").toInt();
        for (const auto &alias : c.value("expectedTitles").toArray())
            trust.expected_titles.append(alias.toString());
        ScoreOptions options;
        options.today = trust.today;
        options.release_date = trust.release_date;
        options.media_kind = trust.kind;
        options.episode_requested = trust.expected_episode.has_value();
        for (const auto &language : context.value("preferred_languages").toArray())
            options.preferred_languages.append(language.toString());
        if (c.value("runtimeMinutes").toInt() > 0)
            options.runtime_minutes = c.value("runtimeMinutes").toInt();
        const auto ranked = SourceScorer::evaluate(streams, trust, options);
        const auto again = SourceScorer::evaluate(streams, trust, options);
        CHECK(ranked.all.size() + ranked.rejected.size() == rows.size());
        CHECK(ranked.primary.has_value() == again.primary.has_value());
        // This captured recent-film pool consists of CAM/TS/TC metadata only.
        // This is an independently inspectable safety assertion, not a human-choice label.
        if (c.value("id").toString() == "clayface")
            CHECK(!ranked.primary);
        if (ranked.primary) {
            CHECK(ranked.primary->parsed.stream.input_index ==
                  again.primary->parsed.stream.input_index);
            CHECK(!ranked.primary->reasons.isEmpty());
        }
        for (int i = 0; i < ranked.all.size(); ++i) {
            CHECK(std::isfinite(ranked.all[i].score));
            if (i)
                CHECK(ranked.all[i - 1].score >= ranked.all[i].score);
        }
        for (const auto &r : ranked.rejected)
            CHECK(!r.reason.isEmpty());
        for (auto it = ranked.by_tier.begin(); it != ranked.by_tier.end(); ++it)
            CHECK(std::isfinite(it.value().score));
        QJsonArray rejected;
        for (const auto &r : ranked.rejected)
            rejected.append(
                QJsonObject{{"rowIndex", r.stream.stream.input_index}, {"reason", r.reason}});
        QJsonArray top;
        for (int i = 0; i < qMin(5, ranked.all.size()); ++i)
            top.append(scoredJson(ranked.all[i]));
        report.append(
            QJsonObject{{"id", c.value("id")},
                        {"title", trust.expected_title},
                        {"provider", c.value("provider")},
                        {"rows", rows.size()},
                        {"kept", ranked.all.size()},
                        {"rejected", rejected},
                        {"primary", ranked.primary ? QJsonValue(scoredJson(*ranked.primary))
                                                   : QJsonValue(QJsonValue::Null)},
                        {"topFive", top},
                        {"selectionReasons", QJsonArray::fromStringList(ranked.selection_reasons)},
                        {"humanPick", QJsonValue::Null},
                        {"humanComparison", "PENDING: no human judgement supplied"}});
        ++total;
        if (!rows.isEmpty())
            ++positive;
        if (trust.kind == "movie")
            ++movies;
        if (trust.is_anime)
            ++anime;
    }
    CHECK(total >= 20);
    CHECK(movies >= 10);
    CHECK(anime >= 10);
    CHECK(positive >= 10);
    if (argc > 1) {
        QFile out(QString::fromLocal8Bit(argv[1]));
        CHECK(out.open(QIODevice::WriteOnly));
        const auto bytes =
            QJsonDocument(
                QJsonObject{{"fixtures", report},
                            {"qualification",
                             "Structural/deterministic replay only; not human pick agreement"}})
                .toJson();
        CHECK(out.write(bytes) == bytes.size());
    }
    QTextStream(stdout) << "fixture_test: " << total << " real responses, " << positive
                        << " nonempty, " << movies << " movie and " << anime
                        << " anime replays; human agreement NOT tested\n";
}
