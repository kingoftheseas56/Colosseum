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
    // on the book page (retired — audio lives in the reader's Audio surface).
    //
    // BIB-08..14 distilled from the same Batch 3 (Biblio book reader; re-verified against the
    // current base 6b33e41 — zero drift). All Draft: these reader facts are Confirmed-at-repository,
    // so Draft (none is Uncertain in this set). The reader "Do not claim" list is law: no claim that
    // the keyboard/navigation workflow is runtime-exercised, no claim that every appearance change is
    // global, no claim that in-book search edits saved highlights, no claim that highlight swatches
    // have accessible color names, no claim that an annotation edits the ebook file, no claim that
    // all hyperlinks behave like footnotes. BIB-15..22 (audiobooks, TTS, resume, metadata, failures)
    // are a later handoff.
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
        }),
        biblio({
            id: "biblio.move-through-book", sourceIds: ["BIB-08"], order: 50,
            title: "How do I move through a book?",
            outcome: "Turn pages, jump by chapter, read your current position, and return from a temporary jump.",
            evidence: ["qml/reader2/ReaderShell.qml", "qml/reader2/BottomRail.qml", "qml/reader2/LeftPanel.qml"],
            openQuestions: ["Keyboard page-turns, mouse/touch navigation, chapter jumps, Return behavior, and focus handling not yet exercised on a target build"],
            contexts: ["biblio-reader"],
            searchTerms: ["turn page", "next page", "previous page", "contents", "chapter", "page of", "return", "navigate book"],
            related: ["biblio.appearance", "biblio.search-in-book", "biblio.bookmarks-highlights"],
            blocks: [
                { kind: "paragraph", text: "Once a book is open in the reader, you move through it a page at a time. The reader's own page-turn keys handle next and previous; Escape steps back through what is open rather than waking the reader's controls." },
                { kind: "bullets", items: ["Next page - Right arrow, Space, or Page Down", "Previous page - Left arrow or Page Up", "Escape - steps back through whatever is open (contextual, not a single Back key)"] },
                { kind: "steps", items: ["Turn pages one at a time with the page-turn keys.", "Open Contents when you want to jump by chapter.", "Choose a chapter to move there.", "If the reader created a return point for the jump, use Return to go back to where you were reading."] },
                { kind: "paragraph", text: "The rail at the bottom reports Page N of M in chapter - your place within the current chapter. When a jump leaves a return point, that rail offers Return to page N, or just Return, to go back to the prior spot." },
                { kind: "note", text: "Catalogue search and Search this book are separate tasks. The top-bar search, contents, appearance, and bookmark controls are icons without visible text labels - use their position, not an invented label." }
            ]
        }),
        biblio({
            id: "biblio.appearance", sourceIds: ["BIB-09", "BIB-10"], order: 60,
            title: "How do I change how a book looks?",
            outcome: "Adjust theme, type, spacing, and layout, and know which settings apply to this book versus all books.",
            evidence: ["qml/reader2/AppearancePanel.qml"],
            openQuestions: ["Each theme/type/spacing/layout control, plus default vs book-specific persistence across a restart, not yet exercised on a target build"],
            contexts: ["biblio-reader"],
            searchTerms: ["appearance", "theme", "paper", "sepia", "slate", "night", "font", "typeface", "literata", "size", "spacing", "margins", "columns", "default", "reset"],
            related: ["biblio.move-through-book"],
            blocks: [
                { kind: "paragraph", text: "Open the reader's appearance controls to change how a book looks. You can set a theme, the typeface and size, the spacing, and the layout - and these are the book's own settings unless you choose to make them the default." },
                { kind: "bullets", items: ["Theme - Paper, Sepia, Slate, Night, Contrast, or Custom; dark themes can invert images", "Typeface - Literata, Fraunces, or Inter, plus weight; size has smaller and larger steppers", "Spacing - line, word, letter, paragraph, and indent", "Layout - margins, justify, flow, line width, hyphenation, and columns; text can be Justified or Ragged, in Pages or Scroll, as Single or Spread"] },
                { kind: "steps", items: ["Open the appearance controls.", "Change the setting you want - theme, type, spacing, or layout.", "Keep reading to judge the result.", "To apply this book's settings to every book, choose Use as default for all books.", "To drop this book's overrides and return to your default, choose Reset to default."] },
                { kind: "note", text: "A change you make applies to the current book first. It becomes the default for all books only when you choose Use as default for all books - the two are distinct, and a book can keep its own override." },
                { kind: "note", text: "Reader appearance is separate from the app's wallpaper. A low-contrast theme pair may show a warning that it could be hard to read." }
            ]
        }),
        biblio({
            id: "biblio.search-in-book", sourceIds: ["BIB-11"], order: 70,
            title: "How do I search inside a book?",
            outcome: "Search the open book - not the Biblio catalogue - and clear the search when you are done.",
            evidence: ["qml/reader2/SearchSheet.qml", "qml/reader2/ReaderShell.qml"],
            openQuestions: ["Result navigation, case/word behavior, large-book responsiveness, and exact close behavior not yet exercised at runtime"],
            contexts: ["biblio-reader"],
            searchTerms: ["search in book", "search this book", "find text", "no results", "type to search"],
            related: ["biblio.move-through-book", "biblio.bookmarks-highlights"],
            blocks: [
                { kind: "paragraph", text: "Search this book searches the book you have open - it is not the Biblio catalogue search. The search field reads Search this book, and before you type it suggests Type to search." },
                { kind: "steps", items: ["Open the reader's search surface.", "Enter the text you want.", "Choose a returned match to jump to it.", "Close the search when you are done."] },
                { kind: "paragraph", text: "A completed search with no matches shows No results. Closing the search surface clears its temporary search highlight and returns keyboard focus to the book." },
                { kind: "note", text: "The temporary highlight from a search is not a saved highlight. Searching the book does not create, change, or remove your saved annotations." }
            ]
        }),
        biblio({
            id: "biblio.bookmarks-highlights", sourceIds: ["BIB-12", "BIB-13"], order: 80,
            title: "How do I bookmark, highlight, or add a note?",
            outcome: "Save your place, mark text with a color, and add or revisit a note - and remove only the one you mean.",
            evidence: ["qml/reader2/LeftPanel.qml", "qml/reader2/SelectionMenu.qml", "qml/reader2/ReaderShell.qml"],
            openQuestions: ["Text selection, swatch focus/accessibility, bookmark removal, note save, Define, clipboard behavior, and annotation deletion not yet exercised at runtime"],
            contexts: ["biblio-reader"],
            searchTerms: ["bookmark", "highlight", "note", "annotate", "bookmarks", "highlights", "define", "copy", "delete", "add a note"],
            related: ["biblio.move-through-book", "biblio.search-in-book"],
            blocks: [
                { kind: "paragraph", text: "The reader keeps bookmarks and highlights in separate panels - Bookmarks and Highlights - each with its own empty state (No bookmarks yet, No highlights yet)." },
                { kind: "steps", items: ["To save your place, bookmark the current page, then open Bookmarks to revisit it.", "To mark text, select the passage; the selection menu appears.", "Choose a highlight color dot to highlight, or choose Note to add a note.", "For a note, type in the editor (Add a note...) and choose Save; revisit it under Highlights.", "To remove an existing highlight, bring up its menu and choose Delete."] },
                { kind: "paragraph", text: "The selection menu also offers Copy and Define. A bookmark records the current page and chapter location." },
                { kind: "note", text: "Highlight colors are chosen by their dot - the reader has no text color names for the swatches, so pick by position, not by a color name." },
                { kind: "note", text: "Bookmarks, highlights, and notes are the reader's own saved marks - they do not edit the ebook file itself. A temporary search highlight is not one of these saved marks." }
            ]
        }),
        biblio({
            id: "biblio.footnotes", sourceIds: ["BIB-14"], order: 90,
            title: "How do footnotes work?",
            outcome: "Read a footnote without losing your place, and dismiss it to continue.",
            evidence: ["qml/reader2/FootnoteCard.qml", "qml/reader2/ReaderShell.qml"],
            openQuestions: ["Common EPUB footnote/endnote structures not yet exercised on a target build"],
            contexts: ["biblio-reader"],
            searchTerms: ["footnote", "endnote", "note", "reference", "citation"],
            related: ["biblio.move-through-book"],
            blocks: [
                { kind: "paragraph", text: "When a book has footnotes or endnotes, selecting a note reference opens a small text card near it with the note's text - the reader intercepts the reference rather than navigating you away from the page." },
                { kind: "steps", items: ["Select the footnote or endnote reference in the text.", "Read the note in the card that appears.", "Dismiss the card to continue from the same place."] },
                { kind: "note", text: "Opening a footnote is a peek, not a chapter or page jump - your reading position stays where it was. Not every kind of link in a book behaves this way; this is for footnote and endnote references." }
            ]
        })
    ];
    return fixtures.concat(production);
}
