#pragma once
// Shared catalog title matching, independent of any product world.
#include <QString>
#include <QDate>
#include <QList>
#include <QSet>
#include <QRegularExpression>
#include <QStringList>

namespace MediaTitleNormalization {

// Author initials may be written separately ("R. R.") or packed ("RR").
// Join only adjacent single-letter tokens; retain all other name components
// and Unicode letters so unrelated authors never collapse to the same key.
inline QString bookAuthorIdentityKey(QString author)
{
    author = author.toCaseFolded();
    author.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    const QStringList words = author.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList key;
    for (int i = 0; i < words.size();) {
        if (words.at(i).size() != 1) {
            key.append(words.at(i++));
            continue;
        }
        QString initials;
        do { initials += words.at(i++); }
        while (i < words.size() && words.at(i).size() == 1);
        key.append(initials);
    }
    return key.join(QLatin1Char(' '));
}

// ── Title cleaner (port of TB2 ScannerUtils::cleanMediaFolderTitle) ────
// Technical release words that are noise wherever they appear.
#define MTN_NOISE_CORE \
        "2160p|1080p|720p|480p|x264|x265|h\\.?264|h\\.?265|hevc|10bit|8bit|hdr|dv|" \
        "webrip|web[\\s.\\-]?dl|bluray|b[dr]rip|hdrip|dvdrip|hdtv|remux|xvid|e?ac3|aac|dts|ddp\\d?|" \
        "repack|multi|dual[\\s.\\-]?audio|triple[\\s.\\-]?audio|dubbed|subbed|" \
        "subs?|nf|amzn|hulu|atvp|hbo|uhd|complete|batch|reenc|" \
        "mini[\\s._\\-]?series|season[\\s._\\-]*\\d{1,2}|s\\d{1,2}"
// Edition words. They are real title words ("The Extended Family"), so the cleaner strips them only
// when the name carries release facts (a qualified year, a quality tag or a season token).
#define MTN_EDITION_WORDS "extended|unrated|remastered|criterion|despecialized|uncensored|proper"

inline const QRegularExpression kShowTitleNoiseRe(
    QStringLiteral("(?i)\\b(" MTN_NOISE_CORE "|" MTN_EDITION_WORDS ")\\b"));

inline const QRegularExpression kShowTitleNoiseNoEditionRe(
    QStringLiteral("(?i)\\b(" MTN_NOISE_CORE ")\\b"));

#undef MTN_NOISE_CORE
#undef MTN_EDITION_WORDS

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

// Unbracketed channel counts need a codec or CH suffix: AAC.51 and 5.1Ch are
// release metadata, whereas a standalone 51 may be part of the actual title.
inline const QRegularExpression kQualifiedAudioChannelRawRe(
    QStringLiteral("(?i)\\b(?:(?:aac|ddp|dd\\+|dd|ac-?3|eac3|truehd|dts-?hd(?:\\s*ma)?|dts)"
                   "[\\s._-]*(?:[1-8]\\.\\d(?:\\.\\d)?|[1-8][01])(?:ch)?|"
                   "[1-8](?:\\.\\d){0,2}ch)\\b"));

// Release-site / group domain tags ("YTS.MX", "RARBG.to") — a single dotted word pair with no
// internal spaces, the scene-tag convention for a tracker/site credit. Same raw-text reasoning
// as the audio-channel tag above: the dot is the only signal, and it survives only in the
// pre-normalization form.
inline const QRegularExpression kDomainTagRawRe(
    QStringLiteral("^(?:www\\.)?[A-Za-z0-9][A-Za-z0-9-]{1,30}\\.[A-Za-z]{2,6}$"));

// Site credit written in front of the title: "www.SANET.ST____Title", "www.SANET.ST  -  Title",
// "Crazy4TV.com - Title". The plain "Site.com" form needs the dash so a title is never eaten.
inline const QRegularExpression kSitePrefixRe(
    QStringLiteral("(?i)^(?:www[ ._]+[a-z0-9-]{2,30}[ ._]+(?:com|net|org|info|to|st|ag|ws|me|tv|cc|io|se|eu|cx|biz|ru|co|nu|am|is|vc|ch|pw|xyz)"
                   "(?:[ _.]*-[ _.]*|[ _.]+)|"
                   "[a-z0-9-]{2,30}\\.(?:com|net|org|info|to|st|ag|ws|me|tv|cc|io|se|eu|cx|biz|ru|co|nu|am|is|vc|ch|pw|xyz)"
                   "(?:\\s*-\\s*|_{2,}))"));

// File sizes written into names: "1.3GB", "950 MB".
inline const QRegularExpression kFileSizeRe(
    QStringLiteral("(?i)\\b\\d+(?:[.,]\\d+)?\\s?(?:gb|mb)\\b"));

// First release fact of a name that has no qualified year: everything after it (group credit,
// audio descriptors, language lists) is release suffix, not title.
inline const QRegularExpression kFirstReleaseFactRe(
    QStringLiteral("(?i)\\b(?:(?:2160|1080|720|480)p|(?:blu[ -]?ray|b[dr]rip|web[ -]?rip|web[ -]?dl|"
                   "hdtv|dvdrip|hdrip|x26[45]|hevc|xvid|hdcam|hdts)\\b)"));

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
    static const QRegularExpression yearOnly(QStringLiteral("^(?:19|20)\\d{2}(?:\\s*[-\u2013\u2014]\\s*(?:(?:19|20)\\d{2})?)?$"));
    static const QRegularExpression pureNumeric(QStringLiteral("^\\d+$"));
    static const QRegularExpression stereoOnly(QStringLiteral("(?i)^(?:3d|hsbs|half[ ._-]?sbs)$"));
    static const QRegularExpression titleText(QStringLiteral("\\p{L}"));

    struct Chunk {
        int start = 0;
        int end = 0;
        QString inner;
        QString normalized;
        bool technical = false; // quality / audio / stereo fact, not just a year or number
        bool noise = false;
    };
    QList<Chunk> chunks;
    auto it = kBracketChunkRe.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();
        Chunk c;
        c.start = match.capturedStart();
        c.end = match.capturedEnd();
        for (int g = 1; g <= 3; ++g) {
            if (!match.captured(g).isNull()) {
                c.inner = match.captured(g).trimmed();
                break;
            }
        }
        c.normalized = c.inner;
        c.normalized.replace('_', ' ');
        c.normalized.replace('.', ' ');
        c.normalized = c.normalized.trimmed();
        // Audio-channel and domain-tag noise is tested against `inner` — the RAW,
        // pre-normalization capture — because both patterns hinge on a literal dot that
        // `normalized` has already replaced with a space.
        c.technical = !c.normalized.isEmpty() &&
            (kShowTitleNoiseRe.match(c.normalized).hasMatch() ||
             kAudioChannelRawRe.match(c.inner).hasMatch() ||
             stereoOnly.match(c.inner).hasMatch());
        c.noise = c.normalized.isEmpty() || c.technical ||
            yearOnly.match(c.normalized).hasMatch() ||
            pureNumeric.match(c.normalized).hasMatch() ||
            kDomainTagRawRe.match(c.inner).hasMatch();
        chunks.append(c);
    }

    bool anyTechnical = false;
    for (const Chunk& c : chunks)
        anyTechnical = anyTechnical || c.technical;

    QString result;
    int lastEnd = 0;
    bool technicalSeen = false;
    for (const Chunk& c : chunks) {
        result += text.mid(lastEnd, c.start - lastEnd);
        const bool titleBefore = titleText.match(result).hasMatch();
        bool drop = c.noise;
        if (!drop && anyTechnical) {
            // A bracket after a quality bracket is a release credit ("[1080p] [pseudo]").
            if (technicalSeen && titleBefore)
                drop = true;
            // A leading fansub/group tag ("[AnimeRG] Title [1080p]"). Short tags stay: "[REC]" is a title.
            else if (!titleBefore && c.inner.size() >= 5 && !c.inner.contains(QLatin1Char(' ')) &&
                     titleText.match(text.mid(c.end)).hasMatch())
                drop = true;
        }
        result += drop ? QStringLiteral(" ") : ' ' + c.normalized + ' ';
        technicalSeen = technicalSeen || c.technical;
        lastEnd = c.end;
    }
    result += text.mid(lastEnd);
    return result;
}

struct ReleaseSuffix {
    int titleEnd = -1;
    int year = 0;
};

// A release year followed by technical encoding/source facts is a boundary,
// including a bracketed year with a credit. Ordinary title words after a year
// are not evidence of a release suffix; a leading numeric title is not a year.
inline ReleaseSuffix qualifiedReleaseSuffix(const QString& raw)
{
    static const QRegularExpression suffix(QStringLiteral(
        "(?i)(?:\\(((?:19|20)\\d{2})(?:\\s*-\\s*[^()]+)?\\)|"
        "\\[((?:19|20)\\d{2})\\]|\\{((?:19|20)\\d{2})\\}|\\b((?:19|20)\\d{2})\\b)"
        "[\\s._-]+(?=(?:(?:2160|1080|720|480)p|(?:blu[ ._-]?ray|b[dr]rip|"
        "dvdrip|hdrip|web[ ._-]?dl|webrip|hdtv|remux|xvid|x26[45]|h[._]?26[45]|3d|hsbs|half[ ._-]?sbs|max[ ._-]+web[ ._-]?dl|"
        // Edition words are release suffix only straight after a year ("1991 DC", "1939 75th Anniversary Edition").
        "criterion|despecialized|remastered|extended|uncensored|unrated|dc|proper|new|hq|hd[ ._-]?cam|cam|ts|hdts|telesync|"
        "\\d{1,3}(?:st|nd|rd|th)[ ._-]+anniversary)\\b))"));
    static const QRegularExpression titleText(QStringLiteral("[\\p{L}\\p{N}]"));
    auto matches = suffix.globalMatch(raw);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (!titleText.match(raw.left(match.capturedStart())).hasMatch()) continue;
        for (int group = 1; group <= 4; ++group)
            if (!match.captured(group).isEmpty()) {
                const int year = match.captured(group).toInt();
                // A distant future numeric title word is not a bare release year.
                if (group == 4 && year > QDate::currentDate().year() + 1) continue;
                return {int(match.capturedStart()), year};
            }
    }
    return {};
}

inline QString cleanMediaFolderTitle(const QString& rawName)
{
    QString raw = rawName.trimmed();
    if (raw.isEmpty())
        return raw;

    // A site credit in front of the title is never title text.
    {
        const auto site = kSitePrefixRe.match(raw);
        if (site.hasMatch() && raw.mid(site.capturedEnd()).trimmed().size() >= 2)
            raw = raw.mid(site.capturedEnd()).trimmed();
    }

    const QList<int> seasonNumbers = extractSeasonNumbers(raw);

    const auto release = qualifiedReleaseSuffix(raw);
    if (release.titleEnd >= 0) raw.truncate(release.titleEnd);

    // Strip bracket/paren/brace noise chunks FIRST, while dots/underscores are still intact —
    // an audio-channel tag ("[5.1]") or a release-site domain tag ("[YTS.MX]") is recognizable
    // as noise only from its own punctuation; the blanket '.'/'_' -> ' ' pass below would
    // otherwise turn "5.1" into the unrecognizable "5 1" and "YTS.MX" into "YTS MX" BEFORE the
    // noise test ever saw them (the exact bug behind a real folder's stray "5 1"/"YTS MX").
    QString cleaned = stripNoiseBracketChunks(raw);
    // Stereo words outside brackets are technical only beside actual release facts.
    static const QRegularExpression technical(QStringLiteral("(?i)\\b(?:2160p|1080p|720p|480p|bluray|b[dr]rip|hdrip|x26[45]|hevc)\\b"));
    const bool releaseFacts = release.year > 0 || technical.match(rawName).hasMatch()
        || !seasonNumbers.isEmpty();
    if (release.year > 0 || technical.match(rawName).hasMatch()) {
        static const QRegularExpression stereo(QStringLiteral("(?i)\\b(?:3d|hsbs|half[ ._-]?sbs)\\b"));
        cleaned.replace(stereo, QStringLiteral(" "));
    }

    cleaned.replace(kQualifiedAudioChannelRawRe, QStringLiteral(" "));
    cleaned.replace(kFileSizeRe, QStringLiteral(" "));

    cleaned.replace('_', ' ');
    cleaned.replace('.', ' ');

    // Without a qualified year, the first quality/source fact still opens the release suffix
    // (group credit, audio descriptors and language lists follow it).
    {
        static const QRegularExpression titleLetters(QStringLiteral("\\p{L}"));
        const auto fact = kFirstReleaseFactRe.match(cleaned);
        if (fact.hasMatch() && titleLetters.match(cleaned.left(fact.capturedStart())).hasMatch())
            cleaned = cleaned.left(fact.capturedStart()).trimmed();
    }

    // Edition words are title words unless the name has release facts but no qualified year (a
    // qualified year already cut the release suffix, so nothing before it is an edition word).
    cleaned.replace(releaseFacts && release.year <= 0 ? kShowTitleNoiseRe : kShowTitleNoiseNoEditionRe, QStringLiteral(" "));

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
