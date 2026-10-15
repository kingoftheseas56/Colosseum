#pragma once

#include "BookTorrentFilePicker.h" // ManifestFile, PickedFile

#include <QList>
#include <QString>

// One eligible comic archive inside a torrent manifest, with the evidence the
// archive picker shows the user (exact-title, token coverage) so a split or
// wrong-volume release can be avoided.
struct ComicArchiveCandidate {
    int index = -1;
    QString name;
    QString extension;
    qint64 bytes = 0;
    bool exactTitle = false;
    int tokenCoverage = 0;
};

// The narrow auto-selection decision for a manifest. Auto-selects ONLY a lone
// comic archive or a single exact canonical-title archive; every other pack
// requires a manual choice among `candidates`.
struct ComicArchiveDecision {
    PickedFile selected;                        // idx = -1 unless auto-selected
    QList<ComicArchiveCandidate> candidates;    // eligible comic archives, best-first
    bool requiresChoice = false;
};

class ComicTorrentFilePicker
{
public:
    static ComicArchiveDecision decide(const QString& title, const QList<ManifestFile>& files);
    // Compatibility wrapper: the auto-selected archive, or a null PickedFile
    // (idx = -1) when the manifest requires a manual choice.
    static PickedFile pick(const QString& title, const QList<ManifestFile>& files);
    static bool isComicArchive(const QString& name);
    static int formatRank(const QString& ext);
    static QString extOf(const QString& name);
};
