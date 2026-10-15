#include "TrackPolicy.h"

#include <QtCore/QHash>
#include <QtCore/QRegularExpression>

#include <algorithm>
#include <functional>

namespace Colosseum::Player2 {
namespace TrackPolicy {
namespace {

const QHash<QString, QString> &langAliases()
{
    static const QHash<QString, QString> aliases = {
        {"eng", "eng"}, {"en", "eng"}, {"english", "eng"},
        {"jpn", "jpn"}, {"ja", "jpn"}, {"jp", "jpn"}, {"japanese", "jpn"},
        {"spa", "spa"}, {"es", "spa"}, {"spanish", "spa"},
        {"fre", "fre"}, {"fra", "fre"}, {"fr", "fre"}, {"french", "fre"},
        {"ger", "ger"}, {"deu", "ger"}, {"de", "ger"}, {"german", "ger"},
        {"kor", "kor"}, {"ko", "kor"}, {"korean", "kor"},
        {"chi", "chi"}, {"zho", "chi"}, {"zh", "chi"}, {"chinese", "chi"}};
    return aliases;
}

QStringList blockedWords(const QString &value)
{
    QStringList out;
    for (const QString &part : value.split(QLatin1Char(','))) {
        const QString word = part.trimmed().toLower();
        if (!word.isEmpty())
            out.append(word);
    }
    return out;
}

// Drops tracks whose title/label contains any blocked word; if that would drop everything, the
// original list is returned (matching the JS: never leave zero candidates).
QVector<int> filterBlocked(const QVector<PolicyTrack> &tracks, const QString &blocked)
{
    const QStringList words = blockedWords(blocked);
    QVector<int> kept;
    if (words.isEmpty()) {
        for (int i = 0; i < tracks.size(); ++i)
            kept.append(i);
        return kept;
    }
    for (int i = 0; i < tracks.size(); ++i) {
        const QString hay = (tracks[i].title + QLatin1Char(' ') + tracks[i].label).toLower();
        bool hit = false;
        for (const QString &word : words) {
            if (hay.contains(word)) { hit = true; break; }
        }
        if (!hit)
            kept.append(i);
    }
    if (kept.isEmpty()) {
        for (int i = 0; i < tracks.size(); ++i)
            kept.append(i);
    }
    return kept;
}

int sourceScore(const PolicyTrack &track, bool preferEmbedded)
{
    if (preferEmbedded)
        return track.external ? 1 : 3;
    return track.external ? 3 : 1;
}

// Highest score wins; ties break by original index (stable). Returns -1 if the best score is <= 0.
TrackSelection bestByScore(const QVector<PolicyTrack> &tracks, const QVector<int> &candidates,
                           const std::function<int(const PolicyTrack &)> &scoreFn,
                           const QString &kind)
{
    int bestIndex = -1;
    int bestScore = 0;
    for (int c : candidates) {
        const int score = scoreFn(tracks[c]);
        if (score > bestScore) {
            bestScore = score;
            bestIndex = c;
        }
    }
    if (bestIndex < 0 || bestScore <= 0)
        return {-1, QStringLiteral("no %1 language match").arg(kind)};
    return {bestIndex, QStringLiteral("%1 score %2").arg(kind).arg(bestScore)};
}

} // namespace

QString normalizeLang(const QString &value)
{
    QString raw = value.trimmed().toLower();
    if (raw.isEmpty())
        return QString();
    raw.remove(QRegularExpression(QStringLiteral("[^a-z]")));
    return langAliases().value(raw, raw);
}

QStringList parseLanguageList(const QString &value, const QStringList &fallback)
{
    QStringList out;
    for (const QString &part : value.split(QLatin1Char(','))) {
        const QString n = normalizeLang(part);
        if (!n.isEmpty() && !out.contains(n))
            out.append(n);
    }
    return out.isEmpty() ? fallback : out;
}

QString trackLang(const PolicyTrack &track)
{
    const QString lang = normalizeLang(track.lang);
    if (!lang.isEmpty())
        return lang;
    const QString text = (track.title + QLatin1Char(' ') + track.label).toLower();
    if (text.contains(QRegularExpression(QStringLiteral("\\b(japanese|jpn|ja)\\b"))))
        return QStringLiteral("jpn");
    if (text.contains(QRegularExpression(QStringLiteral("\\b(english|eng|en)\\b"))))
        return QStringLiteral("eng");
    return QString();
}

int languageScore(const PolicyTrack &track, const QStringList &preferredLangs)
{
    const QString lang = trackLang(track);
    if (lang.isEmpty())
        return 0;
    const int idx = preferredLangs.indexOf(lang);
    return idx < 0 ? 0 : std::max(1, 100 - idx * 10);
}

TrackSelection pickBestAudioTrack(const QVector<PolicyTrack> &tracks,
                                  const QStringList &preferredLangs, const QString &blocked)
{
    const QVector<int> candidates = filterBlocked(tracks, blocked);
    return bestByScore(tracks, candidates, [&](const PolicyTrack &t) {
        const int ls = languageScore(t, preferredLangs);
        if (ls <= 0)
            return 0;
        return ls + (t.isDefault ? 3 : 0);
    }, QStringLiteral("audio"));
}

TrackSelection pickBestSubtitleTrack(const QVector<PolicyTrack> &tracks,
                                     const QStringList &preferredLangs,
                                     const SubtitlePolicyOptions &options)
{
    const QVector<int> candidates = filterBlocked(tracks, options.blockedTrackWords);
    return bestByScore(tracks, candidates, [&](const PolicyTrack &t) {
        if (options.forcedOnly && !t.isForced)
            return 0;
        if (!options.forcedOnly && t.isForced)
            return 0;
        const int ls = languageScore(t, preferredLangs);
        if (ls <= 0)
            return 0;
        return ls + sourceScore(t, options.preferEmbeddedSubtitles) + (t.isDefault ? 2 : 0);
    }, QStringLiteral("subtitle"));
}

} // namespace TrackPolicy
} // namespace Colosseum::Player2
