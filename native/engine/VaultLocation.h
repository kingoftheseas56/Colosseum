#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QString>

// Vault location identity is broader than a filesystem path on Android. SAF and
// MediaStore hand back opaque content:// URIs whose spelling is part of the
// provider contract: QDir::cleanPath would collapse "//" and Windows-style
// case-folding would corrupt escaped/document identifiers.
namespace VaultLocation {

inline bool isContentUri(const QString& value)
{
    return value.startsWith(QLatin1String("content://"), Qt::CaseInsensitive);
}

inline QString normalize(const QString& value)
{
    if (isContentUri(value))
        return value;

    QString normalized = QDir::cleanPath(value);
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return normalized;
}

inline bool containsPath(const QString& root, const QString& path)
{
    return !root.isEmpty() && (path == root || path.startsWith(
        root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/')));
}

struct PersonalPaths {
    QSet<QString> files;
    QSet<QString> directories;

    bool isEmpty() const { return files.isEmpty() && directories.isEmpty(); }
    bool operator==(const PersonalPaths& other) const
    { return files == other.files && directories == other.directories; }
    bool operator!=(const PersonalPaths& other) const { return !(*this == other); }

    bool excludes(const QString& path, bool directory = false) const
    {
        if (isEmpty() || isContentUri(path) || path.isEmpty())
            return false;
        QString ancestor = normalize(path);
        if (!directory && files.contains(ancestor))
            return true;
        while (!ancestor.isEmpty()) {
            if (directories.contains(ancestor))
                return true;
            const qsizetype slash = ancestor.lastIndexOf(QLatin1Char('/'));
            if (slash < 0 || slash == ancestor.size() - 1)
                break;
#ifdef Q_OS_WIN
            ancestor.truncate(slash == 2 && ancestor.at(1) == QLatin1Char(':') ? 3 : slash);
#else
            ancestor.truncate(slash == 0 ? 1 : slash);
#endif
        }
        return false;
    }

    // Legacy Vault rows and other media owners may have reached a file through a link.
    // Scanner traversal uses excludes() on paths it has already checked for links.
    bool excludesPhysical(const QString& path) const
    {
        return excludes(path) || (!isEmpty() && !path.isEmpty() && !isContentUri(path)
            && excludes(QFileInfo(path).canonicalFilePath()));
    }
};

} // namespace VaultLocation
