#pragma once

#include "../ba0-domain-prototype/VinylDomainTypes.h"

#include <QString>
#include <QVector>

namespace vinyl::context {

enum class RelationshipKind {
    PerformanceOf,
    ArtistCredit,
    Performer,
    Producer,
    ReleaseVersion,
    RelatedRecording,
    RelatedWork,
    Unsupported,
};
QString relationshipKindName(RelationshipKind kind);

enum class RelationshipDirectionality { Directed, Bidirectional };
QString relationshipDirectionalityName(RelationshipDirectionality directionality);

enum class CreditRoleKind {
    PrimaryArtist, FeaturedArtist, Performer, Producer,
    Composer, Lyricist, Engineer, Other,
};
QString creditRoleKindName(CreditRoleKind kind);

struct CreditRole final {
    CreditRoleKind kind = CreditRoleKind::Other;
    QString sourceRole;
};

struct ArtistRoleCredit final {
    ArtistMbid artistMbid;
    QString creditedName;
    QVector<CreditRole> roles;
};

struct RelationshipEvidence final {
    QString source;
    QString sourceItem;
    QString relationshipType;
    QString assertionKey;
};

struct ContextNode final {
    CanonicalEntityRef entity;
};

struct ContextRelationship final {
    CanonicalEntityRef source;
    CanonicalEntityRef target;
    RelationshipKind kind = RelationshipKind::Unsupported;
    RelationshipDirectionality directionality = RelationshipDirectionality::Directed;
    QVector<ArtistRoleCredit> artistCredits;
    QString rawRelationshipType;
    QVector<RelationshipEvidence> evidence;
};

class ContextGraph final
{
public:
    void addNode(CanonicalEntityRef entity);
    void addRelationship(ContextRelationship relationship);
    void normalize();

    const QVector<ContextNode> &nodes() const noexcept { return m_nodes; }
    const QVector<ContextRelationship> &relationships() const noexcept
    {
        return m_relationships;
    }

private:
    bool containsNode(const CanonicalEntityRef &entity) const;

    QVector<ContextNode> m_nodes;
    QVector<ContextRelationship> m_relationships;
};

} // namespace vinyl::context
