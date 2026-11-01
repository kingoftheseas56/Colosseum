#include "../../native/devtools/CollectionsTestProfileController.h"
#include "../../native/engine/VaultIdentity.h"
#include "../../native/engine/VaultIndex.h"
#include "../../native/engine/VaultConfig.h"
#include "../../native/engine/VaultLocation.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>

namespace {
int fail(const QString &message)
{
    QTextStream(stderr) << "vault_seed: " << message << '\n';
    return 2;
}
QString absoluteClean(const QString &path)
{
    const QString value = QDir::fromNativeSeparators(path);
    return QFileInfo(value).isAbsolute() && QDir::cleanPath(value) == value ? value : QString();
}
bool inside(const QString &path, const QString &root)
{
#ifdef Q_OS_WIN
    return path.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive);
#else
    return path.startsWith(root + QLatin1Char('/'));
#endif
}
bool readIdentity(const QString &path, QJsonObject *object)
{
    const QFileInfo info(path);
    if (!info.exists()) { *object = {}; return !info.isSymLink(); }
    QFile file(path);
    if (!info.isFile() || info.isSymLink() || info.size() > 8 * 1024 * 1024
        || !file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(8 * 1024 * 1024 + 1), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()
        || doc.object().value(QStringLiteral("version")).toDouble(-1) != 1
        || !doc.object().value(QStringLiteral("entries")).isArray()) return false;
    const QJsonObject parsed = doc.object();
    const QStringList fields{QStringLiteral("version"), QStringLiteral("entries"), QStringLiteral("aliases"),
        QStringLiteral("pathAliases"), QStringLiteral("ceremonyDecisions"), QStringLiteral("pendingCeremonies")};
    if (parsed.size() != fields.size()) return false;
    for (const QString &name : fields) {
        if (!parsed.contains(name)) return false;
        if (name != QLatin1String("version") && !parsed.value(name).isArray()) return false;
    }
    *object = parsed;
    return true;
}
// Inspect an existing index read-only before constructing its schema owner.
bool emptyIndex(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) return !info.isSymLink();
    if (!info.isFile() || info.isSymLink() || info.size() > 16 * 1024 * 1024) return false;
    const QString connection = QStringLiteral("vault_seed_readonly_preflight");
    bool empty = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery tables(db);
            QStringList names;
            if (tables.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"))) {
                while (tables.next()) names.append(tables.value(0).toString());
                if (names.isEmpty()) empty = true;
                else if (names == QStringList{QStringLiteral("files")}) {
                    QSqlQuery count(db);
                    empty = count.exec(QStringLiteral("SELECT COUNT(*) FROM files")) && count.next() && count.value(0).toLongLong() == 0;
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return empty;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vault_seed"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Test-only synthetic AVI fixture with an explicitly adopted Arrival IMDb identifier. The video is not Arrival content. Run with the fixture app stopped."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("root"), QStringLiteral("Existing marked and tagged synthetic app-data root."), QStringLiteral("absolute-path")});
    parser.addOption({QStringLiteral("video"), QStringLiteral("Existing tiny synthetic AVI in a subfolder beneath that root."), QStringLiteral("absolute-path")});
    parser.process(app);
    if (!parser.positionalArguments().isEmpty()) return fail(QStringLiteral("Positional arguments are not accepted."));
    const QString root = absoluteClean(parser.value(QStringLiteral("root")));
    const QString requested = absoluteClean(parser.value(QStringLiteral("video")));
    if (root.isEmpty() || requested.isEmpty()) return fail(QStringLiteral("Both options must be explicit absolute, clean paths."));
    CollectionsTestProfileController containment(root, {}, {});
    if (!containment.enabled()) return fail(QStringLiteral("Refusing an unmarked, untagged, linked, or non-fixture root."));
    const QFileInfo input(requested);
    const QString video = input.canonicalFilePath();
    const QString group = input.canonicalPath();
    if (!input.isFile() || input.isSymLink() || !inside(video, root) || !inside(group, root)
        || input.suffix().compare(QLatin1String("avi"), Qt::CaseInsensitive) != 0
        || input.size() < 64 || input.size() > 16 * 1024 * 1024)
        return fail(QStringLiteral("Video must be an existing AVI of 64 bytes–16 MiB in a fixture subfolder."));
    QFile file(video);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Synthetic AVI is unreadable."));
    const QByteArray header = file.read(12);
    file.close();
    if (!header.startsWith("RIFF") || header.mid(8, 4) != "AVI ")
        return fail(QStringLiteral("The synthetic video has no RIFF/AVI header."));

    QLockFile lock(root + QStringLiteral("/.collections-vault-seeding.lock"));
    if (!lock.tryLock(0)) return fail(QStringLiteral("Another Vault fixture seed operation owns this root."));
    const QString vault = root + QStringLiteral("/vault");
    const QString identityPath = vault + QStringLiteral("/identity.json");
    const QString indexPath = vault + QStringLiteral("/index-v1.sqlite");
    if (!emptyIndex(indexPath)) return fail(QStringLiteral("Existing Vault index is nonempty, unreadable, or not the expected index schema."));
    QJsonObject before;
    if (!readIdentity(identityPath, &before)) return fail(QStringLiteral("Existing Vault identity data is malformed or unsupported; it has not been opened for writing."));
    if (QFileInfo::exists(identityPath + QStringLiteral(".tmp")))
        return fail(QStringLiteral("An unfinished identity write must be preserved."));
    if (before.isEmpty() && (QFileInfo::exists(identityPath + QStringLiteral(".bak"))
        || QFileInfo::exists(identityPath + QStringLiteral(".tmp"))))
        return fail(QStringLiteral("Orphaned Vault identity recovery files require review; they will not be replaced."));
    QHash<QString, QJsonObject> previousEntries;
    for (const QJsonValue &value : before.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject row = value.toObject();
        const QString id = row.value(QStringLiteral("id")).toString();
        if (!value.isObject() || row.size() != 4 || id.isEmpty() || previousEntries.contains(id)
            || !row.value(QStringLiteral("id")).isString() || !row.value(QStringLiteral("path")).isString()
            || !row.value(QStringLiteral("size")).isDouble() || !row.value(QStringLiteral("mtimeMs")).isDouble()
            || !inside(row.value(QStringLiteral("path")).toString(), root))
            return fail(QStringLiteral("Existing identities are not a well-formed fixture-local set."));
        previousEntries.insert(id, row);
    }
    if (!containment.enabled() || !QDir().mkpath(vault)) return fail(QStringLiteral("Fixture containment changed or Vault directory is not writable."));
    const QFileInfo facts(video);
    const QString expectedId = VaultIdentity::computeId(video, facts.size(), facts.lastModified().toMSecsSinceEpoch());
    if (previousEntries.contains(expectedId)) return fail(QStringLiteral("This fixture video already has a Vault identity; existing fixture data is never reseeded."));
    VaultIndex index(indexPath);
    if (!index.isOpen() || index.itemCount() != 0) return fail(QStringLiteral("Real Vault index owner is unavailable or nonempty."));
    VaultIdentity identity(vault);
    if (!identity.pendingCeremonies().isEmpty()) return fail(QStringLiteral("Existing identity decisions must remain untouched."));
    const QString id = identity.idForFile(video, facts.size(), facts.lastModified().toMSecsSinceEpoch());
    if (id.isEmpty() || id != expectedId || !identity.pendingCeremonies().isEmpty())
        return fail(QStringLiteral("Real Vault owner did not admit exactly this new fixture file."));
    QJsonObject after;
    if (!readIdentity(identityPath, &after)) return fail(QStringLiteral("Updated identity file could not be verified."));
    QHash<QString, QJsonObject> actualEntries;
    for (const QJsonValue &value : after.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject row = value.toObject();
        actualEntries.insert(row.value(QStringLiteral("id")).toString(), row);
    }
    if (actualEntries.size() != previousEntries.size() + 1 || !actualEntries.contains(id))
        return fail(QStringLiteral("Identity registration changed more than the one task video."));
    for (auto it = previousEntries.cbegin(); it != previousEntries.cend(); ++it)
        if (actualEntries.value(it.key()) != it.value()) return fail(QStringLiteral("An earlier fixture identity changed unexpectedly."));
    for (auto it = before.constBegin(); it != before.constEnd(); ++it)
        if (it.key() != QLatin1String("entries") && after.value(it.key()) != it.value())
            return fail(QStringLiteral("Existing identity aliases or decision metadata changed unexpectedly."));

    // A rebuildable index row without its owning configured root is orphaned;
    // opening the real Vault can legitimately republish and drop it.
    const QString folder = QFileInfo(group).absolutePath();
    const QString configPath = vault + QStringLiteral("/config.json");
    if (QFileInfo::exists(configPath)) {
        QFile configFile(configPath);
        if (QFileInfo(configPath).isSymLink() || !configFile.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("Existing fixture config is not safely readable."));
        const auto configDoc = QJsonDocument::fromJson(configFile.readAll());
        if (!configDoc.isObject() || configDoc.object().value(QStringLiteral("version")).toInt() != 1
            || !configDoc.object().value(QStringLiteral("roots")).isArray())
            return fail(QStringLiteral("Existing malformed fixture config must be preserved."));
    }
    VaultConfig config(vault);
    config.addRoot(folder, 1793232000000LL);
    config.confirmRoot(folder);
    VaultConfig configReload(vault);
    if (!configReload.isRootConfirmed(folder))
        return fail(QStringLiteral("Fixture root registration did not persist."));
    const QString label = QStringLiteral("Arrival (2016) · synthetic video fixture");
    VaultIndex::FileRow row;
    row.id = id;
    row.rootPath = VaultLocation::normalize(folder);
    row.subtreePath = group;
    row.groupKey = group;
    row.groupTitle = label;
    row.kind = QStringLiteral("video");
    row.path = video;
    row.displayTitle = QStringLiteral("Synthetic AVI for Collections UI verification");
    row.realName = facts.fileName();
    row.size = facts.size();
    row.mtimeMs = facts.lastModified().toMSecsSinceEpoch();
    row.format = QStringLiteral("AVI");
    row.identitySource = QStringLiteral("IMDB");
    row.identityId = QStringLiteral("imdb:tt2543164");
    row.identityTitle = label;
    row.identityWorld = QStringLiteral("Theatre");
    row.identityYear = 2016;
    row.identityState = QStringLiteral("adopted");
    row.identitySynopsis = QStringLiteral("Synthetic test video carrying an explicitly adopted IMDb identifier for route verification. This file is not the film Arrival.");
    // No fabricated media-admission or duration facts: actual app probes retain
    // their normal authority. This fixture establishes only the identified UI row.
    const QFileInfo now(video);
    if (!containment.enabled() || now.size() != row.size || now.lastModified().toMSecsSinceEpoch() != row.mtimeMs
        || index.itemCount() != 0 || !index.upsert(row))
        return fail(QStringLiteral("Fixture changed or the real Vault upsert failed."));
    VaultIndex reopened(indexPath);
    const auto rows = reopened.rowsForGroup(group);
    VaultIdentity restarted(vault);
    if (!reopened.isOpen() || reopened.itemCount() != 1 || rows.size() != 1
        || rows.first().id != id || rows.first().path != video || rows.first().identitySource != QLatin1String("IMDB")
        || rows.first().identityId != row.identityId || rows.first().identityTitle != label
        || rows.first().identityWorld != QLatin1String("Theatre") || rows.first().identityYear != 2016 || !restarted.knows(id))
        return fail(QStringLiteral("Typed fixture index/identity did not survive real owner reloads."));
    const QJsonObject reference{{QStringLiteral("world"), QStringLiteral("vault")}, {QStringLiteral("kind"), QStringLiteral("film")},
        {QStringLiteral("source"), QStringLiteral("imdb")}, {QStringLiteral("sourceId"), row.identityId},
        {QStringLiteral("editionId"), QString()}, {QStringLiteral("title"), label}, {QStringLiteral("cover"), QString()},
        {QStringLiteral("creator"), QString()}, {QStringLiteral("year"), QStringLiteral("2016")},
        {QStringLiteral("route"), QStringLiteral("vault-film-detail")}, {QStringLiteral("payload"), QJsonObject{{QStringLiteral("vaultKey"), group}}}};
    QTextStream(stderr) << "vault_seed: one synthetic AVI fixture indexed using real owners; prior Vinyl identities preserved. Adopted identifier imdb:tt2543164 is for UI testing, not a claim about file content. Group: " << group << '\n';
    const QJsonObject output{{QStringLiteral("collections"), QJsonArray{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("Vault film fixture")}, {QStringLiteral("references"), QJsonArray{reference}}}}}};
    QTextStream(stdout) << QJsonDocument(output).toJson(QJsonDocument::Indented);
    return 0;
}
