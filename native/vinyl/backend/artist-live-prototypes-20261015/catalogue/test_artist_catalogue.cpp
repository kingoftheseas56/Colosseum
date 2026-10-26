#include "ArtistCatalogueQuery.h"

#include <QCoreApplication>
#include <iostream>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>

using namespace vinyl;
using namespace vinyl::catalogue;
using namespace vinyl::catalogue::artist_proof;

namespace {
int checks = 0;
void expect(bool ok, const char *message)
{
    ++checks;
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(2);
    }
    std::cout << "PASS: " << message << '\n';
}

template<class Id> Id must(const char *text)
{
    const auto parsed = Id::parse(QString::fromLatin1(text));
    if (!parsed.ok()) std::abort();
    return *parsed.value;
}

SourceRef source(const char *item, const char *provenance)
{
    const auto id = ProviderId::parse(QStringLiteral("youtube-music"));
    const auto sourceId = ProviderItemId::parse(QString::fromLatin1(item));
    if (!id.ok() || !sourceId.ok()) std::abort();
    return {ProviderItemRef(*id.value, *sourceId.value),
            QString::fromLatin1(provenance)};
}

// Explicit fake-provider contract. Callbacks stay queued even when cancelled,
// to prove the consumer generation/ticket fence rather than fake cooperation.
class QueuedProvider final : public IArtistCatalogueProvider {
public:
    QString corpus = QStringLiteral("mb-dump:2026-10");
    bool online = true;
    QVector<ArtistRow> artists;
    QVector<GroupRow> groups;
    QVector<std::pair<ArtistMbid, ArtistMbid>> redirects;
    std::map<std::uint64_t, std::function<void()>> outstanding;
    std::set<std::uint64_t> cancelled;

    void lookup(const ArtistSelector &selector, std::uint64_t ticket,
                std::function<void(ArtistProviderResult)> completion) override
    {
        outstanding.emplace(ticket, [this, selector, done = std::move(completion)]() mutable {
            if (!online) {
                done(ArtistProviderResult::failure(CatalogueErrorCode::Unavailable));
                return;
            }
            ArtistEnvelope value;
            value.corpusVersion = corpus;
            if (!selector.mbid) {
                value.candidates = artists; // simulate fuzzy search; consumer must exact-filter
            } else {
                for (const auto &redirect : redirects)
                    if (redirect.first == *selector.mbid)
                        value.redirectedTo = redirect.second;
                if (!value.redirectedTo) {
                    for (const auto &artist : artists)
                        if (artist.canonical.mbid == *selector.mbid)
                            value.candidates.push_back(artist);
                }
            }
            done(ArtistProviderResult::success(std::move(value)));
        });
    }

    void releaseGroups(const ArtistMbid &, std::uint64_t ticket,
                       std::function<void(GroupsProviderResult)> completion) override
    {
        outstanding.emplace(ticket, [this, done = std::move(completion)]() mutable {
            if (!online) {
                done(GroupsProviderResult::failure(CatalogueErrorCode::Unavailable));
                return;
            }
            done(GroupsProviderResult::success(GroupsEnvelope{corpus, groups}));
        });
    }

    void cancel(std::uint64_t ticket) noexcept override { cancelled.insert(ticket); }

    void deliver(std::uint64_t ticket)
    {
        auto it = outstanding.find(ticket);
        if (it == outstanding.end()) std::abort();
        auto action = std::move(it->second);
        outstanding.erase(it);
        action();
    }

    std::uint64_t latest() const
    {
        if (outstanding.empty()) std::abort();
        return outstanding.rbegin()->first;
    }
};

struct Fixture {
    ArtistMbid a = must<ArtistMbid>("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    ArtistMbid b = must<ArtistMbid>("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    ArtistMbid old = must<ArtistMbid>("cccccccc-cccc-4ccc-8ccc-cccccccccccc");
    ArtistMbid missing = must<ArtistMbid>("dddddddd-dddd-4ddd-8ddd-dddddddddddd");
    ReleaseGroupMbid g1 = must<ReleaseGroupMbid>("11111111-1111-4111-8111-111111111111");
    ReleaseGroupMbid g2 = must<ReleaseGroupMbid>("22222222-2222-4222-8222-222222222222");
    ReleaseGroupMbid g3 = must<ReleaseGroupMbid>("33333333-3333-4333-8333-333333333333");
    ReleaseGroupMbid g4 = must<ReleaseGroupMbid>("44444444-4444-4444-8444-444444444444");
    ReleaseMbid r1 = must<ReleaseMbid>("51111111-1111-4111-8111-111111111111");
    ReleaseMbid r1b = must<ReleaseMbid>("51111112-1111-4111-8111-111111111111");
    ReleaseMbid r2 = must<ReleaseMbid>("52222222-2222-4222-8222-222222222222");
    ReleaseMbid r3 = must<ReleaseMbid>("53333333-3333-4333-8333-333333333333");
    ReleaseMbid r4 = must<ReleaseMbid>("54444444-4444-4444-8444-444444444444");
    RecordingMbid recording = must<RecordingMbid>("61111111-1111-4111-8111-111111111111");
    QVector<GroupRow> canonicalRows;

    static GroupRow group(ReleaseGroupMbid groupId, ReleaseMbid edition,
                          ArtistMbid artist, const char *title, int year,
                          ReleaseGroupPrimaryType type, const char *status,
                          const char *browseId, QStringList secondary = {})
    {
        const ArtistCredit credit{artist, QStringLiteral("Phoenix")};
        ReleaseGroup group{groupId, QString::fromLatin1(title), {}, {credit},
                           type, secondary, year};
        Release release{edition, groupId, group.title, {credit},
                        QString::fromLatin1(status), QStringLiteral("US"),
                        QDate(year, 1, 1), QString()};
        const SourceRef mapped = source(browseId, "fake:musicbrainz-mapped-browse-id");
        return {group, {release}, {mapped}, {{credit, source("UC_artist_A", "artist:source-id")}}, {}};
    }

    void setup(QueuedProvider &provider)
    {
        provider.artists = {
            {Artist{a, QStringLiteral("Phoenix"), QStringLiteral("Phoenix"),
                QStringLiteral("Band A"), {Alias{QStringLiteral("Stage One"), {}, {}, {}, false}}},
                source("UC_artist_A", "fake:artist-browse")},
            {Artist{b, QStringLiteral("Phoenix"), QStringLiteral("Phoenix"),
                QStringLiteral("Band B"), {}},
                source("UC_artist_B", "fake:artist-browse")}
        };
        provider.redirects.push_back({old, a});
        GroupRow first = group(g1, r1, a, "Alpha", 2001,
                               ReleaseGroupPrimaryType::Album, "Official", "MPRE_alpha");
        const SourcedCredit credit{{a, QStringLiteral("Phoenix")},
                                  source("UC_artist_A", "artist:recording-credit")};
        first.recordings.push_back(SourcedRecording{
            Recording{recording, QStringLiteral("Song One"), {credit.canonical}, 210000, {}},
            source("vid_ABCDE123", "recording:youtube-video"), {credit}
        });
        GroupRow alternate = group(g1, r1b, a, "Alpha", 2001,
                                   ReleaseGroupPrimaryType::Album, "Promotion", "MPRE_alt");
        alternate.recordings.push_back(SourcedRecording{
            first.recordings.at(0).canonical,
            source("vid_ALTERNATE", "recording:alternative-provider-mapping"), {credit}
        });
        canonicalRows = {
            group(g4, r4, a, "Delta", 2005,
                  ReleaseGroupPrimaryType::Broadcast, "Official", "MPRE_delta"),
            first,
            group(g3, r3, a, "Beta Live", 2002,
                  ReleaseGroupPrimaryType::EP, "Bootleg", "MPRE_beta", {"Live"}),
            group(g2, r2, a, "Cobalt", 2005,
                  ReleaseGroupPrimaryType::Single, "Official", "MPRE_cobalt"),
            alternate
        };
        provider.groups = canonicalRows;
    }
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Fixture f;
    QueuedProvider provider;
    f.setup(provider);
    ArtistCatalogueQuery query(provider);
    std::optional<ArtistResolution> resolved;
    auto begin = [&](ArtistSelector selector, DiscographyOptions options = {}) {
        resolved.reset();
        const auto version = query.begin(std::move(selector), std::move(options),
            [&](ArtistResolution response) { resolved = std::move(response); });
        if (!resolved) provider.deliver(provider.latest());
        expect(resolved.has_value(), "provider artist callback delivered");
        expect(resolved->version == version, "resolution carries exact session version");
        return version;
    };
    auto page = [&](std::uint64_t version, PageRequest request) {
        std::optional<DiscographyResult> result;
        const auto ticket = query.page(version, std::move(request),
            [&](DiscographyResult response) { result = std::move(response); });
        if (ticket) provider.deliver(*ticket);
        expect(result.has_value(), "provider page callback delivered");
        return *result;
    };

    auto v = begin(ArtistSelector::byExactName(QStringLiteral("Phoenix")));
    expect(resolved->state == ArtistState::Ambiguous && !resolved->artist,
           "same displayed artist name with two MBIDs fails closed as Ambiguous");
    const auto unready = page(v, PageRequest{2, std::nullopt});
    expect(!unready.ok() && unready.error.code == CatalogueErrorCode::InvalidQuery,
           "discography requires a successfully resolved canonical artist");

    v = begin(ArtistSelector::byExactName(QStringLiteral("Stage One")));
    expect(resolved->state == ArtistState::Found && resolved->artist->mbid == f.a,
           "exact alias lookup resolves canonical ArtistMbid, not alias identity");
    expect(resolved->source && resolved->source->item.itemId().value()
           == QStringLiteral("UC_artist_A"), "artist browse ID remains source-qualified");
    v = begin(ArtistSelector::byExactName(QStringLiteral("Stage")));
    expect(resolved->state == ArtistState::NotFound, "prefix/fuzzy alias must not resolve");
    v = begin(ArtistSelector::byId(f.missing));
    expect(resolved->state == ArtistState::NotFound, "valid unknown MBID returns NotFound");
    v = begin(ArtistSelector::byId(f.old));
    expect(resolved->state == ArtistState::Redirected
           && resolved->redirectedTo == f.a, "merge returns typed Redirected, no guessed follow");
    provider.online = false;
    v = begin(ArtistSelector::byId(f.a));
    expect(resolved->state == ArtistState::Unavailable, "offline artist lookup is Unavailable");
    provider.online = true;

    v = begin(ArtistSelector::byId(f.a));
    expect(resolved->state == ArtistState::Found && resolved->artist->mbid == f.a,
           "exact ID lookup selects artist A despite name collision");
    const auto first = page(v, PageRequest{2, std::nullopt});
    expect(first.ok() && first.value->page.items.size() == 2
           && first.value->page.nextCursor.has_value(),
           "first page returns exactly two release groups and continuation");
    expect(first.value->page.items.at(0).canonical.mbid == f.g2
           && first.value->page.items.at(1).canonical.mbid == f.g4,
           "newest ordering resolves same-year title ties deterministically");
    const PageCursor continuation = *first.value->page.nextCursor;
    const auto second = page(v, PageRequest{2, continuation});
    expect(second.ok() && second.value->page.items.size() == 2
           && !second.value->page.nextCursor,
           "second page finishes without synthetic cursor");
    expect(second.value->page.items.at(0).canonical.mbid == f.g3
           && second.value->page.items.at(1).canonical.mbid == f.g1,
           "deduplication preserves group ordering and page boundaries");
    expect(second.value->page.items.at(1).editions.size() == 2,
           "two release editions/statuses collapse into one release group");
    expect(second.value->page.items.at(1).sources.size() == 2,
           "duplicate group retains both source mappings with provenance");

    const auto &song = second.value->page.items.at(1).recordings.at(0);
    expect(song.canonical.mbid == f.recording && song.canonical.artistCredit.at(0).artistMbid == f.a,
           "recording and artist credits preserve distinct canonical MusicBrainz MBIDs");
    expect(song.source && song.source->item.providerId().value() == QStringLiteral("youtube-music")
           && song.source->item.itemId().value() == QStringLiteral("vid_ABCDE123")
           && song.credits.at(0).source->item.itemId().value()
           == QStringLiteral("UC_artist_A"), "recording/artist browse IDs carry explicit source provenance");
    expect(second.value->page.items.at(1).recordings.size() == 1
           && song.additionalSources.size() == 1
           && song.additionalSources.at(0).item.itemId().value()
           == QStringLiteral("vid_ALTERNATE"),
           "duplicate group merges recording once while retaining alternative source mapping");

    std::reverse(provider.groups.begin(), provider.groups.end());
    const auto repeated = page(v, PageRequest{2, std::nullopt});
    expect(repeated.ok() && repeated.value->page.nextCursor == first.value->page.nextCursor,
           "same immutable revision produces identical cursor despite provider row reordering");
    const auto invalid = page(v, PageRequest{2, PageCursor(QStringLiteral("garbage"))});
    expect(!invalid.ok() && invalid.error.code == CatalogueErrorCode::InvalidPage,
           "malformed cursor returns typed InvalidPage");
    const auto invalidLimit = page(v, PageRequest{101, std::nullopt});
    expect(!invalidLimit.ok() && invalidLimit.error.code == CatalogueErrorCode::InvalidQuery,
           "page size greater than BG1 maximum is rejected");

    DiscographyOptions promotion;
    promotion.statuses = {ReleaseStatus::Promotion};
    v = begin(ArtistSelector::byId(f.a), promotion);
    const auto promos = page(v, PageRequest{5, std::nullopt});
    expect(promos.ok() && promos.value->page.items.size() == 1
           && promos.value->page.items.at(0).canonical.mbid == f.g1,
           "typed promotion status filter applies across editions, returns one group");
    const auto wrongQueryCursor = page(v, PageRequest{2, continuation});
    expect(!wrongQueryCursor.ok() && wrongQueryCursor.error.code == CatalogueErrorCode::InvalidPage,
           "cursor cannot cross query, session, or version identity");

    DiscographyOptions ep;
    ep.groups.primaryTypes = {ReleaseGroupPrimaryType::EP};
    ep.groups.secondaryTypes = {QStringLiteral("Live")};
    ep.statuses = {ReleaseStatus::Bootleg};
    ep.sort = DiscographySort::OldestFirst;
    v = begin(ArtistSelector::byId(f.a), ep);
    const auto eps = page(v, PageRequest{10, std::nullopt});
    expect(eps.ok() && eps.value->page.items.size() == 1
           && eps.value->page.items.at(0).canonical.mbid == f.g3,
           "primary, secondary and status filters compose by AND");

    DiscographyOptions bad;
    bad.groups.firstReleaseYearFrom = 2020;
    bad.groups.firstReleaseYearTo = 2000;
    v = begin(ArtistSelector::byId(f.a), bad);
    expect(resolved->state == ArtistState::InvalidQuery,
           "reversed first-release-year range fails before contacting provider");

    int staleCalls = 0;
    const auto staleVersion = query.begin(ArtistSelector::byId(f.a), {},
        [&](ArtistResolution) { ++staleCalls; });
    const auto staleTicket = provider.latest();
    const auto currentVersion = query.begin(ArtistSelector::byId(f.b), {},
        [&](ArtistResolution) { ++staleCalls; });
    const auto currentTicket = provider.latest();
    expect(currentVersion != staleVersion && provider.cancelled.count(staleTicket),
           "new session explicitly cancels previous request");
    provider.deliver(staleTicket);
    expect(staleCalls == 0, "out-of-order stale artist callback cannot mutate new view");
    provider.deliver(currentTicket);
    expect(staleCalls == 1, "current artist callback is accepted");
    expect(!query.page(staleVersion, PageRequest{}, [&](DiscographyResult) { ++staleCalls; }),
           "stale page session is rejected without delivering a callback");

    v = begin(ArtistSelector::byId(f.a));
    int cancelledPages = 0;
    const auto cancelTicket = query.page(v, PageRequest{2, std::nullopt},
        [&](DiscographyResult) { ++cancelledPages; });
    query.cancel();
    expect(cancelTicket && provider.cancelled.count(*cancelTicket),
           "cancellation forwards outstanding native ticket to provider");
    provider.deliver(*cancelTicket);
    expect(cancelledPages == 0, "late callback after cancellation is suppressed");

    v = begin(ArtistSelector::byId(f.a));
    int deliveredPages = 0;
    const auto oldPage = query.page(v, PageRequest{1, std::nullopt},
        [&](DiscographyResult) { ++deliveredPages; });
    const auto latestPage = query.page(v, PageRequest{2, std::nullopt},
        [&](DiscographyResult) { ++deliveredPages; });
    provider.deliver(*oldPage);
    provider.deliver(*latestPage);
    expect(deliveredPages == 1 && provider.cancelled.count(*oldPage),
           "latest-page wins; superseded page callback is fenced");

    v = begin(ArtistSelector::byId(f.a));
    provider.corpus = QStringLiteral("mb-dump:changed");
    const auto changed = page(v, PageRequest{2, std::nullopt});
    expect(!changed.ok() && changed.error.code == CatalogueErrorCode::InvalidPage,
           "corpus revision mismatch fails instead of mixing snapshots");
    provider.corpus = QStringLiteral("mb-dump:2026-10");

    v = begin(ArtistSelector::byId(f.a));
    provider.online = false;
    const auto unavailable = page(v, PageRequest{2, std::nullopt});
    expect(!unavailable.ok() && unavailable.error.code == CatalogueErrorCode::Unavailable,
           "offline discography is Unavailable, never an empty NotFound page");
    provider.online = true;

    auto contaminated = f.canonicalRows;
    contaminated[0].editions[0].releaseGroupMbid = f.g1;
    provider.groups = contaminated;
    const auto poisoned = page(v, PageRequest{5, std::nullopt});
    expect(!poisoned.ok() && poisoned.error.code == CatalogueErrorCode::Unavailable,
           "mismatched edition-to-release-group identity fails closed");
    provider.groups = f.canonicalRows;
    provider.groups[0].canonical.title = QStringLiteral("Different");
    provider.groups.push_back(f.canonicalRows.at(0));
    const auto conflict = page(v, PageRequest{5, std::nullopt});
    expect(!conflict.ok() && conflict.error.code == CatalogueErrorCode::Unavailable,
           "conflicting metadata for duplicate ReleaseGroupMbid fails closed");
    provider.groups = f.canonicalRows;

    // One Recording can occur twice in one edition and once on another disc/edition.
    const auto rt1 = must<ReleaseTrackMbid>("71111111-1111-4111-8111-111111111111");
    const auto rt2 = must<ReleaseTrackMbid>("72222222-2222-4222-8222-222222222222");
    const auto c1 = *ReleaseTrackCoordinate::create(f.g1, f.r1, rt1, f.recording, 1, 1).value;
    const auto c2 = *ReleaseTrackCoordinate::create(f.g1, f.r1, rt2, f.recording, 2, 1).value;
    const auto c3 = *ReleaseTrackCoordinate::create(f.g1, f.r1b, std::nullopt, f.recording, 1, 2).value;
    auto occurrenceRow = f.canonicalRows.at(1);
    occurrenceRow.editions.push_back(f.canonicalRows.last().editions.first());
    occurrenceRow.occurrences = {{c2, std::nullopt}, {c1, source("file-a", "track-coordinate")},
                                 {c3, std::nullopt}, {c1, source("file-a", "track-coordinate")}};
    provider.groups = {occurrenceRow};
    const auto occurrencePage = page(v, PageRequest{5, std::nullopt});
    expect(occurrencePage.ok(), "typed edition occurrences are accepted");
    const auto &occurrences = occurrencePage.value->page.items.first().occurrences;
    expect(occurrences.size() == 3, "only exact duplicate occurrence evidence is deduplicated");
    expect(occurrences.at(0).coordinate.releaseTrackMbid() == std::optional<ReleaseTrackMbid>{rt1}
           && occurrences.at(1).coordinate.releaseTrackMbid() == std::optional<ReleaseTrackMbid>{rt2},
           "distinct ReleaseTrackMBIDs and medium coordinates survive repeated RecordingMBID");
    expect(occurrences.at(2).coordinate.releaseMbid() == f.r1b
           && occurrences.at(2).coordinate.trackPosition() == 2,
           "another edition occurrence retains its own release and track coordinate");
    // Conflicting identity at one physical release coordinate fails closed.
    const auto otherRecording = must<RecordingMbid>("62222222-2222-4222-8222-222222222222");
    provider.groups[0].occurrences.push_back({
        *ReleaseTrackCoordinate::create(f.g1, f.r1, rt1, otherRecording, 1, 1).value, std::nullopt});
    expect(!page(v, PageRequest{5, std::nullopt}).ok(), "conflicting recording at one release coordinate is rejected");
    provider.groups = {occurrenceRow};
    provider.groups[0].occurrences.push_back({
        *ReleaseTrackCoordinate::create(f.g1, f.r1, rt1, f.recording, 1, 3).value, std::nullopt});
    expect(!page(v, PageRequest{5, std::nullopt}).ok(), "one ReleaseTrackMBID cannot name two coordinates");
    provider.groups = {occurrenceRow};
    std::reverse(provider.groups[0].occurrences.begin(), provider.groups[0].occurrences.end());
    const auto reversedOccurrences = page(v, PageRequest{5, std::nullopt});
    expect(reversedOccurrences.ok() && reversedOccurrences.value->page.items.first().occurrences.size() == 3
           && reversedOccurrences.value->page.items.first().occurrences.first().coordinate.mediumPosition() == 1,
           "occurrence normalization is independent of provider order");
    provider.groups = {occurrenceRow};
    provider.groups[0].occurrences.push_back({
        *ReleaseTrackCoordinate::create(f.g1, f.r2, std::nullopt, f.recording, 1, 1).value, std::nullopt});
    expect(!page(v, PageRequest{5, std::nullopt}).ok(), "occurrence cannot claim an edition absent from its release group");
    provider.groups = f.canonicalRows;

    int afterDestroy = 0;
    std::uint64_t danglingTicket = 0;
    {
        ArtistCatalogueQuery shortLived(provider);
        shortLived.begin(ArtistSelector::byId(f.a), {},
            [&](ArtistResolution) { ++afterDestroy; });
        danglingTicket = provider.latest();
    }
    provider.deliver(danglingTicket);
    expect(afterDestroy == 0 && provider.cancelled.count(danglingTicket),
           "late provider callback after query destruction is safe and silent");

    std::cout << "RESULT PASS checks=" << checks
                  << " provider=queued-in-process-fake transport=none\n";
    return 0;
}
