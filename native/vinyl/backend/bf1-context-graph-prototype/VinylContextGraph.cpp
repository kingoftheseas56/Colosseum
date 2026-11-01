#include "VinylContextGraph.h"

#include <algorithm>
#include <utility>

namespace vinyl::context {
namespace {

QString entityKey(const CanonicalEntityRef &entity)
{
    return QString::number(static_cast<int>(entity.kind()))
        + QLatin1Char(':') + entity.mbidString();
}

QString fieldKey(const QString &value)
{
    return QString::number(value.size()) + QLatin1Char(':') + value;
}

QString roleKey(const CreditRole &role)
{
    return fieldKey(QString::number(static_cast<int>(role.kind))) + fieldKey(role.sourceRole);
}

QString creditKey(const ArtistRoleCredit &credit)
{
    QStringList roles;
    for (const CreditRole &role : credit.roles)
        roles.push_back(roleKey(role));
    roles.sort();
    QString key = fieldKey(credit.artistMbid.toString()) + fieldKey(credit.creditedName)
        + fieldKey(QString::number(roles.size()));
    for (const auto &role : roles) key += fieldKey(role);
    return key;
}

QString semanticKey(const ContextRelationship &edge)
{
    QStringList credits;
    for (const ArtistRoleCredit &credit : edge.artistCredits)
        credits.push_back(creditKey(credit));
    credits.sort();

    const QString unsupportedType =
        edge.kind == RelationshipKind::Unsupported ? edge.rawRelationshipType : QString();

    QString key = fieldKey(entityKey(edge.source)) + fieldKey(entityKey(edge.target))
        + fieldKey(QString::number(static_cast<int>(edge.kind)))
        + fieldKey(QString::number(static_cast<int>(edge.directionality)))
        + fieldKey(QString::number(credits.size()));
    for (const auto &credit : credits) key += fieldKey(credit);
    return key + fieldKey(unsupportedType);
}

QString evidenceKey(const RelationshipEvidence &evidence)
{
    return fieldKey(evidence.source) + fieldKey(evidence.sourceItem)
        + fieldKey(evidence.relationshipType) + fieldKey(evidence.assertionKey);
}

void normalizeEvidence(QVector<RelationshipEvidence> &evidence)
{
    std::sort(evidence.begin(), evidence.end(),
              [](const auto &a, const auto &b) {
                  return evidenceKey(a) < evidenceKey(b);
              });
    evidence.erase(std::unique(evidence.begin(), evidence.end(),
                               [](const auto &a, const auto &b) {
                                   return evidenceKey(a) == evidenceKey(b);
                               }),
                   evidence.end());
}

} // namespace

QString relationshipKindName(RelationshipKind kind)
{
    switch (kind) {
    case RelationshipKind::PerformanceOf: return QStringLiteral("performance_of");
    case RelationshipKind::ArtistCredit: return QStringLiteral("artist_credit");
    case RelationshipKind::Performer: return QStringLiteral("performer");
    case RelationshipKind::Producer: return QStringLiteral("producer");
    case RelationshipKind::ReleaseVersion: return QStringLiteral("release_version");
    case RelationshipKind::RelatedRecording: return QStringLiteral("related_recording");
    case RelationshipKind::RelatedWork: return QStringLiteral("related_work");
    case RelationshipKind::Unsupported: return QStringLiteral("unsupported");
    }
    return QStringLiteral("unsupported");
}

QString relationshipDirectionalityName(RelationshipDirectionality directionality)
{
    return directionality == RelationshipDirectionality::Bidirectional
        ? QStringLiteral("bidirectional") : QStringLiteral("directed");
}

QString creditRoleKindName(CreditRoleKind kind)
{
    switch (kind) {
    case CreditRoleKind::PrimaryArtist: return QStringLiteral("primary_artist");
    case CreditRoleKind::FeaturedArtist: return QStringLiteral("featured_artist");
    case CreditRoleKind::Performer: return QStringLiteral("performer");
    case CreditRoleKind::Producer: return QStringLiteral("producer");
    case CreditRoleKind::Composer: return QStringLiteral("composer");
    case CreditRoleKind::Lyricist: return QStringLiteral("lyricist");
    case CreditRoleKind::Engineer: return QStringLiteral("engineer");
    case CreditRoleKind::Other: return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

bool ContextGraph::containsNode(const CanonicalEntityRef &entity) const
{
    for (const ContextNode &node : m_nodes)
        if (node.entity == entity) return true;
    return false;
}

void ContextGraph::addNode(CanonicalEntityRef entity)
{
    if (!containsNode(entity))
        m_nodes.push_back(ContextNode{std::move(entity)});
}

void ContextGraph::addRelationship(ContextRelationship relationship)
{
    addNode(relationship.source);
    addNode(relationship.target);
    m_relationships.push_back(std::move(relationship));
}

void ContextGraph::normalize()
{
    std::sort(m_nodes.begin(), m_nodes.end(), [](const auto &a, const auto &b) {
        return entityKey(a.entity) < entityKey(b.entity);
    });

    for (ContextRelationship &edge : m_relationships) {
        for (auto &credit : edge.artistCredits) {
            std::sort(credit.roles.begin(), credit.roles.end(), [](const auto &a, const auto &b) {
                return roleKey(a) < roleKey(b);
            });
        }
        std::sort(edge.artistCredits.begin(), edge.artistCredits.end(), [](const auto &a, const auto &b) {
            return creditKey(a) < creditKey(b);
        });
        // Raw relationship tokens have meaning only for Unsupported edges.
        if (edge.kind != RelationshipKind::Unsupported) edge.rawRelationshipType.clear();
        normalizeEvidence(edge.evidence);
    }

    std::sort(m_relationships.begin(), m_relationships.end(),
              [](const auto &a, const auto &b) {
                  return semanticKey(a) < semanticKey(b);
              });

    QVector<ContextRelationship> merged;
    for (ContextRelationship edge : m_relationships) {
        if (!merged.isEmpty()
            && semanticKey(merged.last()) == semanticKey(edge)) {
            merged.last().evidence += edge.evidence;
            normalizeEvidence(merged.last().evidence);
        } else {
            merged.push_back(std::move(edge));
        }
    }
    m_relationships = std::move(merged);
}

} // namespace vinyl::context
