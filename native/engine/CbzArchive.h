#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace MangaTankoban {

struct CbzPageEntry
{
    QString name;
    quint64 uncompressedBytes = 0;
};

class CbzArchive
{
public:
    static QVector<CbzPageEntry> imageEntries(const QString& archivePath,
                                               QString* error = nullptr);
    static QByteArray readEntry(const QString& archivePath,
                                const QString& entryName,
                                QString* error = nullptr);
    static bool writeImagesAtomic(const QString& archivePath,
                                  const QString& sourceDir,
                                  const QStringList& orderedRelativeFiles,
                                  QString* error = nullptr);
};

} // namespace MangaTankoban

