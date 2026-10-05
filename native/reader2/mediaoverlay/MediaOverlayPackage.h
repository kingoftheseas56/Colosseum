#pragma once
#include "reader2/mediaoverlay/MediaOverlayDocument.h"
#include <QMap>
namespace MediaOverlay {
struct PackageItem {
    QString id;
    QString path;
    QString mediaType;
    QString overlayId;
};
struct SpineItem {
    QString id;
    QString path;
    QString overlayPath; // Empty when absent or invalid.
};
struct PackageResult {
    bool ok = false;
    QString error;
    qint64 errorLine = 0;
    QMap<QString,PackageItem> manifest;
    QMap<QString,QString> overlays; // Canonical content path -> canonical overlay path.
    QVector<SpineItem> spine;
    std::optional<qint64> durationMs;
    QMap<QString,qint64> overlayDurations; // Manifest overlay id -> milliseconds.
    QString activeClass;
    QString playbackActiveClass;
    QVector<Warning> warnings;
};
struct MediaOverlayPackage {
    static PackageResult parse(const QByteArray &xml, const QString &packagePath);
    // baseFile is already decoded/canonical; href is a URI reference decoded exactly once.
    // No external URLs, queries, fragments, absolute paths, backslashes or root escapes.
    static std::optional<QString> resolvePath(const QString &baseFile, const QString &href);
    // Resource APIs accept canonical archive names, not encoded URI references.
    static std::optional<QString> containerPath(const QString &path);
};
}
