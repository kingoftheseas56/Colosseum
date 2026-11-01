#include "DiscoveryEnrichment.h"

#include <QChar>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace vinyl::discovery {
namespace {

bool containsControlCharacter(const QString &text)
{
    for (const QChar ch : text) {
        const auto value = ch.unicode();
        if (value < 0x20 || value == 0x7f) {
            return true;
        }
    }
    return false;
}

bool isSafeToken(const QString &text, qsizetype maximumLength)
{
    return !text.isEmpty()
        && text.size() <= maximumLength
        && text.trimmed() == text
        && !containsControlCharacter(text);
}

QString entityOrderKey(const CanonicalEntityRef &entity)
{
    return QStringLiteral("%1:%2")
        .arg(static_cast<int>(entity.kind()), 2, 10, QLatin1Char('0'))
        .arg(entity.mbidString());
}

QString scoreOrderKey(const std::optional<double> &score)
{
    if (!score.has_value()) {
        return QStringLiteral("-");
    }
    return QString::number(*score, 'g', 17);
}

bool discoveryItemLess(const DiscoveryItem &left,
                       const DiscoveryItem &right)
{
    if (left.sourceRank.has_value() != right.sourceRank.has_value()) {
        return left.sourceRank.has_value();
    }

    if (left.sourceRank.has_value()
        && *left.sourceRank != *right.sourceRank) {
        return *left.sourceRank < *right.sourceRank;
    }

    if (left.sourceScore.has_value() != right.sourceScore.has_value()) {
        return left.sourceScore.has_value();
    }

    if (left.sourceScore.has_value()
        && *left.sourceScore != *right.sourceScore) {
        return *left.sourceScore > *right.sourceScore;
    }

    const QString leftEntity = entityOrderKey(left.entity);
    const QString rightEntity = entityOrderKey(right.entity);
    if (leftEntity != rightEntity) {
        return leftEntity < rightEntity;
    }

    if (left.provenance.providerId.value()
        != right.provenance.providerId.value()) {
        return left.provenance.providerId.value()
            < right.provenance.providerId.value();
    }

    return left.provenance.evidenceId
        < right.provenance.evidenceId;
}

DiscoveryResult<DiscoveryCollection> invalid(
    QString field,
    QString detail)
{
    return DiscoveryResult<DiscoveryCollection>::failure(
        DiscoveryErrorCode::Invalid,
        std::move(field),
        std::move(detail));
}

} // namespace

QString discoveryKindName(DiscoveryKind kind)
{
    switch (kind) {
    case DiscoveryKind::Popularity:
        return QStringLiteral("popularity");
    case DiscoveryKind::TrendingCurrentInterest:
        return QStringLiteral("trending_current_interest");
    case DiscoveryKind::NewReleases:
        return QStringLiteral("new_releases");
    case DiscoveryKind::Editorial:
        return QStringLiteral("editorial");
    case DiscoveryKind::Regional:
        return QStringLiteral("regional");
    case DiscoveryKind::ChartAggregate:
        return QStringLiteral("chart_aggregate");
    case DiscoveryKind::Other:
        return QStringLiteral("other");
    }
    return QStringLiteral("unknown");
}

QString discoveryFreshnessName(DiscoveryFreshness freshness)
{
    switch (freshness) {
    case DiscoveryFreshness::Fresh:
        return QStringLiteral("fresh");
    case DiscoveryFreshness::Stale:
        return QStringLiteral("stale");
    }
    return QStringLiteral("unknown");
}

QString discoveryErrorCodeName(DiscoveryErrorCode code)
{
    switch (code) {
    case DiscoveryErrorCode::None:
        return QStringLiteral("none");
    case DiscoveryErrorCode::Unavailable:
        return QStringLiteral("unavailable");
    case DiscoveryErrorCode::Stale:
        return QStringLiteral("stale");
    case DiscoveryErrorCode::Invalid:
        return QStringLiteral("invalid");
    }
    return QStringLiteral("unknown");
}

QVector<DiscoveryItem> orderedDiscoveryItems(
    QVector<DiscoveryItem> items)
{
    std::sort(items.begin(), items.end(), discoveryItemLess);
    return items;
}

DiscoveryCollection::DiscoveryCollection(
    ProviderId sourceProvider,
    DiscoveryKind kind,
    DiscoverySnapshot snapshot,
    std::optional<ProviderItemRef> editorialCollectionRef,
    std::optional<DiscoveryRegionContext> region,
    QVector<DiscoveryItem> items)
    : m_sourceProvider(std::move(sourceProvider))
    , m_kind(kind)
    , m_snapshot(std::move(snapshot))
    , m_editorialCollectionRef(std::move(editorialCollectionRef))
    , m_region(std::move(region))
    , m_items(std::move(items))
{
}

DiscoveryResult<DiscoveryCollection> DiscoveryCollection::create(
    DiscoveryCollectionSpec spec)
{
    if (!isSafeToken(spec.snapshot.token, 256)) {
        return invalid(
            QStringLiteral("snapshot.token"),
            QStringLiteral(
                "Snapshot token must be a non-empty bounded opaque token."));
    }

    if (!spec.snapshot.capturedAtUtc.isValid()) {
        return invalid(
            QStringLiteral("snapshot.capturedAtUtc"),
            QStringLiteral("Snapshot timestamp must be valid."));
    }

    spec.snapshot.capturedAtUtc =
        spec.snapshot.capturedAtUtc.toUTC();

    if (spec.kind == DiscoveryKind::Editorial
        && !spec.editorialCollectionRef.has_value()) {
        return invalid(
            QStringLiteral("editorialCollectionRef"),
            QStringLiteral(
                "Editorial discovery requires provider-owned collection identity."));
    }

    if (spec.kind != DiscoveryKind::Editorial
        && spec.editorialCollectionRef.has_value()) {
        return invalid(
            QStringLiteral("editorialCollectionRef"),
            QStringLiteral(
                "Editorial collection identity is only valid for Editorial discovery."));
    }

    if (spec.editorialCollectionRef.has_value()
        && !(spec.editorialCollectionRef->providerId()
            == spec.sourceProvider)) {
        return invalid(
            QStringLiteral("editorialCollectionRef.providerId"),
            QStringLiteral(
                "Editorial collection identity must belong to the collection source provider."));
    }

    if (spec.kind == DiscoveryKind::Regional
        && !spec.region.has_value()) {
        return invalid(
            QStringLiteral("region"),
            QStringLiteral(
                "Regional discovery requires explicit regional context."));
    }

    if (spec.region.has_value()) {
        if (!isSafeToken(spec.region->scheme, 128)) {
            return invalid(
                QStringLiteral("region.scheme"),
                QStringLiteral(
                    "Region scheme must be a non-empty bounded token."));
        }
        if (!isSafeToken(spec.region->code, 128)) {
            return invalid(
                QStringLiteral("region.code"),
                QStringLiteral(
                    "Region code must be a non-empty bounded token."));
        }
        if (spec.region->label.size() > 256
            || containsControlCharacter(spec.region->label)) {
            return invalid(
                QStringLiteral("region.label"),
                QStringLiteral(
                    "Region label must be bounded and contain no control characters."));
        }
    }

    QSet<QString> seenEntities;
    for (const DiscoveryItem &item : spec.items) {
        if (item.sourceRank.has_value()
            && *item.sourceRank <= 0) {
            return invalid(
                QStringLiteral("items.sourceRank"),
                QStringLiteral(
                    "Source rank must be positive when supplied."));
        }

        if (item.sourceScore.has_value()
            && !std::isfinite(*item.sourceScore)) {
            return invalid(
                QStringLiteral("items.sourceScore"),
                QStringLiteral(
                    "Source score must be finite when supplied."));
        }

        if (!isSafeToken(item.provenance.evidenceId, 512)) {
            return invalid(
                QStringLiteral("items.provenance.evidenceId"),
                QStringLiteral(
                    "Evidence ID must be a non-empty bounded opaque token."));
        }

        if (item.provenance.detail.size() > 1024
            || containsControlCharacter(item.provenance.detail)) {
            return invalid(
                QStringLiteral("items.provenance.detail"),
                QStringLiteral(
                    "Provenance detail must be bounded and contain no control characters."));
        }

        const QString identityKey = entityOrderKey(item.entity);
        if (seenEntities.contains(identityKey)) {
            return invalid(
                QStringLiteral("items.entity"),
                QStringLiteral(
                    "A canonical entity may appear only once inside one discovery collection."));
        }
        seenEntities.insert(identityKey);
    }

    QVector<DiscoveryItem> items =
        orderedDiscoveryItems(std::move(spec.items));

    return DiscoveryResult<DiscoveryCollection>::success(
        DiscoveryCollection(
            std::move(spec.sourceProvider),
            spec.kind,
            std::move(spec.snapshot),
            std::move(spec.editorialCollectionRef),
            std::move(spec.region),
            std::move(items)));
}

QString DiscoveryCollection::deterministicOrderKey() const
{
    const QChar separator = QChar(u'\x001f');
    QStringList parts;
    parts.reserve(10 + m_items.size() * 5);

    parts.push_back(
        QString::number(static_cast<int>(m_kind)));
    parts.push_back(m_sourceProvider.value());

    if (m_editorialCollectionRef.has_value()) {
        parts.push_back(
            m_editorialCollectionRef->providerId().value());
        parts.push_back(
            m_editorialCollectionRef->itemId().value());
    } else {
        parts.push_back(QString());
        parts.push_back(QString());
    }

    if (m_region.has_value()) {
        parts.push_back(m_region->scheme);
        parts.push_back(m_region->code);
        parts.push_back(m_region->label);
    } else {
        parts.push_back(QString());
        parts.push_back(QString());
        parts.push_back(QString());
    }

    parts.push_back(m_snapshot.token);
    parts.push_back(
        m_snapshot.capturedAtUtc.toString(Qt::ISODateWithMs));
    parts.push_back(
        discoveryFreshnessName(m_snapshot.freshness));

    for (const DiscoveryItem &item : m_items) {
        parts.push_back(entityOrderKey(item.entity));
        parts.push_back(
            item.sourceRank.has_value()
                ? QString::number(*item.sourceRank)
                : QStringLiteral("-"));
        parts.push_back(scoreOrderKey(item.sourceScore));
        parts.push_back(
            item.provenance.providerId.value()
            + QLatin1Char(':')
            + item.provenance.evidenceId);
        parts.push_back(item.provenance.detail);
    }

    return parts.join(separator);
}

QVector<DiscoveryCollection> orderedDiscoveryCollections(
    QVector<DiscoveryCollection> collections)
{
    std::sort(
        collections.begin(),
        collections.end(),
        [](const DiscoveryCollection &left,
           const DiscoveryCollection &right) {
            return left.deterministicOrderKey()
                < right.deterministicOrderKey();
        });
    return collections;
}

DiscoveryResult<DiscoveryCollection> requireFresh(
    const DiscoveryCollection &collection)
{
    if (collection.snapshot().freshness
        == DiscoveryFreshness::Stale) {
        return DiscoveryResult<DiscoveryCollection>::failure(
            DiscoveryErrorCode::Stale,
            QStringLiteral("snapshot.freshness"),
            QStringLiteral(
                "Fresh discovery data was required, but the snapshot is explicitly stale."));
    }

    return DiscoveryResult<DiscoveryCollection>::success(
        collection);
}

} // namespace vinyl::discovery
