#pragma once

#include "update/UpdateVersion.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QUrl>
#include <QStringList>

#include <optional>

namespace Colosseum::Update {

enum class HighlightKind { Feature, Statistic, BeforeAfter, Milestone };

struct Artwork {
    QString assetName;
    QByteArray sha256;
};

struct Highlight {
    HighlightKind kind = HighlightKind::Feature;
    QString section;
    QString title;
    QString body;
    QString value;
    QString context;
    QString beforeCaption;
    QString afterCaption;
    QStringList artworkAssets;
};

struct PlatformDownload {
    QString assetName;
    qint64 size = 0;
    QByteArray sha256;
};

struct Manifest {
    int schemaVersion = 0;
    Version version;
    // Optional signed release ordering across editions of one displayed
    // version (no-account 1.1.9 -> account 1.1.9). 0 = legacy manifest,
    // ordered by version alone. When present it is a positive integer that
    // increases with every published release, whatever the displayed version.
    int sequence = 0;
    QString tag;
    QString eyebrow;
    QString title;
    QString summary;
    QString installerAsset;
    qint64 installerSize = 0;
    QByteArray installerSha256;
    QHash<QString, PlatformDownload> downloads;
    Version minimumUpdaterVersion;
    QString notesUrl;
    QList<Highlight> highlights;
    QList<Artwork> artwork;
};

std::optional<Manifest> parseManifest(const QByteArray& verifiedUtf8, QString* error);

// Package names and routes are derived from signed metadata and the official
// release repository. The unsigned feed cannot redirect the browser elsewhere.
QUrl platformDownloadUrl(const Manifest& manifest, const PlatformDownload& download);

} // namespace Colosseum::Update
