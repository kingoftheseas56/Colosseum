#pragma once

#include "../ba0-domain-prototype/VinylDomainTypes.h"

#include <QDateTime>
#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl::discovery {

enum class DiscoveryKind {
    Popularity,
    TrendingCurrentInterest,
    NewReleases,
    Editorial,
    Regional,
    ChartAggregate,
    Other,
};

QString discoveryKindName(DiscoveryKind kind);

enum class DiscoveryFreshness {
    Fresh,
    Stale,
};

QString discoveryFreshnessName(DiscoveryFreshness freshness);

enum class DiscoveryErrorCode {
    None,
    Unavailable,
    Stale,
    Invalid,
};

struct DiscoveryError final {
    DiscoveryErrorCode code = DiscoveryErrorCode::None;
    QString field;
    QString detail;
};

QString discoveryErrorCodeName(DiscoveryErrorCode code);

template <typename T>
struct DiscoveryResult final {
    std::optional<T> value;
    DiscoveryError error;

    bool ok() const noexcept { return value.has_value(); }

    static DiscoveryResult success(T value)
    {
        DiscoveryResult out;
        out.value = std::move(value);
        return out;
    }

    static DiscoveryResult failure(DiscoveryErrorCode code,
                                   QString field = QString(),
                                   QString detail = QString())
    {
        DiscoveryResult out;
        out.error = DiscoveryError{
            code,
            std::move(field),
            std::move(detail),
        };
        return out;
    }
};

struct DiscoverySnapshot final {
    QString token;
    QDateTime capturedAtUtc;
    DiscoveryFreshness freshness = DiscoveryFreshness::Fresh;
};

struct DiscoveryRegionContext final {
    QString scheme;
    QString code;
    QString label;
};

struct DiscoveryProvenance final {
    ProviderId providerId;
    QString evidenceId;
    QString detail;
};

struct DiscoveryItem final {
    CanonicalEntityRef entity;
    std::optional<int> sourceRank;
    std::optional<double> sourceScore;
    DiscoveryProvenance provenance;
};

struct DiscoveryCollectionSpec final {
    ProviderId sourceProvider;
    DiscoveryKind kind = DiscoveryKind::Other;
    DiscoverySnapshot snapshot;
    std::optional<ProviderItemRef> editorialCollectionRef;
    std::optional<DiscoveryRegionContext> region;
    QVector<DiscoveryItem> items;
};

class DiscoveryCollection final
{
public:
    static DiscoveryResult<DiscoveryCollection> create(
        DiscoveryCollectionSpec spec);

    const ProviderId &sourceProvider() const noexcept
    {
        return m_sourceProvider;
    }

    DiscoveryKind kind() const noexcept
    {
        return m_kind;
    }

    const DiscoverySnapshot &snapshot() const noexcept
    {
        return m_snapshot;
    }

    const std::optional<ProviderItemRef> &editorialCollectionRef() const noexcept
    {
        return m_editorialCollectionRef;
    }

    const std::optional<DiscoveryRegionContext> &region() const noexcept
    {
        return m_region;
    }

    const QVector<DiscoveryItem> &items() const noexcept
    {
        return m_items;
    }

    QString deterministicOrderKey() const;

private:
    DiscoveryCollection(
        ProviderId sourceProvider,
        DiscoveryKind kind,
        DiscoverySnapshot snapshot,
        std::optional<ProviderItemRef> editorialCollectionRef,
        std::optional<DiscoveryRegionContext> region,
        QVector<DiscoveryItem> items);

    ProviderId m_sourceProvider;
    DiscoveryKind m_kind = DiscoveryKind::Other;
    DiscoverySnapshot m_snapshot;
    std::optional<ProviderItemRef> m_editorialCollectionRef;
    std::optional<DiscoveryRegionContext> m_region;
    QVector<DiscoveryItem> m_items;
};

QVector<DiscoveryItem> orderedDiscoveryItems(QVector<DiscoveryItem> items);

QVector<DiscoveryCollection> orderedDiscoveryCollections(
    QVector<DiscoveryCollection> collections);

DiscoveryResult<DiscoveryCollection> requireFresh(
    const DiscoveryCollection &collection);

} // namespace vinyl::discovery
