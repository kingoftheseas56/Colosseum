// Task 10 - parity port of qml/TrackLanguage.js. Pure logic, no device.

#include "player2/core/TrackPolicy.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Colosseum::Player2;
using namespace Colosseum::Player2::TrackPolicy;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

PolicyTrack track(const QString &id, const QString &lang, const QString &title,
                  bool isDefault = false, bool isForced = false, bool external = false)
{
    PolicyTrack t;
    t.id = id;
    t.lang = lang;
    t.title = title;
    t.external = external;
    t.isDefault = isDefault;
    t.isForced = isForced;
    return t;
}

void languageNormalizationMatchesJs()
{
    require(normalizeLang(QStringLiteral("en")) == QStringLiteral("eng"), "en -> eng");
    require(normalizeLang(QStringLiteral("Japanese")) == QStringLiteral("jpn"), "Japanese -> jpn");
    require(normalizeLang(QStringLiteral("  FR ")) == QStringLiteral("fre"), "fr alias");
    require(normalizeLang(QStringLiteral("")).isEmpty(), "empty stays empty");
    const QStringList prefs = parseLanguageList(QStringLiteral("en, jp, xx"));
    require(prefs == QStringList({QStringLiteral("eng"), QStringLiteral("jpn"), QStringLiteral("xx")}),
            "parseLanguageList normalizes + dedups");
}

void audioPrefersLanguageThenDefault()
{
    QVector<PolicyTrack> tracks{
        track(QStringLiteral("1"), QStringLiteral("jpn"), QStringLiteral("Japanese")),
        track(QStringLiteral("2"), QStringLiteral("eng"), QStringLiteral("English"), /*default*/ true),
        track(QStringLiteral("3"), QStringLiteral("eng"), QStringLiteral("English commentary")),
    };
    const QStringList prefs{QStringLiteral("eng"), QStringLiteral("jpn")};
    // English preferred over Japanese; commentary blocked; default English wins.
    const TrackSelection pick = pickBestAudioTrack(tracks, prefs, QStringLiteral("commentary"));
    require(pick.index == 1, "audio should pick the default English track, not commentary");

    // A track in no preferred language must never be auto-picked (language gate).
    QVector<PolicyTrack> onlyGerman{track(QStringLiteral("9"), QStringLiteral("ger"), QStringLiteral("German"))};
    require(pickBestAudioTrack(onlyGerman, prefs, QString()).index == -1,
            "no preferred-language audio must not be auto-picked");
}

void subtitleForcedAndSourcePolicy()
{
    QVector<PolicyTrack> tracks{
        track(QStringLiteral("s1"), QStringLiteral("eng"), QStringLiteral("English")),
        track(QStringLiteral("s2"), QStringLiteral("eng"), QStringLiteral("English forced"),
              false, /*forced*/ true),
        track(QStringLiteral("s3"), QStringLiteral("eng"), QStringLiteral("English (ext)"),
              false, false, /*external*/ true),
    };
    const QStringList prefs{QStringLiteral("eng")};

    SubtitlePolicyOptions normal;
    normal.preferredLanguages = prefs;
    // Not forcedOnly: forced tracks are excluded; external preferred over embedded by default.
    const TrackSelection pick = pickBestSubtitleTrack(tracks, prefs, normal);
    require(pick.index == 2, "subtitle should prefer the external English track");

    SubtitlePolicyOptions forcedOnly;
    forcedOnly.preferredLanguages = prefs;
    forcedOnly.forcedOnly = true;
    require(pickBestSubtitleTrack(tracks, prefs, forcedOnly).index == 1,
            "forcedOnly must select the forced track");

    SubtitlePolicyOptions preferEmbedded;
    preferEmbedded.preferredLanguages = prefs;
    preferEmbedded.preferEmbeddedSubtitles = true;
    const TrackSelection embedded = pickBestSubtitleTrack(tracks, prefs, preferEmbedded);
    require(embedded.index == 0, "preferEmbedded should pick the embedded English track");
}

} // namespace

int main()
{
    try {
        languageNormalizationMatchesJs();
        audioPrefersLanguageThenDefault();
        subtitleForcedAndSourcePolicy();
    } catch (const std::exception &error) {
        std::cerr << "player2_track_policy_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_track_policy_test: PASS\n";
    return EXIT_SUCCESS;
}
