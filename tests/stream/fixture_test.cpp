#include "SourceScorer.h"
#include "StreamParser.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
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
            {"sizeBytes", s.parsed.size ? QJsonValue(static_cast<double>(*s.parsed.size))
                                        : QJsonValue(QJsonValue::Null)},
            {"seeders", s.parsed.seeders ? QJsonValue(static_cast<double>(*s.parsed.seeders))
                                         : QJsonValue(QJsonValue::Null)},
            {"audioLanguages", QJsonArray::fromStringList(s.parsed.audio_languages)},
            {"batchRelease", s.parsed.batch_release},
            {"seasonPack", s.parsed.season_pack},
            {"fileIdx", s.parsed.stream.raw.value("fileIdx")},
            {"reasons", reasons}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QDir fixtures(QString::fromUtf8(STREAM_FIXTURE_DIR));
    const auto manifest = readObject(fixtures.filePath("manifest.json"));
    const auto cases = manifest.value("fixtures").toArray();
    CHECK(cases.size() >= 20);
    int positive = 0, total = 0, movies = 0, anime = 0;
    QMap<QString, int> previousPrimaryIndices;
    if (argc > 2) {
        const auto previous = readObject(QString::fromLocal8Bit(argv[2]));
        CHECK(previous.value("fixtures").isArray());
        for (const auto &entry : previous.value("fixtures").toArray()) {
            const auto record = entry.toObject();
            const auto primary = record.value("primary").toObject();
            if (primary.value("rowIndex").isDouble())
                previousPrimaryIndices.insert(record.value("id").toString(),
                                              primary.value("rowIndex").toInt());
        }
    }
    QJsonArray report;
    int safetyChecks = 0, safetyFailures = 0;
    const auto safety = [&](bool condition, const QString &label) {
        ++safetyChecks;
        if (!condition) {
            ++safetyFailures;
            QTextStream(stderr) << "fixture safety failure: " << label << '\n';
        }
    };
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
        // These are engineering safety checks against captured metadata, not human labels.
        const auto caseId = c.value("id").toString();
        if (caseId == "parasite") {
            CHECK(streams.size() > 17);
            const auto parsed = StreamParser::parse(streams[17]);
            safety(!parsed.audio_languages.contains("en", Qt::CaseInsensitive) &&
                       !parsed.audio_languages.contains("English", Qt::CaseInsensitive),
                   "Parasite row 17 must not infer English from its 6.45 GB size");
            for (const auto &row : ranked.all) {
                if (row.parsed.stream.input_index != 17)
                    continue;
                bool falseLanguageBonus = false;
                for (const auto &reason : row.reasons)
                    if (reason.signal.startsWith("preferred-language") && reason.delta > 0)
                        falseLanguageBonus = true;
                safety(!falseLanguageBonus,
                       "Parasite row 17 must not receive a preferred-language bonus from GB");
            }
        }
        if (caseId == "one-piece") {
            safety(ranked.primary.has_value(), "One Piece must have a safe primary");
            if (ranked.primary) {
                const auto &picked = ranked.primary->parsed;
                safety(picked.resolution == Resolution::P1080,
                       "One Piece primary must be 1080p, not the 4K upscale");
                const auto fileIndex = picked.stream.raw.value("fileIdx");
                safety(fileIndex.isDouble() && fileIndex.toDouble() >= 0 &&
                           std::floor(fileIndex.toDouble()) == fileIndex.toDouble(),
                       "One Piece primary must select a concrete torrent file index");
                // Parse only the selected filename, without the parent torrent's range title.
                Stream selected;
                selected.title = picked.filename;
                selected.behavior_hints.insert("filename", picked.filename);
                const auto file = StreamParser::parse(selected);
                safety(!picked.filename.isEmpty() && !file.batch_release && !file.season_pack &&
                           file.episode == trust.expected_episode &&
                           (!file.season || file.season == trust.expected_season) &&
                           (!file.episode_end || file.episode_end == file.episode),
                       "One Piece primary filename must identify only the requested episode");
            }
        }
        if (caseId == "matrix" || caseId == "godfather" || caseId == "dune") {
            constexpr quint64 cap = 15ULL * 1000 * 1000 * 1000;
            bool goodAlternative = false;
            for (const auto &row : ranked.all) {
                const auto &parsed = row.parsed;
                if (parsed.resolution == Resolution::P1080 && parsed.size &&
                    *parsed.size <= cap && parsed.seeders.value_or(0) > 0 &&
                    row.score >= 0 && row.tier != Tier::Rough && parsed.scam_score < 5)
                    goodAlternative = true;
            }
            safety(goodAlternative, caseId + ": fixture must retain a good <=15 GB 1080p alternative");
            safety(ranked.primary && ranked.primary->parsed.resolution == Resolution::P1080 &&
                       ranked.primary->parsed.size && *ranked.primary->parsed.size <= cap,
                   caseId + ": primary must be <=15 GB while good 1080p alternatives exist");
        }
        if (caseId == "inception") {
            safety(ranked.primary.has_value(), "Inception must have a safe primary");
            if (ranked.primary) {
                const auto &picked = ranked.primary->parsed;
                const auto text = picked.stream.title + " " + picked.stream.description + " " +
                                  picked.filename;
                const QRegularExpression multiDub(
                    "(?i)(?:dub.*(?:dvo|avo|ukr)|(?:dvo|avo).*original)");
                safety(!(text.contains("rutracker", Qt::CaseInsensitive) &&
                         multiDub.match(text).hasMatch()),
                       "Inception primary must not be the Rutracker multi-dub release");
            }
        }
        if (caseId == "clayface")
            safety(!ranked.primary, "Clayface CAM/TS/TC-only pool must remain no-pick");
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
        if (previousPrimaryIndices.contains(caseId)) {
            const auto previousIndex = previousPrimaryIndices.value(caseId);
            QJsonObject previousNow{{"rowIndex", previousIndex}};
            for (const auto &row : ranked.all)
                if (row.parsed.stream.input_index == previousIndex) {
                    previousNow = scoredJson(row);
                    previousNow.insert("status", "retained");
                    break;
                }
            for (const auto &row : ranked.rejected)
                if (row.stream.stream.input_index == previousIndex) {
                    previousNow.insert("status", "rejected");
                    previousNow.insert("reason", row.reason);
                    break;
                }
            auto record = report.last().toObject();
            record.insert("previousPrimaryNow", previousNow);
            report[report.size() - 1] = record;
        }
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
    if (safetyFailures) {
        QTextStream(stderr) << safetyFailures << " / " << safetyChecks
                            << " fixture safety assertions failed\n";
        return 1;
    }
    if (argc > 1) {
        QFile out(QString::fromLocal8Bit(argv[1]));
        CHECK(out.open(QIODevice::WriteOnly));
        const auto bytes =
            QJsonDocument(
                QJsonObject{{"fixtures", report},
                            {"comparisonInput", argc > 2
                                ? QJsonValue(QFileInfo(QString::fromLocal8Bit(argv[2])).fileName())
                                : QJsonValue(QJsonValue::Null)},
                            {"qualification",
                             "Structural/deterministic replay and metadata safety assertions; not human pick agreement"}})
                .toJson();
        CHECK(out.write(bytes) == bytes.size());
    }
    QTextStream(stdout) << "fixture_test: " << total << " real responses, " << positive
                        << " nonempty, " << movies << " movie and " << anime
                        << " anime replays; " << safetyChecks
                        << " metadata safety assertions; human agreement NOT tested\n";
}
