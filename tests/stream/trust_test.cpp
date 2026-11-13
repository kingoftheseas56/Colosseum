#include "StreamTrust.h"
#include <QCoreApplication>
#include <iostream>
using namespace colosseum::stream;
namespace {
constexpr quint64 MiB = 1024 * 1024, GiB = 1024 * MiB;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            ++failures;                                                                            \
            std::cerr << __LINE__ << ": " #x "\n";                                                 \
        }                                                                                          \
    } while (false)
ParsedStream base() {
    ParsedStream s;
    s.stream.info_hash = QString(40, 'a');
    s.stream.addon_name = "Addon";
    s.parsed_title = "Sample Movie";
    s.resolution = Resolution::P1080;
    s.codec = Codec::Hevc;
    s.source = Source::WebDl;
    s.size = 2 * GiB;
    s.seeders = 50;
    s.container = "mkv";
    s.year = 2020;
    return s;
}
TrustOptions opts() {
    TrustOptions o;
    o.today = QDate(2026, 11, 8);
    return o;
}
QString reason(const ParsedStream &s, const TrustOptions &o = opts()) {
    auto r = StreamTrust::apply({s}, o);
    CHECK(r.keep.size() + r.rejected.size() == 1);
    return r.rejected.isEmpty() ? QString() : r.rejected[0].reason;
}
void filename(ParsedStream &s, const QString &f) { s.stream.behavior_hints.insert("filename", f); }
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto s = base();
    auto o = opts();
    CHECK(reason(s).isEmpty());
    for (const auto &ext : {".exe", ".zip", ".rar", ".lnk", ".scr", ".bat", ".iso", ".img"}) {
        s = base();
        filename(s, QString("Setup") + ext);
        CHECK(reason(s) == QString("suspicious-extension:") + ext);
    }
    s = base();
    s.season_pack = true;
    s.season = 1;
    o = opts();
    o.allow_season_packs = true;
    o.expected_season = 1;
    o.expected_episode = 3;
    CHECK(reason(s, o).isEmpty());
    s = base();
    filename(s, "movie.trailer.mkv");
    s.size = 50 * MiB;
    CHECK(reason(s) == "trailer-or-extra");
    s = base();
    s.stream.info_hash.clear();
    CHECK(reason(s) == "no-playable-source");
    s.stream.raw.insert("nzbUrl", "https://x/nzb");
    CHECK(reason(s).isEmpty());
    s = base();
    s.size = MiB;
    CHECK(reason(s) == "size-stub");
    s = base();
    s.scam_score = 7;
    CHECK(reason(s) == "scam-score-7");
    o = opts();
    o.allow_cam = true;
    CHECK(reason(s, o).isEmpty());
    o = opts();
    o.disabled = true;
    s.stream.info_hash.clear();
    CHECK(reason(s, o).isEmpty());
    s = base();
    s.parsed_title = "Obsession";
    s.season = 1;
    s.episode = 1;
    o = opts();
    o.kind = "movie";
    CHECK(reason(s, o) == "series-result-for-movie");
    s.episode.reset();
    s.season_pack = true;
    CHECK(reason(s, o) == "series-result-for-movie");
    o.is_anime = true;
    CHECK(reason(s, o).isEmpty());
    s = base();
    s.parsed_title = "Obsession";
    s.year.reset();
    s.source = Source::Other;
    s.resolution = Resolution::SD;
    o = opts();
    o.kind = "movie";
    o.expected_year = 2026;
    o.release_date = o.today.addDays(-7);
    CHECK(reason(s, o) == "cinema-bare-untagged");
    s.year = 2026;
    s.source = Source::CAM;
    s.resolution = Resolution::P1080;
    CHECK(reason(s, o).isEmpty());
    s = base();
    s.stream.raw.insert("fileIdx", 2);
    s.episode = 7;
    o = opts();
    o.expected_episode = 3;
    CHECK(reason(s, o).isEmpty());
    s = base();
    s.stream.description = "🚫 No streams found";
    CHECK(reason(s) == "addon-placeholder-banner");
    s = base();
    s.stream.info_hash.clear();
    s.stream.url = "https://example.com/account";
    s.stream.description = "Premium expires in 3 days";
    CHECK(reason(s) == "addon-status-card");
    s.stream.behavior_hints.insert("fileName", "card.mkv");
    CHECK(reason(s) == "addon-status-card");
    s.stream.behavior_hints.insert("filename", "card.mkv");
    CHECK(reason(s).isEmpty());
    s = base();
    s.stream.name = "⏳ Cloud only";
    CHECK(reason(s).isEmpty());
    s = base();
    filename(s, "Movie_2025_trailer_1080p.mkv");
    CHECK(reason(s) == "trailer-or-extra");
    s = base();
    s.parsed_title = "The Strangers";
    s.year = 2008;
    s.resolution = Resolution::P720;
    o = opts();
    o.kind = "movie";
    o.expected_year = 2026;
    o.release_date = o.today.addDays(-7);
    CHECK(reason(s, o) == "cinema-year-mismatch:2008-vs-2026");
    s = base();
    s.year = 2026;
    s.source = Source::HDTV;
    CHECK(reason(s, o) == "fresh-cinema-fake-hdtv");
    s = base();
    s.size = 180 * MiB;
    o = opts();
    o.kind = "series";
    CHECK(reason(s, o) == "episode-stub-too-small-for-1080p");
    o.is_anime = true;
    CHECK(reason(s, o).isEmpty());
    s = base();
    filename(s, "Show.OVA.1080p.mkv");
    s.size = 80 * MiB;
    o = opts();
    o.kind = "series";
    CHECK(reason(s, o).isEmpty());
    s = base();
    s.parsed_title = "DBM Obsession Viva Las Vegas";
    s.year = 2026;
    o = opts();
    o.kind = "movie";
    o.expected_title = "Obsession";
    o.expected_year = 2026;
    CHECK(reason(s, o) == "title-mismatch");
    s = base();
    s.parsed_title = "Rocky II";
    s.year = 1979;
    o = opts();
    o.kind = "movie";
    o.expected_title = "Rocky";
    o.expected_year = 1976;
    CHECK(reason(s, o).isEmpty());
    for (const auto &pair :
         {std::pair<const char *, const char *>{"Amélie", "Amelie"}, {"Pokémon", "Pokemon"}}) {
        s = base();
        s.parsed_title = QString::fromUtf8(pair.second);
        o = opts();
        o.kind = "movie";
        o.expected_title = QString::fromUtf8(pair.first);
        CHECK(reason(s, o).isEmpty());
    }
    o = opts();
    o.kind = "series";
    o.is_anime = true;
    o.expected_titles = {"Neon Genesis Evangelion", "Shin Seiki Evangelion"};
    s = base();
    s.parsed_title = "Stranger Things";
    CHECK(reason(s, o) == "anime-title-mismatch");
    s.parsed_title = "Evangelion";
    CHECK(reason(s, o).isEmpty());
    o.expected_titles.clear();
    s.parsed_title = "Totally Unrelated Absolute-Numbered Title";
    CHECK(reason(s, o).isEmpty());
    o = opts();
    o.kind = "movie";
    o.expected_year = 2026;
    o.release_date = o.today.addDays(-7);
    s = base();
    s.year = 2026;
    for (auto source : {Source::WebDl, Source::WEBRip, Source::BDRip, Source::HDRip}) {
        s.source = source;
        for (auto res : {Resolution::P1080, Resolution::P720}) {
            s.resolution = res;
            CHECK(reason(s, o) == "fresh-cinema-fake-web");
        }
        s.resolution = Resolution::UHD;
        s.size = 4 * GiB;
        CHECK(reason(s, o) == "fresh-cinema-fake-4k-web");
    }
    s = base();
    s.year = 2026;
    s.source = Source::BluRay;
    CHECK(reason(s, o) == "fresh-cinema-fake-bluray");
    s.source = Source::Other;
    s.remux = true;
    CHECK(reason(s, o) == "fresh-cinema-fake-bluray");
    for (const auto &group :
         {"LAMA", "DJT", "TELLY", "RUDE", "TELESYNC", "TELESYNCH", "DRAGONMONEY"}) {
        s = base();
        s.release_group_normalized = group;
        CHECK(reason(s) == "known-fake-group");
    }
    s = base();
    filename(s, "The.Odyssey.2025.1080p.WEB-DL.Dragon.Money.mkv");
    CHECK(reason(s) == "known-fake-group");
    s = base();
    s.release_group_normalized = "RARBG";
    filename(s, "Movie.2020.1080p.BluRay.x264-RARBG.mkv");
    CHECK(reason(s).isEmpty());
    for (const auto &title :
         {"Kaiju No. 8", "Kaiju No.8", "Mob Psycho #2", "Ranking of Kings Number 3"}) {
        s = base();
        s.parsed_title = title;
        s.year.reset();
        o = opts();
        o.kind = "movie";
        o.expected_title = title;
        CHECK(reason(s, o).isEmpty());
    }
    s = base();
    s.parsed_title = "Kaijuu 8 Gou";
    s.year.reset();
    o = opts();
    o.kind = "movie";
    o.expected_title = "Kaiju No. 8";
    o.expected_year = 2024;
    CHECK(reason(s, o).isEmpty());
    o.kind = "series";
    o.is_anime = true;
    o.expected_title.clear();
    o.expected_titles = {"Kaiju No. 8", "Kaijuu 8-gou"};
    CHECK(reason(s, o).isEmpty());
    s.parsed_title = "Kaiju No 8";
    CHECK(reason(s, o).isEmpty());
    s.parsed_title = "Stranger Things";
    CHECK(reason(s, o) == "anime-title-mismatch");
    // Public behavior coverage beyond upstream's helper-only unit tests.
    s = base();
    s.parsed_title = "John Wick 4";
    s.year = 2023;
    filename(s, "John.Wick.2023.1080p.mkv");
    o = opts();
    o.kind = "movie";
    o.expected_title = "John Wick 4";
    o.expected_year = 2023;
    CHECK(reason(s, o) == "filename-missing-sequel");
    filename(s, "John.Wick.IV.2023.1080p.mkv");
    CHECK(reason(s, o).isEmpty());
    filename(s, "John.Wick.four.2023.1080p.mkv");
    CHECK(reason(s, o).isEmpty());
    s = base();
    s.season = 2;
    o = opts();
    o.expected_season = 1;
    CHECK(reason(s, o) == "season-mismatch:2-vs-1");
    s = base();
    s.episode = 3;
    o = opts();
    o.expected_episode = 5;
    CHECK(reason(s, o) == "episode-mismatch:3-vs-5");
    s.episode_end = 5;
    CHECK(reason(s, o).isEmpty());
    o.expected_episode = 2;
    CHECK(reason(s, o) == "episode-mismatch:3-vs-2");
    o.is_anime = true;
    CHECK(reason(s, o).isEmpty());
    s = base();
    s.scam_score = 8;
    o = opts();
    o.expected_year = 2020;
    CHECK(reason(s, o).isEmpty());
    o.expected_year = 2025;
    CHECK(reason(s, o) == "scam-score-8");
    s = base();
    s.size = 250 * MiB;
    o = opts();
    o.kind = "movie";
    o.expected_year = 2020;
    CHECK(reason(s, o).isEmpty());
    o.expected_year = 2025;
    CHECK(reason(s, o) == "movie-stub-too-small-for-1080p");
    // Tankoban 2 stricter show identity: reject partial matches and read badge fallback.
    o = opts();
    o.kind = "series";
    o.expected_title = "One Piece";
    o.is_anime = true;
    s = base();
    s.parsed_title = "One Tree Hill";
    s.match_text = "One.Tree.Hill.S01.1080p.mkv";
    CHECK(reason(s, o) == "show-identity-mismatch");
    s.parsed_title = "[2176 seeders | 1.34 GB | NyaaSi]";
    s.match_text = "[2176 seeders | 1.34 GB | NyaaSi]\n[SubsPlease] One Piece - 1164 (1080p).mkv";
    CHECK(reason(s, o).isEmpty());
    o.is_anime = false;
    CHECK(reason(s, o).isEmpty());
    o.is_anime = true;
    o.expected_titles = {"One Piece"};
    CHECK(reason(s, o).isEmpty());
    s.match_text = "[5000 seeders]\nCommunity.S01-S06.COMPLETE.1080p.mkv";
    CHECK(!reason(s, o).isEmpty());
    s.parsed_title = "";
    s.match_text = "";
    s.stream.title = "";
    CHECK(!reason(s, o).isEmpty());
    // Date boundary is deterministic and does not depend on wall clock.
    s = base();
    s.year = 2026;
    o = opts();
    o.kind = "movie";
    o.release_date = o.today.addDays(-60);
    CHECK(reason(s, o).isEmpty());
    o.release_date = o.today.addDays(-59);
    CHECK(reason(s, o) == "fresh-cinema-fake-web");
    std::cout << "trust: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
