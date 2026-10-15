#include "ArtistMediaQuery.h"

#include <QByteArray>
#include <QUrl>
#include <algorithm>
#include <map>
#include <utility>

namespace vinyl::artist_media {
namespace {
using Kind = RelatedVideoRelationshipKind;
using EK = RelationshipEvidenceKind;
using ES = RelationshipEvidenceStrength;

ArtistMediaResult fail(quint64 ticket, QueryStatus status, QString reason) {
    ArtistMediaResult r;
    r.ticket = ticket;
    r.status = status;
    r.errorCode = std::move(reason);
    return r;
}

bool validThumbnail(const VideoThumbnailProvenance &p) {
    if (p.httpsLocator.isEmpty())
        return true; // Opaque provider image asset remains sufficient provenance.
    const QUrl url(p.httpsLocator);
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && !url.host().isEmpty() && url.userInfo().isEmpty()
        && !url.hasFragment() && !url.toString().contains(QChar(0));
}

bool validRow(const ProviderVideoRow &r) {
    return !r.title.trimmed().isEmpty() && r.title.size() <= 4096
        && r.channel.size() <= 4096
        && (!r.durationMs || *r.durationMs > 0)
        && (!r.validUntilEpochMs || *r.validUntilEpochMs > 0)
        && (!r.thumbnail || validThumbnail(*r.thumbnail));
}

bool sameThumbnail(const std::optional<VideoThumbnailProvenance> &a,
                   const std::optional<VideoThumbnailProvenance> &b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return a->imageProvider == b->imageProvider &&
           a->imageAsset == b->imageAsset &&
           a->httpsLocator == b->httpsLocator;
}

bool sameRow(const ProviderVideoRow &a, const ProviderVideoRow &b) {
    return a.title == b.title && a.channel == b.channel
        && a.durationMs == b.durationMs
        && a.validUntilEpochMs == b.validUntilEpochMs
        && sameThumbnail(a.thumbnail, b.thumbnail);
    // Discovery method/sourceClaim are non-authoritative, never dedup precedence.
}

QString itemKey(const ArtistMbid &artist, const ProviderVideoRow &row) {
    const QByteArray encoded = row.providerItem.value().toUtf8().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("am1|%1|%2|%3")
        .arg(artist.toString(), row.provider.value(), QString::fromLatin1(encoded));
}

bool validArtistOnlyEvidence(const QVector<RelationshipEvidence> &evidence) {
    bool strongRelation = false;
    for (const auto &ev : evidence) {
        if ((ev.kind == EK::Unknown && ev.strength != ES::Unknown) ||
            (ev.kind == EK::TextSearchSimilarity && ev.strength == ES::Strong) ||
            (ev.detail && ev.detail->trimmed().isEmpty()))
            return false;
        if (ev.strength == ES::Strong &&
            (ev.kind == EK::CanonicalMapping ||
             ev.kind == EK::ProviderRelationshipMetadata ||
             ev.kind == EK::ProviderOfficialDesignation ||
             ev.kind == EK::CuratedMapping))
            strongRelation = true;
    }
    return strongRelation;
}

ArtistMediaResult project(quint64 ticket, const ArtistMbid &expected,
                          const SourceBatch &batch,
                          const ArtistMediaMappingPort &mappings) {
    if (batch.artist != expected)
        return fail(ticket, QueryStatus::Invalid, QStringLiteral("artist_scope_mismatch"));
    if (batch.status == QueryStatus::Unavailable && batch.rows.isEmpty())
        return fail(ticket, QueryStatus::Unavailable, QStringLiteral("source_unavailable"));
    if (batch.status == QueryStatus::Invalid && batch.rows.isEmpty())
        return fail(ticket, QueryStatus::Invalid, QStringLiteral("source_invalid"));
    if (batch.status == QueryStatus::Empty && batch.rows.isEmpty())
        return fail(ticket, QueryStatus::Empty, QStringLiteral("no_provider_media"));
    if (batch.status != QueryStatus::Ready ||
        batch.sourceLabel.trimmed().isEmpty() ||
        batch.snapshotId.trimmed().isEmpty())
        return fail(ticket, QueryStatus::Invalid, QStringLiteral("invalid_source_envelope"));

    // Exact provider ID + opaque item ID. The key's separator is prohibited by BA0.
    std::map<QString, ProviderVideoRow> unique;
    for (const auto &row : batch.rows) {
        if (!validRow(row))
            return fail(ticket, QueryStatus::Invalid, QStringLiteral("invalid_provider_row"));
        const QString key = row.provider.value() + QChar(0x1f) + row.providerItem.value();
        auto found = unique.find(key);
        if (found != unique.end()) {
            if (!sameRow(found->second, row))
                return fail(ticket, QueryStatus::Invalid, QStringLiteral("conflicting_provider_duplicate"));
            continue;
        }
        unique.emplace(key, row);
    }

    ArtistMediaResult result;
    result.ticket = ticket;
    for (const auto &entry : unique) {
        const auto &row = entry.second;
        const ProviderItemRef providerItem(row.provider, row.providerItem);
        const auto mapping = mappings.lookup(expected, providerItem);
        if (!mapping)
            continue; // Provider search alone is not canonical identity evidence.
        if (mapping->artist != expected || !(mapping->provider == row.provider) ||
            !(mapping->providerItem == row.providerItem))
            continue; // Scope confusion cannot acquire a relationship.
        if (mapping->mappingProvenance.trimmed().isEmpty())
            return fail(ticket, QueryStatus::Invalid, QStringLiteral("missing_mapping_provenance"));
        if (mapping->kind == Kind::OtherUnknown)
            continue;

        std::optional<RelatedVideoCandidate> bf2;
        bool directSwitch = false;
        if (mapping->recording) {
            RelatedVideoFacts facts;
            facts.title = row.title;
            if (!row.channel.trimmed().isEmpty()) facts.channel = row.channel;
            facts.durationMs = row.durationMs;
            auto candidate = RelatedVideoCandidate::create(
                *mapping->recording, row.provider, row.providerItem, mapping->kind,
                mapping->evidence, row.discovery, facts);
            if (!candidate.ok()) {
                if (candidate.error.code == RelatedVideoErrorCode::InvalidEvidence ||
                    candidate.error.code == RelatedVideoErrorCode::InvalidFacts)
                    return fail(ticket, QueryStatus::Invalid, QStringLiteral("invalid_relationship_evidence"));
                continue; // In particular: official relationship lacking official proof.
            }
            if (candidate.value->qualification() != RelatedVideoQualification::Qualified)
                continue; // Artist rail requires an exact, qualified relationship.
            directSwitch = candidate.value->eligibleForAudioVideoSwitch();
            bf2 = std::move(*candidate.value);
        } else {
            // Artist-only mapping is meaningful for explicit performances, never
            // for a recording-specific official video or direct Audio | Video.
            if (mapping->kind != Kind::LivePerformance &&
                mapping->kind != Kind::AlternatePerformance)
                continue;
            if (!validArtistOnlyEvidence(mapping->evidence))
                continue;
        }

        ArtistMediaItem item{
            expected, mapping->recording, row.provider, row.providerItem, mapping->kind,
            itemKey(expected, row), row.title, row.channel, batch.sourceLabel,
            batch.snapshotId, mapping->mappingProvenance, row.thumbnail,
            row.durationMs, row.validUntilEpochMs, std::move(bf2), directSwitch
        };
        if (item.kind == Kind::LivePerformance || item.kind == Kind::AlternatePerformance)
            result.live.append(std::move(item));
        else
            result.videos.append(std::move(item));
    }
    const auto byKey = [](const ArtistMediaItem &a, const ArtistMediaItem &b) {
        return a.stableKey < b.stableKey;
    };
    std::sort(result.videos.begin(), result.videos.end(), byKey);
    std::sort(result.live.begin(), result.live.end(), byKey);
    result.status = result.videos.isEmpty() && result.live.isEmpty()
        ? QueryStatus::Empty : QueryStatus::Ready;
    if (result.status == QueryStatus::Empty)
        result.errorCode = QStringLiteral("no_qualified_artist_media");
    return result;
}
} // namespace

struct ArtistMediaQuery::State {
    explicit State(const ArtistMediaMappingPort &port) : mappings(port) {}
    const ArtistMediaMappingPort &mappings;
    quint64 generation = 0;
    bool delivered = false;
    std::optional<ArtistMbid> activeArtist;
    std::optional<ArtistMediaResult> latest;
};

ArtistMediaQuery::ArtistMediaQuery(ArtistMediaSource &source,
                                   const ArtistMediaMappingPort &mappings)
    : m_state(std::make_shared<State>(mappings)), m_source(source) {}
ArtistMediaQuery::~ArtistMediaQuery() = default;

quint64 ArtistMediaQuery::start(const QString &artistMbid, ResultCallback done) {
    const quint64 ticket = ++m_state->generation;
    m_state->delivered = false;
    m_state->latest.reset();
    m_state->activeArtist.reset();
    const auto parsed = ArtistMbid::parse(artistMbid);
    if (!parsed.ok()) {
        const auto bad = fail(ticket, QueryStatus::Invalid, QStringLiteral("invalid_artist_mbid"));
        m_state->latest = bad;
        m_state->delivered = true;
        if (done) done(bad);
        return ticket;
    }
    const auto expected = *parsed.value;
    m_state->activeArtist = expected;
    const std::weak_ptr<State> weak = m_state;
    // One owned callback for both asynchronous completion and synchronous fetch failure.
    // Moving the callback only into complete would discard the exception notification.
    auto callback = std::make_shared<ResultCallback>(std::move(done));
    auto complete = [weak, ticket, expected, callback](SourceBatch batch) {
        const auto s = weak.lock();
        if (!s || s->generation != ticket || s->delivered) return;
        // A failed mapping port or provider parsing is an unavailable query,
        // not permission to return an unqualified official video.
        ArtistMediaResult answer;
        try {
            answer = project(ticket, expected, batch, s->mappings);
        } catch (...) {
            answer = fail(ticket, QueryStatus::Unavailable, QStringLiteral("mapping_port_failure"));
        }
        s->latest = answer;
        s->delivered = true; // Fence reentrant starts/duplicate callbacks.
        if (*callback) (*callback)(answer);
    };
    try {
        m_source.fetch(expected, complete);
    } catch (...) {
        const auto s = m_state;
        if (s->generation == ticket && !s->delivered) {
            auto unavailable = fail(ticket, QueryStatus::Unavailable,
                                    QStringLiteral("source_fetch_failure"));
            s->latest = unavailable;
            s->delivered = true;
            if (*callback) (*callback)(unavailable);
        }
    }
    return ticket;
}

void ArtistMediaQuery::cancel(quint64 ticket) {
    if (ticket != m_state->generation) return; // Old ticket cannot cancel a new query.
    ++m_state->generation;
    m_state->delivered = false;
    m_state->activeArtist.reset();
    m_state->latest.reset();
}

ActionResult ArtistMediaQuery::select(
    quint64 ticket, const QString &mediaKey, const QString &expectedArtistMbid,
    ActionChoice choice, const std::optional<QString> &expectedRecordingMbid,
    qint64 nowEpochMs) const {
    const auto s = m_state;
    if (ticket != s->generation) return {{}, ActionFailure::Stale};
    if (!s->latest || s->latest->ticket != ticket ||
        s->latest->status != QueryStatus::Ready)
        return {{}, ActionFailure::NotReady};
    if (nowEpochMs < 0)
        return {{}, ActionFailure::Invalid};
    const auto artist = ArtistMbid::parse(expectedArtistMbid);
    if (!artist.ok()) return {{}, ActionFailure::Invalid};
    if (!s->activeArtist || *s->activeArtist != *artist.value)
        return {{}, ActionFailure::ArtistMismatch};

    const auto match = [&mediaKey](const ArtistMediaItem &item) {
        return item.stableKey == mediaKey;
    };
    const auto &r = *s->latest;
    const auto vi = std::find_if(r.videos.begin(), r.videos.end(), match);
    const auto li = std::find_if(r.live.begin(), r.live.end(), match);
    const ArtistMediaItem *item = vi != r.videos.end() ? &*vi
                                : li != r.live.end() ? &*li : nullptr;
    if (!item) return {{}, ActionFailure::NotFound};
    if (item->validUntilEpochMs && nowEpochMs >= *item->validUntilEpochMs)
        return {{}, ActionFailure::Expired};

    std::optional<RecordingMbid> expectedRecording;
    if (expectedRecordingMbid) {
        const auto parsed = RecordingMbid::parse(*expectedRecordingMbid);
        if (!parsed.ok()) return {{}, ActionFailure::Invalid};
        expectedRecording = *parsed.value;
        if (!item->recording || *item->recording != *expectedRecording)
            return {{}, ActionFailure::RecordingMismatch};
    }
    if (choice == ActionChoice::RequestAudioVideoSwitch) {
        if (!expectedRecording) return {{}, ActionFailure::Invalid};
        if (!item->directSwitchEligible || !item->bf2Candidate ||
            !item->bf2Candidate->eligibleForAudioVideoSwitch())
            return {{}, ActionFailure::NotEligible};
    }
    return {MediaActionIntent{choice, item->artist, item->recording,
                             item->provider, item->providerItem,
                             item->stableKey,
                             choice == ActionChoice::RequestAudioVideoSwitch
                                 ? item->bf2Candidate
                                 : std::nullopt},
            ActionFailure::None};
}

const char *queryStatusName(QueryStatus s) {
    switch (s) {
    case QueryStatus::Ready: return "ready";
    case QueryStatus::Empty: return "empty";
    case QueryStatus::Unavailable: return "unavailable";
    case QueryStatus::Invalid: return "invalid";
    }
    return "invalid";
}
const char *actionFailureName(ActionFailure s) {
    switch (s) {
    case ActionFailure::None: return "none";
    case ActionFailure::Stale: return "stale";
    case ActionFailure::NotReady: return "not_ready";
    case ActionFailure::NotFound: return "not_found";
    case ActionFailure::Invalid: return "invalid";
    case ActionFailure::ArtistMismatch: return "artist_mismatch";
    case ActionFailure::RecordingMismatch: return "recording_mismatch";
    case ActionFailure::NotEligible: return "not_eligible";
    case ActionFailure::Expired: return "expired";
    }
    return "invalid";
}
} // namespace vinyl::artist_media
