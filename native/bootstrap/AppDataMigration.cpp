#include "bootstrap/AppDataMigration.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

QString cleanAbsolute(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString& left, const QString& right)
{
    const QString a = cleanAbsolute(left);
    const QString b = cleanAbsolute(right);
    if (a.isEmpty() || b.isEmpty())
        return false;
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

QString migrationStamp()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ"));
}

QString migrationLogPath(const QString& currentRoot)
{
    return QDir(currentRoot).filePath(QStringLiteral("migration/appdata-org-v1.log"));
}

QString uniquePath(QString path)
{
    if (!QFileInfo::exists(path))
        return path;
    for (int suffix = 1; ; ++suffix) {
        const QString candidate = path + QLatin1Char('.') + QString::number(suffix);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
}

void appendLog(AppDataMigrationResult* result, const QString& message)
{
    if (result->logPath.isEmpty())
        return;
    QDir().mkpath(QFileInfo(result->logPath).absolutePath());
    QFile file(result->logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
                            + QByteArrayLiteral(" ") + message.toUtf8() + QByteArrayLiteral("\n");
    file.write(line);
    file.flush();
}

void fail(AppDataMigrationResult* result, const QString& message)
{
    result->complete = false;
    if (result->error.isEmpty())
        result->error = message;
    appendLog(result, QStringLiteral("ERROR ") + message);
}

QByteArray fileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
        hash.addData(file.read(1024 * 1024));
    return hash.result();
}

bool sameFileContent(const QString& left, const QString& right)
{
    const QFileInfo a(left);
    const QFileInfo b(right);
    if (!a.isFile() || !b.isFile() || a.size() != b.size())
        return false;
    const QByteArray leftHash = fileSha256(left);
    const QByteArray rightHash = fileSha256(right);
    return !leftHash.isEmpty() && !rightHash.isEmpty() && leftHash == rightHash;
}

bool moveDirectoryContents(const QString& source,
                           const QString& target,
                           const QString& relative,
                           const QString& currentRoot,
                           AppDataMigrationResult* result);

bool moveFile(const QString& source,
              const QString& target,
              AppDataMigrationResult* result)
{
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        fail(result, QStringLiteral("target_parent_unavailable: ") + target);
        return false;
    }
    if (QFile::rename(source, target)) {
        ++result->movedEntries;
        appendLog(result, QStringLiteral("MOVE ") + source + QStringLiteral(" -> ") + target);
        return true;
    }
    if (!QFile::copy(source, target)) {
        fail(result, QStringLiteral("file_copy_failed: ") + source);
        return false;
    }
    if (!QFile::remove(source)) {
        QFile::remove(target);
        fail(result, QStringLiteral("source_cleanup_failed: ") + source);
        return false;
    }
    ++result->movedEntries;
    appendLog(result, QStringLiteral("COPY_MOVE ") + source + QStringLiteral(" -> ") + target);
    return true;
}

QString ensureConflictRoot(const QString& currentRoot, AppDataMigrationResult* result)
{
    if (!result->conflictRoot.isEmpty())
        return result->conflictRoot;
    const QString base = QDir(currentRoot).filePath(
        QStringLiteral("migration-conflicts/appdata-org-v1-") + migrationStamp());
    result->conflictRoot = uniquePath(base);
    if (!QDir().mkpath(result->conflictRoot)) {
        fail(result, QStringLiteral("conflict_root_unavailable: ") + result->conflictRoot);
        result->conflictRoot.clear();
    }
    return result->conflictRoot;
}

bool moveAbsentEntry(const QString& source,
                     const QString& target,
                     const QString& relative,
                     const QString& currentRoot,
                     AppDataMigrationResult* result)
{
    const QFileInfo info(source);
    if ((!info.exists() && !info.isSymLink()))
        return true;

    if (info.isSymLink() || !info.isDir())
        return moveFile(source, target, result);

    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        fail(result, QStringLiteral("target_parent_unavailable: ") + target);
        return false;
    }
    if (QDir().rename(source, target)) {
        ++result->movedEntries;
        appendLog(result, QStringLiteral("MOVE_DIR ") + source + QStringLiteral(" -> ") + target);
        return true;
    }
    if (!QDir().mkpath(target)) {
        fail(result, QStringLiteral("directory_create_failed: ") + target);
        return false;
    }
    return moveDirectoryContents(source, target, relative, currentRoot, result);
}

bool quarantineEntry(const QString& source,
                     const QString& relative,
                     const QString& currentRoot,
                     AppDataMigrationResult* result)
{
    const QString conflictRoot = ensureConflictRoot(currentRoot, result);
    if (conflictRoot.isEmpty())
        return false;
    const QString destination = uniquePath(QDir(conflictRoot).filePath(relative));
    if (!moveAbsentEntry(source, destination, relative, currentRoot, result))
        return false;
    ++result->conflicts;
    appendLog(result, QStringLiteral("CONFLICT ") + relative + QStringLiteral(" -> ") + destination);
    return true;
}

bool moveDirectoryContents(const QString& source,
                           const QString& target,
                           const QString& relative,
                           const QString& currentRoot,
                           AppDataMigrationResult* result)
{
    QDir sourceDir(source);
    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
        QDir::Name | QDir::DirsFirst);

    for (const QFileInfo& sourceInfo : entries) {
        const QString name = sourceInfo.fileName();
        const QString sourcePath = sourceInfo.absoluteFilePath();
        const QString targetPath = QDir(target).filePath(name);
        const QString childRelative = relative.isEmpty()
                                          ? name
                                          : relative + QLatin1Char('/') + name;
        const QFileInfo targetInfo(targetPath);
        const bool targetExists = targetInfo.exists() || targetInfo.isSymLink();

        if (!targetExists) {
            if (!moveAbsentEntry(sourcePath, targetPath, childRelative, currentRoot, result))
                return false;
            continue;
        }

        if (!sourceInfo.isSymLink() && sourceInfo.isDir()
            && !targetInfo.isSymLink() && targetInfo.isDir()) {
            if (!moveDirectoryContents(sourcePath, targetPath, childRelative, currentRoot, result))
                return false;
            continue;
        }

        if (!sourceInfo.isSymLink() && sourceInfo.isFile()
            && !targetInfo.isSymLink() && targetInfo.isFile()
            && sameFileContent(sourcePath, targetPath)) {
            if (!QFile::remove(sourcePath)) {
                fail(result, QStringLiteral("duplicate_source_cleanup_failed: ") + sourcePath);
                return false;
            }
            appendLog(result, QStringLiteral("DEDUP ") + childRelative);
            continue;
        }

        if (!quarantineEntry(sourcePath, childRelative, currentRoot, result))
            return false;
    }

    const QFileInfoList remaining = sourceDir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    if (remaining.isEmpty() && !QDir().rmdir(source)) {
        fail(result, QStringLiteral("legacy_directory_cleanup_failed: ") + source);
        return false;
    }
    return true;
}

} // namespace
AppDataMigrationResult reconcileAppData(const QString& legacyRoot,
                                        const QString& currentRoot)
{
    AppDataMigrationResult result;
    const QString legacy = cleanAbsolute(legacyRoot);
    const QString current = cleanAbsolute(currentRoot);
    if (legacy.isEmpty() || current.isEmpty() || samePath(legacy, current))
        return result;

    result.logPath = migrationLogPath(current);

    const QFileInfo legacyInfo(legacy);
    if (!legacyInfo.exists()) {
        appendLog(&result, QStringLiteral("NOOP legacy root absent: ") + legacy);
        return result;
    }
    if (!legacyInfo.isDir() || legacyInfo.isSymLink()) {
        fail(&result, QStringLiteral("legacy_root_invalid: ") + legacy);
        appendLog(&result, QStringLiteral("FAIL ") + result.error);
        return result;
    }

    if (!QDir().mkpath(current)) {
        fail(&result, QStringLiteral("current_root_unavailable: ") + current);
        appendLog(&result, QStringLiteral("FAIL ") + result.error);
        return result;
    }

    appendLog(&result, QStringLiteral("BEGIN legacy=") + legacy
                       + QStringLiteral(" current=") + current);
    if (!moveDirectoryContents(legacy, current, QString(), current, &result)) {
        appendLog(&result, QStringLiteral("FAIL ") + result.error);
        return result;
    }

    result.complete = true;
    appendLog(&result, QStringLiteral("DONE moved=") + QString::number(result.movedEntries)
                       + QStringLiteral(" conflicts=") + QString::number(result.conflicts));
    return result;
}
