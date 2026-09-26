#pragma once

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class PorticoContentStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit PorticoContentStore(QObject *parent = nullptr);

    int revision() const { return m_revision; }

    Q_INVOKABLE QVariantMap title(const QString &canonicalKey) const;
    Q_INVOKABLE bool contains(const QString &canonicalKey) const;
    Q_INVOKABLE QStringList ids() const;
    Q_INVOKABLE QStringList search(const QString &query, int limit = 40) const;
    Q_INVOKABLE QString providerLabel(const QString &providerId) const;
    Q_INVOKABLE QString kindLabel(const QString &kind) const;
    Q_INVOKABLE QString verbForKind(const QString &kind) const;
    Q_INVOKABLE QString shapeForKind(const QString &kind) const;

public slots:
    void ingestShelf(const QString &feedId, const QVariantMap &shelf);

signals:
    void revisionChanged();

private:
    static QVariantMap toPorticoTitle(const QVariantMap &item);
    static QString normalizedSearchText(QString value);
    void bumpRevision();

    QHash<QString, QVariantMap> m_titles;
    int m_revision = 0;
};
