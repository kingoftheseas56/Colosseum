#pragma once
// VaultKit — the Vault's pure-logic kit. Slice 1 of the Vault execution plan
// (Brotherhood/docs/superpowers/plans/2026-08-08-colosseum-vault-execution-plan.md).
//
// Ported from Tankoban 2's src/core/ScannerUtils (the depth-bounded,
// symlink-loop-safe, cooperatively-cancellable walker; the hard-coded
// ignore-dir set; first-level grouping with loose-file capture; and the
// media-folder title cleaner) and EXTENDED with three things the Vault needs
// that TB2 kept scattered:
//   (a) the Groundworks user `scanIgnore` needle layer (a case-insensitive
//       full-path SUBSTRING match — books_library_handlers.py), sanitized and
//       capped, threaded through every walk;
//   (b) a census classifier that infers each first-level subtree's media kind
//       and flags mixed leaves (spec §5, "one folder one kind"); and
//   (c) the season/episode grammar extracted out of TB2's BulkPackVerifier
//       regex and VideosPage::resolveShowPath season-climb guard, so a bare
//       `Season 1` folder never masquerades as a show.
//
// Pure QtCore only (QDir / QFileInfo / QRegularExpression) — no QObject, no app
// dependencies — so a Qt Test links it standalone and the production scanners
// (Slice 4+) #include it the same way. QML paints, C++ decides.

#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <atomic>
#include <functional>

namespace VaultKit {

// ── Cooperative cancellation (ScannerUtils port) ──────────────────────
// A scanner passes a pointer to one of these into the walk; the walker checks
// it between directory entries and bails early. Pre-fix in TB2 a scan in
// flight had to run to completion even after the user quit — a 30s app-close
// hang on a freshly-attached external drive. Non-owning; nullptr = no-cancel.
struct CancellationToken {
    std::atomic<bool> cancelled{false};
    bool isCancelled() const { return cancelled.load(std::memory_order_acquire); }
    void cancel() { cancelled.store(true, std::memory_order_release); }
};

// Caps unbounded recursion on adversarial trees. Real libraries rarely exceed
// depth 8; 32 leaves headroom for show/season/disc/episode plus edge cases.
constexpr int kMaxWalkDepth = 32;

// ── Ignore layers ─────────────────────────────────────────────────────
// Layer 1 (hard-coded): hidden dirs (leading '.') + the system/junk set.
bool isIgnoredDir(const QString& dirName);

// Layer 2 (user, Groundworks contract): `scanIgnore` NEEDLES — a
// case-insensitive SUBSTRING test against a full path, NOT a glob and NOT a
// name match. sanitize trims, drops empties, dedupes case-insensitively
// (first casing kept), and caps at 200.
QStringList sanitizeIgnoreNeedles(const QStringList& needles);
bool pathHitsNeedle(const QString& path, const QStringList& needles);

// List immediate subdirectories of rootPath, skipping ignored / needle-hit dirs.
QStringList listImmediateSubdirs(const QString& rootPath,
                                 const QStringList& needles = {});

// Recursively collect files under dirPath matching nameFilters (e.g. "*.cbz"),
// skipping ignored / needle-hit dirs and needle-hit files. Depth-bounded and
// symlink-loop-safe (canonical paths tracked). Returns absolute paths.
QStringList walkFiles(const QString& dirPath, const QStringList& nameFilters,
                      const CancellationToken* cancel = nullptr,
                      const QStringList& needles = {});

// Group discovered files by first-level subdirectory of each root. Key =
// absolute series/show folder path; loose files directly in a root land under
// the sentinel key "<root>::LOOSE". onProgress (if set) fires once per
// first-level subdir as its walk begins — (done, total, subdirLeafName) — so a
// caller can drive a live "N of M" scan pill during the slow walk.
QMap<QString, QStringList> groupByFirstLevelSubdir(
    const QStringList& rootFolders, const QStringList& nameFilters,
    const CancellationToken* cancel = nullptr,
    const QStringList& needles = {},
    const std::function<void(int, int, const QString&)>& onProgress = {});

// Clean a media folder name for display (underscores/dots → space, bracket
// noise + quality/release markers stripped, trailing group/year removed,
// season labels preserved and re-appended). Returns the original if cleanup
// went too far (< 2 chars). Port of TB2 ScannerUtils::cleanMediaFolderTitle.
QString cleanMediaFolderTitle(const QString& rawName);

// Fold a cleaned title for offline catalogue identity lookups. This is the
// native counterpart of the catalogue bake norm: lowercase, remove a
// possessive 's, collapse non-alphanumeric runs, then drop one leading
// article. The cleaner runs first so release noise in a Vault filename and a
// plain catalogue title share the same key.
QString normalizedTitle(const QString& rawTitle);

// ── Media kinds + census classifier ───────────────────────────────────
enum class MediaKind { Unknown, Comic, Book, Video };
QString kindName(MediaKind kind);           // "comic" / "book" / "video" / "unknown"
MediaKind kindFromName(const QString& name); // inverse of kindName; Unknown for anything else

const QStringList& comicFilters();          // *.cbz *.cbr
const QStringList& bookFilters();           // *.epub *.pdf *.mobi *.fb2 *.azw3 *.djvu (NOT .txt — release junk)
const QStringList& videoFilters();          // mp4 mkv avi webm mov wmv flv m4v ts mpg mpeg ogv
QStringList allMediaFilters();              // the union, for a single-pass walk
MediaKind kindForFile(const QString& path); // by extension

// The verdict for one leaf's file list: which kind dominates, whether more
// than one kind is present (a "mixed leaf" — flagged on the card), the
// per-kind counts, and the other-kind files (the card's honest leftover line).
struct LeafClassification {
    MediaKind dominant = MediaKind::Unknown;
    bool mixed = false;
    QMap<MediaKind, int> counts;
    QStringList leftovers;
};
LeafClassification classifyLeaf(const QStringList& files);

// One row of the confirmation card: a kind-pure slice of a root (subtree ×
// kind). A mixed leaf yields a slice for its dominant kind plus a leftovers
// list — never a hybrid, never scatter-shelved (spec §5).
struct CensusSlice {
    QString subtreePath;
    MediaKind kind = MediaKind::Unknown;
    int count = 0;                          // dominant-kind file count (what shelves)
    bool mixed = false;
    bool loose = false;                     // came from the root "::LOOSE" bucket
    QStringList sampleTitles;               // up to 3 cleaned titles
    QStringList leftovers;                  // mixed-leaf other-kind files
};
// Walk a single root and return its kind-pure slices, honoring user scanIgnore.
QList<CensusSlice> census(const QString& root,
                          const QStringList& scanIgnore = {},
                          const CancellationToken* cancel = nullptr);

// ── Season / episode grammar ──────────────────────────────────────────
struct SeasonEpisode {
    bool matched = false;
    int season = 0;
    int episode = 0;
    // True when this match came from the absolute-numbering grammar (no season known — the
    // fansub "- NNN" convention, e.g. Gintama), never from SxxExx.
    bool absolute = false;
};

// Filename SxxExx grammar ONLY (no size/extension gate — the caller owns
// those). Ported from TB2 BulkPackVerifier::matchEpisodeFileForSeason's regex.
SeasonEpisode parseSeasonEpisode(const QString& fileName);

// Absolute-numbered episode grammar: a spaced "- NNN" token (2-3 digits, so a 4-digit year
// never false-positives), the fansub convention for shows released without SxxExx — e.g.
// "[Judas] Gintama - 003 [BD 1080p]....mkv" -> episode 3, season unknown (0). Tried on its
// own; the caller decides ordering against parseSeasonEpisode.
SeasonEpisode parseAbsoluteEpisode(const QString& fileName);

// The combined grammar Slice 1 adds: try SxxExx first (an explicit season/episode marking
// always wins), then absolute numbering. `matched` is false only if neither fires.
SeasonEpisode parseEpisodeNumber(const QString& fileName);

// True only for a BARE season-shaped segment ("Season N" / "S01".."S999" /
// "Disc N" / "Volume N" / "Vol N" / "Part N" / "CD N"). A name that merely
// EMBEDS the token ("Community Season 1 [1080p]") is NOT one — that folder IS
// the show root. Anchored guard from TB2 VideosPage::resolveShowPath.
bool isSeasonLikeDirName(const QString& dirName);

// Climb past season-like parents so multi-season shows collapse to one root
// (Sopranos/Season 5/ep and Sopranos/Season 6/ep → one "Sopranos" key).
QString showRootForEpisodePath(const QString& filePath);

// ── Browse-collapse planner (Vault browse-face execution plan, Slice 1) ─────
// The Browse face's single projection of "what is at this folder level, and what is each
// thing": folder-true and media-faced (locked design §3 #2/#4/#8). Operates directly on the
// filesystem (same layer as census()) — VaultLibrary cross-references the result against
// VaultIndex/VaultConfig for identity/away/coverRef decoration; this function owns only the
// STRUCTURAL classification.
enum class BrowseNodeType { Folder, Show, Season, Film, Episode, Clip };
QString browseNodeTypeName(BrowseNodeType type);

struct BrowseNode {
    // Stable identity: a real filesystem path for folder/show(nested)/season/film/episode/clip
    // nodes, or a synthesized "<parentPath>::show::<normalizedBaseTitle>" sentinel (the
    // "<root>::LOOSE" convention's sibling) for a show collapsed from SEPARATE sibling season
    // folders that share no single real folder (the Loki shape). Calling planBrowseLevel again
    // with this key drills one level in.
    QString key;
    BrowseNodeType nodeType = BrowseNodeType::Folder;
    QString displayTitle;
    // The one physical fact this node's card carries (locked design §4.3/§4.7 precedence — this
    // slice supplies the season-presence/episode-count facts it can derive structurally; quality
    // and copy-count facts are later slices' business).
    QString physicalFact;
    // The real filesystem path this node represents — a folder for folder/film/show(nested)/
    // season nodes, a file for episode/clip nodes, or "" for a sibling-collapsed virtual show
    // (see `key`).
    QString path;
    int mediaCount = 0;       // film: 1; folder: media files inside; show: seasons/episodes held
    int seasonNumber = 0;     // season/episode nodes only; 0 = unset
    int episodeNumber = 0;    // episode nodes only; 0 = unset
    QList<int> claimedSeasons; // show nodes: season numbers the folder's OWN NAME claims
    QList<int> heldSeasons;    // show nodes: season numbers actually present on disk
    QStringList seasonFolderPaths; // show nodes: constituent season folders, in season order
};

// Classify the immediate children of `levelPath` into typed browse rows: a folder holding
// exactly one media file collapses to a film; a folder holding nested bare season-directories
// collapses to a show with a season-presence fact (claimed-from-name vs held-on-disk); sibling
// folders whose base title matches (once each one's own trailing season label is stripped)
// collapse to ONE show spanning them; a folder of loose video files that all parse as episodes
// (SxxExx or absolute numbering) collapses to a show; a folder of loose video files that do NOT
// collapses to a plain folder (the Cricket shape — clips, never a show). Extras/Featurettes
// subfolders and any non-media companion (subtitles, artwork, nfo, junk) are never their own
// node and are never counted — folding is structural (extension/name filtering), not a
// post-hoc step. Ordering follows locked design §4.2: folders, then shows, then films,
// alphabetical within each band; a leaf level (episodes/clips) sorts by natural filename order.
QList<BrowseNode> planBrowseLevel(const QString& levelPath,
                                  const QStringList& scanIgnore = {},
                                  const CancellationToken* cancel = nullptr);

// ── Film physical facts (Vault browse-face execution plan, Slice 7) ─────────────────────────
// The detail sheet's structural half: what sits BESIDE a film's one media file in its own
// folder — companions (fold into the sheet, never the grid) and extras (Extras/Featurettes,
// folded the same way but listed and playable, distinct from companions). Same layer as
// planBrowseLevel (filesystem-structural, extension/name driven, no identity/DB knowledge) —
// VaultBrowseDetail decorates the result with copies/evidence from VaultIndex.
struct FilmExtra {
    QString title; // a plain, human-readable name derived from the filename (dots/underscores
                    // -> spaces) — never a provider title, this slice has no catalogue lookup.
    QString path;
};
struct FilmPhysicalFacts {
    // Display-ready chip labels — a loose companion FILE recognized by convention (subtitle
    // extension, .nfo, or a conventional cover name) yields one label; a companion FOLDER
    // (Subs/Subtitles) yields ONE label for the whole folder ("Subs · N files"), not one per
    // file inside it.
    QStringList companions;
    // Extras/Featurettes folder contents — never a grid tile (locked design §4.2), listed here
    // and playable from the sheet.
    QList<FilmExtra> extras;
    // Files seen but recognized as neither the film itself, a companion, nor an extra (release
    // scene junk: status text files, tracker-site images that aren't a conventional cover name).
    // Never surfaced anywhere — this count exists so a test can assert the junk was seen AND
    // correctly excluded, not merely absent by construction.
    int ignoredCount = 0;
};
// `folderPath` is the film's containing folder; `primaryFilePath` is the film's own media file
// (excluded from companion classification). Honors the same user `scanIgnore` needle layer
// every other walk does.
FilmPhysicalFacts describeFilmFolder(const QString& folderPath, const QString& primaryFilePath,
                                     const QStringList& scanIgnore = {});

} // namespace VaultKit
