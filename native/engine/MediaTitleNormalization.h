#pragma once
// Shared catalog title matching, independent of any product world.
#include <QString>
#include <QList>
#include <QSet>
#include <QRegularExpression>

namespace MediaTitleNormalization {

// ── Title cleaner (port of TB2 ScannerUtils::cleanMediaFolderTitle) ────
inline const QRegularExpression kShowTitleNoiseRe(
    QStringLiteral(
        "(?i)\\b("
        "2160p|1080p|720p|480p|x264|x265|h\\.?264|h\\.?265|hevc|10bit|8bit|hdr|dv|"
        "webrip|web[\\s.\\-]?dl|bluray|bdrip|dvdrip|hdtv|remux|aac|dts|ddp\\d?|"
        "proper|repack|extended|unrated|multi|dual[\\s.\\-]?audio|dubbed|subbed|"
        "subs?|nf|amzn|hulu|atvp|max|uhd|complete|batch|season[\\s._\\-]*\\d{1,2}|s\\d{1,2}"
        ")\\b"));

inline const QRegularExpression kSeasonTokenRe(
    QStringLiteral("(?i)\\b(?:season[\\s._\\-]*|s)(\\d{1,2})\\b"));

inline const QRegularExpression kBracketChunkRe(
    QStringLiteral("\\[([^\\]]*)\\]|\\(([^\\)]*)\\)|\\{([^\\}]*)\\}"));

// Audio-channel release tags ("5.1", "7.1", "2.0", "DDP5.1", "TrueHD 7.1") — noise that is only
// recognizable as such from its OWN dot ("5.1"); once the outer cleaner's blanket '.'->' ' pass
// has already run, "5 1" is an unrecognizable two-token fragment (fails "pure numeric", matches
// no noise word) and slips through as stray title text. Tested against the RAW bracket content,
// before any dot/underscore normalization.
inline const QRegularExpression kAudioChannelRawRe(
    QStringLiteral("(?i)^(?:ddp|dd\\+|dd|ac-?3|eac3|truehd|dts-?hd(?:\\s*ma)?|dts|atmos)?"
                   "\\s*\\d(?:\\.\\d){1,2}$"));

// Release-site / group domain tags ("YTS.MX", "RARBG.to") — a single dotted word pair with no
// internal spaces, the scene-tag convention for a tracker/site credit. Same raw-text reasoning
// as the audio-channel tag above: the dot is the only signal, and it survives only in the
// pre-normalization form.
inline const QRegularExpression kDomainTagRawRe(
    QStringLiteral("^[A-Za-z0-9][A-Za-z0-9-]{1,30}\\.[A-Za-z]{2,6}$"));

inline QList<int> extractSeasonNumbers(const QString& raw)
{
    QList<int> out;
    QSet<int> seen;
    auto it = kSeasonTokenRe.globalMatch(raw);
    while (it.hasNext()) {
        auto match = it.next();
        bool ok = false;
        int number = match.captured(1).toInt(&ok);
        if (!ok || number <= 0 || number > 99 || seen.contains(number))
            continue;
        seen.insert(number);
        out.append(number);
    }
    return out;
}

inline QString stripNoiseBracketChunks(const QString& text)
{
    QString result;
    int lastEnd = 0;
    auto it = kBracketChunkRe.globalMatch(text);

    while (it.hasNext()) {
        auto match = it.next();
        result += text.mid(lastEnd, match.capturedStart() - lastEnd);

        QString inner;
        for (int g = 1; g <= 3; ++g) {
            if (!match.captured(g).isNull()) {
                inner = match.captured(g).trimmed();
                break;
            }
        }

        if (inner.isEmpty()) {
            result += ' ';
        } else {
            QString normalized = inner;
            normalized.replace('_', ' ');
            normalized.replace('.', ' ');
            normalized = normalized.trimmed();

            if (normalized.isEmpty()) {
                result += ' ';
            } else {
                static const QRegularExpression yearOnly(QStringLiteral("^(?:19|20)\\d{2}$"));
                static const QRegularExpression pureNumeric(QStringLiteral("^\\d+$"));
                // Audio-channel and domain-tag noise is tested against `inner` — the RAW,
                // pre-normalization capture — because both patterns hinge on a literal dot that
                // `normalized` has already replaced with a space.
                if (yearOnly.match(normalized).hasMatch() ||
                    pureNumeric.match(normalized).hasMatch() ||
                    kShowTitleNoiseRe.match(normalized).hasMatch() ||
                    kAudioChannelRawRe.match(inner).hasMatch() ||
                    kDomainTagRawRe.match(inner).hasMatch()) {
                    result += ' ';
                } else {
                    result += ' ' + normalized + ' ';
                }
            }
        }

        lastEnd = match.capturedEnd();
    }

    result += text.mid(lastEnd);
    return result;
}

inline QString cleanMediaFolderTitle(const QString& rawName)
{
    QString raw = rawName.trimmed();
    if (raw.isEmpty())
        return raw;

    const QList<int> seasonNumbers = extractSeasonNumbers(raw);

    // Strip bracket/paren/brace noise chunks FIRST, while dots/underscores are still intact —
    // an audio-channel tag ("[5.1]") or a release-site domain tag ("[YTS.MX]") is recognizable
    // as noise only from its own punctuation; the blanket '.'/'_' -> ' ' pass below would
    // otherwise turn "5.1" into the unrecognizable "5 1" and "YTS.MX" into "YTS MX" BEFORE the
    // noise test ever saw them (the exact bug behind a real folder's stray "5 1"/"YTS MX").
    QString cleaned = stripNoiseBracketChunks(raw);

    cleaned.replace('_', ' ');
    cleaned.replace('.', ' ');

    cleaned.replace(kShowTitleNoiseRe, QStringLiteral(" "));

    // Orphan "-N" / "+N" tokens left by season/noise removal.
    static const QRegularExpression strayNumToken(
        QStringLiteral("(?:^|\\s)[\\-+]\\s*\\d{1,2}(?=\\s|$)"));
    cleaned.replace(strayNumToken, QStringLiteral(" "));

    // Orphan "+" separators (not "-", which divides real title parts).
    static const QRegularExpression strayPlus(
        QStringLiteral("(?:^|\\s)\\+(?=\\s|$)"));
    cleaned.replace(strayPlus, QStringLiteral(" "));

    static const QRegularExpression trailingGroup(
        QStringLiteral("(?:\\s*-\\s*[A-Za-z0-9]{2,16})+$"));
    cleaned.replace(trailingGroup, QStringLiteral(" "));

    static const QRegularExpression trailingYear(
        QStringLiteral("\\b(?:19|20)\\d{2}\\b$"));
    cleaned.replace(trailingYear, QStringLiteral(" "));

    static const QRegularExpression multiSpace(QStringLiteral("\\s+"));
    cleaned.replace(multiSpace, QStringLiteral(" "));
    static const QRegularExpression trimChars(QStringLiteral("^[\\s\\-._]+|[\\s\\-._]+$"));
    cleaned.replace(trimChars, QString());

    // Re-append a season label ONLY for a SINGLE-season folder, so a lone "Season 1"
    // folder keeps its identity after the noise strip removed the token. A folder that
    // NAMES MULTIPLE seasons is a show root spanning them (e.g. "The Wire S01 S05") —
    // the bare show name is the right title; re-appending every number produced the
    // "The Wire Season 1 Season 5" doubling artifact (Slice 11 Thread C).
    if (seasonNumbers.size() == 1) {
        const QString label = QStringLiteral("Season %1").arg(seasonNumbers.first());
        if (cleaned.isEmpty())
            cleaned = label;
        else if (!cleaned.toLower().contains(label.toLower()))
            cleaned += ' ' + label;
    }

    cleaned.replace(multiSpace, QStringLiteral(" "));
    cleaned.replace(trimChars, QString());

    if (cleaned.length() < 2)
        return raw;

    return cleaned;
}

inline QString normalizedTitle(const QString& rawTitle)
{
    QString s = cleanMediaFolderTitle(rawTitle).toLower();
    s.replace(QRegularExpression(QStringLiteral("['’]s")), QString());
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    s = s.simplified();
    s.replace(QRegularExpression(QStringLiteral("^(the|a|an) ")), QString());
    return s;
}

} // namespace MediaTitleNormalization
