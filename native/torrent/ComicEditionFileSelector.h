#pragma once

#include "ComicEditionIdentity.h"

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVector>

// Pure manifest-selection module for the Tankorent Comic volume-mode feature
// (design: docs/superpowers/specs/2026-07-15-colosseum-tankorent-comic-
// volume-mode-design.md, "Manifest selection"). Given a torrent's file
// manifest and the canonical edition target, decides the SAFE payload — the
// file(s) inside the pack that ARE the requested edition — or a typed
// failure. Never guesses: a format-scoped or issue-range mismatch always
// yields a typed failure rather than a wrong file. No network, no Qt GUI, no
// real file I/O.
namespace ComicEditionFileSelector {

// One entry in the torrent's file manifest. `path` uses '/' separators and
// may include directory segments (e.g. "Compendiums/Invincible v01.cbz").
struct ManifestFile {
    int index = -1;
    QString path;
    qint64 bytes = 0;
};

enum class ComicPayloadKind {
    None,
    SingleArchive,
    IssueArchiveSet,
    LooseImageSubtree,
    CombinedWholeArchive
};

enum class ComicSelectionFailure {
    None,
    TargetMissing,
    Ambiguous,
    CombinedOnly,
    IncompleteIssueSet,
    UnsupportedPayload
};

// One selected file within the decided payload, in the order it should be
// staged/paged.
struct ComicSelectedFile {
    int index = -1;
    QString path;
    qint64 bytes = 0;
    int order = -1;
};

struct ComicPayloadDecision {
    ComicPayloadKind kind = ComicPayloadKind::None;
    ComicSelectionFailure failure = ComicSelectionFailure::TargetMissing;
    QList<ComicSelectedFile> files;       // ordered; empty unless kind selected something
    QStringList missingIssues;            // populated only on IncompleteIssueSet
    QVariantList manualCandidates;        // eligible archives for a manual fallback (Ambiguous)
};

// Evaluates the selection tiers (unique exact title, unique filename
// coverage, unique deepest directory coverage, complete collected-issue set)
// in order and returns the first safe decision, or a typed failure. Only
// supported comic archives (.cbz/.cbr/.cb7/.cbt) and image files
// (.jpg/.jpeg/.png/.gif/.webp/.avif) are ever eligible; any manifest path
// containing a ".." segment is ignored entirely.
ComicPayloadDecision select(const ComicEditionIdentity::ComicEditionTarget& target,
                             const QList<ManifestFile>& files);

// Unions the selected file indices across every decision into a torrent
// file-priority vector sized `fileCount`: 7 (normal priority) for a selected
// index, 0 (skip) everywhere else. Out-of-range indices are ignored.
QVector<int> unionPriorities(const QList<ComicPayloadDecision>& decisions, int fileCount);

} // namespace ComicEditionFileSelector
