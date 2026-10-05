#pragma once
#include "reader2/mediaoverlay/CueTimeline.h"
#include "reader2/mediaoverlay/MediaOverlayPackage.h"
namespace MediaOverlay {
enum class Error {
    None, FileMissing, NotZip, ContainerMissing, ContainerMalformed,
    PackageMissing, PackageMalformed, UnsafePath, ResourceMissing,
    ResourceTooLarge, ReadFailed, WriteFailed
};
QString errorName(Error error);
struct AudioResource { QString path; quint64 size = 0; };
struct LoadResult {
    bool ok = false;
    Error error = Error::None;
    QString message;
    bool hasOverlays = false;
    CueTimeline timeline;
    QVector<AudioResource> audioResources;
    std::optional<qint64> durationMs;
    QMap<QString,qint64> overlayDurations;
    QString activeClass;
    QString playbackActiveClass;
    QVector<Warning> warnings;
};
struct ResourceResult {
    bool ok = false;
    Error error = Error::None;
    QString message;
    QByteArray bytes;
};
struct EpubOverlayLoader {
    static LoadResult load(const QString &epubPath);
    // containerPath is a decoded, container-root-relative archive name (not a URL).
    // Reads are bounded to 256 MiB per resource; XML reads to 8 MiB.
    static ResourceResult readResource(const QString &epubPath, const QString &containerPath);
    // Writes ONLY the explicit destination, atomically. Never bulk-unpacks archive paths.
    static ResourceResult extractResource(const QString &epubPath, const QString &containerPath, const QString &destinationFilePath);
};
}
