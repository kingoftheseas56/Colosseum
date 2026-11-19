#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class DownloadLocation {
public:
    struct Item {
        QString source;
        QString relativeDestination;
        QStringList members; // Empty for a single file. For a folder, exactly the owned files.
        QStringList companionSuffixes; // Exact siblings of a single file, e.g. its recovery record.
    };

    struct File {
        QString source;
        QString destination;
        QString canonicalSource;
        qint64 size = 0;
        QByteArray digest;
    };

    struct Plan {
        QString currentRoot;
        QString destinationRoot;
        QString id;
        QByteArray marker;
        QByteArray device;
        QByteArray sourceMarker;
        bool markerInstalled = false;
        QHash<QString, QString> paths;
        QList<File> files;
        int items = 0;
        int collisions = 0;
        qint64 bytes = 0;
        QString error;

        QVariantMap preview() const;
    };

    explicit DownloadLocation(QString statePath);
    QString root() const { return m_root; }
    const QByteArray &marker() const { return m_marker; }
    const QHash<QString, QString> &aliases() const { return m_aliases; }
    const QHash<QString, QByteArray> &knownLocations() const { return m_knownLocations; }
    const Plan &pending() const { return m_pending; }
    bool committed() const;

    Plan plan(const QString &destinationRoot, QList<Item> items) const;
    QString begin(Plan plan);
    QString copy();
    QString commit();
    QString cleanSources();

private:
    bool save() const;
    bool destinationReady() const;

    QString m_statePath;
    QString m_root;
    QByteArray m_marker;
    QHash<QString, QString> m_aliases;
    QHash<QString, QByteArray> m_knownLocations;
    Plan m_pending;
    bool m_committed = false;
};
