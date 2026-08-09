.pragma library

function lessons() {
    var fixtures = [{
        id: "fixture.biblio", sourceIds: ["FIXTURE-BIBLIO-01"], section: "biblio",
        title: "Biblio fixture lesson", outcome: "Exercise a published Biblio catalog record.",
        status: "published", verifiedCommit: "fixture", verifiedDate: "2026-08-09", order: 10,
        worlds: ["biblio"], firstSupportedVersion: "1.1.0", lastVerifiedVersion: "1.1.0",
        evidence: ["tests/qml/tst_guide_catalog.qml"], openQuestions: [], contexts: ["biblio"],
        searchTerms: ["biblio fixture"], blocks: [], related: [], asset: ""
    }];
    // BIB-01..07 + BIB-23 distilled from Batch 3 (Biblio acquisition + Library; packet frozen at
    // commit 03c16cd, ancestor of the pinned base d11c12c). Every packet-quoted string was re-
    // verified against d11c12c — zero drift. ALL Draft (or Uncertain for BIB-06): repository
    // evidence earns Draft only; target-build verification earns Published. The Batch-3 "Do not
    // claim" list is law: no format-support claim from the badge parser, no "all acquisition stays
    // inside Colosseum" (URL-only edition rows route externally), no Downloaded Library filter
    // (deliberately absent), no seeder-count guarantee, no canonical/best edition, no Listen button
    // on the book page (retired — audio lives in the reader's Audio surface). BIB-08..22 (reader +
    // audiobooks) are out of scope — later handoffs.
    var draft = { status: "draft", verifiedCommit: "", verifiedDate: "",
                  firstSupportedVersion: "", lastVerifiedVersion: "", worlds: ["biblio"], asset: "" };
    function biblio(over) {
        return Object.assign({ section: "biblio" }, draft, over);
    }
    var production = [
        biblio({
            id: "biblio.find-books", sourceIds: ["BIB-01"], order: 10,
            title: "How do I find books and audiobooks in Biblio?",
            outcome: "Recognize Biblio's three tabs, open a book's detail page, and know where audiobook acquisition begins.",
            evidence: ["qml/BiblioWorld.qml"],
            openQuestions: ["Discover/Explore/Library and the Featured carousel not yet exercised on a target build",
                            "Which catalogue/acquisition paths remain available on a fresh profile with no optional source enabled is unresolved"],
            contexts: ["biblio-book"],
            searchTerms: ["find books", "find audiobooks", "discover", "explore", "library tab", "biblio"],
            related: ["biblio.editions", "biblio.library"],
            blocks: [
                { kind: "paragraph", text: "Biblio is the books and audiobooks world. It opens on the Discover tab every time; the tab you were on is not restored on a fresh entry." },
                { kind: "bullets", items: ["Discover - a catalogue grid for browsing and searching", "Explore - the deeper shelf and discovery surface", "Library - your saved books"] },
                { kind: "steps", items: ["Enter Biblio.", "Use Discover or Explore to browse, or Library to return to saved books.", "Select a book to open its detail page.", "If the book has audiobook candidates, its audiobook section is on that detail page."] },
                { kind: "note", text: "Audiobooks belong to Biblio, but they are not a separate world and not a fourth top-level tab. An audiobook is reached from its book's detail page, not from a standalone audiobook discovery tab." },
                { kind: "note", text: "Above the tabs, the Featured in Biblio carousel offers Read and Details actions for highlighted titles." }
            ]
        }),
        biblio({
            id: "biblio.editions", sourceIds: ["BIB-02", "BIB-03", "BIB-04", "BIB-05"], order: 20,
            title: "What is an edition, and how do I get a book to read?",
            outcome: "Understand why several choices appear, pick an available copy, and know when the book is ready to read.",
            evidence: ["qml/BiblioBook.qml"],
            openQuestions: ["Primary CTA, direct edition selection, URL-only rows, copy replacement, and no-source behavior not yet exercised at runtime"],
            contexts: ["biblio-book"],
            searchTerms: ["edition", "get the book", "find the book", "download book", "read", "libgen"],
            related: ["biblio.formats", "biblio.library"],
            blocks: [
                { kind: "paragraph", text: "A book can be offered by several sources, so the detail page may list several editions - different copies with their own format, language, size, and source. The primary action is Read when a local copy already exists; otherwise it reads Get the book, or Find the book when no edition is listed yet." },
                { kind: "steps", items: ["Open the book's detail page.", "If Read is shown, a local copy is ready - choose it to start reading.", "Otherwise choose Get the book, or pick a specific available edition row.", "Watch the row's state - it can show searching, progress, completion, failure, or an external link.", "Once a copy completes, open it with Read or by choosing the completed row."] },
                { kind: "paragraph", text: "The edition area has its own states: EDITIONS · SEARCHING… while sources are queried, an edition count once they load, or EDITIONS · NONE when nothing was found. Searching LibGen… and No editions found describe the same loading-versus-empty distinction." },
                { kind: "note", text: "An edition label is acquisition metadata - it describes where a copy comes from and what format its source named. It does not prove the reader can open that format." },
                { kind: "note", text: "Most editions download inside Colosseum, but not all. A URL-only edition row opens its source page outside the app rather than downloading in-app - that is a different path, not the same download." },
                { kind: "note", text: "On the direct edition-download path, starting a new download clears the previous stored copy for that book first, so picking another edition replaces the old copy rather than piling up several. Use retry only on a row that actually shows it, or pick a different available edition." }
            ]
        }),
        biblio({
            id: "biblio.formats", sourceIds: ["BIB-06"], order: 30, status: "uncertain",
            title: "Which ebook formats can Colosseum actually read?",
            outcome: "Avoid treating an edition's format label as proof that the reader can open that format.",
            evidence: ["qml/BiblioBook.qml"],
            openQuestions: ["No candidate format has been exercised through the production reader-opening path; no support matrix is publishable yet"],
            contexts: ["biblio-book"],
            searchTerms: ["formats", "epub", "pdf", "mobi", "azw", "cbz", "cbr", "file types", "which formats"],
            related: ["biblio.editions"],
            blocks: [
                { kind: "paragraph", text: "Edition rows and their format badges can show many labels - EPUB, PDF, MOBI, AZW3, AZW, CBZ, CBR, DJVU, FB2, and TXT. These are the labels the source names and the page displays." },
                { kind: "note", text: "Seeing a format on an edition is not proof the reader can open it. A format being listed by a source is not the same as being downloadable, and neither is the same as opening successfully in the reader. Until each format is exercised through the real opening path, treat the labels as what the source named - not as a tested guarantee of what opens." },
                { kind: "note", text: "If a book does not open, do not change its filename extension to force it - an extension change does not turn a file into a supported format. Use the book page's current recovery path or try another available edition." }
            ]
        }),
        biblio({
            id: "biblio.library", sourceIds: ["BIB-07", "BIB-23"], order: 40,
            title: "How do I save and find books in my Biblio Library?",
            outcome: "Save books, find them later, sort and filter the saved collection, and remove a book from Library without confusing that with deleting a file.",
            evidence: ["qml/BiblioLibraryPage.qml", "qml/LibraryButton.qml"],
            openQuestions: ["Save/remove persistence and the exact effect of removal while a local copy and reading progress both exist not yet exercised at runtime"],
            contexts: ["biblio-library"],
            searchTerms: ["library", "save book", "in library", "recently added", "last read", "a to z", "in progress", "resume", "remove from library"],
            related: ["biblio.find-books", "biblio.editions"],
            blocks: [
                { kind: "paragraph", text: "Library is Biblio's saved-books tab. Save a book with the Library control on its detail page - it reads Library when unsaved and In Library once saved - and the book lands on the Library tab." },
                { kind: "paragraph", text: "On the Library tab you can search (Search by title or author), sort, and filter:" },
                { kind: "bullets", items: ["Sort by Recently added, Last read, or A-Z", "Filter by All or In Progress"] },
                { kind: "steps", items: ["Open Biblio and choose Library.", "Search, sort, or use In Progress to narrow the list.", "Choose Resume on a card where reading progress is available, or Details to open the book page.", "Use a card's menu and choose Remove from Library to take it out of Library."] },
                { kind: "paragraph", text: "When Library is empty it shows Your library is empty and Save a book with + Library - it lands here. A search or filter with no matches shows Nothing matches." },
                { kind: "note", text: "Library membership, Continue Reading, and a downloaded local copy are three different things. Removing a book from Library does not delete its local copy or erase reading progress." },
                { kind: "note", text: "There is no Downloaded filter on the Library tab - only All and In Progress. Colosseum deliberately omits one, because it has no honest availability signal to filter a saved list by what is currently downloaded." }
            ]
        })
    ];
    return fixtures.concat(production);
}
