.pragma library

function lessons() {
    var fixtures = [
        {
            id: "fixture.published", sourceIds: ["FIXTURE-START-01"], section: "start",
            title: "Fixture published lesson", outcome: "Exercise the published Guide path.",
            status: "published", verifiedCommit: "fixture", verifiedDate: "2026-08-09", order: 10,
            worlds: [], firstSupportedVersion: "1.1.0", lastVerifiedVersion: "1.1.0",
            evidence: ["tests/qml/tst_guide_catalog.qml"], openQuestions: [], contexts: ["home"],
            searchTerms: ["guide fixture"], blocks: [], related: [], asset: ""
        },
        {
            id: "fixture.draft", sourceIds: ["FIXTURE-START-02"], section: "start",
            title: "Draft secret", outcome: "Remain unavailable until verification.",
            status: "draft", verifiedCommit: "", verifiedDate: "", order: 20,
            worlds: [], firstSupportedVersion: "", lastVerifiedVersion: "", evidence: [], openQuestions: [],
            contexts: ["home"], searchTerms: ["draft secret"], blocks: [], related: [], asset: ""
        }
    ];
    // FND-01..FND-19 distilled from Batch 1 (commit 53eafc0), with Batch 7/8 updater wording as the
    // newer authority. ALL Draft or Uncertain: repository evidence earns Draft only — target-build
    // verification is what earns Published. Nothing here is production-visible yet. Forbidden claims
    // (a fourth world, "Esc = Back", a global recent-search clear, RUNNING = installed build) are
    // omitted, and the not-yet-shipped Guide door is deliberately not taught.
    var draft = { status: "draft", verifiedCommit: "", verifiedDate: "",
                  firstSupportedVersion: "", lastVerifiedVersion: "", worlds: [], contexts: ["home"], asset: "" };
    function start(over) {
        return Object.assign({ section: "start", openQuestions: [], related: [] }, draft, over);
    }
    var production = [
        start({
            id: "fnd-three-worlds", sourceIds: ["FND-01", "FND-02"], order: 10,
            title: "What is Colosseum, and what are its three worlds?",
            outcome: "Know Colosseum's three worlds and which one holds a given kind of media.",
            evidence: ["qml/TopBar.qml", "qml/Main.qml"],
            searchTerms: ["what is colosseum", "worlds", "tankoban", "biblio", "theatre", "media type"],
            related: ["fnd-switch-worlds", "fnd-taskbar"],
            blocks: [
                { kind: "paragraph", text: "Colosseum is one app with three worlds. From Home, the pills across the top switch between them." },
                { kind: "bullets", items: ["Tankoban - manga and comics", "Biblio - books and audiobooks", "Theatre - film and television"] },
                { kind: "note", text: "Opening a world is not the same as opening something to read or watch. A world is a place to browse; it does not start a reader or player session." },
                { kind: "paragraph", text: "No acquisition source has to be enabled just to enter a world. Choose Home to leave a world." }
            ]
        }),
        start({
            id: "fnd-switch-worlds", sourceIds: ["FND-03"], order: 20,
            title: "How do I switch worlds?",
            outcome: "Move between Home, Tankoban, Biblio and Theatre without confusing it with switching open sessions.",
            evidence: ["qml/TopBar.qml", "qml/Main.qml"],
            searchTerms: ["switch world", "navigation", "change world", "home"],
            related: ["fnd-three-worlds", "fnd-taskbar"],
            blocks: [
                { kind: "paragraph", text: "Use the top bar to move between worlds. Selecting a world keeps the ones you already visited loaded, so returning to them is quick." },
                { kind: "steps", items: ["Click a world pill - Tankoban, Biblio, or Theatre.", "Click another pill to change worlds.", "Choose Home to return to the app's Home surface."] },
                { kind: "note", text: "Switching worlds is top-bar navigation. The taskbar's session circles switch open media sessions - a different action, and browsing between worlds never closes an open session." }
            ]
        }),
        start({
            id: "fnd-taskbar", sourceIds: ["FND-04"], order: 30,
            title: "What is the taskbar for?",
            outcome: "Understand what the bottom taskbar represents and when to use it.",
            evidence: ["qml/Taskbar.qml", "qml/Main.qml"],
            searchTerms: ["taskbar", "dock", "sessions", "utilities", "arch"],
            related: ["fnd-open-sessions", "fnd-three-worlds"],
            blocks: [
                { kind: "paragraph", text: "The Colosseum arch at the bottom-left expands and collapses the taskbar. The taskbar is where open media sessions return to, alongside the app-wide utilities." },
                { kind: "bullets", items: ["Open Media and Open Recent", "Downloads", "Vault", "Extensions", "Settings", "Update"] },
                { kind: "paragraph", text: "Open sessions appear as circular media-type icons. Several of the same type stack behind one count." },
                { kind: "note", text: "The taskbar is hidden while a reader or player owns the screen. Most controls are icons rather than text labels." }
            ]
        }),
        start({
            id: "fnd-open-sessions", sourceIds: ["FND-05", "FND-06", "FND-07", "FND-08"], order: 40,
            title: "How do open sessions work?",
            outcome: "Minimize media into an open session, switch between sessions, and close one deliberately.",
            evidence: ["native/SessionStore.h", "qml/Taskbar.qml", "qml/Main.qml"],
            searchTerms: ["session", "minimize", "close", "switch session", "restore"],
            related: ["fnd-taskbar", "fnd-continue-library", "fnd-pip-vs-minimize"],
            blocks: [
                { kind: "paragraph", text: "An open comic, book, or Theatre player can be minimized into a session - parked in the taskbar, not deleted. The taskbar expands on its own right after a minimize." },
                { kind: "steps", items: ["Open some media.", "Use its minimize control; it parks in the taskbar and resumes with no reload.", "Click its session circle to restore it.", "If several sessions stack, open the stack and choose the title.", "To end a session, use its close control, or the taskbar's remove target."] },
                { kind: "note", text: "Minimize keeps a session open; Close removes it. Closing the active session returns you to the world behind it. There is no undo for a close - if Continue has the item, resume there." },
                { kind: "note", text: "Open sessions do not survive an app restart. Minimizing the whole Colosseum window is a separate action from parking a session." }
            ]
        }),
        start({
            id: "fnd-continue-library", sourceIds: ["FND-09", "FND-10"], order: 50,
            title: "How do I continue later, and how is Continue different from Library?",
            outcome: "Pick the right place to resume something, or deliberately save it.",
            evidence: ["native/ProgressStore.h", "native/CollectionStore.h", "qml/ContinueRow.qml"],
            searchTerms: ["continue", "library", "resume", "save", "in library"],
            related: ["fnd-open-sessions", "fnd-history"],
            blocks: [
                { kind: "paragraph", text: "Continue remembers what you opened and how far you got, and it survives a restart. A Continue row disappears when it has nothing in it." },
                { kind: "bullets", items: ["Home - Continue", "Tankoban and Biblio - Continue Reading", "Theatre - Continue Watching"] },
                { kind: "paragraph", text: "Library is a deliberate save, kept separately from Continue. Its control reads Library when unsaved and In Library once saved." },
                { kind: "note", text: "An open session, Continue, Library, and a downloaded local copy are four different things. Saving to Library does not download, and removing from Library does not delete a local copy." }
            ]
        }),
        start({
            id: "fnd-search", sourceIds: ["FND-11", "FND-12"], order: 60,
            title: "How do I search a world and remove a recent search?",
            outcome: "Search inside the current world, reuse a recent query, and remove one recent query.",
            evidence: ["qml/SearchSurface.qml", "qml/BiblioSearch.qml", "native/SearchHistoryStore.h"],
            searchTerms: ["search", "recent search", "remove recent search", "find"],
            related: ["fnd-switch-worlds"],
            blocks: [
                { kind: "paragraph", text: "Enter a world first, then use the search icon at the top right. Search runs inside that world - Tankoban, Biblio, and Theatre each have their own." },
                { kind: "steps", items: ["Open the world's search.", "Type at least two characters and wait for results.", "Pick a result, or press Enter to open the top result when there is one.", "Reopen search to reuse a recent query.", "Select a recent query's remove target to take just that one out."] },
                { kind: "note", text: "You take recent searches out one at a time. Recent searches are kept per world, and they are not your reading or watching history." }
            ]
        }),
        start({
            id: "fnd-keyboard", sourceIds: ["FND-13"], order: 70,
            title: "Which app-wide keys are safe to rely on?",
            outcome: "Know the few app-wide keys whose behavior is stable enough to teach.",
            evidence: ["qml/Main.qml"],
            searchTerms: ["keyboard", "shortcut", "ctrl+o", "ctrl+q", "escape"],
            related: ["fnd-open-sessions"],
            blocks: [
                { kind: "bullets", items: ["Ctrl+O - open the Open Media picker from anywhere", "Ctrl+Q - quit Colosseum"] },
                { kind: "note", text: "Escape is contextual, not a single Back key: it steps back through whatever is open, and at Home it quits. Readers and the player have their own separate shortcut lists." }
            ]
        }),
        start({
            id: "fnd-fullscreen", sourceIds: ["FND-14"], order: 80,
            title: "How do I switch between fullscreen and a window?",
            outcome: "Toggle Colosseum's shell between fullscreen and a window.",
            evidence: ["qml/TopBar.qml"],
            searchTerms: ["fullscreen", "window", "windowed", "maximize"],
            related: ["fnd-pip-vs-minimize"],
            blocks: [
                { kind: "paragraph", text: "The glyph at the top right toggles the shell between fullscreen and a window. The icon shows the action - expand while windowed, contract while fullscreen." },
                { kind: "note", text: "This is the whole-window toggle. It is different from minimizing a media session and from Theatre's picture-in-picture, and it does not close or restart what you are viewing." }
            ]
        }),
        start({
            id: "fnd-pip-vs-minimize", sourceIds: ["FND-15"], order: 90,
            title: "How is picture-in-picture different from minimizing?",
            outcome: "Tell picture-in-picture apart from parking a Theatre session.",
            evidence: ["qml/PlayerPage.qml"],
            searchTerms: ["picture in picture", "pip", "minimize", "theatre"],
            related: ["fnd-open-sessions"],
            blocks: [
                { kind: "paragraph", text: "In the Theatre player, minimizing parks playback as a taskbar session and leaves the immersive view. Picture-in-picture instead keeps the video visible in a small view." },
                { kind: "note", text: "Session minimize, the whole-window minimize, and picture-in-picture are three separate things. The full picture-in-picture detail belongs with Theatre." }
            ]
        }),
        start({
            id: "fnd-updates", sourceIds: ["FND-16", "FND-17"], order: 100,
            title: "How do I update Colosseum and recover when an update stops?",
            outcome: "Read the update state and use only the action Colosseum offers for it.",
            evidence: ["qml/UpdatePage.qml", "native/update/UpdateService.cpp"],
            searchTerms: ["update", "check for updates", "download update", "restart and update", "update failed"],
            related: ["fnd-installed-version"],
            blocks: [
                { kind: "paragraph", text: "The Update page lives on the expanded taskbar. Follow only the primary action shown for the current state." },
                { kind: "steps", items: ["When idle, choose Check for updates.", "If one is available, choose Download update.", "Let it verify, then at Ready to restart choose Restart and update."] },
                { kind: "bullets", items: ["Paused - Resume download", "Recoverable error - Retry download", "Could not be verified - Check for a newer release"] },
                { kind: "note", text: "Cancel download pauses the update so you can resume it; it does not discard it. Do not bypass verification. A Manual update required state points you to GitHub but does not itself install anything, and acquisition sources have no effect on app updates." }
            ]
        }),
        start({
            id: "fnd-installed-version", sourceIds: ["FND-18"], order: 110, status: "uncertain",
            title: "Where can I find my installed Colosseum version?",
            outcome: "Understand why the running version is not yet reliably shown.",
            evidence: ["qml/update/UpdateReleaseHero.qml", "native/update/UpdateService.h"],
            openQuestions: ["Which UI, if any, is intended to expose the true installed version?"],
            searchTerms: ["installed version", "running version", "build number", "about"],
            related: ["fnd-updates"],
            blocks: [
                { kind: "paragraph", text: "Colosseum tracks the installed version and the latest release as separate values. The Update page can show a RUNNING line, but it currently reflects the latest release, not the build you are actually running." },
                { kind: "note", text: "Until that display is corrected, do not rely on the RUNNING line to report your installed build. This lesson stays unpublished until a trustworthy installed-version readout is confirmed." }
            ]
        }),
        start({
            id: "fnd-history", sourceIds: ["FND-19"], order: 120, status: "uncertain",
            title: "Can I see everything I have read or watched before?",
            outcome: "Know whether a complete consumption-history surface exists.",
            evidence: ["native/ProgressStore.h"],
            openQuestions: ["Does a comprehensive history surface exist, or is the intended answer no?"],
            searchTerms: ["history", "everything i watched", "everything i read", "past"],
            related: ["fnd-continue-library", "fnd-search"],
            blocks: [
                { kind: "paragraph", text: "Colosseum keeps Continue progress and recent search terms, and each world shows Continue. This inspection did not find a single surface that lists everything you have ever read or watched." },
                { kind: "note", text: "Continue, recent searches, and Library are each different from a complete history. Use Continue or Library while they still hold the title. This lesson stays unpublished until product confirms whether such a surface exists." }
            ]
        })
    ];
    return fixtures.concat(production);
}
