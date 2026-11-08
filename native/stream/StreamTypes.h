#pragma once
#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>

namespace colosseum::stream {
enum class Resolution { UHD, P1080, P720, P480, SD };
enum class HdrFormat { Hdr10, Hdr10Plus, Dv, DvHdr10, Hlg };
enum class Codec { Hevc, Avc, Av1, Vp9, Mpeg2, Other };
enum class AudioCodec { Atmos, TrueHd, DtsHdMa, Dts, DdPlus, Ac3, Aac, Opus, Flac, Other };
enum class Source {
    BluRay,
    REMUX,
    WebDl,
    WEBRip,
    BDRip,
    HDRip,
    DVDRip,
    HDTV,
    CAM,
    TS,
    HDTS,
    TC,
    SCR,
    Other
};
enum class Tier { UhdDv, UhdHdr, Uhd, P1080Hdr, P1080, P720, SD, Rough };
struct AudioInfo {
    AudioCodec codec = AudioCodec::Other;
    int channels = 2;
    std::optional<int> bit_depth;
};
struct Stream {
    QString name, title, description, info_hash, url, yt_id, external_url, addon_id, addon_name;
    QJsonObject behavior_hints, raw;
    std::optional<int> addon_priority, addon_return_idx;
    std::optional<quint64> size;
    std::optional<quint32> seeders;
    int input_index = -1;
};
struct ParsedStream {
    Stream stream;
    QString parsed_title, episode_title, filename, match_text;
    Resolution resolution = Resolution::SD;
    std::optional<HdrFormat> hdr_format;
    Codec codec = Codec::Other;
    Source source = Source::Other;
    AudioInfo audio;
    QStringList audio_languages;
    std::optional<quint64> size;
    std::optional<quint32> seeders;
    QString container, release_group, release_group_normalized, edition, anime_hash;
    bool remux = false;
    std::optional<int> year, season, episode, episode_end, disc_index;
    std::optional<std::pair<int, int>> year_range;
    bool season_pack = false, batch_release = false, dual_audio = false;
    int repack_iteration = 0, scam_score = 0;
    bool proper = false, hardcoded = false;
};
struct TrustOptions {
    QString kind, expected_title, imdb_id;
    std::optional<int> expected_year, expected_season, expected_episode;
    QDate release_date;
    QDate today = QDate::currentDate();
    bool allow_season_packs = true, allow_cam = false, allow_size_outliers = false;
    bool strict = true, disabled = false, require_preferred_language = false, is_anime = false;
    QStringList preferred_languages, preferred_audio_langs, expected_titles;
};
struct ScoreOptions {
    QStringList preferred_languages;
    QDate release_date;
    QDate today = QDate::currentDate();
    QString media_kind, preferred_release_group, prefer_addon_id;
    std::optional<int> runtime_minutes;
    std::optional<double> bandwidth_mbps;
    bool in_theaters = false, respect_addon_order = false, episode_requested = false;
};
struct ScoreReason {
    QString signal;
    double delta = 0;
};
struct ScoredStream {
    ParsedStream parsed;
    double score = 0;
    QList<ScoreReason> reasons;
    Tier tier = Tier::SD;
};
struct Rejection {
    ParsedStream stream;
    QString reason;
};
struct TrustResult {
    QList<ParsedStream> keep;
    QList<Rejection> rejected;
};
struct RankedPicker {
    std::optional<ScoredStream> primary;
    QMap<QString, ScoredStream> by_tier;
    QList<ScoredStream> all;
    QList<Rejection> rejected;
    QStringList selection_reasons;
};
struct CorpusStats {
    std::optional<double> days_since_release;
    double trusted_tracked_fraction = 0, theater_capture_fraction = 0, webish_fraction = 0;
    int trusted_tracked_count = 0;
    std::optional<quint64> median_size, p90_size;
    std::optional<quint32> p10_seeders, p90_seeders;
};
Stream stream_from_json(const QJsonObject &row, int index = -1, const QString &addon_name = {},
                        int addon_priority = -1);
} // namespace colosseum::stream
