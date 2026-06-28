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

    void selfTest() const;

private:
    bool isReady() const;

    QString m_dbPath;
    QString m_connectionName;
    QSqlDatabase m_db;
};
