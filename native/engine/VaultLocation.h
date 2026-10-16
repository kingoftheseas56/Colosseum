#pragma once

#include <QDir>
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

} // namespace VaultLocation
