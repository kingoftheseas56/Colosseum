#include "VaultIdentifier.h"

#include "ComicsCatalog.h"
#include "ImdbCatalog.h"
#include "MalCatalog.h"
#include "VaultIndex.h"
#include "VaultGroupTrace.h"
#include "VaultKit.h"
#include "VaultLocation.h"
#include "MediaTitleNormalization.h"

#include <QVariantList>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QVariantMap>
#include <QRegularExpression>
#include <QStringList>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString firstNonEmpty(const QString& first, const QString& fallback)
{
    return first.isEmpty() ? fallback : first;
}

QString adoptedType(const QString& type)
{
    const auto normalized = type.trimmed().toLower();
    return normalized == QLatin1String("movie") || normalized == QLatin1String("series")
        || normalized == QLatin1String("mini") ? normalized : QString();
}

void clearIdentity(VaultIndex::FileRow& row)
{
    row.identityId.clear();
    row.identityTitle.clear();
    row.identityType.clear();
    row.identitySource.clear();
    row.identitySynopsis.clear();
    row.identityCoverUrl.clear();
    row.identityWorld.clear();
    row.identityYear = 0;
    row.identityRating = 0;   // phase-4 G1: the adopted IMDb facts die with the identity
    row.identityGenres.clear();
    row.identityState.clear();
    row.identityCandidateCount = 0;
}

struct LookupTitle {
    QString normalized;
    int year = 0;
};

LookupTitle lookupTitle(const QString& title)
{
    return {VaultKit::normalizedTitle(title), VaultKit::parsedTitleYear(title)};
}

// The census captures the qualified release year before cleaning removes it.
// Preserve that fact to disambiguate remakes; parse the title only for older rows
// or paths that never stamped parsedYear. Leading numeric title words stay intact.
void applyLookupYear(LookupTitle& lookup, const VaultIndex::FileRow& row)
{
    if (row.parsedYear > 0)
        lookup.year = row.parsedYear;
}

// A file that is only a release-group credit ("ETRG.mp4", "RARBG.com.mp4", a file named for the
// folder's trailing -GROUP) is a promo clip, never a feature.
bool isReleaseCreditClip(const VaultIndex::FileRow& row)
{
    return !VaultIdentifier::junkClipReason(row).isEmpty();
}

// "Movies 2010", "Downloads (2015)": a folder named for a shelf, not a film.
bool isGenericFolderTitle(const QString& normalized)
{
    static const QSet<QString> generic = {
        QStringLiteral("movie"), QStringLiteral("movies"), QStringLiteral("film"), QStringLiteral("films"),
        QStringLiteral("video"), QStringLiteral("videos"), QStringLiteral("download"), QStringLiteral("downloads"),
        QStringLiteral("new"), QStringLiteral("old"), QStringLiteral("hd"), QStringLiteral("collection"),
        QStringLiteral("collections"), QStringLiteral("misc"), QStringLiteral("stuff"), QStringLiteral("my"),
        QStringLiteral("all"), QStringLiteral("english"), QStringLiteral("hindi"), QStringLiteral("telugu"),
        QStringLiteral("tamil"), QStringLiteral("dubbed"), QStringLiteral("action"), QStringLiteral("comedy"),
        QStringLiteral("drama"), QStringLiteral("horror"), QStringLiteral("animation"), QStringLiteral("cartoons"),
        QStringLiteral("tv"), QStringLiteral("shows"), QStringLiteral("series"), QStringLiteral("sci"),
        QStringLiteral("fi"), QStringLiteral("thriller"), QStringLiteral("romance"), QStringLiteral("anime")};
    static const QRegularExpression letters(QStringLiteral("\\p{L}"));
    if (!letters.match(normalized).hasMatch()) return true;
    for (const QString& t : normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (!generic.contains(t)) return false;
    return true;
}

bool extrasLikeFolderName(const QString& name)
{
    static const QRegularExpression re(QStringLiteral(
        "(?i)\\b(?:extras?|bonus|featurettes?|behind the scenes|special features|deleted scenes)\\b"));
    // Samples keep their existing behaviour; they are not part of this filing.
    static const QRegularExpression sample(QStringLiteral("(?i)^samples?(?:\\s*&\\s*screenshots)?$"));
    if (sample.match(name.trimmed()).hasMatch()) return false;
    return VaultKit::isExtrasDirName(name) || re.match(name).hasMatch();
}

struct BookLookupFacts {
    QString title;
    QString author;
    bool valid = false;
    QStringList alternativeTitles;
    bool abbreviatedAuthor = false;
    QString franchise;
};

// This compares two supplied facts without dropping parenthetical distinctions.
// The catalogue owns identity normalization and the qualifier veto.
QString bookEvidenceKey(QString text)
{
    text = text.toLower();
    text.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    return text.simplified();
}

// Same conservative segment grammar for embedded evidence and library work
// titles. Numbering is decoration, not a different work; unknown subtitles stay.
QString bookWorkTitle(const QString& title, const QString& context = {})
{
    // Brackets are segment delimiters here, not themselves a semantic veto.
    static const QRegularExpression veto(QStringLiteral("(?i)") + QString::fromLatin1(kBiblioIdentityTitleQualifier).section(QLatin1Char('|'), 1));
    static const QRegularExpression derivative(QStringLiteral("(?i)\\b(?:graphic novel|summary|study guide|companion|analysis|workbook|box set|boxed set|collection|omnibus|sampler|excerpt|free preview|abridged)\\b|\\b[0-9]+\\s*/\\s*[0-9]+\\b"));
    if (veto.match(title).hasMatch() || derivative.match(title).hasMatch()) return title;
    static const QRegularExpression split(QStringLiteral("\\s+-\\s+|\\s+[\\x{2013}\\x{2014}]\\s+|[:()\\[\\]]"));
    static const QRegularExpression numbering(QStringLiteral(
        "(?i)\\b(?:book|vol\\.?|volume)\\s*(?:[0-9]+|[ivxlcdm]+|one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|thirteen|fourteen|fifteen|sixteen|seventeen|eighteen|nineteen|twenty)\\b|\\b(?:first|second|third|fourth|fifth|sixth|seventh|eighth|ninth|tenth)\\s+book\\b|#\\s*[0-9]+\\b|,\\s*[0-9]+\\s*$"));
    static const QRegularExpression series(QStringLiteral("(?i)\\b(?:trilogy|saga|series|chronicles|cycle|legends|sequence|quartet|duology|archive|song)\\b"));
    const auto segments = title.split(split, Qt::SkipEmptyParts);
    if (segments.size() < 2) return title;
    QStringList works;
    for (const auto& segment : segments) {
        if (segment.trimmed().isEmpty()) continue;
        static const QRegularExpression bareNumber(QStringLiteral("^(?:[0-9]{1,3}|[IVX]{1,6})$"));
        if (!numbering.match(segment).hasMatch() && !series.match(segment).hasMatch()
            && (context.isEmpty() || (bookEvidenceKey(segment) != bookEvidenceKey(context)
                && !bareNumber.match(segment.trimmed()).hasMatch()))) works.append(segment.trimmed());
    }
    return works.size() == 1 ? works.first() : title;
}

BookLookupFacts bookFilenameFacts(const QString& filename)
{
    BookLookupFacts facts;
    const QFileInfo file(filename);
    const QString format = file.suffix().toLower();
    if (format != QLatin1String("mobi") && format != QLatin1String("pdf") && format != QLatin1String("epub")) return facts;
    const QString rawStem = file.completeBaseName();
    QString stem = rawStem;
    stem.replace(QLatin1Char('_'), QLatin1Char(' '));
    static const QRegularExpression separator(QStringLiteral("\\s+-\\s+"));
    const QStringList parts = stem.split(separator, Qt::KeepEmptyParts);
    static const QRegularExpression qualified(QString::fromLatin1(kBiblioIdentityTitleQualifier));
    if (parts.size() < 2 || qualified.match(parts.mid(0, parts.size() - 1).join(QStringLiteral(" - "))).hasMatch())
        return facts;
    if (parts.size() == 2) {
        facts.title = parts.first().trimmed();
        facts.author = parts.last().trimmed();
        // An underscore followed by whitespace can separate a series imprint
        // from a work title; either the complete title or the remainder may
        // be real. Keep both candidates until the exactly-one-work gate.
        const auto boundary = separator.match(stem);
        const QString rawTitle = boundary.hasMatch() ? rawStem.left(boundary.capturedStart()) : QString();
        static const QRegularExpression seriesPrefix(QStringLiteral("^([^_]+)_\\s+(.+)$"));
        const auto prefixed = seriesPrefix.match(rawTitle);
        if (prefixed.hasMatch()) {
            const QString suffix = prefixed.captured(2).trimmed();
            if (suffix.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() >= 2)
                facts.alternativeTitles.append(suffix);
        }
        // Underscore-delimited exports sometimes append an inverted series
        // name ending in its article ("Title_Series_Name_The"). Consider only
        // title prefixes with at least two words and a multiword series tail;
        // a hit is never adopted if any other prefix names another work.
        if (rawTitle.endsWith(QStringLiteral("_The"), Qt::CaseInsensitive)) {
            const QStringList segments = rawTitle.split(QLatin1Char('_'), Qt::SkipEmptyParts);
            for (int count = 2; count <= segments.size() - 3; ++count)
                facts.alternativeTitles.append(segments.mid(0, count).join(QLatin1Char(' ')));
        }
    } else if (parts.size() == 4) {
        static const QRegularExpression number(QStringLiteral("^(?:[0-9]{1,3}|[IVX]{1,6})$"));
        if (parts.first().trimmed().isEmpty() || !number.match(parts.at(1).trimmed()).hasMatch())
            return facts;
        if (bookEvidenceKey(parts.at(2)) == bookEvidenceKey(parts.first())) return facts;
        // Only the explicit series / number / distinct volume / author grammar
        // may drop a prefix. The volume title remains the lookup title.
        facts.title = parts.at(2).trimmed();
        facts.franchise = parts.first().trimmed();
        facts.author = parts.last().trimmed();
    } else return facts;
    static const QRegularExpression trailingArticle(QStringLiteral("(?i)^(.+),\\s+(the|a|an)$"));
    const auto article = trailingArticle.match(facts.title);
    if (article.hasMatch()) facts.title = article.captured(2) + QLatin1Char(' ') + article.captured(1);
    const QStringList authorParts = bookEvidenceKey(facts.author).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    static const QRegularExpression incomplete(QStringLiteral("(?i)[0-9]|\\.\\.\\.|\\b(?:unknown|anonymous|unspecified)\\b"));
    static const QRegularExpression genericTitle(QStringLiteral("(?i)^(?:[0-9a-f]{16,}|[0-9]+|(?:book|volume|vol|part)\\s*[0-9ivx]+)$"));
    static const QRegularExpression surnameFragment(QStringLiteral("^\\p{L}{5,}$"));
    facts.abbreviatedAuthor = authorParts.size() == 1
        && surnameFragment.match(facts.author).hasMatch();
    facts.valid = !facts.title.isEmpty() && !genericTitle.match(facts.title).hasMatch()
        && ((authorParts.size() >= 2 && authorParts.last().size() > 1) || facts.abbreviatedAuthor)
        && !incomplete.match(facts.author).hasMatch();
    return facts;
}

bool bookAbbreviatedSurnameMatches(const QString& fragment, const QString& catalogueAuthor)
{
    // An explicit "Surname, Given" export still supplies a full author;
    // only the surname-fragment comparison uses the declared surname order.
    const auto comma = catalogueAuthor.indexOf(QLatin1Char(','));
    const QString ordered = comma > 0 ? catalogueAuthor.mid(comma + 1) + QLatin1Char(' ') + catalogueAuthor.left(comma) : catalogueAuthor;
    const QStringList names = MediaTitleNormalization::bookAuthorIdentityKey(ordered)
        .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return names.size() >= 2 && fragment.size() >= 5
        && names.last().startsWith(fragment.toCaseFolded());
}

QString bookFingerprint(const QList<VaultIndex::FileRow>& rows)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    for (auto row : rows) {
        // SQLite round-trips null QStrings as empty strings. Optimistic
        // guards compare evidence, not that storage distinction.
        for (QString* value : {&row.id, &row.path, &row.rootPath, &row.subtreePath, &row.kind,
             &row.groupKey, &row.groupTitle, &row.displayTitle, &row.realName, &row.format,
             &row.embeddedTitle, &row.author, &row.metadataSource, &row.identityId,
             &row.identitySource, &row.identityTitle, &row.identityWorld, &row.identityState, &row.errorState})
            if (value->isNull()) *value = QStringLiteral("");
        out << row.id << row.path << row.rootPath << row.subtreePath << row.kind
            << row.groupKey << row.groupTitle << row.displayTitle
            << row.realName << row.format << row.size << row.mtimeMs
            << row.embeddedTitle << row.author << row.metadataSource
            << row.identityId << row.identitySource << row.identityTitle << row.identityWorld << row.identityState << row.identitySuppressed
            << row.away << row.errorState;
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}


// A corrupt old coarse group must be regrouped by a controlled scan before any
// whole-group mutation can overwrite unrelated titles or manual decisions.
bool safeGroupAction(const QList<VaultIndex::FileRow>& rows)
{
    QSet<QString> identities, kinds, filmTitles, episodeTitles, unnamedShows, filmFolders;
    int standaloneVideos = 0;
    bool hasGenericParts = false;
    QStringList siblingPaths;
    for (const auto& member : rows) siblingPaths.append(member.path);
    static const QRegularExpression genericPart(QStringLiteral(
        "(?i)^(?:movie|main|feature|part[._\\s-]*\\d+|cd[._\\s-]*\\d+|disc[._\\s-]*\\d+)$"));
    for (const auto& row : rows) {
        kinds.insert(row.kind);
        if (!row.identityId.isEmpty()) identities.insert(row.identityId);
        if (row.kind != QLatin1String("video")) continue;
        const auto context = VaultKit::videoEpisodeContext(row.path, siblingPaths);
        const QString episode = context.title;
        if (context.number.matched && !episode.isEmpty()) episodeTitles.insert(VaultKit::normalizedTitle(episode));
        else if (context.number.matched)
            unnamedShows.insert(VaultLocation::normalize(VaultKit::showRootForEpisodePath(row.path)));
        else {
            ++standaloneVideos;
            filmFolders.insert(VaultLocation::normalize(QFileInfo(row.path).absolutePath()));
            const QString title = VaultKit::standaloneVideoTitle(row.realName);
            if (genericPart.match(title).hasMatch()) hasGenericParts = true;
            else filmTitles.insert(VaultKit::normalizedTitle(title));
        }
    }
    if (kinds.size() > 1 || identities.size() > 1 || episodeTitles.size() > 1 || unnamedShows.size() > 1)
        return false;
    // Being beside an episode is not evidence that an unidentified film is its extra.
    // Explicit extras relationships remain owned by VaultConfig, outside this group verb.
    if (!episodeTitles.isEmpty() || !unnamedShows.isEmpty()) return standaloneVideos == 0;
    if (standaloneVideos > 1 && filmFolders.size() != 1) return false;
    // Generic multipart labels inherit one physical film-folder context, never a
    // coarse legacy group containing files from different folders.
    if (hasGenericParts && standaloneVideos > 1
        && VaultLocation::normalize(rows.first().groupKey) != *filmFolders.constBegin())
        return false;
    return filmTitles.size() <= 1;
}

} // namespace

VaultIdentifier::VaultIdentifier(VaultIndex* index, ComicsCatalog* comics,
                                 MalCatalog* mal, ImdbCatalog* imdb,
                                 QObject* parent)
    : QObject(parent), m_index(index), m_comics(comics), m_mal(mal), m_imdb(imdb)
{
    auto retry = [this] {
        if (m_retryScheduled || m_pendingGroups.isEmpty()) return;
        m_retryScheduled = true;
        QTimer::singleShot(0, this, [this] { m_retryScheduled = false; retryPending(); });
    };
    if (m_comics) connect(m_comics, &ComicsCatalog::revisionChanged, this, retry);
    if (m_mal) connect(m_mal, &MalCatalog::revisionChanged, this, retry);
    if (m_imdb) {
        connect(m_imdb, &ImdbCatalog::revisionChanged, this, retry);
        connect(m_imdb, &ImdbCatalog::readyChanged, this, retry);
    }
}

QString VaultIdentifier::videoGenres(const QVariant& value)
{
    QStringList genres;
    const auto list = value.metaType().id() == QMetaType::QString
        ? value.toString().split(QChar(0x00b7)) : value.toStringList();
    for (const auto& genre : list) {
        const auto text = genre.trimmed();
        if (!text.isEmpty() && !genres.contains(text)) genres.append(text);
    }
    return genres.join(QStringLiteral(" · "));
}

void VaultIdentifier::backfillVideoGenres()
{
    if (!m_index) return;
    const auto snapshot = m_index->rowsForKind(QStringLiteral("video"));
    // Re-read current rows on completion: an in-flight request must not resurrect
    // an old identity, undo manual choices, or overwrite newly supplied genres.
    QPointer<VaultIdentifier> self(this);
    auto save = [self](const QString& identity, const QString& genres) {
        if (!self || !self->m_index || genres.isEmpty()) return;
        QList<VaultIndex::FileRow> updates;
        for (auto row : self->m_index->rowsForKind(QStringLiteral("video"))) {
            if (row.identityId != identity || row.identitySuppressed || !row.identityGenres.trimmed().isEmpty()) continue;
            row.identityGenres = genres;
            updates.append(row);
        }
        if (!updates.isEmpty()) self->m_index->upsertMany(updates);
    };
    // Reuse sibling facts before making any requests (season/group boundaries
    // do not create separate identities).
    for (const auto& row : snapshot)
        if (!row.identitySuppressed && !row.identityGenres.trimmed().isEmpty())
            m_videoGenreFacts.insert(row.identityId, row.identityGenres);
    QSet<QString> visited;
    for (const auto& row : snapshot) {
        static const QRegularExpression imdbId(QStringLiteral("^imdb:(tt[0-9]+)$"));
        const auto id = imdbId.match(row.identityId);
        if (!id.hasMatch() || row.identitySuppressed || !row.identityGenres.trimmed().isEmpty()
            || visited.contains(row.identityId)) continue;
        visited.insert(row.identityId);
        if (m_videoGenreFacts.contains(row.identityId)) {
            save(row.identityId, m_videoGenreFacts.value(row.identityId));
            continue;
        }
        if (m_videoGenreAttempts.contains(row.identityId)) continue;
        const auto type = row.identityType == QLatin1String("movie") ? QStringLiteral("movie")
            : row.identityType == QLatin1String("series") || row.identityType == QLatin1String("mini") ? QStringLiteral("series") : QString();
        if (type.isEmpty()) continue; // Never guess the identity's medium from a filename.
        m_videoGenreAttempts.insert(row.identityId); // before dispatch, including synchronous callbacks
        const QString identity = row.identityId, tt = id.captured(1);
        auto done = [self, save, identity, tt](const QVariantMap& meta) {
            if (!self || meta.value(QStringLiteral("id")).toString() != tt) return;
            const auto genres = videoGenres(meta.value(QStringLiteral("genres")));
            if (genres.isEmpty()) return;
            self->m_videoGenreFacts.insert(identity, genres);
            save(identity, genres);
        };
        if (m_videoGenreLookup) m_videoGenreLookup(tt, type, done);
        else {
            auto manager = findChild<QNetworkAccessManager*>(QStringLiteral("vaultGenreNetwork"));
            if (!manager) { manager = new QNetworkAccessManager(this); manager->setObjectName(QStringLiteral("vaultGenreNetwork")); }
            QNetworkRequest request(QUrl(QStringLiteral("https://v3-cinemeta.strem.io/meta/%1/%2.json").arg(type, tt)));
            request.setTransferTimeout(15000);
            auto reply = manager->get(request);
            connect(reply, &QNetworkReply::finished, this, [reply, done] {
                if (reply->error() == QNetworkReply::NoError)
                    done(QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("meta")).toObject().toVariantMap());
                reply->deleteLater();
            });
        }
    }
}

void VaultIdentifier::retryPending()
{
    const auto groups = m_pendingGroups;
    for (const auto& group : groups) {
        m_pendingGroups.remove(group);
        const auto rows = m_index ? m_index->rowsForGroup(group) : QList<VaultIndex::FileRow>{};
        bool eligible = !rows.isEmpty();
        for (const auto& row : rows)
            if (!row.identityId.isEmpty() || row.identitySuppressed || row.away || !row.errorState.isEmpty()) eligible = false;
        constexpr auto origin = "catalogue-retry";
        if (!eligible) {
            VaultGroupTrace trace("skip", origin, group);
            trace.rows(rows);
            trace.reason("retry-ineligible");
            continue;
        }
        const auto match = matchGroup(group, origin);
        if (match.adopted) applyGroup(group, match, origin);
        else if (match.candidateCount > 1) recordAmbiguous(group, match.candidateCount, origin);
    }
}

void VaultIdentifier::setBookCatalogLookup(QObject* owner,
    std::function<BiblioIdentityLookup(const QString&, const QString&)> lookup,
    std::function<BiblioIdentityLookup(const QString&, const QString&, const QString&)> online,
    std::function<bool(const BiblioIdentityCandidate&)> retain)
{
    ++m_bookLookupGeneration;
    m_pendingBookGroups.clear();
    m_bookRetryKeys.clear();
    m_bookRetryScheduled = false;
    m_bookLookupOwner = owner;
    m_bookLookup = std::move(lookup);
    m_bookOnlineLookup = std::move(online);
    m_bookRetain = std::move(retain);
}

void VaultIdentifier::retryBookCatalog()
{
    ++m_bookCatalogRevision;
    if (m_bookRetryScheduled || m_pendingBookGroups.isEmpty()) return;
    if (!m_bookLookupOwner || !m_bookLookup) { m_pendingBookGroups.clear(); return; }
    m_bookRetryScheduled = true;
    m_bookRetryKeys = m_pendingBookGroups.keys();
    const quint64 generation = m_bookLookupGeneration;
    QTimer::singleShot(0, this, [this, generation] { retryNextBookCatalog(generation); });
}

void VaultIdentifier::retryNextBookCatalog(quint64 generation)
{
    if (generation != m_bookLookupGeneration) return;
    if (m_bookRetryKeys.isEmpty() || !m_bookLookupOwner || !m_index) {
        m_bookRetryScheduled = false;
        m_bookRetryKeys.clear();
        return;
    }
    const QString group = m_bookRetryKeys.takeLast();
    const QString expected = m_pendingBookGroups.take(group);
    const auto rows = m_index->rowsForGroup(group);
    bool eligible = !expected.isEmpty() && !rows.isEmpty() && bookFingerprint(rows) == expected;
    for (const auto& row : rows)
        if ((!row.identityId.isEmpty() && !automaticPrivateEpub(row)) || row.identitySuppressed || row.away || !row.errorState.isEmpty())
            eligible = false;
    constexpr auto origin = "book-catalogue-retry";
    if (eligible) {
        const auto match = matchGroup(group, origin);
        if (match.adopted) applyGroup(group, match, origin);
        else if (match.candidateCount > 1) recordAmbiguous(group, match.candidateCount, origin);
    } else {
        VaultGroupTrace trace("skip", origin, group);
        trace.rows(rows);
        trace.reason("book-evidence-changed-or-ineligible");
    }
    // One group per turn, with a hard bounded pending set; no network or refresh.
    QTimer::singleShot(16, this, [this, generation] { retryNextBookCatalog(generation); });
}

VaultIdentifier::Match VaultIdentifier::matchGroup(const QString& groupKey, const char* origin) const
{
    Match match;
    VaultGroupTrace trace("match", origin, groupKey);
    const auto finish = [&](const char* reason, const Match& result) {
        if (trace.active()) {
            trace.reason(reason);
            trace.set("candidateCount", result.candidateCount);
            trace.set("adopted", result.adopted);
            if (result.adopted) {
                trace.set("provider", result.source);
                trace.set("identityId", result.sourceId);
                trace.set("matchedTitle", result.title);
                trace.set("matchedYear", result.year);
            }
            if (!result.bookLookupReason.isEmpty())
                trace.set("bookLookupReason", result.bookLookupReason);
        }
        return result;
    };
    m_pendingGroups.remove(groupKey);
    m_pendingBookGroups.remove(groupKey);
    if (!m_index || groupKey.isEmpty())
        return finish("no-index-or-group", match);

    const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    trace.rows(rows);
    if (rows.isEmpty())
        return finish("no-rows", match);
    const VaultIndex::FileRow& row = rows.constFirst();
    QStringList siblingPaths;
    for (const auto& member : rows) siblingPaths.append(member.path);
    QSet<int> videoYears;
    const QString groupTitle = row.groupTitle.trimmed();
    for (const VaultIndex::FileRow& candidate : rows) {
        // Coherence is a group-level contract. A real series keeps one shared
        // group title while its files may carry different display titles.
        if (candidate.groupKey != groupKey || candidate.kind != row.kind
            || candidate.groupTitle.trimmed() != groupTitle || candidate.away
            || !candidate.errorState.isEmpty())
            return finish("group-inconsistent-away-or-error", match);
        if (candidate.kind == QLatin1String("video")
            && !VaultKit::videoEpisodeContext(candidate.path, siblingPaths).number.matched) {
            if (candidate.parsedYear > 0) videoYears.insert(candidate.parsedYear);
            const int year = VaultKit::parsedTitleYear(QFileInfo(candidate.realName).completeBaseName());
            if (year > 0) videoYears.insert(year);
        }
    }
    if (videoYears.size() > 1) return finish("conflicting-release-years", match);
    // identitySuppressed records the user's last Un-identify choice so a future
    // rescan stays filename-honest. It must not block this explicit Identify
    // gesture: a deliberate re-identification is allowed to clear that marker.
    if (row.away || !row.errorState.isEmpty())
        return finish("away-or-error", match);

    if (row.kind == QLatin1String("book")) {
        const QString embeddedTitle = row.embeddedTitle.trimmed();
        // All formats share the same evidence/ambiguity gate. Only automatic
        // EPUB or proven online identities may converge, never a user's choice.
        for (const auto& member : rows)
            if (member.identityState == QLatin1String("manual")
                || (!member.identityId.isEmpty() && !automaticPrivateEpub(member) && !automaticOnlineBook(member)))
                return finish("protected-book-identity", match);
        const bool epub = row.metadataSource == QLatin1String("EPUB");
        auto sharedLookup = [&]() -> Match {
            if (rows.size() != 1) return match;
            const QString format = QFileInfo(row.realName).suffix().toLower();
            if (format != QLatin1String("mobi") && format != QLatin1String("pdf") && format != QLatin1String("epub")) return match;
            const auto filename = bookFilenameFacts(row.realName);
            BookLookupFacts facts = filename;
            facts.title = bookWorkTitle(facts.title);
            const bool hasEmbeddedFacts = !embeddedTitle.isEmpty() || !row.author.trimmed().isEmpty()
                || !row.metadataSource.isEmpty();
            if (hasEmbeddedFacts) {
                if (embeddedTitle.isEmpty() || row.author.trimmed().isEmpty()) {
                    match.bookLookupReason = QStringLiteral("incomplete-embedded-facts");
                    return match;
                }
                facts = {bookWorkTitle(embeddedTitle), row.author.trimmed(), true};
                bool titleAgrees = !filename.valid;
                QStringList hypotheses{filename.title}; hypotheses.append(filename.alternativeTitles);
                for (const auto& hypothesis : hypotheses) {
                    QString context = filename.franchise;
                    if (context.isEmpty() && bookEvidenceKey(hypothesis) != bookEvidenceKey(filename.title)) {
                        context = bookEvidenceKey(filename.title);
                        context.remove(bookEvidenceKey(hypothesis)); context = context.simplified();
                    }
                    if (bookEvidenceKey(bookWorkTitle(hypothesis)) == bookEvidenceKey(bookWorkTitle(embeddedTitle, context))) {
                        titleAgrees = true; facts.title = bookWorkTitle(hypothesis); break;
                    }
                }
                if (filename.valid && (!titleAgrees
                    || (filename.abbreviatedAuthor
                        ? !bookAbbreviatedSurnameMatches(filename.author, facts.author)
                        : MediaTitleNormalization::bookAuthorIdentityKey(filename.author)
                            != MediaTitleNormalization::bookAuthorIdentityKey(facts.author)))) {
                    match.bookLookupReason = QStringLiteral("conflicting-file-facts");
                    return match;
                }
            }
            if (!facts.valid) {
                match.bookLookupReason = QStringLiteral("no-complete-title-author-evidence");
                return match;
            }
            trace.set("query", facts.title);
            const QString fingerprint = bookFingerprint(rows);
            const quint64 generation = m_bookLookupGeneration;
            const quint64 catalogRevision = m_bookCatalogRevision;
            QStringList titles{facts.title};
            static const QRegularExpression qualified(QString::fromLatin1(kBiblioIdentityTitleQualifier));
            QSet<QString> titleKeys{bookEvidenceKey(facts.title)};
            for (const QString& alternative : facts.alternativeTitles) {
                const QString key = bookEvidenceKey(alternative);
                if (!key.isEmpty() && !titleKeys.contains(key) && !qualified.match(alternative).hasMatch()) {
                    titleKeys.insert(key);
                    titles.append(alternative);
                }
            }
            // Library truth precedes every snapshot/cache/network lookup. A
            // physical sibling can carry full author evidence in its filename
            // even when its index author column has not been enriched.
            const bool converging = automaticOnlineBook(row);
            QHash<QString, VaultIndex::FileRow> existing;
            QHash<QString, int> identityCounts;
            QSet<QString> protectedTargets;
            const auto bookRows = bookRowsAtRevision();
            for (const auto& sibling : bookRows) {
                if (sibling.id == row.id || sibling.away || !sibling.errorState.isEmpty()
                    || sibling.identitySuppressed || sibling.identityState == QLatin1String("ambiguous")
                    || !sibling.identityId.startsWith(QLatin1String("biblio:"))) continue;
                const auto siblingFacts = bookFilenameFacts(sibling.realName);
                const QString author = sibling.author.trimmed().isEmpty() ? siblingFacts.author : sibling.author;
                const bool authorFits = facts.abbreviatedAuthor ? bookAbbreviatedSurnameMatches(facts.author, author)
                    : MediaTitleNormalization::bookAuthorIdentityKey(facts.author) == MediaTitleNormalization::bookAuthorIdentityKey(author)
                        || (sibling.author.trimmed().isEmpty() && siblingFacts.abbreviatedAuthor
                            && bookAbbreviatedSurnameMatches(siblingFacts.author, facts.author));
                if (!authorFits) continue;
                for (const auto& title : titles)
                    if (bookEvidenceKey(bookWorkTitle(sibling.identityTitle)) == bookEvidenceKey(title)) {
                        ++identityCounts[sibling.identityId];
                        if ((converging || automaticPrivateEpub(row))
                            && !snapshotBook(sibling) && !automaticOnlineBook(sibling))
                            protectedTargets.insert(sibling.identityId);
                        if (!converging || sibling.identityId != row.identityId)
                            existing.insert(sibling.identityId, sibling);
                        break;
                    }
            }
            if (!existing.isEmpty()) {
                match.candidateCount = existing.size();
                if (existing.size() > 1) {
                    QStringList ids = existing.keys(); ids.sort();
                    match.bookLookupReason = QStringLiteral("ambiguous-library-identities");
                    match.bookLookupWarning = ids.join(QStringLiteral(", "));
                    return match;
                }
                const auto sibling = existing.cbegin().value();
                if (protectedTargets.contains(sibling.identityId)) return match;
                if (converging) {
                    // Only snapshot works or proven automatic online works can
                    // receive a convergence. A manual/chosen sibling is not authority.
                    if (!snapshotBook(sibling) && !automaticOnlineBook(sibling)) return match;
                    if (sibling.identityState == QLatin1String("manual")) return match;
                    if (automaticOnlineBook(sibling)) {
                        const int currentCount = identityCounts.value(row.identityId) + 1;
                        const int targetCount = identityCounts.value(sibling.identityId);
                        if (currentCount > targetCount
                            || (currentCount == targetCount && row.identityId < sibling.identityId)) return match;
                    }
                }
                match.adopted = true; match.source = QStringLiteral("BIBLIO");
                match.sourceId = sibling.identityId; match.title = sibling.identityTitle;
                match.coverUrl = sibling.identityCoverUrl; match.synopsis = sibling.identitySynopsis;
                match.world = QStringLiteral("Biblio"); match.bookLibraryMatch = true;
                match.bookLookupReason = QStringLiteral("existing-library-work");
                match.bookEvidenceFingerprint = fingerprint;
                return match;
            }
            // Existing online identities only converge locally, never start
            // another catalogue/provider adoption or fall back to private EPUB.
            if (converging) return match;
            if (!m_bookLookupOwner || !m_bookLookup) {
                match.bookLookupReason = QStringLiteral("catalogue-unavailable");
                return match;
            }
            // Probe every plausible title before choosing. A prefix-only hit
            // must never override a second work found under the full title.
            QHash<QString, BiblioIdentityCandidate> works;
            for (const QString& title : titles) {
                const auto found = m_bookLookup(title, facts.abbreviatedAuthor ? QString() : facts.author);
                match.bookLookupStatus = found.status;
                match.bookLookupWarning = found.warning;
                if (!m_bookLookupOwner || generation != m_bookLookupGeneration
                    || catalogRevision != m_bookCatalogRevision) return match;
                if (found.status != BiblioIdentityLookup::Status::Available) {
                    match.bookLookupReason = found.status == BiblioIdentityLookup::Status::ReadError
                        ? QStringLiteral("catalogue-read-error") : QStringLiteral("catalogue-unavailable");
                    if (m_pendingBookGroups.size() < 256) m_pendingBookGroups.insert(groupKey, fingerprint);
                    return match;
                }
                for (const auto& candidate : found.candidates)
                    if (!candidate.canonicalId.isEmpty() && !candidate.title.isEmpty()
                        && !candidate.author.isEmpty())
                        works.insert(candidate.canonicalId, candidate);
            }
            // Only the all-local no-exact-work outcome may use online evidence.
            if (works.isEmpty() && m_bookOnlineLookup) {
                bool unavailable = false;
                for (const QString &title : titles) {
                    const auto found = m_bookOnlineLookup(title, facts.author, (facts.title + QLatin1Char(' ') + facts.franchise).trimmed());
                    if (found.status != BiblioIdentityLookup::Status::Available) {
                        unavailable = true;
                        continue; // schedule every alternative before yielding
                    }
                    for (const auto &candidate : found.candidates)
                        works.insert(candidate.canonicalId, candidate);
                }
                if (unavailable) {
                    match.bookLookupStatus = BiblioIdentityLookup::Status::Unavailable;
                    match.bookLookupReason = QStringLiteral("catalogue-unavailable");
                    if (m_pendingBookGroups.size() < 256) m_pendingBookGroups.insert(groupKey, fingerprint);
                    return match;
                }
            }
            match.candidateCount = works.size();
            if (works.size() != 1
                || (facts.abbreviatedAuthor
                    && !bookAbbreviatedSurnameMatches(facts.author, works.cbegin().value().author))) {
                match.bookLookupReason = works.size() > 1
                    ? QStringLiteral("ambiguous-work") : QStringLiteral("no-exact-work");
                if (m_pendingBookGroups.size() < 256) m_pendingBookGroups.insert(groupKey, fingerprint);
                return match;
            }
            const auto candidate = works.cbegin().value();
            match.adopted = true;
            match.candidateCount = 1;
            match.source = QStringLiteral("BIBLIO");
            match.sourceId = QStringLiteral("biblio:") + candidate.canonicalId;
            match.title = candidate.title;
            match.coverUrl = candidate.coverUrl;
            match.world = QStringLiteral("Biblio");
            match.bookProvenance = candidate.sources;
            match.bookWork = candidate;
            match.bookLookupReason = QStringLiteral("exact-title-author-work");
            match.bookEvidenceFingerprint = fingerprint;
            match.bookLookupGeneration = generation;
            match.bookCatalogRevision = catalogRevision;
            return match;
        };
        match = sharedLookup();
        if (match.adopted || automaticOnlineBook(row) || !epub || match.bookLookupReason == QLatin1String("conflicting-file-facts")
            || match.candidateCount > 1)
            return finish(match.adopted ? "book-candidate-selected" : "no-book-adoption", match);
        // No shared work yet: preserve the embedded title and private physical identity.
        if (embeddedTitle.isEmpty()) return finish("no-embedded-title", match);
        for (const auto& candidate : rows) {
            if (candidate.metadataSource != QLatin1String("EPUB") || candidate.away
                || !candidate.errorState.isEmpty()
                || candidate.embeddedTitle.simplified().compare(embeddedTitle.simplified(), Qt::CaseInsensitive) != 0)
                return finish("inconsistent-embedded-titles", match);
        }
        match.adopted = true;
        match.source = QStringLiteral("EPUB");
        match.sourceId = QStringLiteral("epub:") + row.id;
        match.title = embeddedTitle;
        match.synopsis = row.synopsis;
        match.world = QStringLiteral("Biblio");
        return finish("private-epub-fallback", match);
    }

    const QString title = row.groupTitle.trimmed();
    // Vault ux uplift S16: the census's parsed year is the cleaner's own find, and it wins
    // the lookup over the cleaned title (whose year the cleaner already removed), so a
    // remade title matches the catalogue's year filter instead of reading ambiguous.
    LookupTitle lookup = lookupTitle(title);
    if (row.kind == QLatin1String("video")) {
        const QString junkReason = junkClipReason(row);
        if (!junkReason.isEmpty()) {
            trace.set("junkReason", junkReason);
            return finish("junk-clip", match);
        }
        const auto context = VaultKit::videoEpisodeContext(row.path, siblingPaths);
        const bool episode = context.number.matched;
        if (!episode) {
            const QString rawFile = QFileInfo(row.realName).completeBaseName();
            const auto fileLookup = lookupTitle(rawFile);
            const QString rawGroup = row.groupKey == row.path
                ? QFileInfo(row.groupKey).completeBaseName() : QFileInfo(row.groupKey).fileName();
            const auto folderLookup = lookupTitle(rawGroup);
            // Conflicting qualified release facts are uncertainty, not a reason
            // to retry different queries until one happens to hit.
            if (fileLookup.year > 0 && folderLookup.year > 0
                && fileLookup.year != folderLookup.year) return finish("conflicting-folder-year", match);
            if (fileLookup.year > 0 && row.parsedYear > 0
                && fileLookup.year != row.parsedYear) return finish("conflicting-census-year", match);
            if (fileLookup.year > 0) lookup = fileLookup;
            else {
                applyLookupYear(lookup, row);
                // Repair only the demonstrable old loss of a leading numeric
                // title word. A franchise prefix or edition in a folder is
                // never stronger than a valid filename title.
                static const QRegularExpression numericPrefix(QStringLiteral("^\\d+ "));
                const auto prefix = numericPrefix.match(folderLookup.normalized);
                if (folderLookup.year > 0 && prefix.hasMatch()
                    && folderLookup.normalized.mid(prefix.capturedLength()) == lookup.normalized)
                    lookup = folderLookup;
                if (lookup.year == 0 && folderLookup.normalized == lookup.normalized)
                    lookup.year = folderLookup.year;
            }
        } else {
            if (!context.title.isEmpty()) lookup = lookupTitle(context.title);
            applyLookupYear(lookup, row);
        }
    } else applyLookupYear(lookup, row);
    const QString normalizedTitle = lookup.normalized;
    trace.set("query", normalizedTitle);
    trace.set("queryYear", lookup.year);
    if (title.isEmpty() || normalizedTitle.isEmpty())
        return finish("empty-query", match);

    if (row.kind == QLatin1String("comic")) {
        QVariantList comics;
        QVariantList rawComics;
        const bool comicsAvailable = m_comics && m_comics->available();
        if (comicsAvailable) {
            rawComics = m_comics->exactMatches(title);
            for (const QVariant& value : std::as_const(rawComics)) {
                const QVariantMap hit = value.toMap();
                if (VaultKit::normalizedTitle(hit.value(QStringLiteral("title")).toString())
                    == normalizedTitle)
                    comics.append(value);
            }
        }
        const bool comicsPending = m_comics && m_comics->remoteWaiting();
        trace.provider("gcd", comicsAvailable, comicsAvailable, comicsPending,
                       comics, "gcdId", rawComics.size());
        if (trace.active() && comicsAvailable && !m_comics->ready())
            trace.set("gcdQueryKey", m_comics->remoteQueryKey(QStringLiteral("exactMatches"), {title}));
        if (comicsPending) {
            m_pendingGroups.insert(groupKey);
            return finish("waiting-for-gcd", match);
        }

        QVariantList mal;
        // ComicsCatalog owns the western-comics lane. Only fall back to MAL when
        // it has no exact western candidate, so a title present in both corpora
        // never becomes an artificial cross-source ambiguity.
        const bool searchMal = comics.isEmpty() && m_mal && m_mal->available();
        if (searchMal)
            mal = m_mal->matchByTitle(normalizedTitle, lookup.year, {});

        const bool malPending = m_mal && m_mal->remoteWaiting();
        if (trace.active())
            trace.provider("mal", m_mal && m_mal->available(), searchMal, malPending,
                           mal, "mal_id", mal.size());
        if (trace.active() && searchMal && !m_mal->ready())
            trace.set("malQueryKey", m_mal->remoteQueryKey(QStringLiteral("matchByTitle"),
                {MediaTitleNormalization::normalizedTitle(normalizedTitle), lookup.year, QString()}));
        if (malPending) {
            m_pendingGroups.insert(groupKey);
            return finish("waiting-for-mal", match);
        }

        // One exact candidate across the allowed offline catalogues is the
        // certainty threshold. An ambiguity stays filename-honest.
        const int candidateCount = comics.size() + mal.size();
        match.candidateCount = candidateCount;
        if (candidateCount != 1)
            return finish(candidateCount > 1 ? "ambiguous" : "no-exact-candidate", match);

        if (comics.size() == 1) {
            const QVariantMap hit = comics.constFirst().toMap();
            const int gcdId = hit.value(QStringLiteral("gcdId")).toInt();
            if (gcdId <= 0)
                return finish("invalid-gcd-id", Match());
            const QVariantMap series = m_comics->series(gcdId);
            if (trace.active() && !m_comics->ready())
                trace.set("gcdSeriesQueryKey", m_comics->remoteQueryKey(QStringLiteral("series"), {gcdId}));
            if (m_comics->remoteWaiting()) {
                m_pendingGroups.insert(groupKey);
                return finish("waiting-for-gcd-series", Match());
            }
            match.adopted = true;
            match.source = QStringLiteral("COMICS");
            match.sourceId = QStringLiteral("comics:") + QString::number(gcdId);
            match.title = firstNonEmpty(series.value(QStringLiteral("title")).toString(),
                                        hit.value(QStringLiteral("title")).toString());
            match.synopsis = series.value(QStringLiteral("synopsis")).toString();
            match.coverUrl = firstNonEmpty(series.value(QStringLiteral("cover")).toString(),
                                           hit.value(QStringLiteral("cover")).toString());
            match.world = QStringLiteral("Tankoban");
            match.year = firstNonEmpty(QString::number(series.value(QStringLiteral("year")).toInt()),
                                       QString::number(hit.value(QStringLiteral("year")).toInt()))
                             .toInt();
            return finish("unique-gcd-candidate", match);
        }

        const QVariantMap hit = mal.constFirst().toMap();
        const int malId = hit.value(QStringLiteral("mal_id")).toInt();
        if (malId <= 0)
            return finish("invalid-mal-id", Match());
        match.adopted = true;
        match.source = QStringLiteral("MAL");
        match.sourceId = QStringLiteral("mal:") + QString::number(malId);
        match.title = firstNonEmpty(hit.value(QStringLiteral("title_english")).toString(),
                                   hit.value(QStringLiteral("title")).toString());
        match.synopsis = hit.value(QStringLiteral("synopsis")).toString();
        match.coverUrl = hit.value(QStringLiteral("coverUrl")).toString();
        match.world = QStringLiteral("Tankoban");
        match.year = hit.value(QStringLiteral("year")).toInt();
        return finish("unique-mal-candidate", match);
    }

    if (row.kind == QLatin1String("video")) {
        if (!m_imdb || !m_imdb->available()) {
            if (m_imdb) m_pendingGroups.insert(groupKey);
            return finish(m_imdb ? "waiting-for-imdb-availability" : "no-imdb-provider", match);
        }
        bool episodesOnly = true;
        int lastAbsoluteEpisode = 0;
        for (const auto& member : rows) {
            const auto episode = VaultKit::videoEpisodeContext(member.path, siblingPaths).number;
            episodesOnly = episodesOnly && episode.matched;
            if (episode.matched && episode.absolute)
                lastAbsoluteEpisode = qMax(lastAbsoluteEpisode, episode.episode);
        }
        QVariantList hits;
        for (const auto& value : m_imdb->matchByTitle(normalizedTitle, lookup.year)) {
            const auto type = value.toMap().value(QStringLiteral("type")).toString();
            if (!episodesOnly || type == QLatin1String("series") || type == QLatin1String("mini"))
                hits.append(value);
        }
        if (m_imdb->remoteWaiting()) {
            trace.provider("imdb", true, true, true, hits, "tt", hits.size());
            m_pendingGroups.insert(groupKey);
            return finish("waiting-for-imdb", match);
        }
        if (episodesOnly && lastAbsoluteEpisode > 0 && hits.size() > 1) {
            QVariantList fits;
            bool countsKnown = true;
            for (const auto& value : std::as_const(hits)) {
                const int count = value.toMap().value(QStringLiteral("episodes")).toInt();
                countsKnown = countsKnown && count > 0;
                if (count >= lastAbsoluteEpisode)
                    fits.append(value);
            }
            // Missing episode counts provide no evidence against a candidate.
            if (countsKnown && fits.size() == 1)
                hits = std::move(fits);
        }
        match.candidateCount = hits.size();
        if (hits.isEmpty()) {
            // Zero candidates is absence, not ambiguity; two narrow, year-bound retries follow, each
            // through the same three-step lookup, each adopting only on exactly one candidate.
            QVariantList retry;
            bool anyEpisode = false;
            for (const auto& member : rows)
                anyEpisode = anyEpisode || VaultKit::videoEpisodeContext(member.path, siblingPaths).number.matched;
            if (episodesOnly && lookup.year > 0) {
                // "X-Men The Animated Series (1992 - 1997)": the folder names the show plus a
                // generic descriptor; the descriptor is not part of the catalogue title.
                static const QRegularExpression descriptor(QStringLiteral(
                    "^(.+?)\\s+(?:the\\s+)?(?:(?:animated|complete|tv|original|classic)\\s+)?(?:series|collection)$"));
                const auto d = descriptor.match(normalizedTitle);
                if (d.hasMatch())
                    for (const auto& value : m_imdb->matchByTitle(d.captured(1), lookup.year)) {
                        const auto type = value.toMap().value(QStringLiteral("type")).toString();
                        if (type == QLatin1String("series") || type == QLatin1String("mini")) retry.append(value);
                    }
            } else if (!anyEpisode) {
                retry = folderTitleHits(rows, row);
            }
            if (m_imdb->remoteWaiting()) {
                m_pendingGroups.insert(groupKey);
                return finish("waiting-for-imdb-fallback", match);
            }
            if (retry.size() == 1)
                hits = retry;
        }
        trace.provider("imdb", true, true, false, hits, "tt", hits.size());
        if (hits.size() != 1)
            return finish(hits.size() > 1 ? "ambiguous" : "no-exact-candidate", match);
        const QVariantMap hit = hits.constFirst().toMap();
        const QString tt = hit.value(QStringLiteral("tt")).toString();
        if (tt.isEmpty())
            return finish("invalid-imdb-id", match);
        match.adopted = true;
        match.source = QStringLiteral("IMDB");
        match.sourceId = QStringLiteral("imdb:") + tt;
        match.title = hit.value(QStringLiteral("title")).toString();
        match.type = adoptedType(hit.value(QStringLiteral("type")).toString());
        match.synopsis = hit.value(QStringLiteral("synopsis")).toString();
        match.coverUrl = QStringLiteral("https://live.metahub.space/poster/medium/%1/img").arg(tt);
        match.world = QStringLiteral("Theatre");
        match.year = hit.value(QStringLiteral("year")).toInt();
        // Phase-4 G1 ruling (2026-08-25): the adopted identity's rating + genres ride the
        // Match. The bake already returns both; ImdbCatalog folds the baked db's JSON genre
        // array to a QVariantList, so the join reducer here goes straight to the list. Votes
        // deliberately not carried (the ruling excluded them — a popularity count ages).
        match.rating = hit.value(QStringLiteral("rating")).toDouble();
        match.genres = videoGenres(hit.value(QStringLiteral("genres")));
        return finish("unique-imdb-candidate", match);
    }

    return finish("unsupported-kind", match);
}

bool VaultIdentifier::automaticOnlineBook(const VaultIndex::FileRow& row) const
{
    if (row.kind != QLatin1String("book") || row.identitySource != QLatin1String("BIBLIO")
        || row.identityState != QLatin1String("adopted") || row.identitySuppressed
        || (!row.identityId.startsWith(QLatin1String("biblio:user:apple:"))
            && !row.identityId.startsWith(QLatin1String("biblio:user:openlibrary:")))
        || !m_bookLookupOwner || !m_bookLookup) return false;
    const auto filename = bookFilenameFacts(row.realName);
    const auto found = m_bookLookup(bookWorkTitle(row.identityTitle),
        row.author.trimmed().isEmpty() ? filename.author : row.author);
    if (found.status != BiblioIdentityLookup::Status::Available) return false;
    for (const auto& work : found.candidates) {
        if (QStringLiteral("biblio:") + work.canonicalId != row.identityId) continue;
        for (const auto& source : work.sources)
            if (source.field == QLatin1String("title")
                && (source.source == QLatin1String("apple") || source.source == QLatin1String("openlibrary"))
                && work.canonicalId == QStringLiteral("user:") + source.source + QLatin1Char(':') + source.sourceId)
                return true;
    }
    return false;
}

bool VaultIdentifier::snapshotBook(const VaultIndex::FileRow& row) const
{
    if (row.identitySource != QLatin1String("BIBLIO") || row.identityState == QLatin1String("manual")) return false;
    if (row.identityId.startsWith(QLatin1String("biblio:ta-"))) return true;
    if (row.identityId.startsWith(QLatin1String("biblio:user:")) || !m_bookLookupOwner || !m_bookLookup) return false;
    const auto filename = bookFilenameFacts(row.realName);
    const auto found = m_bookLookup(bookWorkTitle(row.identityTitle),
        row.author.trimmed().isEmpty() ? filename.author : row.author);
    if (found.status == BiblioIdentityLookup::Status::Available)
        for (const auto& work : found.candidates)
            if (QStringLiteral("biblio:") + work.canonicalId == row.identityId) return true;
    return false;
}

bool VaultIdentifier::automaticPrivateEpub(const VaultIndex::FileRow& row)
{
    return row.kind == QLatin1String("book") && row.identityId == QStringLiteral("epub:") + row.id
        && row.identitySource == QLatin1String("EPUB")
        && (row.identityState.isEmpty() || row.identityState == QLatin1String("adopted"))
        && !row.identitySuppressed;
}

const QList<VaultIndex::FileRow>& VaultIdentifier::bookRowsAtRevision() const
{
    const quint64 revision = m_index->revision();
    if (m_bookRowsRevision != revision) {
        m_bookRows = m_index->rowsForKind(QStringLiteral("book"));
        m_bookRowsRevision = revision;
        m_bookRowsFingerprint.clear();
    }
    return m_bookRows;
}

bool VaultIdentifier::shouldAutoIdentify(const QList<VaultIndex::FileRow>& rows, const char* origin)
{
    const auto skip = [&](const char* reason) {
        VaultGroupTrace trace("skip", origin, rows.isEmpty() ? QString() : rows.constFirst().groupKey);
        trace.rows(rows);
        trace.reason(reason);
        return false;
    };
    if (rows.isEmpty()) return skip("no-rows");
    bool migration = false;
    for (const auto& row : rows) {
        if (row.identityState == QLatin1String("manual")) return skip("manual-identity");
        if (row.identitySuppressed) return skip("identity-suppressed");
        if (row.away) return skip("away");
        if (!row.errorState.isEmpty()) return skip("row-error");
        if (!row.identityId.isEmpty()) {
            if (!automaticPrivateEpub(row) && !automaticOnlineBook(row)) return skip("already-identified");
            migration = true;
        }
    }
    if (!migration) return true;
    // Include siblings: new library evidence deserves one new convergence probe.
    const auto& bookRows = bookRowsAtRevision();
    if (m_bookRowsFingerprint.isEmpty())
        m_bookRowsFingerprint = bookFingerprint(bookRows);
    const QString key = rows.first().groupKey;
    if (m_epubAttempts.value(key) == m_bookRowsFingerprint) return skip("book-evidence-already-checked");
    m_epubAttempts.insert(key, m_bookRowsFingerprint);
    return true;
}

int VaultIdentifier::autoIdentifyExisting()
{
    if (!m_index)
        return 0;

    int adopted = 0;
    const QStringList kinds = {QStringLiteral("comic"), QStringLiteral("book"),
                               QStringLiteral("video")};
    for (const QString& kind : kinds) {
        const QVariantList groups = m_index->groupsForKind(kind);
        for (const QVariant& value : groups) {
            const QString groupKey = value.toMap().value(QStringLiteral("groupKey")).toString();
            if (groupKey.isEmpty())
                continue;
            const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
            if (rows.isEmpty())
                continue;

            if (!shouldAutoIdentify(rows))
                continue;

            const Match match = matchGroup(groupKey);
            if (match.adopted && rows.first().identityId != match.sourceId
                && applyGroup(groupKey, match))
                ++adopted;
            else if (kind == QLatin1String("book") && match.candidateCount > 1)
                recordAmbiguous(groupKey, match.candidateCount);
        }
    }
    backfillVideoGenres();
    return adopted;
}

bool VaultIdentifier::applyGroup(const QString& groupKey, const Match& match, const char* origin)
{
    VaultGroupTrace trace("save", origin, groupKey);
    trace.set("identityId", match.sourceId);
    const auto failed = [&](const char* reason) {
        trace.reason(reason);
        trace.set("saved", false);
        return false;
    };
    if (!m_index || groupKey.isEmpty() || !match.adopted || match.source.isEmpty()
        || match.sourceId.isEmpty() || match.title.isEmpty())
        return failed("incomplete-identity");
    QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    trace.rows(rows);
    if (rows.isEmpty() || !safeGroupAction(rows))
        return failed("unsafe-or-missing-group");
    if (!match.bookEvidenceFingerprint.isEmpty()
        && ((!match.bookLibraryMatch && (!m_bookLookupOwner || match.bookLookupGeneration != m_bookLookupGeneration
            || match.bookCatalogRevision != m_bookCatalogRevision))
            || bookFingerprint(rows) != match.bookEvidenceFingerprint))
        return failed("stale-book-evidence");
    if (match.bookLibraryMatch) {
        const auto current = matchGroup(groupKey, "book-revalidate");
        if (!current.adopted || !current.bookLibraryMatch || current.sourceId != match.sourceId)
            return failed("changed-library-match");
    }
    if (!match.bookWork.canonicalId.isEmpty() && m_bookRetain
        && !m_bookRetain(match.bookWork)) return failed("book-retain-failed");
    for (VaultIndex::FileRow& row : rows) {
        row.identityId = match.sourceId;
        row.identityTitle = match.title;
        row.identityType = adoptedType(match.type);
        row.identitySource = match.source;
        row.identitySynopsis = match.synopsis;
        row.identityCoverUrl = match.coverUrl;
        row.identityWorld = match.world;
        row.identityYear = match.year;
        row.identityRating = match.rating;   // phase-4 G1: the adopted IMDb facts persist
        row.identityGenres = match.genres;
        row.identitySuppressed = false;
        // Adoption clears any prior ambiguity — identify-in-place settles the tile.
        row.identityState = match.manualChoice ? QStringLiteral("manual") : QStringLiteral("adopted");
        row.identityCandidateCount = 0;
    }
    const bool saved = m_index->upsertMany(rows);
    trace.set("saved", saved);
    trace.reason(saved ? "identity-persisted" : "index-write-failed");
    if (saved && match.source == QLatin1String("EPUB") && m_pendingBookGroups.contains(groupKey))
        m_pendingBookGroups.insert(groupKey, bookFingerprint(rows));
    return saved;
}

bool VaultIdentifier::identifyGroupWith(const QString& groupKey, const Match& match)
{
    Match chosen = match;
    chosen.manualChoice = true;
    return applyGroup(groupKey, chosen);
}

bool VaultIdentifier::unidentifyGroup(const QString& groupKey)
{
    if (!m_index || groupKey.isEmpty())
        return false;
    QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty() || !safeGroupAction(rows))
        return false;
    for (VaultIndex::FileRow& row : rows) {
        clearIdentity(row);
        row.identitySuppressed = true;
        row.identityState = QStringLiteral("suppressed");
        if (row.kind == QLatin1String("book")) {
            row.displayTitle = VaultKit::cleanMediaFolderTitle(row.realName);
            row.author.clear();
            row.format.clear();
            row.coverRef.clear();
            row.synopsis.clear();
            row.metadataSource.clear();
            row.embeddedTitle.clear();
        }
    }
    return m_index->upsertMany(rows);
}

bool VaultIdentifier::reshelveGroup(const QString& groupKey, const QString& kind)
{
    if (!m_index || groupKey.isEmpty()
        || !(kind == QLatin1String("comic") || kind == QLatin1String("book")
             || kind == QLatin1String("video")))
        return false;
    QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty() || !safeGroupAction(rows))
        return false;
    for (VaultIndex::FileRow& row : rows) {
        row.kind = kind;
        clearIdentity(row);
        row.identitySuppressed = false;
    }
    return m_index->upsertMany(rows);
}

QVariantList VaultIdentifier::folderTitleHits(const QList<VaultIndex::FileRow>& rows, const VaultIndex::FileRow& row) const
{
    // A single feature file in its own folder: the folder name is the better title when the
    // file's own name gave nothing ("aaf-sstroba.720p.mkv" inside "...Black.Adam.2010...-aAF").
    if (!m_index || !m_imdb || rows.size() != 1 || VaultKit::isSampleVideo(row.path) || isReleaseCreditClip(row))
        return {};
    const QFileInfo file(row.path);
    const QString dirName = file.absoluteDir().dirName();
    const int folderYear = VaultKit::parsedTitleYear(dirName);
    if (folderYear <= 0) return {};
    const LookupTitle folder = lookupTitle(dirName);
    if (folder.normalized.isEmpty() || isGenericFolderTitle(folder.normalized)) return {};
    const LookupTitle own = lookupTitle(file.completeBaseName());
    if (own.year > 0 && own.year != folderYear) return {};
    if (row.parsedYear > 0 && row.parsedYear != folderYear) return {};
    // No other feature may share the folder; samples, credit clips and subfolders (Extras/Samples) don't count.
    for (const auto& other : m_index->rowsForRoot(row.rootPath)) {
        if (other.kind != QLatin1String("video") || other.path == row.path) continue;
        if (QFileInfo(other.path).absolutePath() != file.absolutePath()) continue;
        if (VaultKit::isSampleVideo(other.path) || isReleaseCreditClip(other)) continue;
        return {};
    }
    return m_imdb->matchByTitle(folder.normalized, folderYear);
}

QString VaultIdentifier::junkClipReason(const VaultIndex::FileRow& row)
{
    if (row.kind != QLatin1String("video") || !row.identityId.isEmpty() || row.identitySuppressed
        || row.away || !row.errorState.isEmpty() || row.size <= 0) return {};
    const QFileInfo file(row.path);
    const QString stem = QFileInfo(row.realName).completeBaseName();
    static const QRegularExpression sample(QStringLiteral("(?i)(?:^|[^a-z0-9])sample(?:$|[^a-z0-9])"));
    static const QRegularExpression sampleDir(QStringLiteral("(?i)^samples?$"));
    if (row.size < 400LL * 1024 * 1024
        && (sample.match(stem).hasMatch() || sampleDir.match(file.absoluteDir().dirName()).hasMatch()))
        return QStringLiteral("sample-under-400MB");
    static const QRegularExpression domain(QStringLiteral("(?i)^(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\\.)+(?:[a-z]{2}|com|org|net|info|biz|name|mobi|pro|aero|coop|museum|xxx|xyz|online|site|club|tech|store|website)$"));
    static const QRegularExpression group(QStringLiteral("-([A-Za-z0-9]{2,16})$"));
    const auto tag = group.match(file.absoluteDir().dirName());
    // A domain pasted onto a release basename is not a domain-only credit clip.
    static const QRegularExpression releaseShape(QStringLiteral(
        "(?i)(?:^|[^a-z0-9])(?:[12][0-9]{3}|[0-9]{3,4}p|bluray|brrip|hdrip|webrip|x26[45]|hevc)(?:$|[^a-z0-9])"));
    if (row.size < 60LL * 1024 * 1024 && ((domain.match(stem).hasMatch() && !releaseShape.match(stem).hasMatch())
        || (tag.hasMatch() && stem.compare(tag.captured(1), Qt::CaseInsensitive) == 0)))
        return QStringLiteral("promo-under-60MB");
    return {};
}

QHash<QString, QString> VaultIdentifier::physicalExtrasParents(const QList<VaultIndex::FileRow>& videoRows)
{
    auto segments = [](const QString& path) {
        return QDir::fromNativeSeparators(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    };
    // Matched episodes prove their enclosing show folder is not an extras directory,
    // even when its release name ends in '+ Extras'. Nested bonus folders still are.
    QSet<QString> showFolders;
    for (const auto& r : videoRows)
        if (!r.identityId.isEmpty() && !r.identitySuppressed && VaultKit::parseEpisodeNumber(r.realName).matched)
            showFolders.insert(VaultLocation::normalize(VaultKit::showRootForEpisodePath(r.path)));
    // ancestor directory -> identities of identified feature rows beneath it (extras folders excluded)
    QHash<QString, QSet<QString>> identitiesUnder;
    QHash<QString, QString> titleOf;
    for (const auto& r : videoRows) {
        if (r.kind != QLatin1String("video") || r.identityId.isEmpty() || r.identitySuppressed) continue;
        titleOf.insert(r.identityId, r.identityTitle);
        const QStringList parts = segments(r.path);
        for (int depth = 1; depth < parts.size(); ++depth) {
            bool extra = false;
            for (int i = depth; i < parts.size() - 1; ++i)
                extra = extra || (extrasLikeFolderName(parts.at(i))
                    && !showFolders.contains(VaultLocation::normalize(parts.mid(0, i + 1).join(QLatin1Char('/')))));
            if (!extra) identitiesUnder[parts.mid(0, depth).join(QLatin1Char('/'))].insert(r.identityId);
        }
    }
    QHash<QString, QString> out;
    for (const auto& r : videoRows) {
        if (r.kind != QLatin1String("video") || !r.identityId.isEmpty() || r.identitySuppressed || r.id.isEmpty()
            || r.away || !r.errorState.isEmpty()) continue;
        const bool junk = !junkClipReason(r).isEmpty();
        const QStringList parts = segments(r.path);
        int extrasIndex = -1;
        for (int i = parts.size() - 2; i >= 0; --i)
            if (extrasLikeFolderName(parts.at(i))) { extrasIndex = i; break; }
        if (junk) {
            extrasIndex = parts.size() - 1;
            if (parts.at(extrasIndex - 1).compare(QLatin1String("sample"), Qt::CaseInsensitive) == 0
                || parts.at(extrasIndex - 1).compare(QLatin1String("samples"), Qt::CaseInsensitive) == 0) --extrasIndex;
        }
        if (extrasIndex < 1) continue;
        const int rootDepth = segments(r.rootPath).size();
        for (int depth = extrasIndex; depth >= 1 && depth > rootDepth; --depth) {
            if (junk && depth != extrasIndex) break; // only this release, never a shelf's unrelated feature
            const auto found = identitiesUnder.constFind(parts.mid(0, depth).join(QLatin1Char('/')));
            if (found == identitiesUnder.constEnd() || found->isEmpty()) continue;
            if (found->size() == 1) {
                // The folder must be named for that title ("The Sopranos - ... + Extras",
                // "Dr.Strangelove.1964..."): a collection folder ("Sitcoms") that merely holds an
                // identified show is not the parent of a sibling show's "... And Extras" folder.
                const QString identity = *found->constBegin();
                if (junk) { out.insert(r.id, identity); break; }
                const QString folderName = VaultKit::normalizedTitle(parts.at(depth - 1));
                QStringList forms{VaultKit::normalizedTitle(titleOf.value(identity))};
                for (const QString& head : ImdbCatalog::subtitleHeads(titleOf.value(identity))) forms.append(head);
                const QString padded = QLatin1Char(' ') + folderName + QLatin1Char(' ');
                for (const QString& form : forms)
                    if (!form.isEmpty() && padded.contains(QLatin1Char(' ') + form + QLatin1Char(' '))) {
                        out.insert(r.id, identity);
                        break;
                    }
            }
            break; // the nearest ancestor holding any identified title decides, ambiguous or not
        }
    }
    return out;
}

bool VaultIdentifier::recordAmbiguous(const QString& groupKey, int candidateCount, const char* origin)
{
    VaultGroupTrace trace("save-ambiguity", origin, groupKey);
    trace.set("candidateCount", candidateCount);
    if (!m_index || groupKey.isEmpty() || candidateCount <= 1) {
        trace.reason("invalid-ambiguity");
        return false;
    }
    QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    trace.rows(rows);
    if (rows.isEmpty()) {
        trace.reason("missing-group");
        return false;
    }
    bool dirty = false;
    for (VaultIndex::FileRow& row : rows) {
        // Never overwrite a group that has already resolved either way (adopted, or a user's
        // explicit Un-identify) — ambiguity recording only applies to a group that has never
        // resolved, so a later re-check of an already-settled group is a harmless no-op.
        if (!row.identityId.isEmpty() || row.identitySuppressed)
            continue;
        if (row.identityState != QLatin1String("ambiguous")
            || row.identityCandidateCount != candidateCount) {
            row.identityState = QStringLiteral("ambiguous");
            row.identityCandidateCount = candidateCount;
            dirty = true;
        }
    }
    const bool saved = dirty && m_index->upsertMany(rows);
    trace.set("saved", saved);
    trace.reason(!dirty ? "already-recorded-or-settled" : saved ? "ambiguity-persisted" : "index-write-failed");
    return saved;
}
