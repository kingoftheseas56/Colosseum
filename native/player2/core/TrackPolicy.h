#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

namespace Colosseum::Player2 {

// A faithful C++ port of the production player's qml/TrackLanguage.js selection logic, so Player 2
// auto-selects the same audio/subtitle track the current player would. This is parity, not a new
// policy: language match GATES selection (a non-preferred-language track scores 0 and is never
// auto-picked); source/default/forced are only tie-breakers among language matches.
struct PolicyTrack
{
    QString id;
    QString lang;
    QString title;
    QString label;
    bool external = false;
    bool isDefault = false;
    bool isForced = false;
};

struct SubtitlePolicyOptions
{
    QStringList preferredLanguages;      // already language-preference ordered (normalized or raw)
    QString blockedTrackWords;           // comma list, e.g. "commentary"
    bool forcedOnly = false;
    bool preferEmbeddedSubtitles = false;
};

struct TrackSelection
{
    int index = -1;                      // index into the input vector, -1 == no auto pick
    QString reason;                      // human/diagnostic reason
};

namespace TrackPolicy {

QString normalizeLang(const QString &value);
QStringList parseLanguageList(const QString &value, const QStringList &fallback = {});
QString trackLang(const PolicyTrack &track);
int languageScore(const PolicyTrack &track, const QStringList &preferredLangs);

TrackSelection pickBestAudioTrack(const QVector<PolicyTrack> &tracks,
                                  const QStringList &preferredLangs, const QString &blocked);
TrackSelection pickBestSubtitleTrack(const QVector<PolicyTrack> &tracks,
                                     const QStringList &preferredLangs,
                                     const SubtitlePolicyOptions &options);

} // namespace TrackPolicy

} // namespace Colosseum::Player2
