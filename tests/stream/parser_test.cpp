#include "stream/StreamParser.h"
#include <QCoreApplication>
#include <QDebug>
#include <cmath>
#include <cstdlib>
using namespace colosseum::stream;
namespace {
int checks = 0;
void check(bool ok, const char *expr, int line) {
    ++checks;
    if (!ok) {
        qCritical() << "parser_test:" << line << expr;
        std::exit(1);
    }
}
#define CHECK(x) check(bool(x), #x, __LINE__)
Stream row(const QString &title) {
    Stream s;
    s.title = title;
    s.addon_name = "Torrentio";
    return s;
}
ParsedStream parse(const QString &title) { return StreamParser::parse(row(title)); }
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // Harbor parser.rs tests, excluding intentionally removed debrid-cache assertions.
    auto p =
        parse("The.Matrix.1999.1080p.WEB-DL.DDP5.1.H.264-FLUX\n👤 25 💾 2.5 GB ⚡ Real-Debrid");
    CHECK(p.parsed_title == "The Matrix");
    CHECK(p.resolution == Resolution::P1080);
    CHECK(p.codec == Codec::Avc);
    CHECK(p.source == Source::WebDl);
    CHECK(p.year == 1999);
    CHECK(p.audio.codec == AudioCodec::DdPlus);
    CHECK(p.audio.channels == 6);
    CHECK(p.release_group == "FLUX");
    CHECK(p.release_group_normalized == "FLUX");
    CHECK(p.size && *p.size > 2000000000);
    CHECK(p.seeders == 25);
    p = parse("The.Dark.Knight.2008.1080p.BluRay.x265-YTS");
    CHECK(p.resolution == Resolution::P1080);
    CHECK(p.codec == Codec::Hevc);
    CHECK(p.source == Source::BluRay);
    CHECK(p.year == 2008);
    p = parse("Some.Movie.2024.HDCAM.x264-NEW\n💾 1.2 GB");
    CHECK(p.source == Source::CAM);
    CHECK(p.year == 2024);
    p = parse("Dune.Part.Two.2024.2160p.UHD.BluRay.REMUX.DV.HDR10.HEVC.TrueHD.7.1.Atmos-"
              "FraMeSToR\n💾 80.5 GB");
    CHECK(p.resolution == Resolution::UHD);
    CHECK(p.codec == Codec::Hevc);
    CHECK(p.source == Source::REMUX);
    CHECK(p.hdr_format == HdrFormat::DvHdr10);
    CHECK(p.remux);
    CHECK(p.audio.codec == AudioCodec::Atmos);
    CHECK(p.audio.channels == 8);
    CHECK(p.year == 2024);
    CHECK(p.release_group == "FraMeSToR");
    CHECK(p.release_group_normalized == "FRAMESTOR");
    CHECK(StreamParser::isTrustedGroup(p.release_group_normalized));
    p = parse("Breaking.Bad.S01.Complete.1080p.BluRay.x264-DEMAND");
    CHECK(p.season == 1);
    CHECK(!p.episode);
    CHECK(p.season_pack);
    CHECK(p.resolution == Resolution::P1080);
    p = parse("The.Office.S03E10.A.Benihana.Christmas.720p.WEB-DL.x264.mkv");
    CHECK(p.season == 3);
    CHECK(p.episode == 10);
    CHECK(p.resolution == Resolution::P720);
    CHECK(p.episode_title.contains("Benihana"));
    CHECK(p.container == "mkv");
    p = StreamParser::parse(Stream{});
    CHECK(p.resolution == Resolution::SD);
    CHECK(p.codec == Codec::Other);
    CHECK(p.source == Source::Other);
    auto s = row("Movie.2020.1080p.WEB-DL.x264");
    s.behavior_hints.insert("videoSize", 5000000000.0);
    CHECK(StreamParser::parse(s).size == 5000000000ULL);
    for (const auto &pair : QList<QPair<QString, double>>{{"500", 500},
                                                          {"2.75", 2.75},
                                                          {"2,75", 2.75},
                                                          {"2.750", 2.75},
                                                          {"1,500", 1500},
                                                          {"1 500", 1500},
                                                          {"1.080,5", 1080.5},
                                                          {"1,234,567", 1234567}}) {
        p = parse("Movie.2020.1080p.WEB-DL\n💾 " + pair.first + " MB");
        CHECK(p.size);
        CHECK(std::abs(double(*p.size) - std::round(pair.second * 1048576)) < 2);
    }
    p = parse("Movie.2020.1080p.WEB-DL\n💾 2,75 GB");
    CHECK(p.size && *p.size > 2900000000ULL && *p.size < 3000000000ULL);
    p = parse("Movie.2020.1080p.WEB-DL\n💾 1 500 MB");
    CHECK(p.size && *p.size > 1500000000ULL && *p.size < 1600000000ULL);
    p = parse("Movie.2020 1080 2.75 GB");
    CHECK(p.size && *p.size > 2900000000ULL && *p.size < 3000000000ULL);
    // Filename identity must come from media text, not a stats badge or an add-on label.
    s = row("[2176 seeders | 1.34 GB | NyaaSi]");
    s.description = "[SubsPlease] One Piece - 1123 (1080p) [A1B2C3D4].mkv";
    s.name = "Torrentio 4K";
    p = StreamParser::parse(s);
    CHECK(p.parsed_title == "One Piece");
    CHECK(p.episode == 1123);
    CHECK(p.resolution == Resolution::P1080);
    CHECK(p.release_group_normalized == "SUBSPLEASE");
    CHECK(p.anime_hash == "A1B2C3D4");
    s = row("Movie.2024.2160p.BluRay.HEVC");
    s.behavior_hints.insert("filename", "Movie.2024.720p.HDCAM.x264.mkv");
    p = StreamParser::parse(s);
    CHECK(p.source == Source::CAM);
    CHECK(p.resolution == Resolution::P720);
    CHECK(p.filename.endsWith("mkv"));
    s = row("[2176 seeders | 1.34 GB | NyaaSi]");
    s.behavior_hints.insert("filename", "💾 1.34 GB 👤 2176");
    s.description = "One.Piece.S01E04.1080p.WEB-DL.mkv";
    p = StreamParser::parse(s);
    CHECK(p.parsed_title == "One Piece");
    CHECK(p.episode == 4);
    for (const auto &name : QStringList{"Show.S01E02E03.1080p.mkv", "Show.S01E02-E03.1080p.mkv",
                                        "Show.1x02-03.1080p.mkv"}) {
        p = parse(name);
        CHECK(p.season == 1);
        CHECK(p.episode == 2);
        CHECK(p.episode_end == 3);
        CHECK(p.batch_release);
    }
    p = parse("Show.S01E02-E12.1080p.mkv");
    CHECK(p.batch_release);
    CHECK(p.episode_end == 12);
    p = parse("Show.S02.1080p.WEB-DL.mkv");
    CHECK(p.season_pack);
    CHECK(p.batch_release);
    CHECK(StreamParser::isBatchRelease("One Piece 1089~1104"));
    CHECK(StreamParser::isBatchRelease("One Piece E1123~E1133"));
    CHECK(StreamParser::isBatchRelease("One Piece 001-574"));
    CHECK(StreamParser::isBatchRelease("Show S01-S05"));
    CHECK(StreamParser::isBatchRelease("Show Complete"));
    CHECK(!StreamParser::isBatchRelease("Show x265-10bit"));
    CHECK(!StreamParser::isBatchRelease("Amatsu Batch One Piece E1123 1080p"));
    CHECK(StreamParser::hasDualAudio("Dual"));
    CHECK(StreamParser::hasDualAudio("Dual Audio"));
    CHECK(StreamParser::hasDualAudio("Multi-Audio"));
    CHECK(!StreamParser::hasDualAudio("MultiSub"));
    CHECK(!StreamParser::hasDualAudio("Multiple Subtitle"));
    p = parse("Film.2020.1080p.WEB-DL.ENG.JPN.Dual.Audio.mkv");
    CHECK(p.dual_audio);
    CHECK(p.audio_languages.contains("English"));
    CHECK(p.audio_languages.contains("Japanese"));
    CHECK(p.audio_languages.contains("Multi"));
    p = parse("Film.2020.1080p.WEB-DL\n🇬🇧 🇯🇵");
    CHECK(p.audio_languages.contains("English"));
    CHECK(p.audio_languages.contains("Japanese"));
    p = parse("Film.2020.1080p.WEB-DL.[EN FR].mkv");
    CHECK(p.audio_languages.contains("English"));
    CHECK(p.audio_languages.contains("French"));
    p = parse("Film.2020.1080p.WEB-DL.HDR10+.AV1.10bit.REPACK2.PROPER.mkv");
    CHECK(p.hdr_format == HdrFormat::Hdr10Plus);
    CHECK(p.codec == Codec::Av1);
    CHECK(p.audio.bit_depth == 10);
    CHECK(p.repack_iteration == 2);
    CHECK(p.proper);
    p = parse("Film.2020.1080p.WEB-DL.VP9.6ch.mkv");
    CHECK(p.codec == Codec::Vp9);
    CHECK(p.audio.channels == 6);
    p = parse("Film.2020.1080p.BluRay.EXTENDED.DISC2.HC.mkv");
    CHECK(p.edition == "EXTENDED");
    CHECK(p.disc_index == 2);
    CHECK(p.hardcoded);
    p = parse("Show.2000-2008.S01.1080p.mkv");
    CHECK(p.year_range && p.year_range->first == 2000 && p.year_range->second == 2008);
    p = parse("Film.2020.2160p.WEB-DL\n1 GB");
    CHECK(p.scam_score == 3);
    s = row("Film.2020.1080p.WEB-DL\n2 GB 👤 25");
    s.size = 3000000000ULL;
    s.seeders = 0;
    p = StreamParser::parse(s);
    CHECK(p.size == 3000000000ULL);
    CHECK(p.seeders == 0);
    // Source/audio/container marker coverage is independent of the primary scoring policy.
    for (const auto &pair : QList<QPair<QString, Source>>{{"HC-HDRip", Source::CAM},
                                                          {"CAM", Source::CAM},
                                                          {"TS-CAM", Source::CAM},
                                                          {"HD-TS", Source::HDTS},
                                                          {"TELESYNC", Source::TS},
                                                          {"TSRip", Source::TS},
                                                          {"PDVDRip", Source::TS},
                                                          {"TELECINE", Source::TC},
                                                          {"HDTC", Source::TC},
                                                          {"DVDSCR", Source::SCR},
                                                          {"WEB-SCR", Source::SCR},
                                                          {"REMUX", Source::REMUX},
                                                          {"BRRip", Source::BluRay},
                                                          {"BDRip", Source::BluRay},
                                                          {"WEB-DL", Source::WebDl},
                                                          {"WEBRip", Source::WEBRip},
                                                          {"HDRip", Source::HDRip},
                                                          {"DVDRip", Source::DVDRip},
                                                          {"HDTV", Source::HDTV}}) {
        p = parse("Film.2020.1080p." + pair.first + ".mkv");
        CHECK(p.source == pair.second);
    }
    for (const auto &pair : QList<QPair<QString, AudioCodec>>{{"Atmos", AudioCodec::Atmos},
                                                              {"TrueHD", AudioCodec::TrueHd},
                                                              {"DTS-HD.MA", AudioCodec::DtsHdMa},
                                                              {"DTS", AudioCodec::Dts},
                                                              {"EAC3", AudioCodec::DdPlus},
                                                              {"AC3", AudioCodec::Ac3},
                                                              {"AAC", AudioCodec::Aac},
                                                              {"FLAC", AudioCodec::Flac},
                                                              {"Opus", AudioCodec::Opus}}) {
        p = parse("Film.2020.1080p." + pair.first + ".mkv");
        CHECK(p.audio.codec == pair.second);
    }
    for (const auto &ext : QStringList{"mkv", "mp4", "m4v", "avi", "webm", "mov", "ts", "wmv"}) {
        p = parse("Film.2020.1080p.WEB-DL." + ext);
        CHECK(p.container == ext);
    }
    CHECK(!StreamParser::isTrustedGroup("UNTRUSTED"));
    CHECK(parse("Film.2020.1080p.HLG.mkv").hdr_format == HdrFormat::Hlg);
    CHECK(parse("Film.2020.1080p.Dolby.Vision.mkv").hdr_format == HdrFormat::Dv);
    CHECK(parse("Film.2020.1080p.HDR.mkv").hdr_format == HdrFormat::Hdr10);
    CHECK(parse("Film.2020.720p.mkv").resolution == Resolution::P720);
    CHECK(parse("Film.2020.480p.mkv").resolution == Resolution::P480);
    CHECK(parse("Film.2020.FHD.mkv").resolution == Resolution::P1080);
    CHECK(parse("Film.2020.UHD.mkv").resolution == Resolution::UHD);
    CHECK(parse("Film.2020.1080p.MPEG2.mkv").codec == Codec::Mpeg2);
    s = row("Film.2020.1080p.WEB-DL.mkv");
    s.behavior_hints.insert("videoSize", -1);
    CHECK(!StreamParser::parse(s).size);
    s.behavior_hints.insert("videoSize", 1e30);
    CHECK(!StreamParser::parse(s).size);
    p = parse("Film.2020.1080p.WEB-DL\n👤 999999999999999999999");
    CHECK(!p.seeders);
    p = parse("Film.2020.1080p.WEB-DL\n💾 999999999999999999999 GB");
    CHECK(!p.size);
    // Underscores are release-name separators, not word-internal camouflage for CAM.
    for (const auto &pair : QList<QPair<QString, Source>>{{"CAM", Source::CAM},
                                                          {"HDCAM", Source::CAM},
                                                          {"HD_TS", Source::HDTS},
                                                          {"HDTS", Source::HDTS},
                                                          {"TELESYNC", Source::TS},
                                                          {"TSRip", Source::TS},
                                                          {"TELECINE", Source::TC},
                                                          {"DVDSCR", Source::SCR},
                                                          {"WEB_SCR", Source::SCR},
                                                          {"WEB_DL", Source::WebDl},
                                                          {"REMUX", Source::REMUX}}) {
        s = row("Movie_2020_" + pair.first + "_1080p.mkv");
        s.size = 2ULL * 1073741824;
        s.seeders = 200;
        p = StreamParser::parse(s);
        CHECK(p.source == pair.second);
        CHECK(p.resolution == Resolution::P1080);
        CHECK(p.parsed_title == "Movie");
        CHECK(p.filename == s.title);
    }
    p = parse("GUTS_2020_1080p_WEB_DL.mkv");
    CHECK(p.source == Source::WebDl);
    p = parse("CAMERA_2020_1080p_WEB_DL.mkv");
    CHECK(p.source == Source::WebDl);
    p = parse("Movie_2020_1080p_HDR10+_AV1_10bit_REPACK2_PROPER_EXTENDED_DUAL_AUDIO_ENG_JPN.mkv");
    CHECK(p.hdr_format == HdrFormat::Hdr10Plus);
    CHECK(p.codec == Codec::Av1);
    CHECK(p.audio.bit_depth == 10);
    CHECK(p.repack_iteration == 2);
    CHECK(p.proper);
    CHECK(p.edition == "EXTENDED");
    CHECK(p.dual_audio);
    CHECK(p.audio_languages.contains("English"));
    CHECK(p.audio_languages.contains("Japanese"));
    p = parse("Show_S02_1080p_WEB_DL.mkv");
    CHECK(p.season == 2);
    CHECK(p.season_pack);
    CHECK(p.batch_release);
    CHECK(StreamParser::hasDualAudio("Show_Dual_Audio_1080p"));
    CHECK(StreamParser::isBatchRelease("Show_Complete_1080p"));
    p = parse("The_Office_S03E10_A_Benihana_Christmas_720p_WEB_DL_x264.mkv");
    CHECK(p.episode_title == "A Benihana Christmas");
    // Real Torrentio pack: the selected file is episode 001, but the release
    // description identifies the containing multi-episode torrent.
    s = row("Большой Куш / One Piece / Ван-Пис [TV] [1-130 >700] "
            "[RUS(int) JAP ENG+Sub] [1999 WEBRip] [1080p]");
    s.behavior_hints.insert("filename",
                            "[pushPOP] Большой Куш - 001 [Special Edition HD] [1080p].mkv");
    p = StreamParser::parse(s);
    CHECK(p.episode == 1);
    CHECK(p.batch_release);
    CHECK(StreamParser::isBatchRelease("One Piece [1-130 >700]"));
    CHECK(StreamParser::isBatchRelease("Show [1-12] [1080p]"));
    CHECK(StreamParser::isBatchRelease("Show [2 - 9]"));
    CHECK(!StreamParser::isBatchRelease("Show [5.1] [1080p]"));
    CHECK(!StreamParser::isBatchRelease("Show [1999 WEBRip] [2020]"));
    CHECK(!StreamParser::isBatchRelease("Show [1-1]"));
    CHECK(!StreamParser::isBatchRelease("Show [9-1]"));
    CHECK(!StreamParser::isBatchRelease("Show 1-12 [1080p]"));
    CHECK(!StreamParser::isBatchRelease("Show [1-12bit]"));
    qInfo() << "parser_test:" << checks << "checks passed";
}
