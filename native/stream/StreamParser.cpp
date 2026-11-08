// Adapted from Harbor parser.rs, MIT, commit 431c447977f8bc422d49ede47ad9b2681edb1649.
// Copyright (c) Harbor contributors. See THIRD_PARTY_NOTICES.md.
// Batch/dual-audio detection follows Tankoban 2 AutoSourcePicker, commit
// d6d804d6ce899c2c4aef37a2345e4d67de970acc.
#include "StreamParser.h"
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace colosseum::stream {
namespace {
using Rx = QRegularExpression;
bool has(const Rx &r, const QString &s) { return r.match(s).hasMatch(); }
// Regex word boundaries treat '_' as a word character, but release names use it
// as a separator. Normalize only the technical scanning view; keep raw data and
// the selected filename intact for identity, diagnostics and downstream playback.
QString technicalText(QString text) { return text.replace('_', '.'); }
QString normalizedGroup(QString s) {
    s = s.toUpper();
    s.remove(Rx(QStringLiteral("[^A-Z0-9]")));
    return s;
}
static const Rx LANG_RX(QString::fromUtf8(
    R"rx((?i)\b(ENG(?:LISH)?|ITA(?:LIAN)?|RUS(?:SIAN)?|HIN(?:DI)?|ESP|SPA(?:NISH)?|LAT(?:INO)?|CASTELLANO|KOR(?:EAN)?|JPN|JAPANESE|JAP|CHN|CHI(?:NESE)?|ZHO|MAN(?:DARIN)?|CANTONESE|POR(?:TUGUESE)?|PTBR|DUBLADO|GER(?:MAN)?|DEU|FRA|FRENCH|FRE|VFF|VFQ|VOSTFR|TUR(?:KISH)?|ARA(?:BIC)?|TAM(?:IL)?|TEL(?:UGU)?|CES|CZECH|CZE|DAN(?:ISH)?|FIN(?:NISH)?|HEB(?:REW)?|HUN(?:GARIAN)?|NLD|DUTCH|DUT|NOR(?:WEGIAN)?|POL(?:ISH)?|RON|ROM|ROMANIAN|SWE(?:DISH)?|THA|THAI|UKR(?:AINIAN)?|VIE(?:TNAMESE)?|MULTI|DUAL)\b)rx"));
static const Rx FLAG_RX(QString::fromUtf8(R"rx([\x{1F1E6}-\x{1F1FF}][\x{1F1E6}-\x{1F1FF}])rx"));
static const Rx ISO_PAIR_RX(QString::fromUtf8(
    R"rx((?i)(?:^|[\.\-_\s\[(])(EN|FR|ES|IT|DE|PT|RU|JA|JP|KO|KR|ZH|CN|HI|AR|TR|NL|PL|RO|SV|SE|DA|FI|CS|CZ|HU|TH|UK|UA|VI|GB|US|MX|BR|CA|AU|NZ|TW|HK|IL|HE|SA|AE|EG|GR|ID|MY|PH|IR|FA)(?:[\.\-_\s\])]|$))rx"));
static const Rx TORRENTIO_NOISE_PREFIX_RX(QString::fromUtf8(
    R"rx(^[\s\x{1F464}\x{1F465}\x{1F4BE}\x{1F4E6}\x{26A1}\x{1F310}\x{1F4FA}\x{1F3AC}\x{1F50A}]+)rx"));
static const Rx TORRENTIO_NOISE_SUFFIX_RX(QString::fromUtf8(
    R"rx([\s\x{1F464}\x{1F465}\x{1F4BE}\x{1F4E6}\x{26A1}\x{1F310}\x{1F4FA}\x{1F3AC}\x{1F50A}]+$)rx"));
static const Rx CONTAINER_RX(QString::fromUtf8(R"rx((?i)\.(mkv|mp4|m4v|avi|webm|mov|ts|wmv)\b)rx"));
static const Rx SIZE_RX(QString::fromUtf8(
    R"rx((?i)(\d{1,3}(?:[ ,]\d{3})+(?:[.,]\d{1,2})?|\d[\d.,]*\d|\d)\s*(GB|MB|TB|GiB|MiB|TiB)\b)rx"));
static const Rx SEEDERS_RX(
    QString::fromUtf8(R"rx((?i)(?:\x{1F465}|\x{1F464}|S:|seeds?:?|\bS\s*=\s*)\s*(\d+))rx"));
static const Rx ANIME_HASH_RX(QString::fromUtf8(R"rx((?i)\[([0-9A-F]{8})\])rx"));
static const Rx CHANNELS_RX(QString::fromUtf8(R"rx(\b(7\.1|5\.1|6\.1|2\.1|2\.0)\b)rx"));
static const Rx BIT_DEPTH_RX(QString::fromUtf8(R"rx((?i)\b(8|10|12)\s*bit\b)rx"));
static const Rx REPACK_RX(QString::fromUtf8(R"rx((?i)\bREPACK(\d+)?\b)rx"));
static const Rx REMUX_RX(QString::fromUtf8(R"rx((?i)\bRemux\b)rx"));
static const Rx HARDCODED_RX(QString::fromUtf8(R"rx((?i)\b(HC|HARDCODED|HARDSUB)\b)rx"));
static const Rx YEAR_RANGE_RX(QString::fromUtf8(R"rx(\b(19\d\d|20\d\d)[\-\.](19\d\d|20\d\d)\b)rx"));
static const Rx DISC_RX(QString::fromUtf8(R"rx((?i)\bDISC\s*(\d+)\b)rx"));
static const Rx EDITION_RX(QString::fromUtf8(
    R"rx((?i)\b(IMAX|EXTENDED|DIRECTORS?[.\s]?CUT|THEATRICAL|UNRATED|UNCUT|REMASTERED|RESTORATION|CRITERION|OPEN[.\s]?MATTE|HYBRID)\b)rx"));
static const Rx QUALITY_STOP_RX(QString::fromUtf8(
    R"rx((?i)\.(?:480p|576p|720p|1080p|1440p|2160p|4k|uhd|hdr|hdr10|dv|dovi|bluray|bdrip|brrip|web[\.\-]?dl|webrip|hdrip|hdtv|remux|cam|ts|hdts|tc|scr|x264|x265|hevc|avc|h\.?264|h\.?265|av1|aac|ac3|ddp?|eac3|dts|truehd|atmos|flac|opus|10bit|8bit|repack|proper|extended|directors?|imax|hybrid|hdr10\+|repack\d?|multi|dual|dubbed|sub|subbed|complete|amzn|nf|hulu|max|atvp|dsnp))rx"));
static const Rx FN_TORRENTIO_PREFIX_RX(QString::fromUtf8(
    R"rx((?i)^(?:torrentio|comet|mediafusion|aiostreams|knightcrawler|jackettio|torbox)\b)rx"));
static const Rx
    FN_QUALITY_ONLY_RX(QString::fromUtf8(R"rx((?i)^(?:4k|1080p|720p|480p|sd|hd|hdr|dv|uhd)$)rx"));
static const Rx FN_NOISE_LEAD_RX(QString::fromUtf8(
    R"rx(^[\x{1F464}\x{1F465}\x{1F4BE}\x{1F4E6}\x{26A1}\x{1F310}\x{1F4FA}\x{1F3AC}\x{1F50A}])rx"));
static const Rx
    FN_LABEL_PREFIX_RX(QString::fromUtf8(R"rx((?i)^(?:size|seeders?|peers?|languages?)\s*[:=])rx"));
static const Rx
    FN_LIBRARY_LABEL_RX(QString::fromUtf8(R"rx((?i)^\[(?:RD|TB|AD|PM|DL)\+\]\s+\S+\s+library)rx"));
static const Rx FN_YEAR_RX(QString::fromUtf8(R"rx(\b(?:19|20)\d{2}\b)rx"));
static const Rx FN_RES_NUM_RX(QString::fromUtf8(R"rx((?i)\b\d{3,4}p\b)rx"));
static const Rx FN_RES_4K_RX(QString::fromUtf8(R"rx((?i)\b(?:4k|uhd|2160p)\b)rx"));
static const Rx FN_EPISODE_RX(QString::fromUtf8(R"rx((?i)\bS\d{1,2}E\d{1,3}\b)rx"));
static const Rx FN_SOURCE_RX(QString::fromUtf8(
    R"rx((?i)\b(?:Blu[\.\-]?Ray|WEB[\.\-]?DL|WEBRip|HDRip|BDRip|HDTV|REMUX|Remux|HDCAM|TELESYNC|TELECINE|CAM|HDTS|DVDRip)\b)rx"));
static const Rx FN_CODEC_RX(
    QString::fromUtf8(R"rx((?i)\b(?:x264|x265|HEVC|AVC|h264|h265|AV1|MPEG2|MPEG-2)\b)rx"));
static const Rx FN_CONTAINER_RX(QString::fromUtf8(R"rx((?i)\.(?:mkv|mp4|m4v|avi|ts)\b)rx"));
static const Rx
    SEASON_PACK_RX(QString::fromUtf8(R"rx((?i)\b(complete|season[\s\.]?pack|s\d{1,2})\b)rx"));
static const Rx SEASON_PACK_BARE_S_RX(QString::fromUtf8(R"rx((?i)\bs\d{1,2}E)rx"));
static const Rx EPTITLE_TRIM_LEAD_RX(QString::fromUtf8(R"rx(^[\.\-_\s]+)rx"));
static const Rx EPTITLE_DOTS_RX(QString::fromUtf8(R"rx([\.\-_]+)rx"));
static const Rx EPTITLE_SPACES_RX(QString::fromUtf8(R"rx(\s+)rx"));
static const Rx EPTITLE_BAD_RX(QString::fromUtf8(R"rx((?i)^(?:e\d+|episode|hdtv|webrip)$)rx"));
static const Rx GROUP_NORMALIZE_RX(QString::fromUtf8(R"rx([^A-Z0-9])rx"));
static const Rx INVISIBLE_RX(QString::fromUtf8(R"rx([\x{200B}-\x{200D}\x{2060}\x{FEFF}])rx"));
static const Rx VARIATION_SELECTOR_RX(QString::fromUtf8(R"rx(\x{FE0F})rx"));
static const QList<QPair<Rx, HdrFormat>> HDR_FORMATS = {
    {Rx(QString::fromUtf8(
         R"rx((?i)\bDV[+\-\s.]?HDR10\+?\b|\bDoVi[+\-\s.]?HDR10\+?\b|\bDolby[\.\s]?Vision[+\-\s.]?HDR10\+?\b)rx")),
     HdrFormat::DvHdr10},
    {Rx(QString::fromUtf8(R"rx((?i)\bDV\b|\bDoVi\b|\bDolby[\.\s]?Vision\b)rx")), HdrFormat::Dv},
    {Rx(QString::fromUtf8(R"rx((?i)\bHDR10\+(?![A-Za-z0-9]))rx")), HdrFormat::Hdr10Plus},
    {Rx(QString::fromUtf8(R"rx((?i)\bHLG\b)rx")), HdrFormat::Hlg},
    {Rx(QString::fromUtf8(R"rx((?i)\bHDR10?\b|\bHDR\b)rx")), HdrFormat::Hdr10},
};
static const QList<QPair<Rx, Source>> SOURCE_RX = {
    {Rx(QString::fromUtf8(R"rx((?i)\bHC[\s._\-]?(?:HDRip|HD[\s._\-]?Rip|CAM(?:Rip)?)\b)rx")),
     Source::CAM},
    {Rx(QString::fromUtf8(
         R"rx((?i)\b(?:HD|Clean|New|HQ|TS)[\s._\-]?CAM(?:Rip)?\b|\bCAM(?:Rip)?\b)rx")),
     Source::CAM},
    {Rx(QString::fromUtf8(R"rx((?i)\bHD[\s._\-]?TS\b|\bHDTS\b)rx")), Source::HDTS},
    {Rx(QString::fromUtf8(R"rx((?i)\bTELESYNC\b|\bTS[\s._\-]?Rip\b|\bPDVDRip\b)rx")), Source::TS},
    {Rx(QString::fromUtf8(R"rx((?i)\bTELECINE\b|\bHD[\s._\-]?TC\b)rx")), Source::TC},
    {Rx(QString::fromUtf8(
         R"rx((?i)\bSCREENER\b|\bDVDSCR\b|\bDVDScreener\b|\bBDSCR\b|\bWEB[\s._\-]?SCR\b|\bSCR\b)rx")),
     Source::SCR},
    {Rx(QString::fromUtf8(R"rx((?i)\bRemux\b)rx")), Source::REMUX},
    {Rx(QString::fromUtf8(R"rx((?i)\bBluRay\b|\bBDRip\b|\bBRRip\b)rx")), Source::BluRay},
    {Rx(QString::fromUtf8(R"rx((?i)\bWEB[\.\-]?DL\b)rx")), Source::WebDl},
    {Rx(QString::fromUtf8(R"rx((?i)\bWEBRip\b|\bWEB-Rip\b)rx")), Source::WEBRip},
    {Rx(QString::fromUtf8(R"rx((?i)\bHDRip\b)rx")), Source::HDRip},
    {Rx(QString::fromUtf8(R"rx((?i)\bDVDRip\b)rx")), Source::DVDRip},
    {Rx(QString::fromUtf8(R"rx((?i)\bHDTV\b)rx")), Source::HDTV},
};
static const QList<QPair<Rx, AudioCodec>> AUDIO_CODEC_RX = {
    {Rx(QString::fromUtf8(R"rx((?i)\bAtmos\b)rx")), AudioCodec::Atmos},
    {Rx(QString::fromUtf8(R"rx((?i)\bTrueHD\b)rx")), AudioCodec::TrueHd},
    {Rx(QString::fromUtf8(R"rx((?i)\bDTS-HD\.?MA\b|\bDTS\.?HD\.?MA\b)rx")), AudioCodec::DtsHdMa},
    {Rx(QString::fromUtf8(R"rx((?i)\bDTS\b)rx")), AudioCodec::Dts},
    {Rx(QString::fromUtf8(R"rx((?i)\bDDP?5?\.?1\+?\b|\bE-?AC3\b|\bDD\+\b)rx")), AudioCodec::DdPlus},
    {Rx(QString::fromUtf8(R"rx((?i)\bAC3\b)rx")), AudioCodec::Ac3},
    {Rx(QString::fromUtf8(R"rx((?i)\bAAC\b)rx")), AudioCodec::Aac},
    {Rx(QString::fromUtf8(R"rx((?i)\bFLAC\b)rx")), AudioCodec::Flac},
    {Rx(QString::fromUtf8(R"rx((?i)\bOpus\b)rx")), AudioCodec::Opus},
};
static const QMap<QString, QString> LANG_TOKENS = {
    {QStringLiteral("ENG"), QStringLiteral("English")},
    {QStringLiteral("ENGLISH"), QStringLiteral("English")},
    {QStringLiteral("ITA"), QStringLiteral("Italian")},
    {QStringLiteral("ITALIAN"), QStringLiteral("Italian")},
    {QStringLiteral("RUS"), QStringLiteral("Russian")},
    {QStringLiteral("RUSSIAN"), QStringLiteral("Russian")},
    {QStringLiteral("HIN"), QStringLiteral("Hindi")},
    {QStringLiteral("HINDI"), QStringLiteral("Hindi")},
    {QStringLiteral("ESP"), QStringLiteral("Spanish")},
    {QStringLiteral("SPA"), QStringLiteral("Spanish")},
    {QStringLiteral("SPANISH"), QStringLiteral("Spanish")},
    {QStringLiteral("LAT"), QStringLiteral("Spanish")},
    {QStringLiteral("LATINO"), QStringLiteral("Spanish")},
    {QStringLiteral("CASTELLANO"), QStringLiteral("Spanish")},
    {QStringLiteral("KOR"), QStringLiteral("Korean")},
    {QStringLiteral("KOREAN"), QStringLiteral("Korean")},
    {QStringLiteral("JPN"), QStringLiteral("Japanese")},
    {QStringLiteral("JAPANESE"), QStringLiteral("Japanese")},
    {QStringLiteral("JAP"), QStringLiteral("Japanese")},
    {QStringLiteral("CHN"), QStringLiteral("Chinese")},
    {QStringLiteral("CHI"), QStringLiteral("Chinese")},
    {QStringLiteral("CHINESE"), QStringLiteral("Chinese")},
    {QStringLiteral("ZHO"), QStringLiteral("Chinese")},
    {QStringLiteral("MAN"), QStringLiteral("Chinese")},
    {QStringLiteral("MANDARIN"), QStringLiteral("Chinese")},
    {QStringLiteral("CANTONESE"), QStringLiteral("Chinese")},
    {QStringLiteral("POR"), QStringLiteral("Portuguese")},
    {QStringLiteral("PORTUGUESE"), QStringLiteral("Portuguese")},
    {QStringLiteral("PTBR"), QStringLiteral("Portuguese")},
    {QStringLiteral("DUBLADO"), QStringLiteral("Portuguese")},
    {QStringLiteral("GER"), QStringLiteral("German")},
    {QStringLiteral("GERMAN"), QStringLiteral("German")},
    {QStringLiteral("DEU"), QStringLiteral("German")},
    {QStringLiteral("FRA"), QStringLiteral("French")},
    {QStringLiteral("FRENCH"), QStringLiteral("French")},
    {QStringLiteral("FRE"), QStringLiteral("French")},
    {QStringLiteral("VFF"), QStringLiteral("French")},
    {QStringLiteral("VFQ"), QStringLiteral("French")},
    {QStringLiteral("VOSTFR"), QStringLiteral("French")},
    {QStringLiteral("TUR"), QStringLiteral("Turkish")},
    {QStringLiteral("TURKISH"), QStringLiteral("Turkish")},
    {QStringLiteral("ARA"), QStringLiteral("Arabic")},
    {QStringLiteral("ARABIC"), QStringLiteral("Arabic")},
    {QStringLiteral("TAM"), QStringLiteral("Tamil")},
    {QStringLiteral("TAMIL"), QStringLiteral("Tamil")},
    {QStringLiteral("TEL"), QStringLiteral("Telugu")},
    {QStringLiteral("TELUGU"), QStringLiteral("Telugu")},
    {QStringLiteral("CES"), QStringLiteral("Czech")},
    {QStringLiteral("CZECH"), QStringLiteral("Czech")},
    {QStringLiteral("CZE"), QStringLiteral("Czech")},
    {QStringLiteral("DAN"), QStringLiteral("Danish")},
    {QStringLiteral("DANISH"), QStringLiteral("Danish")},
    {QStringLiteral("FIN"), QStringLiteral("Finnish")},
    {QStringLiteral("FINNISH"), QStringLiteral("Finnish")},
    {QStringLiteral("HEB"), QStringLiteral("Hebrew")},
    {QStringLiteral("HEBREW"), QStringLiteral("Hebrew")},
    {QStringLiteral("HUN"), QStringLiteral("Hungarian")},
    {QStringLiteral("HUNGARIAN"), QStringLiteral("Hungarian")},
    {QStringLiteral("NLD"), QStringLiteral("Dutch")},
    {QStringLiteral("DUTCH"), QStringLiteral("Dutch")},
    {QStringLiteral("DUT"), QStringLiteral("Dutch")},
    {QStringLiteral("NOR"), QStringLiteral("Norwegian")},
    {QStringLiteral("NORWEGIAN"), QStringLiteral("Norwegian")},
    {QStringLiteral("POL"), QStringLiteral("Polish")},
    {QStringLiteral("POLISH"), QStringLiteral("Polish")},
    {QStringLiteral("RON"), QStringLiteral("Romanian")},
    {QStringLiteral("ROMANIAN"), QStringLiteral("Romanian")},
    {QStringLiteral("ROM"), QStringLiteral("Romanian")},
    {QStringLiteral("SWE"), QStringLiteral("Swedish")},
    {QStringLiteral("SWEDISH"), QStringLiteral("Swedish")},
    {QStringLiteral("THA"), QStringLiteral("Thai")},
    {QStringLiteral("THAI"), QStringLiteral("Thai")},
    {QStringLiteral("UKR"), QStringLiteral("Ukrainian")},
    {QStringLiteral("UKRAINIAN"), QStringLiteral("Ukrainian")},
    {QStringLiteral("VIE"), QStringLiteral("Vietnamese")},
    {QStringLiteral("VIETNAMESE"), QStringLiteral("Vietnamese")}};
static const QMap<QString, QString> FLAG_TO_LANGUAGE = {
    {QStringLiteral("US"), QStringLiteral("English")},
    {QStringLiteral("GB"), QStringLiteral("English")},
    {QStringLiteral("CA"), QStringLiteral("English")},
    {QStringLiteral("AU"), QStringLiteral("English")},
    {QStringLiteral("NZ"), QStringLiteral("English")},
    {QStringLiteral("IE"), QStringLiteral("English")},
    {QStringLiteral("ES"), QStringLiteral("Spanish")},
    {QStringLiteral("MX"), QStringLiteral("Spanish")},
    {QStringLiteral("AR"), QStringLiteral("Spanish")},
    {QStringLiteral("CO"), QStringLiteral("Spanish")},
    {QStringLiteral("PE"), QStringLiteral("Spanish")},
    {QStringLiteral("CL"), QStringLiteral("Spanish")},
    {QStringLiteral("IT"), QStringLiteral("Italian")},
    {QStringLiteral("DE"), QStringLiteral("German")},
    {QStringLiteral("AT"), QStringLiteral("German")},
    {QStringLiteral("CH"), QStringLiteral("German")},
    {QStringLiteral("FR"), QStringLiteral("French")},
    {QStringLiteral("BE"), QStringLiteral("French")},
    {QStringLiteral("LU"), QStringLiteral("French")},
    {QStringLiteral("JP"), QStringLiteral("Japanese")},
    {QStringLiteral("KR"), QStringLiteral("Korean")},
    {QStringLiteral("KP"), QStringLiteral("Korean")},
    {QStringLiteral("CN"), QStringLiteral("Chinese")},
    {QStringLiteral("HK"), QStringLiteral("Chinese")},
    {QStringLiteral("TW"), QStringLiteral("Chinese")},
    {QStringLiteral("SG"), QStringLiteral("Chinese")},
    {QStringLiteral("PT"), QStringLiteral("Portuguese")},
    {QStringLiteral("BR"), QStringLiteral("Portuguese")},
    {QStringLiteral("RU"), QStringLiteral("Russian")},
    {QStringLiteral("BY"), QStringLiteral("Russian")},
    {QStringLiteral("IN"), QStringLiteral("Hindi")},
    {QStringLiteral("PK"), QStringLiteral("Hindi")},
    {QStringLiteral("SA"), QStringLiteral("Arabic")},
    {QStringLiteral("AE"), QStringLiteral("Arabic")},
    {QStringLiteral("EG"), QStringLiteral("Arabic")},
    {QStringLiteral("IQ"), QStringLiteral("Arabic")},
    {QStringLiteral("JO"), QStringLiteral("Arabic")},
    {QStringLiteral("KW"), QStringLiteral("Arabic")},
    {QStringLiteral("LB"), QStringLiteral("Arabic")},
    {QStringLiteral("MA"), QStringLiteral("Arabic")},
    {QStringLiteral("QA"), QStringLiteral("Arabic")},
    {QStringLiteral("SY"), QStringLiteral("Arabic")},
    {QStringLiteral("TN"), QStringLiteral("Arabic")},
    {QStringLiteral("IL"), QStringLiteral("Hebrew")},
    {QStringLiteral("TR"), QStringLiteral("Turkish")},
    {QStringLiteral("NL"), QStringLiteral("Dutch")},
    {QStringLiteral("NO"), QStringLiteral("Norwegian")},
    {QStringLiteral("PL"), QStringLiteral("Polish")},
    {QStringLiteral("RO"), QStringLiteral("Romanian")},
    {QStringLiteral("MD"), QStringLiteral("Romanian")},
    {QStringLiteral("SE"), QStringLiteral("Swedish")},
    {QStringLiteral("DK"), QStringLiteral("Danish")},
    {QStringLiteral("FI"), QStringLiteral("Finnish")},
    {QStringLiteral("CZ"), QStringLiteral("Czech")},
    {QStringLiteral("HU"), QStringLiteral("Hungarian")},
    {QStringLiteral("TH"), QStringLiteral("Thai")},
    {QStringLiteral("UA"), QStringLiteral("Ukrainian")},
    {QStringLiteral("VN"), QStringLiteral("Vietnamese")},
    {QStringLiteral("GR"), QStringLiteral("Greek")},
    {QStringLiteral("ID"), QStringLiteral("Indonesian")},
    {QStringLiteral("MY"), QStringLiteral("Malay")},
    {QStringLiteral("PH"), QStringLiteral("Tagalog")},
    {QStringLiteral("IR"), QStringLiteral("Persian")}};
static const QMap<QString, QString> ISO_PAIR_TO_LANGUAGE = {
    {QStringLiteral("EN"), QStringLiteral("English")},
    {QStringLiteral("GB"), QStringLiteral("English")},
    {QStringLiteral("US"), QStringLiteral("English")},
    {QStringLiteral("CA"), QStringLiteral("English")},
    {QStringLiteral("AU"), QStringLiteral("English")},
    {QStringLiteral("NZ"), QStringLiteral("English")},
    {QStringLiteral("ES"), QStringLiteral("Spanish")},
    {QStringLiteral("MX"), QStringLiteral("Spanish")},
    {QStringLiteral("IT"), QStringLiteral("Italian")},
    {QStringLiteral("DE"), QStringLiteral("German")},
    {QStringLiteral("FR"), QStringLiteral("French")},
    {QStringLiteral("PT"), QStringLiteral("Portuguese")},
    {QStringLiteral("BR"), QStringLiteral("Portuguese")},
    {QStringLiteral("RU"), QStringLiteral("Russian")},
    {QStringLiteral("JA"), QStringLiteral("Japanese")},
    {QStringLiteral("JP"), QStringLiteral("Japanese")},
    {QStringLiteral("KO"), QStringLiteral("Korean")},
    {QStringLiteral("KR"), QStringLiteral("Korean")},
    {QStringLiteral("ZH"), QStringLiteral("Chinese")},
    {QStringLiteral("CN"), QStringLiteral("Chinese")},
    {QStringLiteral("TW"), QStringLiteral("Chinese")},
    {QStringLiteral("HK"), QStringLiteral("Chinese")},
    {QStringLiteral("HI"), QStringLiteral("Hindi")},
    {QStringLiteral("AR"), QStringLiteral("Arabic")},
    {QStringLiteral("SA"), QStringLiteral("Arabic")},
    {QStringLiteral("AE"), QStringLiteral("Arabic")},
    {QStringLiteral("EG"), QStringLiteral("Arabic")},
    {QStringLiteral("TR"), QStringLiteral("Turkish")},
    {QStringLiteral("NL"), QStringLiteral("Dutch")},
    {QStringLiteral("PL"), QStringLiteral("Polish")},
    {QStringLiteral("RO"), QStringLiteral("Romanian")},
    {QStringLiteral("SV"), QStringLiteral("Swedish")},
    {QStringLiteral("SE"), QStringLiteral("Swedish")},
    {QStringLiteral("DA"), QStringLiteral("Danish")},
    {QStringLiteral("FI"), QStringLiteral("Finnish")},
    {QStringLiteral("CS"), QStringLiteral("Czech")},
    {QStringLiteral("CZ"), QStringLiteral("Czech")},
    {QStringLiteral("HU"), QStringLiteral("Hungarian")},
    {QStringLiteral("TH"), QStringLiteral("Thai")},
    {QStringLiteral("UK"), QStringLiteral("Ukrainian")},
    {QStringLiteral("UA"), QStringLiteral("Ukrainian")},
    {QStringLiteral("VI"), QStringLiteral("Vietnamese")},
    {QStringLiteral("IL"), QStringLiteral("Hebrew")},
    {QStringLiteral("HE"), QStringLiteral("Hebrew")},
    {QStringLiteral("GR"), QStringLiteral("Greek")},
    {QStringLiteral("ID"), QStringLiteral("Indonesian")},
    {QStringLiteral("MY"), QStringLiteral("Malay")},
    {QStringLiteral("PH"), QStringLiteral("Tagalog")},
    {QStringLiteral("IR"), QStringLiteral("Persian")},
    {QStringLiteral("FA"), QStringLiteral("Persian")}};
static const QSet<QString> TRUSTED_GROUPS = {
    QStringLiteral("FRDS"),         QStringLiteral("FRAMESTOR"),
    QStringLiteral("FORM"),         QStringLiteral("EVO"),
    QStringLiteral("RARBG"),        QStringLiteral("ETHEL"),
    QStringLiteral("FLUX"),         QStringLiteral("QXR"),
    QStringLiteral("MEGUSTA"),      QStringLiteral("ION10"),
    QStringLiteral("PSA"),          QStringLiteral("AMIABLE"),
    QStringLiteral("GALAXYRG"),     QStringLiteral("WEBDV"),
    QStringLiteral("RZEROX"),       QStringLiteral("SIC"),
    QStringLiteral("TGX"),          QStringLiteral("NTB"),
    QStringLiteral("NTG"),          QStringLiteral("TEPES"),
    QStringLiteral("GECKOS"),       QStringLiteral("SUCCESSFULCRAB"),
    QStringLiteral("SUBSPLEASE"),   QStringLiteral("ERAI"),
    QStringLiteral("ERAIRAWS"),     QStringLiteral("JUDAS"),
    QStringLiteral("ASW"),          QStringLiteral("EMBER"),
    QStringLiteral("ANE"),          QStringLiteral("CLEO"),
    QStringLiteral("BEATRICERAWS"), QStringLiteral("AKIHITO"),
    QStringLiteral("VODES"),        QStringLiteral("NANDESUKA"),
    QStringLiteral("SMOL"),         QStringLiteral("TENRAISENSEI"),
    QStringLiteral("GST"),          QStringLiteral("ANIMEKAIZOKU"),
    QStringLiteral("REINFORCE"),    QStringLiteral("RAWS"),
    QStringLiteral("OZR"),          QStringLiteral("PURGATORY"),
    QStringLiteral("SHK"),          QStringLiteral("KOTUWA"),
    QStringLiteral("KIRION"),       QStringLiteral("COMMIE"),
    QStringLiteral("DAMEDESUYO"),   QStringLiteral("MTBB"),
    QStringLiteral("GJM"),          QStringLiteral("SOFCJ")};

QString cleanTitle(QString s) {
    s.remove(Rx(QStringLiteral(R"(^[.\s_]+|[.\s_(\-]+$)")));
    s.replace('.', ' ');
    s.replace('_', ' ');
    return s.simplified();
}
QString stripInvisibles(QString s) {
    s.remove(INVISIBLE_RX);
    s.remove(VARIATION_SELECTOR_RX);
    return s;
}
int filenameScore(const QString &rawLine) {
    const QString line = technicalText(rawLine);
    if (line.size() < 8 || has(FN_TORRENTIO_PREFIX_RX, line) || has(FN_QUALITY_ONLY_RX, line) ||
        has(FN_NOISE_LEAD_RX, line))
        return -100;
    static const Rx stats(
        R"((?i)^\[?\s*\d+\s*(?:seeders|seeds|peers)\b|^\[?\s*\d+(?:[.,]\d+)?\s*(?:GB|MB|TB)\b)");
    if (has(FN_LABEL_PREFIX_RX, line) || has(FN_LIBRARY_LABEL_RX, line) || has(stats, line))
        return -50;
    bool year = has(FN_YEAR_RX, line),
         resolution = has(FN_RES_NUM_RX, line) || has(FN_RES_4K_RX, line),
         episode = has(FN_EPISODE_RX, line);
    bool source = has(FN_SOURCE_RX, line), codec = has(FN_CODEC_RX, line),
         container = has(FN_CONTAINER_RX, line), dots = line.count('.') >= 3;
    if (!(year || resolution || episode || source || codec || container || dots))
        return -20;
    return (line.size() >= 20 ? 2 : 0) + (dots ? 3 : 0) + (year ? 2 : 0) + (resolution ? 2 : 0) +
           (episode ? 3 : 0) + (source ? 3 : 0) + (codec ? 1 : 0) + (container ? 2 : 0);
}
QString filenameHint(const Stream &s) {
    QString f = s.behavior_hints.value("filename").toString();
    if (f.isEmpty())
        f = s.behavior_hints.value("fileName").toString();
    return stripInvisibles(f).trimmed();
}
QString extractFilename(const Stream &s) {
    const auto hint = filenameHint(s);
    // A plausible actual filename is authoritative, unlike a provider's display badge.
    if (!hint.contains('\n') && !hint.contains('\r') && filenameScore(hint) >= 0)
        return hint;
    QString best;
    int bestScore = std::numeric_limits<int>::min();
    for (const auto &raw : QStringList{s.title, hint, s.description, s.name}) {
        const auto lines =
            stripInvisibles(raw).split(Rx(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
        for (QString line : lines) {
            line.remove(TORRENTIO_NOISE_PREFIX_RX);
            line.remove(TORRENTIO_NOISE_SUFFIX_RX);
            line = line.trimmed();
            if (line.isEmpty())
                continue;
            const int score = filenameScore(line);
            if (score > bestScore) {
                bestScore = score;
                best = line;
            }
        }
    }
    return best;
}
struct Ptt {
    QString title, resolution, codec, group;
    std::optional<int> year, season, episode, bitdepth;
    std::optional<double> channels;
    bool extended = false, theatrical = false, uncut = false, openmatte = false, hardcoded = false,
         proper = false, repack = false, remastered = false, unrated = false, criterion = false;
};
Ptt parsePtt(const QString &rawTitle) {
    const QString title = technicalText(rawTitle);
    Ptt p;
    qsizetype end = title.size();
    auto mark = [&](const QRegularExpressionMatch &m) {
        if (m.hasMatch() && m.capturedStart() > 0)
            end = std::min(end, m.capturedStart());
    };
    auto rx = [](const char *pattern) { return Rx(QString::fromUtf8(pattern)); };
    auto m = rx(R"([^a-zA-Z0-9][\(\[]?((?:19[0-9]|20[0-9])[0-9])[\)\]]?)").match(title);
    if (m.hasMatch()) {
        p.year = m.captured(1).toInt();
        mark(m);
    }
    for (const auto &pattern :
         {R"((?i)([0-9]{3,4}[pi]))", R"((?i)\b(4k))", R"((?i)(FHD|\b1080\b))", R"((?i)(UHD))"}) {
        m = rx(pattern).match(title);
        if (m.hasMatch()) {
            p.resolution = m.captured(1).toLower();
            if (p.resolution == "fhd" || p.resolution == "1080")
                p.resolution = "1080p";
            mark(m);
            break;
        }
    }
    const QList<QPair<const char *, bool *>> flags = {
        {R"((?i)\bEXTENDED(?:[\s.]CUT)?\b)", &p.extended},
        {R"((?i)\bTheatrical(?:[. ]Cut)?\b)", &p.theatrical},
        {R"((?i)\bUNCUT\b)", &p.uncut},
        {R"((?i)\bOPEN[. ]MATTE\b)", &p.openmatte},
        {R"((?i)\b(?:HC|HARDCODED)\b)", &p.hardcoded},
        {R"((?i)\b(?:REAL.)?PROPER\b)", &p.proper},
        {R"((?i)\b(?:REPACK\d*|RERIP)\b)", &p.repack},
        {R"((?i)\bRemaster(?:ed)?\b)", &p.remastered},
        {R"((?i)\b(?:unrated|uncensored)\b)", &p.unrated},
        {R"((?i)\bCriterion\b)", &p.criterion}};
    for (const auto &pair : flags) {
        m = rx(pair.first).match(title);
        if (m.hasMatch()) {
            *pair.second = true;
            mark(m);
        }
    }
    for (const auto &pattern :
         {R"((?i)h[-. ]?265|hevc)", R"((?i)h[-. ]?264|avc)",
          R"((?i)dvix|mpeg[-. ]?2|divx|xvid|x[-. ]?26[45]|\bav1\b|\bvp9\b)"}) {
        m = rx(pattern).match(title);
        if (m.hasMatch()) {
            p.codec = m.captured().toLower();
            p.codec.remove(rx("[ .-]"));
            mark(m);
            break;
        }
    }
    m = rx(R"((?<!\d)(7[.\s]1|6[.\s]1|5[.\s]1|2[.\s][01])\b)").match(title);
    if (m.hasMatch()) {
        auto ch = m.captured(1);
        ch.replace(' ', '.');
        p.channels = ch.toDouble();
        mark(m);
    }
    if (!p.channels) {
        m = rx(R"((?i)\b([268])ch\b)").match(title);
        if (m.hasMatch()) {
            p.channels = m.captured(1) == "8" ? 7.1 : m.captured(1) == "6" ? 5.1 : 2.0;
            mark(m);
        }
    }
    m = rx(R"((?i)\b(8|10|12|16|24)[-\s.]?bits?\b)").match(title);
    if (m.hasMatch()) {
        p.bitdepth = m.captured(1).toInt();
        mark(m);
    }
    m = rx(R"((?i)-[ \(\[]*(?:\w+[ \]\)]+)?(\w+(?:\.\w+)?)[\)\]]?(?:\.(?:mkv|mp4))?$)")
            .match(title);
    if (m.hasMatch()) {
        QString group = rawTitle.mid(m.capturedStart(1), m.capturedLength(1));
        group.remove(rx(R"((?i)\.(mkv|mp4)$)"));
        if (group.compare("mkv", Qt::CaseInsensitive) != 0 &&
            group.compare("mp4", Qt::CaseInsensitive) != 0 && !has(rx(R"(^\d+$)"), group)) {
            p.group = group;
            mark(m);
        }
    }
    // Anime release groups are leading bracketed labels, not the trailing CRC32.
    m = rx(R"(^\[([^\]]+)\]\s*)").match(title);
    if (m.hasMatch() && !has(rx(R"((?i)^(?:[0-9A-F]{8}|\d+p|\d+\s*seeders.*)$)"), m.captured(1)))
        p.group = rawTitle.mid(m.capturedStart(1), m.capturedLength(1));
    for (const auto &pattern :
         {R"((?i)([0-9]{1,3})xall)", R"((?i)S([0-9]{1,3})[ ._-]*E[0-9])",
          R"((?i)([0-9]{1,3})x[0-9])", R"((?i)(?:Saison|Season)[. _-]?([0-9]{1,3}))",
          R"((?i)\bS([0-9]{1,3})(?![0-9]))"}) {
        m = rx(pattern).match(title);
        if (m.hasMatch()) {
            p.season = m.captured(1).toInt();
            mark(m);
            break;
        }
    }
    for (const auto &pattern :
         {R"((?i)S[0-9]{1,3}[ ._-]*E([0-9]{1,5}))", R"((?i)[0-9]{1,3}x([0-9]{1,5}))",
          R"((?i)[eé]p(?:isode)?[. _-]?([0-9]{1,5}))",
          R"((?i)(?:^|[ ._-])E([0-9]{1,5})(?:v\d+)?\b)"}) {
        m = rx(pattern).match(title);
        if (m.hasMatch()) {
            p.episode = m.captured(1).toInt();
            mark(m);
            break;
        }
    }
    if (!p.episode) {
        // SubsPlease/Erai and similar absolute numbering: a spaced dash, not a movie year.
        m = rx(R"(\s-\s(\d{1,5})(?:v\d+)?(?:\s|\[|\(|$))").match(title);
        if (m.hasMatch()) {
            p.episode = m.captured(1).toInt();
            mark(m);
        }
    }
    QString raw = title.left(end);
    raw.remove(rx(R"(^\[[^\]]+\]\s*)"));
    p.title = cleanTitle(raw);
    return p;
}
Resolution resolutionOf(const QString &s) {
    if (s.contains("2160") || s == "4k" || s == "uhd")
        return Resolution::UHD;
    if (s.contains("1080"))
        return Resolution::P1080;
    if (s.contains("720"))
        return Resolution::P720;
    if (s.contains("480"))
        return Resolution::P480;
    return Resolution::SD;
}
Codec codecOf(QString s) {
    s = s.toLower();
    if (s.contains("265") || s == "hevc")
        return Codec::Hevc;
    if (s.contains("264") || s == "avc")
        return Codec::Avc;
    if (s.contains("av1"))
        return Codec::Av1;
    if (s.contains("vp9"))
        return Codec::Vp9;
    if (s.contains("mpeg"))
        return Codec::Mpeg2;
    return Codec::Other;
}
std::optional<double> sizeNumber(const QString &token) {
    const auto groups = token.split(Rx(QStringLiteral("[ .,]")), Qt::KeepEmptyParts);
    for (const auto &g : groups)
        if (g.isEmpty())
            return {};
    if (groups.size() == 1) {
        bool ok = false;
        auto n = token.toDouble(&ok);
        return ok ? std::optional<double>(n) : std::nullopt;
    }
    const auto tail = groups.last();
    bool thousands = false;
    if (groups.size() > 2) {
        thousands = true;
        for (qsizetype i = 1; i < groups.size(); ++i)
            if (groups.at(i).size() != 3)
                thousands = false;
    } else
        thousands = tail.size() == 3 && token.at(token.size() - tail.size() - 1) != '.';
    QString digits = token;
    digits.remove(Rx(QStringLiteral("[^0-9]")));
    if (!thousands)
        digits.insert(digits.size() - tail.size(), '.');
    bool ok = false;
    auto n = digits.toDouble(&ok);
    return ok ? std::optional<double>(n) : std::nullopt;
}
std::optional<quint64> parseSize(const QString &text) {
    const auto m = SIZE_RX.match(text);
    if (!m.hasMatch())
        return {};
    const auto number = sizeNumber(m.captured(1));
    if (!number || !std::isfinite(*number) || *number < 0)
        return {};
    const auto unit = m.captured(2).toLower();
    const double factor = unit.startsWith('t')   ? 1099511627776.0
                          : unit.startsWith('g') ? 1073741824.0
                                                 : 1048576.0;
    const double bytes = std::round(*number * factor);
    if (bytes < 0 || bytes >= double(std::numeric_limits<quint64>::max()))
        return {};
    return static_cast<quint64>(bytes);
}
QStringList parseLanguages(const QString &text) {
    QStringList result;
    auto add = [&](const QString &s) {
        if (!s.isEmpty() && !result.contains(s))
            result.append(s);
    };
    auto it = LANG_RX.globalMatch(text);
    while (it.hasNext()) {
        auto token = it.next().captured().toUpper();
        add(LANG_TOKENS.value(token, (token == "MULTI" || token == "DUAL") ? "Multi" : ""));
    }
    it = FLAG_RX.globalMatch(text);
    while (it.hasNext()) {
        const auto codes = it.next().captured().toUcs4();
        if (codes.size() != 2)
            continue;
        QString code;
        code += QChar(ushort(codes[0] - 0x1F1E6 + 'A'));
        code += QChar(ushort(codes[1] - 0x1F1E6 + 'A'));
        add(FLAG_TO_LANGUAGE.value(code));
    }
    // Lookarounds let adjacent language tokens share separators (EN FR JP).
    static const Rx pairs(QStringLiteral(
        R"((?i)(?<![A-Za-z0-9])(EN|FR|ES|IT|DE|PT|RU|JA|JP|KO|KR|ZH|CN|HI|AR|TR|NL|PL|RO|SV|SE|DA|FI|CS|CZ|HU|TH|UK|UA|VI|GB|US|MX|BR|CA|AU|NZ|TW|HK|IL|HE|SA|AE|EG|GR|ID|MY|PH|IR|FA)(?![A-Za-z0-9]))"));
    it = pairs.globalMatch(text);
    while (it.hasNext())
        add(ISO_PAIR_TO_LANGUAGE.value(it.next().captured(1).toUpper()));
    const bool multi = result.removeAll("Multi") > 0;
    if (result.size() > 1)
        result.prepend("Multi");
    else if (result.isEmpty() && multi)
        result.append("Multi");
    return result;
}
QString episodeTitle(const QString &rawFilename, const Ptt &p) {
    const QString filename = technicalText(rawFilename);
    if (!p.season || !p.episode)
        return {};
    const QString code =
        QString("S%1E%2").arg(*p.season, 2, 10, QChar('0')).arg(*p.episode, 2, 10, QChar('0'));
    const auto idx = filename.toUpper().indexOf(code);
    if (idx < 0)
        return {};
    QString after = filename.mid(idx + code.size());
    after.remove(EPTITLE_TRIM_LEAD_RX);
    const auto stop = QUALITY_STOP_RX.match(after);
    if (stop.hasMatch() && stop.capturedStart() > 0)
        after = after.left(stop.capturedStart());
    after.replace(EPTITLE_DOTS_RX, " ");
    after = after.simplified();
    if (after.size() < 2 || after.size() > 80 || has(EPTITLE_BAD_RX, after))
        return {};
    return after;
}
QString edition(const QString &text, const Ptt &p) {
    if (p.extended)
        return "EXTENDED";
    if (p.unrated)
        return "UNRATED";
    if (p.theatrical)
        return "THEATRICAL";
    if (p.uncut)
        return "UNCUT";
    if (p.remastered)
        return "REMASTERED";
    if (p.criterion)
        return "CRITERION";
    if (p.openmatte)
        return "OPEN MATTE";
    const auto m = EDITION_RX.match(text);
    QString e = m.captured(1).toUpper();
    e.replace(Rx(QStringLiteral("[.\\s]+")), " ");
    return e;
}
int scamScore(Source source, Resolution res, std::optional<quint64> size) {
    int score = 0;
    if (size) {
        quint64 floor = res == Resolution::UHD                               ? 5ULL * 1073741824
                        : res == Resolution::P1080                           ? 700ULL * 1048576
                        : (res == Resolution::P720 || res == Resolution::SD) ? 250ULL * 1048576
                                                                             : 0;
        if (*size < floor)
            score += 3;
    }
    if (res == Resolution::SD && source == Source::Other)
        score += 2;
    return score;
}
} // namespace

bool StreamParser::isTrustedGroup(const QString &normalized) {
    return TRUSTED_GROUPS.contains(normalized);
}
bool StreamParser::isBatchRelease(const QString &text) {
    // Tankoban 2: bare "Batch" is deliberately not a pack marker.
    static const Rx batch(QStringLiteral(
        R"((?i)\d\s*~\s*e?\d|(?<![A-Za-z0-9])\d{2,4}\s*-\s*\d{2,4}(?![A-Za-z])|\bs\d{1,3}\s*-\s*s?\d{1,3}\b|\b(?:complete\s*series|complete|season[ ._-]*pack|all[ ._-]*episodes)\b|\bs\d{1,3}e\d+[ ._-]*e\d+|\bs\d{1,3}e\d+\s*-\s*e?\d+|\b\d{1,3}x\d+\s*-\s*\d+)"));
    const QString scanned = technicalText(text);
    if (has(batch, scanned)) {
        return true;
    }
    // Some anime torrents describe their pack as "[1-130 >700]" while the
    // selected filename is episode 001. Accept a single-digit start only in a
    // complete bracketed numeric range, not loose numbers or technical tags.
    static const Rx bracketedRange(
        QStringLiteral(R"(\[\s*([0-9])\s*-\s*(\d{1,5})(?:\s*>\s*\d{1,5})?\s*\])"));
    auto ranges = bracketedRange.globalMatch(scanned);
    while (ranges.hasNext()) {
        const auto range = ranges.next();
        if (range.captured(2).toInt() > range.captured(1).toInt()) {
            return true;
        }
    }
    return false;
}
bool StreamParser::hasDualAudio(const QString &text) {
    static const Rx dual(
        QStringLiteral(R"((?i)\bdual\b|\bdual[\s._-]?audio\b|\bmulti[\s._-]?audio\b)"));
    return has(dual, technicalText(text));
}
ParsedStream StreamParser::parse(const Stream &stream) {
    ParsedStream out;
    out.stream = stream;
    out.filename = extractFilename(stream);
    out.match_text = stripInvisibles(
        QStringList{out.filename, stream.title, stream.description, stream.name}.join(' '));
    const QString text = technicalText(out.match_text);
    const auto p = parsePtt(out.filename.isEmpty() ? text : out.filename);
    out.parsed_title =
        p.title.isEmpty() ? (out.filename.isEmpty() ? text : out.filename).left(100) : p.title;
    out.resolution = resolutionOf(p.resolution);
    out.codec = codecOf(p.codec);
    for (const auto &pair : HDR_FORMATS)
        if (has(pair.first, text)) {
            out.hdr_format = pair.second;
            break;
        }
    for (const auto &pair : SOURCE_RX)
        if (has(pair.first, text)) {
            out.source = pair.second;
            break;
        }
    for (const auto &pair : AUDIO_CODEC_RX)
        if (has(pair.first, text)) {
            out.audio.codec = pair.second;
            break;
        }
    const auto channels = CHANNELS_RX.match(text);
    double ch = channels.hasMatch() ? channels.captured(1).toDouble() : p.channels.value_or(2.0);
    out.audio.channels = std::abs(ch - 7.1) < 0.05   ? 8
                         : std::abs(ch - 6.1) < 0.05 ? 7
                         : std::abs(ch - 5.1) < 0.05 ? 6
                         : std::abs(ch - 2.1) < 0.05 ? 3
                                                     : 2;
    auto m = BIT_DEPTH_RX.match(text);
    out.audio.bit_depth = m.hasMatch() ? std::optional<int>(m.captured(1).toInt()) : p.bitdepth;
    out.audio_languages = parseLanguages(text);
    out.size = stream.size;
    if (!out.size) {
        const auto value = stream.behavior_hints.value("videoSize");
        const double n = value.toDouble(-1);
        if (std::isfinite(n) && n > 0 && n < double(std::numeric_limits<quint64>::max()) &&
            std::floor(n) == n)
            out.size = static_cast<quint64>(n);
    }
    if (!out.size)
        out.size = parseSize(text);
    out.seeders = stream.seeders;
    if (!out.seeders) {
        m = SEEDERS_RX.match(text);
        if (!m.hasMatch()) {
            static const Rx after(QStringLiteral(R"((?i)\b(\d+)\s+(?:seeders|seeds)\b)"));
            m = after.match(text);
        }
        if (m.hasMatch()) {
            bool ok = false;
            auto n = m.captured(1).toULongLong(&ok);
            if (ok && n <= std::numeric_limits<quint32>::max())
                out.seeders = static_cast<quint32>(n);
        }
    }
    for (const auto &src : QStringList{filenameHint(stream), out.filename, text}) {
        m = CONTAINER_RX.match(src);
        if (m.hasMatch()) {
            out.container = m.captured(1).toLower();
            break;
        }
    }
    out.release_group = p.group;
    out.release_group_normalized = normalizedGroup(p.group);
    out.remux = has(REMUX_RX, text);
    out.edition = edition(text, p);
    out.year = p.year;
    m = YEAR_RANGE_RX.match(text);
    if (m.hasMatch()) {
        int a = m.captured(1).toInt(), b = m.captured(2).toInt();
        if (b > a && b - a < 30)
            out.year_range = std::make_pair(a, b);
    }
    out.season = p.season;
    out.episode = p.episode;
    out.episode_end = p.episode;
    // Preserve episode ranges; the trust stage can validate either inclusive endpoint.
    for (const auto &pattern :
         {R"((?i)(?:^|[^a-z0-9])s(\d{1,3})[ ._-]*e(\d{1,5})[ ._-]*e(\d{1,5})(?:\D|$))",
          R"((?i)(?:^|[^a-z0-9])s(\d{1,3})[ ._-]*e(\d{1,5})\s*-\s*e?(\d{1,5})(?:\D|$))",
          R"((?i)(?:^|[^a-z0-9])(\d{1,3})x(\d{1,5})\s*-\s*(\d{1,5})(?:\D|$))"}) {
        m = Rx(QString::fromUtf8(pattern)).match(out.filename);
        if (m.hasMatch()) {
            int a = m.captured(2).toInt(), b = m.captured(3).toInt();
            if (a > 0 && b >= a) {
                out.season = m.captured(1).toInt();
                out.episode = a;
                out.episode_end = b;
            }
            break;
        }
    }
    out.season_pack = p.season.has_value() && !p.episode.has_value() && has(SEASON_PACK_RX, text);
    out.batch_release = out.season_pack || isBatchRelease(text);
    out.dual_audio = hasDualAudio(text);
    m = DISC_RX.match(text);
    if (m.hasMatch())
        out.disc_index = m.captured(1).toInt();
    m = REPACK_RX.match(text);
    out.repack_iteration =
        m.hasMatch() ? (m.captured(1).isEmpty() ? 1 : m.captured(1).toInt()) : (p.repack ? 1 : 0);
    out.proper = p.proper;
    out.hardcoded = p.hardcoded || has(HARDCODED_RX, text);
    m = ANIME_HASH_RX.match(text);
    out.anime_hash = m.captured(1).toUpper();
    out.episode_title = episodeTitle(out.filename, p);
    out.scam_score = scamScore(out.source, out.resolution, out.size);
    return out;
}
} // namespace colosseum::stream
