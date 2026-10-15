#include "ArtistCatalogueQuery.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace vinyl::catalogue::artist_proof {
namespace {

QString digest(const QString &input)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Sha256).toHex());
}

template<class T>
QString joinedEnumValues(const QVector<T> &values)
{
    QStringList result;
    for (const T item : values)
        result.push_back(QString::number(static_cast<int>(item)));
    result.sort();
    result.removeDuplicates();
    return result.join(QLatin1Char(','));
}

QString fingerprint(const ArtistMbid &id, const DiscographyOptions &options)
{
    QStringList secondary = options.groups.secondaryTypes;
    secondary.sort();
    secondary.removeDuplicates();
    const QString body = id.toString() + QLatin1Char('|')
        + joinedEnumValues(options.groups.primaryTypes) + QLatin1Char('|')
        + secondary.join(QLatin1Char(',')) + QLatin1Char('|')
        + (options.groups.firstReleaseYearFrom
               ? QString::number(*options.groups.firstReleaseYearFrom) : QStringLiteral("~"))
        + QLatin1Char('|')
        + (options.groups.firstReleaseYearTo
               ? QString::number(*options.groups.firstReleaseYearTo) : QStringLiteral("~"))
        + QLatin1Char('|') + joinedEnumValues(options.statuses)
        + QLatin1Char('|') + QString::number(static_cast<int>(options.sort));
    return digest(body);
}

bool hasCredit(const QVector<ArtistCredit> &credits, const ArtistMbid &artist)
{
    for (const auto &credit : credits)
        if (credit.artistMbid == artist)
            return true;
    return false;
}

bool sameCredits(const QVector<ArtistCredit> &a, const QVector<ArtistCredit> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (a.at(i).artistMbid != b.at(i).artistMbid
            || a.at(i).creditedName != b.at(i).creditedName) return false;
    return true;
}

bool sameGroup(const ReleaseGroup &a, const ReleaseGroup &b)
{
    return a.mbid == b.mbid && a.title == b.title
        && a.primaryType == b.primaryType
        && a.secondaryTypes == b.secondaryTypes
        && a.firstReleaseYear == b.firstReleaseYear
        && sameCredits(a.artistCredit, b.artistCredit);
}

bool sameRelease(const Release &a, const Release &b)
{
    return a.mbid == b.mbid && a.releaseGroupMbid == b.releaseGroupMbid
        && a.status == b.status && a.country == b.country
        && a.releaseDate == b.releaseDate && a.title == b.title
        && sameCredits(a.artistCredit, b.artistCredit);
}

bool sameRecording(const SourcedRecording &a, const SourcedRecording &b)
{
    return a.canonical.mbid == b.canonical.mbid
        && a.canonical.title == b.canonical.title
        && a.canonical.lengthMs == b.canonical.lengthMs
        && sameCredits(a.canonical.artistCredit, b.canonical.artistCredit);
}

QString sourceKey(const SourceRef &r)
{
    // Length framing is injective even for opaque provider IDs containing ':'.
    const QString provider = r.item.providerId().value();
    const QString item = r.item.itemId().value();
    return QString::number(provider.size()) + QLatin1Char(':') + provider
        + QString::number(item.size()) + QLatin1Char(':') + item
        + QString::number(r.provenance.size()) + QLatin1Char(':') + r.provenance;
}

bool sameSource(const SourceRef &a, const SourceRef &b)
{
    return a.item == b.item && a.provenance == b.provenance;
}

bool sameSourcedCredit(const SourcedCredit &a, const SourcedCredit &b)
{
    if (a.canonical.artistMbid != b.canonical.artistMbid
        || a.canonical.creditedName != b.canonical.creditedName
        || a.source.has_value() != b.source.has_value()) return false;
    return !a.source || sameSource(*a.source, *b.source);
}

bool validSource(const SourceRef &r)
{
    return !r.provenance.trimmed().isEmpty();
}

ReleaseStatus statusOf(QString value)
{
    value = value.trimmed().toCaseFolded();
    if (value == QStringLiteral("official")) return ReleaseStatus::Official;
    if (value == QStringLiteral("promotion")) return ReleaseStatus::Promotion;
    if (value == QStringLiteral("bootleg")) return ReleaseStatus::Bootleg;
    if (value == QStringLiteral("pseudo-release")
        || value == QStringLiteral("pseudo release")) return ReleaseStatus::PseudoRelease;
    return ReleaseStatus::Other;
}

bool containsStatus(const QVector<ReleaseStatus> &statuses, const GroupRow &row)
{
    if (statuses.isEmpty()) return true;
    for (const auto &edition : row.editions)
        if (statuses.contains(statusOf(edition.status))) return true;
    return false;
}

bool matches(const DiscographyOptions &options, const GroupRow &row)
{
    const auto &filter = options.groups;
    if (!filter.primaryTypes.isEmpty()
        && !filter.primaryTypes.contains(row.canonical.primaryType)) return false;
    if (!filter.secondaryTypes.isEmpty()) {
        bool secondaryFound = false;
        for (const QString &type : filter.secondaryTypes)
            if (row.canonical.secondaryTypes.contains(type)) secondaryFound = true;
        if (!secondaryFound) return false;
    }
    if (filter.firstReleaseYearFrom
        && (!row.canonical.firstReleaseYear
            || *row.canonical.firstReleaseYear < *filter.firstReleaseYearFrom)) return false;
    if (filter.firstReleaseYearTo
        && (!row.canonical.firstReleaseYear
            || *row.canonical.firstReleaseYear > *filter.firstReleaseYearTo)) return false;
    return containsStatus(options.statuses, row);
}

bool groupLess(const GroupRow &a, const GroupRow &b, DiscographySort sort)
{
    const auto &x = a.canonical;
    const auto &y = b.canonical;
    if (sort == DiscographySort::OldestFirst || sort == DiscographySort::NewestFirst) {
        if (x.firstReleaseYear.has_value() != y.firstReleaseYear.has_value())
            return x.firstReleaseYear.has_value(); // unknown years are always last
        if (x.firstReleaseYear && *x.firstReleaseYear != *y.firstReleaseYear)
            return sort == DiscographySort::OldestFirst
                ? *x.firstReleaseYear < *y.firstReleaseYear
                : *x.firstReleaseYear > *y.firstReleaseYear;
    }
    const int titleCmp = QString::compare(x.title, y.title, Qt::CaseInsensitive);
    if (titleCmp != 0)
        return sort == DiscographySort::TitleDescending
            ? titleCmp > 0 : titleCmp < 0;
    return x.mbid.toString() < y.mbid.toString();
}

void appendUnique(QVector<SourceRef> &into, const SourceRef &source)
{
    for (const auto &existing : into)
        if (sourceKey(existing) == sourceKey(source)) return;
    into.push_back(source);
}

CatalogueResult<QVector<GroupRow>> normalizedGroups(
    const ArtistMbid &artist, const DiscographyOptions &options,
    const QVector<GroupRow> &incoming)
{
    std::map<QString, GroupRow> groups;
    for (const GroupRow &row : incoming) {
        if (!hasCredit(row.canonical.artistCredit, artist))
            return CatalogueResult<QVector<GroupRow>>::failure(
                CatalogueErrorCode::Unavailable, QStringLiteral("artistCredit"),
                QStringLiteral("provider returned another artist's release group"));
        for (const auto &s : row.sources)
            if (!validSource(s))
                return CatalogueResult<QVector<GroupRow>>::failure(
                    CatalogueErrorCode::Unavailable, QStringLiteral("provenance"));
        for (const auto &c : row.credits) {
            if (!hasCredit(row.canonical.artistCredit, c.canonical.artistMbid)
                || (c.source && !validSource(*c.source)))
                return CatalogueResult<QVector<GroupRow>>::failure(
                    CatalogueErrorCode::Unavailable, QStringLiteral("artistCredits"));
        }
        for (const auto &r : row.recordings) {
            if (r.source && !validSource(*r.source))
                return CatalogueResult<QVector<GroupRow>>::failure(
                    CatalogueErrorCode::Unavailable, QStringLiteral("recordingSource"));
            for (const auto &additional : r.additionalSources)
                if (!validSource(additional))
                    return CatalogueResult<QVector<GroupRow>>::failure(
                        CatalogueErrorCode::Unavailable, QStringLiteral("additionalSource"));
            for (const auto &c : r.credits) {
                if (!hasCredit(r.canonical.artistCredit, c.canonical.artistMbid)
                    || (c.source && !validSource(*c.source)))
                    return CatalogueResult<QVector<GroupRow>>::failure(
                        CatalogueErrorCode::Unavailable, QStringLiteral("recordingCredits"));
            }
        }
        for (const Release &edition : row.editions)
            if (edition.releaseGroupMbid != row.canonical.mbid)
                return CatalogueResult<QVector<GroupRow>>::failure(
                    CatalogueErrorCode::Unavailable, QStringLiteral("releaseGroupMbid"));

        const QString key = row.canonical.mbid.toString();
        auto inserted = groups.emplace(key, row);
        GroupRow &current = inserted.first->second;
        if (!inserted.second) {
            if (!sameGroup(current.canonical, row.canonical))
                return CatalogueResult<QVector<GroupRow>>::failure(
                    CatalogueErrorCode::Unavailable, QStringLiteral("releaseGroup"),
                    QStringLiteral("conflicting records for one canonical MBID"));
            for (const auto &edition : row.editions) {
                auto match = std::find_if(current.editions.begin(), current.editions.end(),
                    [&](const Release &existing) { return edition.mbid == existing.mbid; });
                if (match == current.editions.end()) current.editions.push_back(edition);
                else if (!sameRelease(*match, edition))
                    return CatalogueResult<QVector<GroupRow>>::failure(
                        CatalogueErrorCode::Unavailable, QStringLiteral("edition"));
            }
            for (const SourceRef &source : row.sources)
                appendUnique(current.sources, source);
            for (const auto &credit : row.credits) {
                const bool duplicate = std::any_of(
                    current.credits.begin(), current.credits.end(),
                    [&](const SourcedCredit &existing) {
                        return sameSourcedCredit(existing, credit);
                    });
                if (!duplicate) current.credits.push_back(credit);
            }
            for (const auto &recording : row.recordings) {
                auto match = std::find_if(current.recordings.begin(), current.recordings.end(),
                    [&](const SourcedRecording &existing) {
                        return existing.canonical.mbid == recording.canonical.mbid;
                    });
                if (match == current.recordings.end()) current.recordings.push_back(recording);
                else {
                    if (!sameRecording(*match, recording))
                        return CatalogueResult<QVector<GroupRow>>::failure(
                            CatalogueErrorCode::Unavailable, QStringLiteral("recording"));
                    if (recording.source) {
                        if (!match->source) match->source = recording.source;
                        else if (!sameSource(*match->source, *recording.source))
                            appendUnique(match->additionalSources, *recording.source);
                    }
                    for (const auto &extra : recording.additionalSources)
                        if (!match->source || !sameSource(*match->source, extra))
                            appendUnique(match->additionalSources, extra);
                    for (const auto &credit : recording.credits)
                        if (!std::any_of(match->credits.begin(), match->credits.end(),
                              [&](const SourcedCredit &existing) {
                                  return sameSourcedCredit(existing, credit);
                              }))
                            match->credits.push_back(credit);
                }
            }
        }
    }
    QVector<GroupRow> result;
    result.reserve(static_cast<qsizetype>(groups.size()));
    for (auto &item : groups) {
        GroupRow &row = item.second;
        // An edition can be repeated inside a single provider row as well as
        // across duplicate groups. Never emit the edition twice.
        std::sort(row.editions.begin(), row.editions.end(), [](const Release &a, const Release &b) {
            return a.mbid.toString() < b.mbid.toString();
        });
        for (int i = 1; i < row.editions.size();) {
            if (row.editions.at(i - 1).mbid != row.editions.at(i).mbid) {
                ++i;
            } else if (!sameRelease(row.editions.at(i - 1), row.editions.at(i))) {
                return CatalogueResult<QVector<GroupRow>>::failure(
                    CatalogueErrorCode::Unavailable, QStringLiteral("edition"),
                    QStringLiteral("one release MBID carries conflicting versions"));
            } else {
                row.editions.removeAt(i);
            }
        }
        std::sort(row.editions.begin(), row.editions.end(), [](const Release &a, const Release &b) {
            if (a.releaseDate != b.releaseDate) return a.releaseDate < b.releaseDate;
            return a.mbid.toString() < b.mbid.toString();
        });
        std::sort(row.sources.begin(), row.sources.end(), [](const SourceRef &a, const SourceRef &b) {
            return sourceKey(a) < sourceKey(b);
        });
        std::sort(row.recordings.begin(), row.recordings.end(),
                  [](const SourcedRecording &a, const SourcedRecording &b) {
            return a.canonical.mbid.toString() < b.canonical.mbid.toString();
        });
        if (matches(options, row)) result.push_back(row);
    }
    std::sort(result.begin(), result.end(), [&](const GroupRow &a, const GroupRow &b) {
        return groupLess(a, b, options.sort);
    });
    return CatalogueResult<QVector<GroupRow>>::success(std::move(result));
}

QString cursorToken(const QString &fingerprint, const QString &revision,
                    std::uint64_t version, int offset)
{
    const QJsonObject data{
        {QStringLiteral("q"), fingerprint}, {QStringLiteral("r"), revision},
        {QStringLiteral("v"), QString::number(version)}, {QStringLiteral("n"), offset}
    };
    const QByteArray encoded = QJsonDocument(data).toJson(QJsonDocument::Compact)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QString value = QString::fromLatin1(encoded);
    return QStringLiteral("ac1.") + value + QLatin1Char('.')
        + digest(value).left(16);
}

std::optional<int> readCursor(
    const PageCursor &cursor, const QString &fingerprint,
    const QString &revision, std::uint64_t version, int itemCount)
{
    const QString token = cursor.token();
    const QStringList parts = token.split(QLatin1Char('.'));
    if (token.size() > 1024 || parts.size() != 3
        || parts.at(0) != QStringLiteral("ac1")
        || parts.at(2) != digest(parts.at(1)).left(16)) return std::nullopt;
    const QByteArray bytes = QByteArray::fromBase64(
        parts.at(1).toLatin1(), QByteArray::Base64UrlEncoding);
    if (QString::fromLatin1(bytes.toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))
        != parts.at(1)) return std::nullopt;
    QJsonParseError err;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !json.isObject()) return std::nullopt;
    const QJsonObject payload = json.object();
    if (payload.value(QStringLiteral("q")).toString() != fingerprint
        || payload.value(QStringLiteral("r")).toString() != revision
        || payload.value(QStringLiteral("v")).toString() != QString::number(version))
        return std::nullopt;
    const QJsonValue n = payload.value(QStringLiteral("n"));
    if (!n.isDouble()) return std::nullopt;
    const int offset = n.toInt(-1);
    if (offset <= 0 || offset >= itemCount) return std::nullopt;
    return offset;
}

ArtistResolution errorArtist(ArtistState code, std::uint64_t version, QString detail = {})
{
    ArtistResolution result;
    result.state = code;
    result.version = version;
    result.detail = std::move(detail);
    return result;
}

bool sameArtist(const Artist &a, const Artist &b)
{
    return a.mbid == b.mbid && a.name == b.name
        && a.disambiguation == b.disambiguation;
}

bool exactMatch(const Artist &artist, const QString &name)
{
    if (artist.name == name) return true;
    for (const auto &alias : artist.aliases)
        if (alias.name == name) return true;
    return false;
}

} // namespace

struct ArtistCatalogueQuery::State {
    explicit State(IArtistCatalogueProvider &p) : provider(p) {}
    IArtistCatalogueProvider &provider;
    std::uint64_t generation = 0;
    std::uint64_t nextTicket = 1;
    std::set<std::uint64_t> pending;
    std::optional<std::uint64_t> lastPage;
    std::optional<ArtistMbid> artist;
    QString revision;
    DiscographyOptions options;
};

ArtistSelector ArtistSelector::byId(ArtistMbid id)
{
    ArtistSelector value;
    value.mbid = std::move(id);
    return value;
}

ArtistSelector ArtistSelector::byExactName(QString text)
{
    ArtistSelector value;
    value.exactName = std::move(text);
    return value;
}

ArtistCatalogueQuery::ArtistCatalogueQuery(IArtistCatalogueProvider &provider)
    : m_state(std::make_shared<State>(provider))
{
}

ArtistCatalogueQuery::~ArtistCatalogueQuery()
{
    cancel(); // callbacks keep only a weak state reference; no use-after-free
    m_state.reset();
}

void ArtistCatalogueQuery::cancel() noexcept
{
    const auto state = m_state;
    if (!state) return;
    ++state->generation;
    for (const std::uint64_t ticket : state->pending)
        state->provider.cancel(ticket);
    state->pending.clear();
    state->lastPage.reset();
    state->artist.reset();
    state->revision.clear();
}

std::uint64_t ArtistCatalogueQuery::version() const noexcept
{
    return m_state ? m_state->generation : 0;
}

std::uint64_t ArtistCatalogueQuery::begin(
    ArtistSelector selector, DiscographyOptions options,
    std::function<void(ArtistResolution)> completion)
{
    cancel();
    auto state = m_state;
    state->options = std::move(options);
    const auto generation = state->generation;
    const bool exactlyOneIdentity =
        selector.mbid.has_value() != !selector.exactName.isEmpty();
    const auto &filter = state->options.groups;
    if (!exactlyOneIdentity || (!selector.mbid && selector.exactName.trimmed().isEmpty())
        || (filter.firstReleaseYearFrom && filter.firstReleaseYearTo
            && *filter.firstReleaseYearFrom > *filter.firstReleaseYearTo)) {
        completion(errorArtist(ArtistState::InvalidQuery, generation));
        return generation;
    }
    const auto ticket = state->nextTicket++;
    state->pending.insert(ticket);
    std::weak_ptr<State> weak(state);
    state->provider.lookup(selector, ticket,
        [weak, generation, ticket, selector, completion = std::move(completion)]
        (ArtistProviderResult response) mutable {
        auto s = weak.lock();
        if (!s || s->generation != generation || s->pending.erase(ticket) == 0) return;
        if (!response.ok()) {
            const ArtistState kind = response.error.code == CatalogueErrorCode::NotFound
                ? ArtistState::NotFound : ArtistState::Unavailable;
            completion(errorArtist(kind, generation, response.error.detail));
            return;
        }
        const ArtistEnvelope &envelope = *response.value;
        if (envelope.corpusVersion.trimmed().isEmpty()) {
            completion(errorArtist(ArtistState::Unavailable, generation,
                                   QStringLiteral("missing corpus revision")));
            return;
        }
        if (envelope.redirectedTo) {
            if (!selector.mbid || !envelope.candidates.isEmpty()
                || *envelope.redirectedTo == *selector.mbid) {
                completion(errorArtist(ArtistState::Unavailable, generation,
                                       QStringLiteral("invalid provider redirect")));
                return;
            }
            ArtistResolution result = errorArtist(ArtistState::Redirected, generation);
            result.redirectedTo = envelope.redirectedTo;
            result.corpusVersion = envelope.corpusVersion;
            completion(result);
            return;
        }
        std::map<QString, ArtistRow> matches;
        for (const ArtistRow &candidate : envelope.candidates) {
            if (candidate.source && !validSource(*candidate.source)) {
                completion(errorArtist(ArtistState::Unavailable, generation,
                                       QStringLiteral("missing source provenance")));
                return;
            }
            if (selector.mbid && candidate.canonical.mbid != *selector.mbid) {
                completion(errorArtist(ArtistState::Unavailable, generation,
                                       QStringLiteral("ID lookup returned wrong artist")));
                return;
            }
            if (!selector.mbid && !exactMatch(candidate.canonical, selector.exactName))
                continue; // fuzzy provider matches must NEVER become exact identity
            const QString key = candidate.canonical.mbid.toString();
            const auto inserted = matches.emplace(key, candidate);
            if (!inserted.second && !sameArtist(inserted.first->second.canonical,
                                                candidate.canonical)) {
                completion(errorArtist(ArtistState::Unavailable, generation,
                                       QStringLiteral("conflicting canonical artist")));
                return;
            }
        }
        if (matches.empty()) {
            completion(errorArtist(ArtistState::NotFound, generation));
            return;
        }
        if (matches.size() > 1) {
            completion(errorArtist(ArtistState::Ambiguous, generation,
                                   QStringLiteral("exact name matches multiple MBIDs")));
            return;
        }
        const ArtistRow &match = matches.begin()->second;
        s->artist = match.canonical.mbid;
        s->revision = envelope.corpusVersion;
        ArtistResolution result = errorArtist(ArtistState::Found, generation);
        result.artist = match.canonical;
        result.source = match.source;
        result.corpusVersion = envelope.corpusVersion;
        completion(std::move(result));
    });
    return generation;
}

std::optional<std::uint64_t> ArtistCatalogueQuery::page(
    std::uint64_t generation, PageRequest request,
    std::function<void(DiscographyResult)> completion)
{
    auto state = m_state;
    if (generation != state->generation) return std::nullopt;
    if (!state->artist) {
        completion(DiscographyResult::failure(CatalogueErrorCode::InvalidQuery,
                                              QStringLiteral("artist"),
                                              QStringLiteral("resolve Artist Found first")));
        return std::nullopt;
    }

    if (request.limit < 1 || request.limit > 100) {
        completion(DiscographyResult::failure(CatalogueErrorCode::InvalidQuery,
                                              QStringLiteral("limit")));
        return std::nullopt;
    }
    if (state->lastPage) {
        state->provider.cancel(*state->lastPage);
        state->pending.erase(*state->lastPage);
    }
    const auto ticket = state->nextTicket++;
    state->pending.insert(ticket);
    state->lastPage = ticket;
    const ArtistMbid artist = *state->artist;
    const QString revision = state->revision;
    const DiscographyOptions options = state->options;
    std::weak_ptr<State> weak(state);
    state->provider.releaseGroups(artist, ticket,
        [weak, generation, ticket, artist, revision, options, request,
         completion = std::move(completion)](GroupsProviderResult response) mutable {
        const auto s = weak.lock();
        if (!s || s->generation != generation
            || s->pending.erase(ticket) == 0 || s->lastPage != ticket) return;
        s->lastPage.reset();
        if (!response.ok()) {
            completion(DiscographyResult::failure(response.error.code,
                                                  response.error.field, response.error.detail));
            return;
        }
        if (response.value->corpusVersion != revision) {
            completion(DiscographyResult::failure(CatalogueErrorCode::InvalidPage,
                        QStringLiteral("corpusVersion"), QStringLiteral("snapshot changed")));
            return;
        }
        auto normalized = normalizedGroups(artist, options, response.value->rows);
        if (!normalized.ok()) {
            completion(DiscographyResult::failure(normalized.error.code,
                                                  normalized.error.field,
                                                  normalized.error.detail));
            return;
        }
        const auto &rows = *normalized.value;
        const QString queryHash = fingerprint(artist, options);
        int offset = 0;
        if (request.cursor) {
            const auto parsed = readCursor(*request.cursor, queryHash,
                                           revision, generation, rows.size());
            if (!parsed) {
                completion(DiscographyResult::failure(CatalogueErrorCode::InvalidPage,
                                                      QStringLiteral("cursor")));
                return;
            }
            offset = *parsed;
        }
        DiscographyPage out;
        out.corpusVersion = revision;
        out.sessionVersion = generation;
        const int end = std::min(offset + request.limit, static_cast<int>(rows.size()));
        for (int i = offset; i < end; ++i) out.page.items.push_back(rows.at(i));
        if (end < rows.size())
            out.page.nextCursor = PageCursor(cursorToken(queryHash, revision, generation, end));
        completion(DiscographyResult::success(std::move(out)));
    });
    return ticket;
}

} // namespace vinyl::catalogue::artist_proof
