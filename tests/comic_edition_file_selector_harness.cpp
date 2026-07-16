// Comic edition manifest-selection contract: given a torrent's file manifest
// and the canonical edition target, decide the SAFE payload — coverage-match
// a single collected-edition archive when possible, else assemble the
// edition's issue range from loose issue archives, else a loose-page
// directory, else a typed failure. Format-aware: a Compendium target must
// pick the Compendium file, never a TPB or issue file of the same series
// (design: docs/superpowers/specs/2026-07-15-colosseum-tankorent-comic-
// volume-mode-design.md, "Manifest selection").
#include "torrent/ComicEditionFileSelector.h"
#include "torrent/ComicEditionIdentity.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

using ComicEditionFileSelector::ComicPayloadDecision;
using ComicEditionFileSelector::ComicPayloadKind;
using ComicEditionFileSelector::ComicSelectionFailure;
using ComicEditionFileSelector::ManifestFile;

ManifestFile mf(int index, const QString& path, qint64 bytes = 32 * 1024 * 1024)
{
    return ManifestFile{index, path, bytes};
}

bool hasIndex(const ComicPayloadDecision& d, int index)
{
    for (const auto& f : d.files)
        if (f.index == index) return true;
    return false;
}
} // namespace

int main()
{
    using namespace ComicEditionIdentity;
    using namespace ComicEditionFileSelector;

    // ── Tier: unique filename coverage match wins over a same-series TPB ──
    const ComicEditionTarget compendium1 = buildTarget(
        QStringLiteral("ed-compendium-1"), QStringLiteral("sid"), QStringLiteral("Invincible"),
        QStringLiteral("Invincible Compendium #1"), QStringLiteral("Compendium"), QString(), QString());
    require(compendium1.format == ComicCollectionFormat::Compendium && compendium1.ordinal == 1,
            "sanity: compendium target resolves format + ordinal");

    {
        const QList<ManifestFile> pack = {
            mf(0, QStringLiteral("Compendiums/Invincible Compendium v01.cbz")),
            mf(1, QStringLiteral("TPBs/Invincible v01.cbz")),
            mf(2, QStringLiteral("Issues/Invincible 001.cbz")),
            mf(3, QStringLiteral("Issues/Invincible 002.cbz")),
        };
        const ComicPayloadDecision d = select(compendium1, pack);
        require(d.kind == ComicPayloadKind::SingleArchive, "compendium pack selects a single archive");
        require(d.failure == ComicSelectionFailure::None, "compendium pack decision is not a failure");
        require(d.files.size() == 1 && d.files.first().index == 0,
                "the Compendiums file is selected");
        require(!hasIndex(d, 1), "the same-series TPB file is never selected for a Compendium target");
    }

    // ── Two equal top candidates → Ambiguous, no files selected ──
    {
        const QList<ManifestFile> pack = {
            mf(0, QStringLiteral("SetA/Invincible Compendium v01.cbz")),
            mf(1, QStringLiteral("SetB/Invincible Compendium v01.cbz")),
        };
        const ComicPayloadDecision d = select(compendium1, pack);
        require(d.failure == ComicSelectionFailure::Ambiguous, "two equal candidates are Ambiguous");
        require(d.files.isEmpty(), "an ambiguous decision selects nothing");
        require(d.manualCandidates.size() == 2, "both equal candidates surface for a manual choice");
    }

    // ── An inclusive multi-edition archive is CombinedOnly, never auto-isolated ──
    {
        const QList<ManifestFile> pack = {
            mf(0, QStringLiteral("Invincible Compendiums v01-v03.cbz")),
        };
        const ComicPayloadDecision d = select(compendium1, pack);
        require(d.kind == ComicPayloadKind::CombinedWholeArchive,
                "an archive whose name spans multiple editions is CombinedWholeArchive");
        require(d.failure == ComicSelectionFailure::CombinedOnly,
                "CombinedWholeArchive requires explicit whole-archive confirmation");
    }

    // ── Complete collected-issue archive set, ordered by target.collectedIssues ──
    const ComicEditionTarget threeIssues = buildTarget(
        QStringLiteral("ed-issues"), QStringLiteral("sid"), QStringLiteral("Invincible"),
        QStringLiteral("Invincible Digital Issues"), QString(), QString(), QStringLiteral("Invincible #1-3"));
    require(threeIssues.collectedIssuesComplete && threeIssues.collectedIssues.size() == 3,
            "sanity: three-issue target parses complete");

    {
        const QList<ManifestFile> pack = {
            mf(0, QStringLiteral("Invincible/issue-001.cbz")),
            mf(1, QStringLiteral("Invincible/issue-002.cbz")),
            mf(2, QStringLiteral("Invincible/issue-003.cbz")),
        };
        const ComicPayloadDecision d = select(threeIssues, pack);
        require(d.kind == ComicPayloadKind::IssueArchiveSet, "a complete issue set assembles");
        require(d.files.size() == 3, "all three required issues are selected");
        require(d.files[0].index == 0 && d.files[0].order == 0, "issue #1 is first");
        require(d.files[1].index == 1 && d.files[1].order == 1, "issue #2 is second");
        require(d.files[2].index == 2 && d.files[2].order == 2, "issue #3 is third");
    }

    // ── A missing required issue disables the automatic download entirely ──
    {
        const QList<ManifestFile> pack = {
            mf(0, QStringLiteral("Invincible/issue-001.cbz")),
            mf(2, QStringLiteral("Invincible/issue-003.cbz")),
        };
        const ComicPayloadDecision d = select(threeIssues, pack);
        require(d.failure == ComicSelectionFailure::IncompleteIssueSet,
                "a missing required issue reports IncompleteIssueSet");
        require(d.files.isEmpty(), "an incomplete issue set downloads nothing automatically");
        require(!d.missingIssues.isEmpty() && d.missingIssues.join(QStringLiteral(",")).contains(QStringLiteral("2")),
                "the missing issue (#2) is named");
    }

    // ── Cross-series identity: a bonus archive is selected ONLY when the
    //    target's collected-issue set actually requires it ──
    {
        const QList<ManifestFile> pack = {
            mf(0, QStringLiteral("Invincible/issue-001.cbz")),
            mf(1, QStringLiteral("Invincible/issue-002.cbz")),
            mf(2, QStringLiteral("Invincible/issue-003.cbz")),
            mf(3, QStringLiteral("The Pact #4.cbz")),
        };

        const ComicEditionTarget withPact = buildTarget(
            QStringLiteral("ed-with-pact"), QStringLiteral("sid"), QStringLiteral("Invincible"),
            QStringLiteral("Invincible Special Edition"), QString(), QString(),
            QStringLiteral("Invincible #1-3; The Pact #4"));
        require(withPact.collectedIssuesComplete && withPact.collectedIssues.size() == 4,
                "sanity: cross-series target parses complete with 4 issues");
        const ComicPayloadDecision withPactDecision = select(withPact, pack);
        require(withPactDecision.kind == ComicPayloadKind::IssueArchiveSet,
                "cross-series issue set assembles when required");
        require(withPactDecision.files.size() == 4 && hasIndex(withPactDecision, 3),
                "The Pact #4 is selected when the target requires it");

        const ComicEditionTarget withoutPact = buildTarget(
            QStringLiteral("ed-without-pact"), QStringLiteral("sid"), QStringLiteral("Invincible"),
            QStringLiteral("Invincible Digital Issues"), QString(), QString(),
            QStringLiteral("Invincible #1-3"));
        const ComicPayloadDecision withoutPactDecision = select(withoutPact, pack);
        require(withoutPactDecision.kind == ComicPayloadKind::IssueArchiveSet,
                "the same pack still assembles the required issues without The Pact");
        require(withoutPactDecision.files.size() == 3 && !hasIndex(withoutPactDecision, 3),
                "The Pact #4 is NOT selected when the target does not require it");
    }

    // ── Loose page directory: a unique deepest-directory coverage match ──
    {
        const QList<ManifestFile> pack = {
            mf(0, QStringLiteral("Invincible Compendium 1/001.jpg")),
            mf(1, QStringLiteral("Invincible Compendium 1/002.jpg")),
            mf(2, QStringLiteral("Invincible Compendium 1/003.jpg")),
        };
        const ComicPayloadDecision d = select(compendium1, pack);
        require(d.kind == ComicPayloadKind::LooseImageSubtree, "a matching page directory selects loose images");
        require(d.files.size() == 3, "all three pages are selected");
        require(d.files[0].index == 0 && d.files[1].index == 1 && d.files[2].index == 2,
                "pages are ordered by path");
    }

    // ── unionPriorities: 7 for every selected index across decisions, 0 elsewhere ──
    {
        ComicPayloadDecision decision;
        decision.kind = ComicPayloadKind::SingleArchive;
        decision.files.append(ComicSelectedFile{2, QStringLiteral("a"), 1, 0});
        decision.files.append(ComicSelectedFile{5, QStringLiteral("b"), 1, 1});

        const QVector<int> priorities = unionPriorities({decision}, 8);
        require(priorities.size() == 8, "priority vector is sized to the manifest file count");
        require(priorities[2] == 7 && priorities[5] == 7, "selected indices carry priority 7");
        int nonZero = 0;
        for (int i = 0; i < priorities.size(); ++i)
            if (i != 2 && i != 5) nonZero += priorities[i];
        require(nonZero == 0, "every unselected index carries priority 0");
    }

    // ── A collected edition under the generic "Collected Edition" umbrella must
    //    match as the WHOLE volume, never fall into issue-set assembly. Real
    //    eyes-on regression: "Saga: Book One" was wrongly flagged "missing
    //    issues #10-18" because the umbrella conflicted with the title's Book. ──
    {
        const ComicEditionTarget sagaBookOne = buildTarget(
            "chSaga", "sid", "Saga", "Saga: Book One", "Collected Edition",
            "9781632150783", "Saga #1-18");
        require(sagaBookOne.format == ComicCollectionFormat::Book && sagaBookOne.ordinal == 1
                    && !sagaBookOne.formatAmbiguous,
                "umbrella defers to Book/1 so the whole-edition coverage tiers stay enabled");

        const QList<ManifestFile> volume = { mf(0, "Saga Book One (2014) (Digital).cbz") };
        const ComicPayloadDecision d = select(sagaBookOne, volume);
        require(d.kind == ComicPayloadKind::SingleArchive && d.failure == ComicSelectionFailure::None,
                "the collected volume matches as a single archive");
        require(d.failure != ComicSelectionFailure::IncompleteIssueSet,
                "a collected edition is never judged an incomplete issue pack");
    }

    std::cout << "COMIC_EDITION_FILE_SELECTOR_OK\n";
    return 0;
}
