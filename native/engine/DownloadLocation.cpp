#include "DownloadLocation.h"
#include "DownloadMediaPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;
using DownloadMediaPaths::fileDigest;
using DownloadMediaPaths::pathKey;

namespace {
QString tr(const char *text)
{
    return QCoreApplication::translate("DownloadLocation", text);
}

QJsonObject pathMap(const QHash<QString, QString> &paths)
{
    QJsonObject result;
    for (auto it = paths.cbegin(); it != paths.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QHash<QString, QString> readPathMap(const QJsonObject &object)
{
    QHash<QString, QString> result;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        result.insert(it.key(), it.value().toString());
    return result;
}

QString stagingPath(const DownloadLocation::Plan &plan)
{
    return QDir(plan.destinationRoot).filePath(u".colosseum-move-"_s + plan.id);
}

QString driveUnavailable()
{
    return tr("The download drive is unavailable. Reconnect it to continue.");
}

QString saveFailed()
{
    return tr("Colosseum could not save the download move. Try again.");
}
}

QVariantMap DownloadLocation::Plan::preview() const
{
    return {
        {u"currentPath"_s, QDir::toNativeSeparators(currentRoot)},
        {u"proposedPath"_s, QDir::toNativeSeparators(destinationRoot)},
        {u"items"_s, items}, {u"bytes"_s, bytes}, {u"collisions"_s, collisions},
        {u"error"_s, error}
    };
}

DownloadLocation::DownloadLocation(QString statePath)
    : m_statePath(std::move(statePath)), m_root(DownloadMediaPaths::defaultRoot())
{
    QFile file(m_statePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    m_root = object.value(u"root"_s).toString(m_root);
    m_marker = object.value(u"marker"_s).toString().toLatin1();
    m_aliases = readPathMap(object.value(u"aliases"_s).toObject());
    const auto knownLocations = object.value(u"knownLocations"_s).toObject();
    for (auto it = knownLocations.begin(); it != knownLocations.end(); ++it)
        m_knownLocations.insert(it.key(), it.value().toString().toLatin1());
    const QJsonObject pending = object.value(u"pending"_s).toObject();
    m_pending.id = pending.value(u"id"_s).toString();
    m_pending.currentRoot = pending.value(u"currentRoot"_s).toString();
    m_pending.destinationRoot = pending.value(u"destinationRoot"_s).toString();
    m_pending.marker = pending.value(u"marker"_s).toString().toLatin1();
    m_pending.device = QByteArray::fromHex(pending.value(u"device"_s).toString().toLatin1());
    m_pending.sourceMarker = pending.value(u"sourceMarker"_s).toString().toLatin1();
    m_pending.markerInstalled = pending.value(u"markerInstalled"_s).toBool();
    m_pending.paths = readPathMap(pending.value(u"paths"_s).toObject());
    m_pending.items = pending.value(u"items"_s).toInt();
    m_pending.collisions = pending.value(u"collisions"_s).toInt();
    m_committed = pending.value(u"committed"_s).toBool();
    for (const auto value : pending.value(u"files"_s).toArray()) {
        const QJsonObject row = value.toObject();
        const qint64 size = row.value(u"size"_s).toInteger();
        m_pending.files.append({row.value(u"source"_s).toString(),
                                row.value(u"destination"_s).toString(),
                                row.value(u"canonicalSource"_s).toString(), size,
                                QByteArray::fromHex(row.value(u"digest"_s).toString().toLatin1())});
        m_pending.bytes += size;
    }
}

bool DownloadLocation::committed() const
{
    return !m_pending.id.isEmpty() && m_committed;
}

bool DownloadLocation::save() const
{
    QJsonArray files;
    for (const File &file : m_pending.files) {
        files.append(QJsonObject{{u"source"_s, file.source}, {u"destination"_s, file.destination},
                                 {u"canonicalSource"_s, file.canonicalSource},
                                 {u"size"_s, file.size},
                                 {u"digest"_s, QString::fromLatin1(file.digest.toHex())}});
    }
    const QJsonObject pending{
        {u"id"_s, m_pending.id}, {u"currentRoot"_s, m_pending.currentRoot},
        {u"destinationRoot"_s, m_pending.destinationRoot}, {u"paths"_s, pathMap(m_pending.paths)},
        {u"marker"_s, QString::fromLatin1(m_pending.marker)},
        {u"device"_s, QString::fromLatin1(m_pending.device.toHex())},
        {u"sourceMarker"_s, QString::fromLatin1(m_pending.sourceMarker)},
        {u"markerInstalled"_s, m_pending.markerInstalled},
        {u"items"_s, m_pending.items}, {u"collisions"_s, m_pending.collisions},
        {u"files"_s, files}, {u"committed"_s, m_committed}
    };
    QJsonObject knownLocations;
    for (auto it = m_knownLocations.cbegin(); it != m_knownLocations.cend(); ++it)
        knownLocations.insert(it.key(), QString::fromLatin1(it.value()));
    const QJsonObject object{{u"root"_s, m_root}, {u"marker"_s, QString::fromLatin1(m_marker)},
                             {u"knownLocations"_s, knownLocations},
                             {u"aliases"_s, pathMap(m_aliases)}, {u"pending"_s, pending}};
    const QByteArray contents = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (!QDir().mkpath(QFileInfo(m_statePath).absolutePath()))
        return false;
    QSaveFile file(m_statePath);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.commit();
}

DownloadLocation::Plan DownloadLocation::plan(const QString &destinationRoot, QList<Item> items) const
{
    Plan result;
    result.currentRoot = m_root;
    result.destinationRoot = destinationRoot;
    const QFileInfo rootInfo(destinationRoot);
    const QStorageInfo storage(rootInfo.isDir() ? destinationRoot : rootInfo.absolutePath());
    result.device = storage.device();
    result.sourceMarker = m_marker;
    if (!QDir::isAbsolutePath(destinationRoot) || !QFileInfo(rootInfo.absolutePath()).isDir()
        || !DownloadMediaPaths::available(destinationRoot, {})) {
        result.error = tr("This folder is unavailable or read-only.");
    } else if (QFileInfo::exists(DownloadMediaPaths::markerPath(destinationRoot))
               && (m_marker.isEmpty() || !DownloadMediaPaths::available(destinationRoot, m_marker))) {
        result.error = tr("This folder is already used by a different Colosseum download library.");
    }

    std::stable_sort(items.begin(), items.end(), [](const Item &left, const Item &right) {
        return pathKey(left.source) < pathKey(right.source);
    });
    QSet<QString> occupied;
    QSet<QString> sources;
    QSet<QString> uniqueFiles;
    for (const Item &item : std::as_const(items)) {
        const QString sourceKey = pathKey(item.source);
        if (item.source.isEmpty() || sources.contains(sourceKey))
            continue;
        sources.insert(sourceKey);
        const bool folder = !item.members.isEmpty();
        const QString category = item.relativeDestination.section(QLatin1Char('/'), 0, 0);
        const bool stayingInPlace = pathKey(destinationRoot) == pathKey(m_root)
            && DownloadMediaPaths::isWithin(QDir(destinationRoot).filePath(category), item.source);
        const QString target = stayingInPlace ? item.source : DownloadMediaPaths::availablePath(
            destinationRoot, item.relativeDestination, folder, occupied, item.companionSuffixes);
        ++result.items;
        if (target.isEmpty() && result.error.isEmpty())
            result.error = tr("A destination folder conflicts with an existing file.");
        if (!target.isEmpty() && !stayingInPlace) {
            const QString requested = QDir(destinationRoot).filePath(item.relativeDestination);
            if (pathKey(target) != pathKey(requested))
                ++result.collisions;
            occupied.insert(pathKey(target).toCaseFolded());
            result.paths.insert(sourceKey, target);
            for (const QString &suffix : item.companionSuffixes)
                occupied.insert(pathKey(target + suffix).toCaseFolded());
        }
        QStringList members = folder ? item.members : QStringList{QString()};
        if (!folder)
            members.append(item.companionSuffixes);
        for (const QString &member : members) {
            const QString source = folder ? QDir::cleanPath(QDir(item.source).filePath(member)) : item.source + member;
            const QFileInfo sourceInfo(source);
            const QString fileKey = pathKey(source);
            if (uniqueFiles.contains(fileKey))
                continue;
            uniqueFiles.insert(fileKey);
            if ((folder && !DownloadMediaPaths::destinationParentsSafe(item.source, source))
                || DownloadMediaPaths::waitingForDrive(source)
                || !sourceInfo.isFile() || sourceInfo.isSymLink()) {
                if (result.error.isEmpty())
                    result.error = tr("A managed download is unavailable. Check the source drive and try again.");
                continue;
            }
            const QString destination = folder ? QDir::cleanPath(QDir(target).filePath(member)) : target + member;
            result.bytes += sourceInfo.size();
            if (!stayingInPlace) {
                result.paths.insert(fileKey, destination);
                result.files.append({source, destination, sourceInfo.canonicalFilePath(), sourceInfo.size(), {}});
            }
        }
    }
    qint64 required = 0;
    for (const File &file : std::as_const(result.files))
        required += file.size;
    if (result.error.isEmpty() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < required)
        result.error = tr("There is not enough free space to verify the files before moving them.");
    if (result.error.isEmpty() && result.files.isEmpty() && pathKey(m_root) == pathKey(destinationRoot))
        result.error = tr("Colosseum downloads already use this folder.");
    return result;
}

QString DownloadLocation::begin(Plan plan)
{
    if (!m_pending.id.isEmpty())
        return tr("A download move is already pending.");
    if (!plan.error.isEmpty() || plan.destinationRoot.isEmpty())
        return plan.error.isEmpty() ? driveUnavailable() : plan.error;
    plan.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plan.marker = m_marker.isEmpty() ? plan.id.toLatin1() : m_marker;
    m_pending = std::move(plan);
    if (save())
        return {};
    m_pending = {};
    return saveFailed();
}

bool DownloadLocation::destinationReady() const
{
    return DownloadMediaPaths::available(m_pending.destinationRoot, m_pending.marker);
}

QString DownloadLocation::copy()
{
    if (m_pending.id.isEmpty() || m_committed)
        return {};
    const QString root = m_pending.destinationRoot;
    const QString marker = DownloadMediaPaths::markerPath(root);
    if (!destinationReady()) {
        const QFileInfo rootInfo(root);
        const QStorageInfo storage(rootInfo.isDir() ? root : rootInfo.absolutePath());
        if (m_pending.markerInstalled || storage.device() != m_pending.device
            || !DownloadMediaPaths::available(root, {}) || QFileInfo::exists(marker)
            || !QFileInfo(QFileInfo(root).absolutePath()).isDir() || !QDir().mkpath(root)) {
            return driveUnavailable();
        }
        QFile file(marker);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || file.write(m_pending.marker) != m_pending.marker.size() || !file.flush())
            return driveUnavailable();
        file.close();
    }
    if (!m_pending.markerInstalled) {
        m_pending.markerInstalled = true;
        if (!save())
            return saveFailed();
    }
    const QString stage = stagingPath(m_pending);
    if (!DownloadMediaPaths::destinationParentsSafe(root, stage + u"/placeholder"_s)
        || !QDir().mkpath(stage)) {
        return tr("Colosseum could not create a temporary folder for the move.");
    }
    for (qsizetype i = 0; i < m_pending.files.size(); ++i) {
        File &file = m_pending.files[i];
        if (!destinationReady())
            return driveUnavailable();
        const QString scratch = QDir(stage).filePath(QString::number(i));
        if (!DownloadMediaPaths::destinationParentsSafe(root, file.destination))
            return tr("A destination folder changed after the preview.");
        if (QFileInfo::exists(file.destination)) {
            if (file.digest.isEmpty() || QFileInfo::exists(scratch)
                || fileDigest(file.destination, file.size) != file.digest) {
                return tr("A file appeared at the destination after the preview.");
            }
            continue;
        }
        if (DownloadMediaPaths::waitingForDrive(file.source))
            return driveUnavailable();
        if (pathKey(QFileInfo(file.source).canonicalFilePath()) != pathKey(file.canonicalSource))
            return tr("A source path changed after the preview.");
        const QByteArray sourceDigest = fileDigest(file.source, file.size);
        if (sourceDigest.isEmpty() || (!file.digest.isEmpty() && file.digest != sourceDigest))
            return tr("A source file changed or became unavailable during the move.");
        if (QFileInfo::exists(scratch) && fileDigest(scratch, file.size) != sourceDigest && !QFile::remove(scratch))
            return tr("Colosseum could not retry the temporary copy.");
        if (!QFileInfo::exists(scratch) && !QFile::copy(file.source, scratch))
            return tr("Colosseum could not copy a download. Check the drive and available space.");
        if (fileDigest(scratch, file.size) != sourceDigest)
            return tr("Colosseum could not verify the copied download.");
        const QDateTime modified = QFileInfo(file.source).lastModified();
        QFile copied(scratch);
        if (copied.open(QIODevice::ReadWrite)) {
            copied.setFileTime(modified, QFileDevice::FileModificationTime);
            copied.close();
        }
        file.digest = sourceDigest;
    }

    if (!save())
        return saveFailed();

    // All digests are now durable. A restart can distinguish a previously
    // published destination from a new conflict without risking the sources.
    for (qsizetype i = 0; i < m_pending.files.size(); ++i) {
        if (!destinationReady())
            return driveUnavailable();
        const File &file = m_pending.files.at(i);
        const QString scratch = QDir(stage).filePath(QString::number(i));
        if (!QFileInfo::exists(scratch) && fileDigest(file.destination, file.size) == file.digest)
            continue;
        if (!QDir().mkpath(QFileInfo(file.destination).absolutePath())
            || !DownloadMediaPaths::destinationParentsSafe(root, file.destination)
            || !QFile::rename(scratch, file.destination)) {
            return tr("Colosseum could not publish a copied download. The source file is still in place.");
        }
    }
    return {};
}

QString DownloadLocation::commit()
{
    if (m_pending.id.isEmpty() || m_committed)
        return {};
    if (!destinationReady())
        return driveUnavailable();
    for (const File &file : std::as_const(m_pending.files)) {
        if (file.digest.isEmpty() || fileDigest(file.destination, file.size) != file.digest) {
            return tr("Some downloads have not been verified at the destination.");
        }
    }
    const QString previousRoot = m_root;
    const QByteArray previousMarker = m_marker;
    const auto previousAliases = m_aliases;
    const auto previousLocations = m_knownLocations;
    for (auto it = m_aliases.begin(); it != m_aliases.end(); ++it)
        it.value() = m_pending.paths.value(pathKey(it.value()), it.value());
    for (auto it = m_pending.paths.cbegin(); it != m_pending.paths.cend(); ++it)
        m_aliases.insert(it.key(), it.value());
    m_root = m_pending.destinationRoot;
    m_marker = m_pending.marker;
    if (!previousMarker.isEmpty())
        m_knownLocations.insert(previousRoot, previousMarker);
    m_knownLocations.insert(m_root, m_marker);
    m_committed = true;
    if (save())
        return {};
    m_root = previousRoot;
    m_marker = previousMarker;
    m_aliases = previousAliases;
    m_knownLocations = previousLocations;
    m_committed = false;
    return saveFailed();
}

QString DownloadLocation::cleanSources()
{
    if (!committed())
        return {};
    if (!destinationReady())
        return driveUnavailable();
    for (const File &file : std::as_const(m_pending.files)) {
        if (!m_pending.sourceMarker.isEmpty()
            && DownloadMediaPaths::isWithin(m_pending.currentRoot, file.source)
            && !DownloadMediaPaths::available(m_pending.currentRoot, m_pending.sourceMarker)) {
            return tr("Reconnect the original download drive to finish the move.");
        }
        if (!QFileInfo::exists(file.source))
            continue;
        // A modified or replaced source now belongs to its current owner. Leave it alone.
        if (pathKey(QFileInfo(file.source).canonicalFilePath()) != pathKey(file.canonicalSource)
            || fileDigest(file.source, file.size) != file.digest)
            continue;
        if (fileDigest(file.destination, file.size) != file.digest)
            return tr("A moved download is unavailable. Colosseum kept the source file.");
        if (!QFile::remove(file.source))
            return tr("Colosseum could not remove a source file. Close anything using it and retry.");
    }
    QDir().rmdir(stagingPath(m_pending));
    const Plan previous = m_pending;
    m_pending = {};
    m_committed = false;
    if (save())
        return {};
    m_pending = previous;
    m_committed = true;
    return saveFailed();
}
