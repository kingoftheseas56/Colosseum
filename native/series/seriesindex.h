#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class SeriesIndex : public QObject {
    Q_OBJECT

public:
    explicit SeriesIndex(const QString &dbPath, QObject *parent = nullptr);
    ~SeriesIndex() override;

    Q_INVOKABLE QVariantMap lookup(const QString &title,
                                   const QString &author = QString()) const;
    Q_INVOKABLE QVariantList seriesEntries(const QString &series) const;
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 24) const;
    Q_INVOKABLE QVariantList topBooks(int limit = 10) const;
    Q_INVOKABLE QVariantList topSeries(int limit = 12) const;
    Q_INVOKABLE QVariantMap bookDetail(const QString &title,
                                       const QString &author = QString()) const;
    Q_INVOKABLE QVariantMap bookDetailById(int workId) const;

    void selfTest() const;

private:
    enum class SchemaMode {
        Unknown,
        LegacyBooks,
        CanonicalGraph,
    };

    bool isReady() const;
    void detectSchema();

    QString m_dbPath;
    QString m_connectionName;
    QSqlDatabase m_db;
    SchemaMode m_schemaMode = SchemaMode::Unknown;
};
