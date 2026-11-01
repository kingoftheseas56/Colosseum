#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Profile-owned named collections, adapted from Arc 70. CollectionStore remains
// the independent per-world Library owner. Identity/reopen references come from
// the existing owning adapters; this class never resolves providers or media.
class NamedCollectionsStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool healthy READ healthy NOTIFY errorChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit NamedCollectionsStore(const QString &filePath, QObject *parent = nullptr);

    int revision() const { return m_revision; }
    bool healthy() const { return m_loadHealthy; }
    QString error() const { return m_error; }
    QString filePath() const { return m_filePath; }

    // Lossless device-local profile adoption. Conflicting saved versions are
    // retained as separate collections; this is not a cloud/export projection.
    static bool mergeForAdoption(const QByteArray &account, const QByteArray &local,
        QByteArray *merged, QString *error = nullptr);

    Q_INVOKABLE QVariantList collections() const;
    Q_INVOKABLE QVariantMap collection(const QString &collectionId) const;
    Q_INVOKABLE QVariantList items(const QString &collectionId) const;
    Q_INVOKABLE QStringList memberships(const QString &referenceKey) const;
    Q_INVOKABLE bool contains(const QString &collectionId, const QString &referenceKey) const;

    // All-or-nothing create, including the initial membership when supplied.
    Q_INVOKABLE QString createCollection(const QString &name,
        const QVariantMap &initialReference = {}, const QString &description = {});
    // Optional new name joins the same transaction as all staged memberships.
    Q_INVOKABLE QVariantMap saveMemberships(const QVariantMap &reference,
        const QStringList &selectedIds, const QString &newCollectionName = {});
    Q_INVOKABLE bool deleteCollection(const QString &collectionId);

    // Drafts retain their starting revision and store-instance ownership. Save
    // may only reorder/remove existing references and edit their personal notes.
    Q_INVOKABLE QVariantMap beginEdit(const QString &collectionId);
    Q_INVOKABLE bool saveEdit(const QString &token, const QString &name,
        const QString &description, const QStringList &orderedKeys,
        const QVariantMap &notes);
    Q_INVOKABLE bool cancelEdit(const QString &token);
    Q_INVOKABLE bool reload();

signals:
    void changed();
    void errorChanged();

private:
    struct Member { QVariantMap reference; QString note; };
    struct List {
        QString id;
        QString name;
        QString description;
        qint64 createdAt = 0;
        qint64 updatedAt = 0;
        QVector<Member> members;
    };
    struct Draft { QString collectionId; int revision = 0; };

    static QString normalName(const QString &value);
    static QString nameKey(const QString &value);
    static bool boundedText(const QString &value, qsizetype limit, bool multiline = false);
    static bool normalizedReference(const QVariantMap &input, QVariantMap *output);
    static bool sameIdentity(const QVariantMap &left, const QVariantMap &right);
    static int indexForId(const QVector<List> &lists, const QString &id);
    static bool hasName(const QVector<List> &lists, const QString &name,
                        const QString &excludingId = {});
    static QVariantMap row(const List &list);
    static QByteArray serialize(const QVector<List> &lists);
    static bool parse(const QByteArray &bytes, QVector<List> *lists, QString *error);
    bool mutableState();
    bool commit(QVector<List> candidate);
    bool load(bool notify);
    bool fail(const QString &message);
    QVariantMap result(bool ok, const QString &collectionId = {}) const;
    void setError(const QString &message);

    QString m_filePath;
    QVector<List> m_lists;
    QHash<QString, Draft> m_drafts;
    QByteArray m_diskDigest;
    bool m_fileExisted = false;
    int m_revision = 0;
    bool m_loadHealthy = true;
    QString m_error;
};
