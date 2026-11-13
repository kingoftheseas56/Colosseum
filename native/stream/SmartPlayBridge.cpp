#include "SmartPlayBridge.h"
#include "SourceScorer.h"
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>

QVariantMap SmartPlayBridge::pick(const QVariantList &rows, const QString &quality,
                                const QVariantMap &context) const {
    using namespace colosseum::stream;
    if (quality == "off")
        return {{"index", -1}, {"reason", "off"}};
    QList<Stream> streams;
    streams.reserve(rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
        auto row = QJsonObject::fromVariantMap(rows[i].toMap());
        if (row.value("name").toString().isEmpty())
            row.insert("name", row.value("quality"));
        streams.push_back(stream_from_json(row, int(i)));
    }
    TrustOptions trust;
    const auto metadata = context.value("metadata").toMap();
    trust.kind = context.value("type").toString();
    if (trust.kind == "tv") trust.kind = "series";
    trust.expected_title = context.value("title", metadata.value("name")).toString();
    trust.imdb_id = context.value("imdbId").toString();
    if (context.value("year").toInt() > 0)
        trust.expected_year = context.value("year").toInt();
    if (context.contains("season")) trust.expected_season = context.value("season").toInt();
    if (context.contains("episode")) trust.expected_episode = context.value("episode").toInt();
    const auto id = context.value("id").toString();
    const auto episode = QRegularExpression("^(tt\\d+):(\\d+):(\\d+)$").match(id);
    if (episode.hasMatch()) {
        trust.imdb_id = episode.captured(1);
        if (!trust.expected_season) trust.expected_season = episode.captured(2).toInt();
        if (!trust.expected_episode) trust.expected_episode = episode.captured(3).toInt();
    }
    trust.release_date = QDate::fromString(
        context.value("releaseDate", metadata.value("released")).toString().left(10), Qt::ISODate);
    if (!trust.expected_year && trust.release_date.isValid()) trust.expected_year = trust.release_date.year();
    trust.expected_titles = context.value("aliases", metadata.value("aliases")).toStringList();
    if (!trust.expected_titles.isEmpty() && !trust.expected_title.isEmpty())
        trust.expected_titles.push_front(trust.expected_title);
    trust.is_anime = context.value("isAnime").toBool() || id.startsWith("kitsu:")
        || metadata.value("sourceId").toString().startsWith("kitsu:");
    ScoreOptions score;
    score.prefer_fast_start = true;
    score.target_resolution = quality == "4k" ? Resolution::UHD : Resolution::P1080;
    score.release_date = trust.release_date;
    int runtime = context.value("runtimeMinutes").toInt();
    if (runtime <= 0) {
        const auto minutes = QRegularExpression("^\\s*(\\d+)\\s*(?:min(?:utes)?\\b)?\\s*$",
            QRegularExpression::CaseInsensitiveOption).match(metadata.value("runtime").toString());
        if (minutes.hasMatch()) runtime = minutes.captured(1).toInt();
    }
    if (runtime > 0) score.runtime_minutes = runtime;
    for (const auto &code : context.value("preferredLanguages").toStringList()) {
        const auto language = QLocale::codeToLanguage(code, QLocale::AnyLanguageCode);
        if (!code.trimmed().isEmpty()) score.preferred_languages.push_back(
            language == QLocale::AnyLanguage ? code : QLocale::languageToString(language));
    }
    const auto result = SourceScorer::evaluate(streams, trust, score);
    return {{"index", result.primary ? result.primary->parsed.stream.input_index : -1},
            {"reason", result.selection_reasons.value(0)}};
}
