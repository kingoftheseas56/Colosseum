// Behavioral port of Harbor scoring tests, plus Colosseum selection policy.
#include "stream/SourceScorer.h"
#include <QCoreApplication>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace colosseum::stream;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__));    \
    } while (false)
static ParsedStream base(int index = 0) {
    ParsedStream p;
    p.resolution = Resolution::P1080;
    p.source = Source::WebDl;
    p.stream.input_index = index;
    p.stream.info_hash = "test-hash";
    return p;
}
static ScoreOptions options() {
    ScoreOptions o;
    o.today = QDate(2026, 11, 8);
    return o;
}
static ScoredStream scored(ParsedStream p, ScoreOptions o = options(), CorpusStats c = {}) {
    return SourceScorer::score(p, o, c);
}
static bool reason(const ScoredStream &s, const QString &name,
                   double delta = std::numeric_limits<double>::quiet_NaN()) {
    for (const auto &r : s.reasons)
        if (r.signal == name && (std::isnan(delta) || r.delta == delta))
            return true;
    return false;
}
static int primary(const RankedPicker &p) {
    return p.primary ? p.primary->parsed.stream.input_index : -1;
}
static void conservation(const ScoredStream &s) {
    double sum = 0;
    for (const auto &r : s.reasons)
        sum += r.delta;
    CHECK(std::abs(sum - s.score) < 1e-9);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int tests = 0, failures = 0;
    auto run = [&](const char *name, const std::function<void()> &f) {
        ++tests;
        try {
            f();
        } catch (const std::exception &e) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    };
    run("4K HDR Atmos HEVC", [] {
        auto p = base();
        p.resolution = Resolution::UHD;
        p.hdr_format = HdrFormat::Hdr10;
        p.codec = Codec::Hevc;
        p.audio.codec = AudioCodec::Atmos;
        p.audio.channels = 8;
        auto s = scored(p);
        CHECK(s.score == 36);
        CHECK(s.tier == Tier::UhdHdr);
        CHECK(reason(s, "Atmos", 3));
        conservation(s);
    });
    run("Dolby Vision", [] {
        auto p = base();
        p.resolution = Resolution::UHD;
        p.hdr_format = HdrFormat::DvHdr10;
        auto s = scored(p);
        CHECK(s.score == 31);
        CHECK(s.tier == Tier::UhdDv);
        CHECK(reason(s, "DV+HDR10", 6));
    });
    run("No debrid, Easynews or URL bonus", [] {
        auto p = base();
        p.stream.addon_name = "Easynews+";
        p.stream.url = "https://example.invalid/video";
        p.stream.raw.insert("cached", true);
        CHECK(scored(p).score == 20);
        p.stream.addon_name = "MediaFusion";
        CHECK(scored(p).score == 28);
    });
    run("CAM rough", [] {
        auto p = base();
        p.resolution = Resolution::P720;
        p.source = Source::CAM;
        auto s = scored(p);
        CHECK(s.score == -72);
        CHECK(s.tier == Tier::Rough);
    });
    run("Seeders", [] {
        auto p = base();
        p.seeders = 5;
        CHECK(scored(p).score == 20);
        p.seeders = 0;
        auto s = scored(p);
        CHECK(s.score == -8);
        CHECK(reason(s, "zero-seeders-stale-meta", -20));
        CHECK(reason(s, "zero-seeders-soft", -8));
        p.seeders = 95;
        CHECK(scored(p).score == 29);
        p.seeders = 500;
        CHECK(scored(p).score == 30);
    });
    run("Preferred languages", [] {
        auto p = base();
        auto o = options();
        o.preferred_languages = {"en"};
        p.audio_languages = {"English"};
        CHECK(scored(p, o).score == 32);
        p.audio_languages = {"French"};
        CHECK(scored(p, o).score == 6);
        p.audio_languages.clear();
        CHECK(scored(p, o).score == 17);
        p.audio_languages = {"Multi"};
        CHECK(scored(p, o).score == 24);
    });
    run("All tiers", [] {
        auto p = base();
        p.resolution = Resolution::UHD;
        p.hdr_format = HdrFormat::Dv;
        CHECK(scored(p).tier == Tier::UhdDv);
        p.hdr_format = HdrFormat::Hdr10;
        CHECK(scored(p).tier == Tier::UhdHdr);
        p.hdr_format.reset();
        CHECK(scored(p).tier == Tier::Uhd);
        p.resolution = Resolution::P1080;
        CHECK(scored(p).tier == Tier::P1080);
        p.hdr_format = HdrFormat::Hlg;
        CHECK(scored(p).tier == Tier::P1080Hdr);
        p.resolution = Resolution::P720;
        CHECK(scored(p).tier == Tier::P720);
        p.resolution = Resolution::P480;
        CHECK(scored(p).tier == Tier::SD);
        p.source = Source::SCR;
        CHECK(scored(p).tier == Tier::Rough);
    });
    run("Primary 1080p before 720p before 4K", [] {
        auto a = base(0), b = base(1), c = base(2);
        b.resolution = Resolution::P720;
        c.resolution = Resolution::UHD;
        auto r = SourceScorer::rankAndPick({scored(c), scored(b), scored(a)}, options());
        CHECK(primary(r) == 0);
        CHECK(r.all.front().parsed.stream.input_index == 2);
        CHECK(r.by_tier.size() == 3);
        CHECK(primary(SourceScorer::rankAndPick({scored(c), scored(b)}, options())) == 1);
        CHECK(primary(SourceScorer::rankAndPick({scored(c)}, options())) == 2);
    });
    run("All rows score sorted and tier best", [] {
        auto a = base(0), b = base(1);
        a.seeders = 20;
        b.seeders = 200;
        auto r = SourceScorer::rankAndPick({scored(a), scored(b)}, options());
        CHECK(primary(r) == 1);
        CHECK(r.all.front().score > r.all.back().score);
        CHECK(r.by_tier["1080p"].parsed.stream.input_index == 1);
    });
    run("Unsafe 1080p falls back", [] {
        auto a = base(0), b = base(1);
        a.scam_score = 100;
        b.resolution = Resolution::P720;
        CHECK(primary(SourceScorer::rankAndPick({scored(a), scored(b)}, options())) == 1);
    });
    run("No pick for CAM, screener or zero seed torrent", [] {
        auto a = base();
        a.source = Source::CAM;
        CHECK(!SourceScorer::rankAndPick({scored(a)}, options()).primary);
        a.source = Source::SCR;
        CHECK(!SourceScorer::rankAndPick({scored(a)}, options()).primary);
        a.source = Source::WebDl;
        a.seeders = 0;
        a.stream.addon_name = "MediaFusion";
        a.stream.addon_priority = 0;
        CHECK(!SourceScorer::rankAndPick({scored(a)}, options()).primary);
    });
    run("No pick empty or nonplayable", [] {
        CHECK(!SourceScorer::rankAndPick({}, options()).primary);
        auto p = base();
        p.stream.info_hash.clear();
        CHECK(!SourceScorer::rankAndPick({scored(p)}, options()).primary);
    });
    run("Addon priority tie deterministic", [] {
        auto a = base(0), b = base(1);
        a.stream.addon_priority = 2;
        b.stream.addon_priority = 0;
        auto x = scored(a), y = scored(b);
        x.score = y.score = 40;
        auto r = SourceScorer::rankAndPick({x, y}, options());
        CHECK(primary(r) == 1);
    });
    run("Respect addon order cannot override score", [] {
        auto a = base(0), b = base(1);
        a.seeders = 100;
        b.seeders = 10;
        auto o = options();
        o.respect_addon_order = true;
        a.stream.addon_return_idx = 1;
        b.stream.addon_return_idx = 0;
        CHECK(primary(SourceScorer::rankAndPick({scored(b, o), scored(a, o)}, o)) == 0);
    });
    run("Dual audio wins exact score ties", [] {
        auto a = base(0), b = base(1);
        b.dual_audio = true;
        b.stream.addon_priority = 5;
        auto x = scored(a), y = scored(b);
        CHECK(x.score == y.score);
        CHECK(primary(SourceScorer::rankAndPick({x, y}, options())) == 1);
    });
    run("Single episode beats pack", [] {
        auto a = base(0), b = base(1);
        a.batch_release = true;
        a.season_pack = true;
        a.seeders = 1000;
        b.seeders = 1;
        auto o = options();
        o.episode_requested = true;
        CHECK(primary(SourceScorer::rankAndPick({scored(a, o), scored(b, o)}, o)) == 1);
        b.resolution = Resolution::P720;
        CHECK(primary(SourceScorer::rankAndPick({scored(a, o), scored(b, o)}, o)) == 1);
    });
    run("Pack available when no single", [] {
        auto a = base(0);
        a.batch_release = true;
        a.season_pack = true;
        auto o = options();
        o.episode_requested = true;
        CHECK(primary(SourceScorer::rankAndPick({scored(a, o)}, o)) == 0);
    });
    run("Corpus fractions", [] {
        auto a = base(), b = base(), c = base(), d = base();
        a.source = Source::CAM;
        a.seeders = 30;
        b.seeders = 100;
        c.source = Source::HDTV;
        c.seeders = 5;
        d.source = Source::BluRay;
        d.stream.url = "https://example.invalid/x";
        auto cs = SourceScorer::computeCorpusStats({a, b, c, d}, options());
        CHECK(cs.trusted_tracked_count == 3);
        CHECK(cs.trusted_tracked_fraction == 0.75);
        CHECK(std::abs(cs.theater_capture_fraction - 1.0 / 3) < 1e-9);
        CHECK(std::abs(cs.webish_fraction - 2.0 / 3) < 1e-9);
    });
    run("Corpus empty", [] {
        auto c = SourceScorer::computeCorpusStats({}, options());
        CHECK(c.trusted_tracked_count == 0);
        CHECK(c.theater_capture_fraction == 0);
        CHECK(!c.median_size);
    });
    run("Corpus percentiles", [] {
        QList<ParsedStream> list;
        for (auto size : {1, 5, 10, 50, 100}) {
            auto p = base();
            p.size = size;
            p.seeders = size;
            list.push_back(p);
        }
        auto c = SourceScorer::computeCorpusStats(list, options());
        CHECK(c.median_size == 10);
        CHECK(c.p90_size == 50);
        CHECK(c.p10_seeders == 1);
        CHECK(c.p90_seeders == 50);
    });
    run("Repack proper", [] {
        auto p = base();
        p.proper = true;
        CHECK(scored(p).score == 21);
        p.proper = false;
        p.repack_iteration = 1;
        CHECK(scored(p).score == 21);
        p.repack_iteration = 5;
        CHECK(scored(p).score == 22);
    });
    run("Trusted group REMUX preferred", [] {
        auto p = base();
        p.release_group_normalized = "FLUX";
        p.remux = true;
        p.source = Source::BluRay;
        CHECK(scored(p).score == 25);
        auto o = options();
        o.preferred_release_group = "FLUX";
        CHECK(scored(p, o).score == 33);
    });
    run("CAM hidden in filename", [] {
        auto p = base();
        p.stream.title = "Some.Movie.1080p.WEB-DL.HDCAM.mkv";
        auto s = scored(p);
        CHECK(s.score == -80);
        CHECK(reason(s, "title-says-hires-filename-says-cam", -100));
        p.resolution = Resolution::UHD;
        CHECK(scored(p).score == -175);
    });
    run("Addon priority decay", [] {
        auto p = base();
        p.stream.addon_priority = 0;
        CHECK(scored(p).score == 32);
        p.stream.addon_priority = 2;
        CHECK(scored(p).score == 24);
        p.stream.addon_priority = 5;
        CHECK(scored(p).score == 20);
    });
    run("Lossless audio and AV1 not browser penalized", [] {
        auto p = base();
        p.audio.codec = AudioCodec::DtsHdMa;
        p.codec = Codec::Av1;
        p.container = "mkv";
        CHECK(scored(p).score == 23);
    });
    run("Size mismatch and lossy exemption", [] {
        auto p = base();
        p.size = 200 * 1024 * 1024;
        auto o = options();
        o.runtime_minutes = 120;
        CHECK(reason(scored(p, o), "size-mismatch", -120));
        p.release_group_normalized = "YTS";
        CHECK(!reason(scored(p, o), "size-mismatch"));
    });
    run("Bandwidth budget", [] {
        auto p = base();
        p.size = quint64(10) * 1024 * 1024 * 1024;
        auto o = options();
        o.runtime_minutes = 90;
        o.bandwidth_mbps = 5;
        auto s = scored(p, o);
        CHECK(s.score < 0);
        bool found = false;
        for (auto r : s.reasons)
            if (r.signal.startsWith("bitrate-exceeds-budget"))
                found = true;
        CHECK(found);
    });
    run("Recent tiny movie", [] {
        auto p = base();
        p.size = 100 * 1024 * 1024;
        auto o = options();
        o.media_kind = "movie";
        o.release_date = o.today.addDays(-5);
        CHECK(reason(scored(p, o), "new-release-virus-100mb", -250));
        o.media_kind = "series";
        CHECK(!reason(scored(p, o), "new-release-virus-100mb"));
    });
    run("Recent undersized 4K", [] {
        auto p = base();
        p.resolution = Resolution::UHD;
        p.size = quint64(4) * 1024 * 1024 * 1024;
        auto o = options();
        o.release_date = o.today.addDays(-45);
        CHECK(reason(scored(p, o), "4k-undersized-4.0gb", -250));
    });
    run("Dominated theater pool extends window", [] {
        auto p = base();
        auto o = options();
        o.media_kind = "movie";
        o.release_date = o.today.addDays(-50);
        CorpusStats c;
        c.trusted_tracked_count = 10;
        c.theater_capture_fraction = .8;
        c.webish_fraction = .1;
        p.source = Source::TS;
        CHECK(reason(scored(p, o, c), "fresh-theater-cinema-window", 75));
        p.source = Source::WEBRip;
        CHECK(reason(scored(p, o, c), "fresh-fake-soft", -45));
        c.theater_capture_fraction = .1;
        c.webish_fraction = .7;
        CHECK(!reason(scored(p, o, c), "fresh-fake-soft"));
    });
    run("Recent year mismatch", [] {
        auto p = base();
        p.year = 2025;
        auto o = options();
        o.release_date = QDate(2026, 10, 1);
        CHECK(reason(scored(p, o), "year-off-by-1:2025vs2026-recent", -75));
    });
    run("Severe fake never selected despite origin bonus", [] {
        auto p = base();
        p.stream.addon_id = "origin";
        p.scam_score = 20;
        auto o = options();
        o.prefer_addon_id = "origin";
        CHECK(scored(p, o).score > 0);
        CHECK(!SourceScorer::rankAndPick({scored(p, o)}, o).primary);
    });
    run("Nonfinite scores never selected", [] {
        auto a = scored(base(0)), b = scored(base(1));
        a.score = std::numeric_limits<double>::quiet_NaN();
        CHECK(primary(SourceScorer::rankAndPick({a, b}, options())) == 1);
    });
    run("Rough row skipped beside clean primary", [] {
        auto a = base(0), b = base(1);
        a.source = Source::CAM;
        auto x = scored(a), y = scored(b);
        x.score = 500;
        CHECK(primary(SourceScorer::rankAndPick({x, y}, options())) == 1);
    });
    run("Ordinary anime size warning is not a hard rejection", [] {
        Stream raw;
        raw.title = "[SubsPlease] One Piece - 1164 (1080p).mkv";
        raw.info_hash = "0123456789abcdef0123456789abcdef01234567";
        raw.size = 300ULL * 1024 * 1024;
        raw.seeders = 200;
        raw.input_index = 9;
        raw.behavior_hints.insert("filename", raw.title);
        TrustOptions trust;
        trust.kind = "series";
        trust.expected_title = "One Piece";
        trust.expected_episode = 1164;
        trust.is_anime = true;
        auto o = options();
        o.media_kind = "series";
        o.runtime_minutes = 24;
        auto r = SourceScorer::evaluate({raw}, trust, o);
        CHECK(r.rejected.isEmpty());
        CHECK(r.all.size() == 1);
        CHECK(r.all.front().parsed.scam_score == 3);
        CHECK(primary(r) == 9);
    });
    run("Underscore CAM cannot bypass scorer fallback", [] {
        auto p = base();
        p.stream.title = "Movie_2020_CAM_1080p.mkv";
        CHECK(reason(scored(p), "title-says-hires-filename-says-cam", -100));
        p.stream.title = "GUTS_2020_1080p.mkv";
        CHECK(!reason(scored(p), "title-says-hires-filename-says-cam"));
    });
    run("Selection explanations", [] {
        auto r = SourceScorer::rankAndPick({}, options());
        CHECK(!r.selection_reasons.isEmpty());
        auto p = base();
        p.seeders = 0;
        r = SourceScorer::rankAndPick({scored(p)}, options());
        CHECK(!r.selection_reasons.isEmpty());
        p.seeders = 20;
        r = SourceScorer::rankAndPick({scored(p)}, options());
        CHECK(r.primary);
        CHECK(!r.selection_reasons.isEmpty());
        CHECK(r.selection_reasons.front().contains("1080p"));
    });
    std::cout << tests - failures << "/" << tests << " scorer cases passed\n";
    return failures ? 1 : 0;
}
