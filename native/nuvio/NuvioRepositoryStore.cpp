#include "NuvioRepositoryStore.h"
#include "NuvioManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <utility>

namespace {
constexpr qsizetype MaxManifest = 128 * 1024;
constexpr qsizetype MaxScript = 512 * 1024;
constexpr qsizetype MaxIndex = 4 * 1024 * 1024;
constexpr int MaxRepos = 16;
constexpr int MaxScrapers = 64;

bool sameOrigin(const QUrl &first, const QUrl &second)
{
    return first.host() == second.host() && first.port(443) == second.port(443);
}

bool validId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9._-]{0,79}\\z"));
    return pattern.match(id).hasMatch();
}

bool validHash(const QByteArray &hash)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[a-f0-9]{64}\\z"));
    return pattern.match(QString::fromLatin1(hash)).hasMatch();
}

bool validFilename(const QString &name)
{
    static const QRegularExpression segment(QStringLiteral("\\A[A-Za-z0-9_-][A-Za-z0-9._-]*\\z"));
    if (name.size() > 240 || !name.endsWith(QStringLiteral(".js")))
        return false;
    for (const QString &part : name.split(QLatin1Char('/'))) {
        if (!segment.match(part).hasMatch())
            return false;
    }
    return true;
}

bool validLabel(const QString &label, qsizetype limit)
{
    if (label.trimmed().isEmpty() || label.size() > limit)
        return false;
    for (const QChar c : label) {
        if (!c.isPrint())
            return false;
    }
    return true;
}

QString repoIdFor(const QUrl &url)
{
    return QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
}

bool writeAtomic(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
} // namespace

NuvioRepositoryStore::NuvioRepositoryStore(QString storageDirectory, Fetch fetch, QObject *parent)
    : QObject(parent),
      m_directory(QDir(std::move(storageDirectory)).absolutePath()),
      m_fetch(std::move(fetch))
{
    load();
}

bool NuvioRepositoryStore::readManifest(const QByteArray &body, Repository &repo)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonObject manifest = document.object();
    const QString name = manifest.value(QStringLiteral("name")).toString();
    const QJsonValue list = manifest.value(QStringLiteral("scrapers"));
    if (!validLabel(name, 120) || !list.isArray() || list.toArray().size() > MaxScrapers)
        return false;

    QList<Scraper> scrapers;
    QSet<QString> ids;
    for (const QJsonValue &value : list.toArray()) {
        const QJsonObject object = value.toObject();
        Scraper scraper;
        scraper.id = object.value(QStringLiteral("id")).toString();
        scraper.name = object.value(QStringLiteral("name")).toString();
        scraper.version = object.value(QStringLiteral("version")).toString();
        scraper.filename = object.value(QStringLiteral("filename")).toString();
        const QJsonArray types = object.value(QStringLiteral("supportedTypes")).toArray();
        if (!validId(scraper.id) || ids.contains(scraper.id) || !validLabel(scraper.name, 120)
            || !validLabel(scraper.version, 48) || !validFilename(scraper.filename)
            || types.isEmpty() || types.size() > 16) {
            return false;
        }
        for (const QJsonValue &type : types) {
            const QString key = type.toString();
            if (!validId(key) || scraper.types.contains(key))
                return false;
            scraper.types.append(key);
        }
        ids.insert(scraper.id);
        scrapers.append(std::move(scraper));
    }
    repo.name = name;
    repo.scrapers = std::move(scrapers);
    return true;
}

void NuvioRepositoryStore::load()
{
    QFile file(QDir(m_directory).filePath(QStringLiteral("repositories.json")));
    if (!file.exists())
        return;

    m_indexReadable = false;
    m_error = tr("Couldn't read the saved Nuvio repositories.");
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxIndex)
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.read(MaxIndex + 1)).object();
    const QJsonValue list = root.value(QStringLiteral("repositories"));
    if (root.value(QStringLiteral("schema")).toInt() != 1 || !list.isArray()
        || list.toArray().size() > MaxRepos) {
        return;
    }

    QList<Repository> repositories;
    QSet<QString> ids;
    for (const QJsonValue &value : list.toArray()) {
        const QJsonObject item = value.toObject();
        Repository repo;
        repo.address = QUrl(item.value(QStringLiteral("address")).toString(), QUrl::StrictMode);
        repo.effectiveUrl = QUrl(item.value(QStringLiteral("effectiveUrl")).toString(), QUrl::StrictMode);
        if (!NuvioManifest::usableUrl(repo.address) || !NuvioManifest::usableUrl(repo.effectiveUrl)
            || !readManifest(QJsonDocument(item).toJson(QJsonDocument::Compact), repo)) {
            return;
        }
        repo.id = repoIdFor(repo.address);
        if (ids.contains(repo.id))
            return;
        ids.insert(repo.id);

        const QJsonArray entries = item.value(QStringLiteral("scrapers")).toArray();
        for (qsizetype i = 0; i < entries.size(); ++i) {
            const QJsonObject entry = entries.at(i).toObject();
            Scraper &scraper = repo.scrapers[i];
            scraper.enabled = entry.value(QStringLiteral("enabled")).toBool();
            scraper.hash = entry.value(QStringLiteral("sha256")).toString().toLatin1();
            if ((!scraper.hash.isEmpty() && !validHash(scraper.hash))
                || (scraper.enabled && scraper.hash.isEmpty())) {
                return;
            }
        }
        repositories.append(std::move(repo));
    }
    m_repositories = std::move(repositories);
    m_indexReadable = true;
    m_error.clear();
    pruneSnapshots();
}

bool NuvioRepositoryStore::save(const QList<Repository> &repositories) const
{
    QJsonArray list;
    for (const Repository &repo : repositories) {
        QJsonArray scrapers;
        for (const Scraper &scraper : repo.scrapers) {
            scrapers.append(QJsonObject{
                {QStringLiteral("id"), scraper.id},
                {QStringLiteral("name"), scraper.name},
                {QStringLiteral("version"), scraper.version},
                {QStringLiteral("filename"), scraper.filename},
                {QStringLiteral("supportedTypes"), QJsonArray::fromStringList(scraper.types)},
                {QStringLiteral("enabled"), scraper.enabled},
                {QStringLiteral("sha256"), QString::fromLatin1(scraper.hash)}
            });
        }
        list.append(QJsonObject{
            {QStringLiteral("name"), repo.name},
            {QStringLiteral("address"), QString::fromLatin1(repo.address.toEncoded())},
            {QStringLiteral("effectiveUrl"), QString::fromLatin1(repo.effectiveUrl.toEncoded())},
            {QStringLiteral("scrapers"), scrapers}
        });
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("repositories"), list}
    }).toJson(QJsonDocument::Compact);
    return bytes.size() <= MaxIndex && QDir().mkpath(m_directory)
        && writeAtomic(QDir(m_directory).filePath(QStringLiteral("repositories.json")), bytes);
}

QString NuvioRepositoryStore::snapshotPath(const QByteArray &hash) const
{
    return QDir(m_directory).filePath(QStringLiteral("scripts/%1.js").arg(QString::fromLatin1(hash)));
}

void NuvioRepositoryStore::pruneSnapshots() const
{
    if (!m_indexReadable
        || !QFileInfo(QDir(m_directory).filePath(QStringLiteral("repositories.json"))).isFile()) {
        return;
    }

    const QFileInfo directory(QDir(m_directory).filePath(QStringLiteral("scripts")));
    if (!directory.isDir() || directory.isSymLink() || directory.isJunction())
        return;

    QSet<QByteArray> referenced;
    for (const Repository &repo : m_repositories) {
        for (const Scraper &scraper : repo.scrapers) {
            if (!scraper.hash.isEmpty())
                referenced.insert(scraper.hash);
        }
    }

    const QDir scripts(directory.absoluteFilePath());
    const auto files = scripts.entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::Hidden);
    for (const QFileInfo &file : files) {
        const QString name = file.fileName();
        if (!file.isFile() || file.isSymLink() || !name.endsWith(QStringLiteral(".js")))
            continue;
        const QByteArray hash = name.chopped(3).toLatin1();
        if (validHash(hash) && !referenced.contains(hash))
            QFile::remove(file.absoluteFilePath());
    }
}

bool NuvioRepositoryStore::hasSnapshot(const QByteArray &hash) const
{
    if (hash.isEmpty())
        return false;
    const QString path = snapshotPath(hash);
    const QFileInfo info(path);
    return info.isFile() && !info.isSymLink() && info.size() > 0 && info.size() <= MaxScript;
}

QByteArray NuvioRepositoryStore::storeSnapshot(const QByteArray &script) const
{
    if (script.isEmpty())
        return {};
    const QByteArray hash = QCryptographicHash::hash(script, QCryptographicHash::Sha256).toHex();
    if (hasSnapshot(hash)) {
        QFile existing(snapshotPath(hash));
        if (existing.open(QIODevice::ReadOnly) && existing.read(MaxScript + 1) == script)
            return hash;
    }
    if (!QDir().mkpath(QDir(m_directory).filePath(QStringLiteral("scripts")))
        || !writeAtomic(snapshotPath(hash), script)) {
        return {};
    }
    return hash;
}

QVariantList NuvioRepositoryStore::repositories() const
{
    QVariantList result;
    for (const Repository &repo : m_repositories) {
        QVariantList plugins;
        for (const Scraper &scraper : repo.scrapers) {
            plugins.append(QVariantMap{
                {QStringLiteral("id"), scraper.id},
                {QStringLiteral("name"), scraper.name},
                {QStringLiteral("version"), scraper.version},
                {QStringLiteral("enabled"), scraper.enabled}
            });
        }
        result.append(QVariantMap{
            {QStringLiteral("id"), repo.id},
            {QStringLiteral("name"), repo.name},
            {QStringLiteral("plugins"), plugins}
        });
    }
    return result;
}

QList<NuvioRepositoryStore::Plugin> NuvioRepositoryStore::enabledPlugins(const QString &mediaType) const
{
    if (mediaType != QStringLiteral("movie") && mediaType != QStringLiteral("tv"))
        return {};

    QList<Plugin> plugins;
    for (const Repository &repo : m_repositories) {
        for (const Scraper &scraper : repo.scrapers) {
            if (scraper.enabled && scraper.types.contains(mediaType) && hasSnapshot(scraper.hash))
                plugins.append({repo.id, scraper.id, scraper.name, snapshotPath(scraper.hash), scraper.hash});
        }
    }
    return plugins;
}

int NuvioRepositoryStore::repositoryIndex(const QString &id) const
{
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (m_repositories.at(i).id == id)
            return i;
    }
    return -1;
}

void NuvioRepositoryStore::begin()
{
    m_busy = true;
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }
    emit busyChanged();
}

void NuvioRepositoryStore::finish(const QString &error)
{
    if (m_error != error) {
        m_error = error;
        emit errorChanged();
    }
    if (m_busy) {
        // A failed operation still has the last committed index in memory.
        // A successful one has already published changed() to its consumers.
        pruneSnapshots();
        m_busy = false;
        emit busyChanged();
    }
}

void NuvioRepositoryStore::commit(QList<Repository> repositories)
{
    if (!save(repositories)) {
        finish(tr("Couldn't save the Nuvio repository changes."));
        return;
    }
    m_repositories = std::move(repositories);
    emit changed();
    finish();
}

void NuvioRepositoryStore::fetchBounded(const QUrl &url, qsizetype limit, FetchCompletion completion)
{
    if (!m_fetch || !NuvioManifest::usableUrl(url)) {
        completion({}, {}, tr("The repository could not be reached."));
        return;
    }

    m_fetch(url, limit, this,
            [guard = QPointer<NuvioRepositoryStore>(this), limit,
             completed = std::make_shared<bool>(false), completion = std::move(completion)]
            (const QByteArray &body, const QUrl &effectiveUrl, const QString &error) {
        if (!guard || std::exchange(*completed, true))
            return;
        if (!error.isEmpty() || !NuvioManifest::usableUrl(effectiveUrl) || body.size() > limit) {
            completion({}, {}, guard->tr("The repository could not be reached or returned an invalid response."));
            return;
        }
        completion(body, effectiveUrl, {});
    });
}

void NuvioRepositoryStore::addRepository(const QString &address)
{
    if (m_busy || !m_indexReadable)
        return;

    const QString input = address.trimmed();
    QUrl url(input, QUrl::StrictMode);
    if (input.size() > 2048 || !NuvioManifest::usableUrl(url)) {
        finish(tr("Enter an HTTPS repository link without sign-in details, a query, or a fragment."));
        return;
    }
    url = url.adjusted(QUrl::NormalizePathSegments);
    if (url.port() == 443)
        url.setPort(-1);
    if (repositoryIndex(repoIdFor(url)) >= 0) {
        finish(tr("This Nuvio repository is already installed."));
        return;
    }
    if (m_repositories.size() >= MaxRepos) {
        finish(tr("The limit of %1 Nuvio repositories has been reached.").arg(MaxRepos));
        return;
    }
    begin();
    fetchManifest(url, -1);
}

void NuvioRepositoryStore::refreshRepository(const QString &repoId)
{
    if (m_busy || !m_indexReadable)
        return;
    const int index = repositoryIndex(repoId);
    if (index < 0)
        return;

    begin();
    fetchManifest(m_repositories.at(index).address, index);
}

void NuvioRepositoryStore::removeRepository(const QString &repoId)
{
    if (m_busy || !m_indexReadable)
        return;
    const int index = repositoryIndex(repoId);
    if (index < 0)
        return;

    begin();
    QList<Repository> updated = m_repositories;
    updated.removeAt(index);
    commit(std::move(updated));
}

void NuvioRepositoryStore::fetchManifest(const QUrl &url, int index)
{
    fetchBounded(url, MaxManifest,
                 [this, url, index](const QByteArray &body, const QUrl &effectiveUrl, const QString &error) {
        if (!error.isEmpty()) {
            finish(error);
            return;
        }
        auto repo = std::make_shared<Repository>();
        if (!readManifest(body, *repo)) {
            finish(tr("This link didn't return a valid Nuvio repository manifest."));
            return;
        }

        repo->id = repoIdFor(url);
        repo->address = url;
        repo->effectiveUrl = effectiveUrl;
        if (index < 0) {
            auto updated = m_repositories;
            updated.append(std::move(*repo));
            commit(std::move(updated));
            return;
        }

        const Repository &previous = m_repositories.at(index);
        for (Scraper &scraper : repo->scrapers) {
            for (const Scraper &old : previous.scrapers) {
                if (scraper.id == old.id) {
                    scraper.enabled = old.enabled;
                    const QUrl updatedScript = repo->effectiveUrl.resolved(QUrl(scraper.filename));
                    const QUrl previousScript = previous.effectiveUrl.resolved(QUrl(old.filename));
                    if (scraper.version == old.version && updatedScript == previousScript)
                        scraper.hash = old.hash;
                    break;
                }
            }
        }
        refreshScripts(repo, index, 0);
    });
}

void NuvioRepositoryStore::refreshScripts(const std::shared_ptr<Repository> &repo,
                                          int repoIndex, int scraperIndex)
{
    while (scraperIndex < repo->scrapers.size() && !repo->scrapers.at(scraperIndex).enabled)
        ++scraperIndex;
    if (scraperIndex == repo->scrapers.size()) {
        auto updated = m_repositories;
        updated[repoIndex] = *repo;
        commit(std::move(updated));
        return;
    }

    const Scraper &scraper = repo->scrapers.at(scraperIndex);
    const QUrl script = repo->effectiveUrl.resolved(QUrl(scraper.filename));
    fetchBounded(script, MaxScript,
                 [this, repo, repoIndex, scraperIndex, script]
                 (const QByteArray &body, const QUrl &effectiveUrl, const QString &error) {
        if (!error.isEmpty() || !sameOrigin(script, effectiveUrl)) {
            finish(tr("Couldn't refresh an enabled Nuvio plugin. The previous repository was kept."));
            return;
        }
        const QByteArray hash = storeSnapshot(body);
        if (hash.isEmpty()) {
            finish(tr("Couldn't save an updated Nuvio plugin. The previous repository was kept."));
            return;
        }
        repo->scrapers[scraperIndex].hash = hash;
        refreshScripts(repo, repoIndex, scraperIndex + 1);
    });
}

void NuvioRepositoryStore::setPluginEnabled(const QString &repoId, const QString &pluginId, bool enabled)
{
    if (m_busy || !m_indexReadable)
        return;
    const int index = repositoryIndex(repoId);
    if (index < 0)
        return;

    const Repository &repo = m_repositories.at(index);
    for (int i = 0; i < repo.scrapers.size(); ++i) {
        const Scraper &scraper = repo.scrapers.at(i);
        if (scraper.id != pluginId || scraper.enabled == enabled)
            continue;

        begin();
        if (!enabled || hasSnapshot(scraper.hash)) {
            auto updated = m_repositories;
            updated[index].scrapers[i].enabled = enabled;
            commit(std::move(updated));
            return;
        }
        if (!scraper.hash.isEmpty()) {
            finish(tr("The saved plugin is missing. Remove and add its repository to fetch it again."));
            return;
        }

        const QUrl script = repo.effectiveUrl.resolved(QUrl(scraper.filename));
        fetchBounded(script, MaxScript,
                     [this, index, i, script](const QByteArray &body, const QUrl &effectiveUrl, const QString &error) {
            if (!error.isEmpty() || !sameOrigin(script, effectiveUrl)) {
                finish(tr("Couldn't download the Nuvio plugin from its repository."));
                return;
            }
            const QByteArray hash = storeSnapshot(body);
            if (hash.isEmpty()) {
                finish(tr("Couldn't save the Nuvio plugin."));
                return;
            }
            auto updated = m_repositories;
            Scraper &scraper = updated[index].scrapers[i];
            scraper.hash = hash;
            scraper.enabled = true;
            commit(std::move(updated));
        });
        return;
    }
}
