#pragma once
#include "VinylDomainTypes.h"
#include <QJsonObject>
#include <variant>

namespace vinyl {
// A provider ID remains opaque even when it happens to look like an MBID.
// There is deliberately no implicit conversion back to CanonicalEntityRef.
class RecommendationEntityRef final {
public:
    RecommendationEntityRef(CanonicalEntityRef canonical)
        : m_kind(canonical.kind()), m_identity(std::move(canonical)) {}
    static RecommendationEntityRef fromCanonical(CanonicalEntityRef value) {
        return RecommendationEntityRef(std::move(value));
    }
    static RecommendationEntityRef fromProvider(CanonicalEntityKind kind, ProviderItemRef value) {
        return RecommendationEntityRef(kind, std::move(value));
    }
    static DomainResult<RecommendationEntityRef> fromJson(const QJsonObject &object);
    CanonicalEntityKind kind() const noexcept { return m_kind; }
    const CanonicalEntityRef *canonicalRef() const noexcept {
        return std::get_if<CanonicalEntityRef>(&m_identity);
    }
    const ProviderItemRef *providerRef() const noexcept {
        return std::get_if<ProviderItemRef>(&m_identity);
    }
    bool isCanonical() const noexcept { return canonicalRef() != nullptr; }
    QString mbidString() const { return isCanonical() ? canonicalRef()->mbidString() : QString(); }
    QString stableKey() const;
    QJsonObject toJson() const;
    bool operator==(const RecommendationEntityRef &other) const noexcept {
        return m_kind == other.m_kind && m_identity == other.m_identity;
    }
    bool operator!=(const RecommendationEntityRef &other) const noexcept { return !(*this == other); }
private:
    RecommendationEntityRef(CanonicalEntityKind kind, ProviderItemRef value)
        : m_kind(kind), m_identity(std::move(value)) {}
    CanonicalEntityKind m_kind;
    std::variant<CanonicalEntityRef, ProviderItemRef> m_identity;
};
inline bool operator==(const CanonicalEntityRef &left, const RecommendationEntityRef &right) noexcept {
    return right == RecommendationEntityRef(left);
}
inline bool operator!=(const CanonicalEntityRef &left, const RecommendationEntityRef &right) noexcept {
    return !(left == right);
}
} // namespace vinyl
