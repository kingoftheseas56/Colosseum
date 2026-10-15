#pragma once

#include "update/UpdateVersion.h"

#include <QByteArray>
#include <QList>
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

struct Manifest {
    int schemaVersion = 0;
    Version version;
    QString tag;
    QString eyebrow;
    QString title;
    QString summary;
    QString installerAsset;
    qint64 installerSize = 0;
    QByteArray installerSha256;
    Version minimumUpdaterVersion;
    QString notesUrl;
    QList<Highlight> highlights;
    QList<Artwork> artwork;
};

std::optional<Manifest> parseManifest(const QByteArray& verifiedUtf8, QString* error);

} // namespace Colosseum::Update
