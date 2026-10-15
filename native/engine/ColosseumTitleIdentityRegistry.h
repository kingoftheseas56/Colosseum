#pragma once
#include <QObject>
#include <QHash>
#include <QList>
#include <QMultiHash>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
class ColosseumTitleIdentityRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(QString errorCode READ errorCode CONSTANT)
public:
    explicit ColosseumTitleIdentityRegistry(const QString &path = QString(), QObject *parent = nullptr);
    bool ready() const;
    QString errorCode() const;
    QString sourcePath() const;
    Q_INVOKABLE QVariantMap resolve(const QString &world, const QString &kind, const QString &directMediaId, QVariantList aliases) const;
    static QString defaultResourcePath();
    static bool isCanonicalMediaId(const QString &mediaId);
    static bool isJoinedFixtureIdentity(const QString &world, const QString &kind, const QString &mediaId);
    // Persisted provider-id pivot memory (kitsu:/mal:… → tt…). Empty path
    // disables persistence; the registry stays stateless by default so tests
    // and resource-only builds remain deterministic.
    void setAliasUnionsPath(const QString &path);
private:
    struct Row { QString world; QString kind; QString mediaId; QList<QPair<QString, QString>> aliases; };
    bool load(const QString &path);
    QVariantMap unavailable(const QString &world, const QString &kind, const QString &code) const;
    QVariantMap resolved(const Row &row) const;
    QVariantMap resolvedMediaId(
        const QString &world, const QString &kind, const QString &mediaId) const;
    static bool admittedPair(const QString &world, const QString &kind);
    static QString aliasKey(const QString &nameSpace, const QString &value);
    // Catalogue-derived identities (Arc 49 Slice 1): every identified title in an
    // admitted pair resolves to a deterministic v5 "ct1:" id derived from its exact
    // identity-bearing alias, so coverage needs no hand list. Seed rows in
    // title-identities-v1.json pin canonical ids (Frieren migration) and win over
    // derivation. The first id byte carries the world/kind pair tag so a direct
    // id can be re-validated on open without the originating alias.
    static int pairTag(const QString &world, const QString &kind);
    static bool isDerivedMediaId(const QString &mediaId);
    static int derivedMediaIdTag(const QString &mediaId);
    QString deriveMediaId(
        const QString &world, const QString &kind,
        const QString &nameSpace, const QString &value) const;
    QString deriveNamedMediaId(
        const QString &world, const QString &kind,
        const QString &nameSpace, const QString &value, int tag) const;
    QString deriveIdentity(
        const QString &world, const QString &kind,
        const QVariantList &aliases) const;
    void recordAliasUnion(
        const QString &nameSpace, const QString &from, const QString &to) const;
    QVariantList expandAliasUnions(const QVariantList &aliases) const;
    QList<Row> m_rows;
    QHash<QString, int> m_mediaRows;
    QMultiHash<QString, int> m_aliasRows;
    QString m_sourcePath;
    QString m_errorCode;
    bool m_ready = false;
    mutable QHash<QString, QString> m_aliasUnions;
    QString m_aliasUnionsPath;
    bool m_aliasUnionsEnabled = false;
};
