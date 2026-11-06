#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <functional>
#include <memory>

class NuvioRepositoryStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList repositories READ repositories NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    using FetchCompletion = std::function<void(const QByteArray &body, const QUrl &effectiveUrl,
                                               const QString &error)>;
    using Fetch = std::function<void(const QUrl &, qsizetype maxBytes, QObject *context,
                                     FetchCompletion)>;

    struct Plugin {
        QString repoId;
        QString id;
        QString name;
        QString scriptPath;
        QByteArray sha256;
    };

    explicit NuvioRepositoryStore(QString storageDirectory, Fetch fetch, QObject *parent = nullptr);

    QVariantList repositories() const;
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QList<Plugin> enabledPlugins(const QString &mediaType) const;

    Q_INVOKABLE void addRepository(const QString &address);
    Q_INVOKABLE void refreshRepository(const QString &repoId);
    Q_INVOKABLE void removeRepository(const QString &repoId);
    Q_INVOKABLE void setPluginEnabled(const QString &repoId, const QString &pluginId, bool enabled);

signals:
    void changed();
    void busyChanged();
    void errorChanged();

private:
    struct Scraper {
        QString id;
        QString name;
        QString version;
        QString filename;
        QStringList types;
        bool enabled = false;
        QByteArray hash;
    };

    struct Repository {
        QString id;
        QString name;
        QUrl address;
        QUrl effectiveUrl;
        QList<Scraper> scrapers;
    };

    static bool readManifest(const QByteArray &body, Repository &repo);
    void load();
    bool save(const QList<Repository> &repositories) const;
    void pruneSnapshots() const;
    QString snapshotPath(const QByteArray &hash) const;
    bool hasSnapshot(const QByteArray &hash) const;
    QByteArray storeSnapshot(const QByteArray &script) const;
    int repositoryIndex(const QString &id) const;
    void begin();
    void finish(const QString &error = {});
    void commit(QList<Repository> repositories);
    void fetchManifest(const QUrl &url, int index);
    void fetchBounded(const QUrl &url, qsizetype limit, FetchCompletion completion);
    void refreshScripts(const std::shared_ptr<Repository> &repo, int repoIndex, int scraperIndex);

    QString m_directory;
    Fetch m_fetch;
    QList<Repository> m_repositories;
    QString m_error;
    bool m_busy = false;
    bool m_indexReadable = true;
};
