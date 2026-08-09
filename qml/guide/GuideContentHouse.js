.pragma library

function lessons() {
    var fixtures = [{
        id: "fixture.house", sourceIds: ["FIXTURE-HOUSE-01"], section: "house",
        title: "House fixture lesson", outcome: "Exercise a published House catalog record.",
        status: "published", verifiedCommit: "fixture", verifiedDate: "2026-08-09", order: 10,
        worlds: ["house"], firstSupportedVersion: "1.1.0", lastVerifiedVersion: "1.1.0",
        evidence: ["tests/qml/tst_guide_catalog.qml"], openQuestions: [], contexts: ["house"],
        searchTerms: ["house fixture"], blocks: [], related: [], asset: ""
    }];
    // VLT-01..VLT-09 distilled from Batch 5 (Vault and local media; packet frozen at commit
    // 157986433, ancestor of the pinned base e6e16c50). ALL Draft: repository evidence earns Draft
    // only — target-build verification earns Published. The Batch-5 "Do not claim" list is law:
    // no CBR support claim, no codec-pack/repair/extension-rename advice, no folder-drag-to-Vault,
    // no open-adds-to-Library, no Clear-deletes-progress, no multi-open, no deleted-file inference.
    // VLT-07 stays a draft that advertises the picker filters without publishing a support matrix.
    var draft = { status: "draft", verifiedCommit: "", verifiedDate: "",
                  firstSupportedVersion: "", lastVerifiedVersion: "", worlds: [], asset: "" };
    function vault(over) {
        return Object.assign({ section: "vault" }, draft, over);
    }
    var production = [
        vault({
            id: "vault.open-media", sourceIds: ["VLT-01", "VLT-02", "VLT-03"], order: 10,
            title: "Open media already on this computer",
            outcome: "Open one local comic, book, or video through the app's own open path.",
            evidence: ["qml/Main.qml", "qml/Taskbar.qml", "native/engine/LocalLaunch.cpp"],
            openQuestions: ["Picker, file drop, and Ctrl+O not yet exercised against a target build",
                            "Whether the picker's multi-selection stays first-file-only is unresolved"],
            contexts: ["open-media"],
            searchTerms: ["open file", "local file", "ctrl o", "open media", "drop file"],
            related: ["vault.recent-files", "vault.local-failure"],
            blocks: [
                { kind: "paragraph", text: "Colosseum can open a comic, book, or video file that is already on this computer. The taskbar's Open Media action, dropping a file onto an ordinary surface, and Ctrl+O all send the file through the same local opening path." },
                { kind: "steps", items: ["Expand the Colosseum taskbar and choose the Open Media icon, or press Ctrl+O.", "In the native Open Media picker, choose one media file and confirm.", "If Colosseum accepts the file, the matching comic, book, or video session opens.", "If the file is rejected, read the message shown - no session is created."] },
                { kind: "paragraph", text: "As an alternative, drop one file anywhere on an ordinary app surface; it goes through the same path." },
                { kind: "note", text: "Open Media opens a file now. It is not adding a folder to the Vault, and opening a local file does not add it to Library." }
            ]
        }),
        vault({
            id: "vault.recent-files", sourceIds: ["VLT-04", "VLT-05", "VLT-06"], order: 20,
            title: "How do Open Recent and Clear work?",
            outcome: "Reopen an available remembered local file, understand an unavailable row, and clear only the remembered shortcuts.",
            evidence: ["qml/OpenRecentPanel.qml", "qml/Taskbar.qml", "qml/Main.qml", "native/engine/LocalLaunch.cpp"],
            openQuestions: ["Reopen after a move or name change, and Clear with live reading progress, not yet exercised at runtime"],
            contexts: ["recent-files"],
            searchTerms: ["open recent", "recent files", "clear recent", "unavailable", "local marker"],
            related: ["vault.open-media"],
            blocks: [
                { kind: "paragraph", text: "Open Recent is a disclosure attached to the Open Media action on the expanded taskbar. It remembers local files Colosseum has opened successfully." },
                { kind: "steps", items: ["Expand the taskbar and open the disclosure on the Open Media action.", "In Recent, choose an available row. Colosseum reopens the file, or switches to the matching session if it is already open.", "To remove the remembered shortcuts, choose Clear."] },
                { kind: "note", text: "Clear removes only the remembered shortcuts - not the files themselves - and leaves reading and watching progress untouched." },
                { kind: "note", text: "An unavailable row usually means the file moved or changed its name; that does not mean Colosseum removed the file. Find the file at its current location and open it again with Open Media." }
            ]
        }),
        vault({
            id: "vault.local-formats", sourceIds: ["VLT-07"], order: 30,
            title: "Which local media formats can I open?",
            outcome: "Understand that the picker's advertised formats are not yet a proven support matrix.",
            evidence: ["qml/Main.qml", "native/engine/LocalLaunch.cpp"],
            openQuestions: ["CBR is accepted by extension with no general in-place reader guarantee - the highest-priority runtime check",
                            "No advertised extension has been exercised end-to-end through the production open path"],
            contexts: ["local-formats"],
            searchTerms: ["formats", "file types", "supported formats", "cbz", "cbr", "epub", "mp4", "mkv"],
            related: ["vault.open-media", "vault.local-failure"],
            blocks: [
                { kind: "paragraph", text: "The Open Media picker advertises comics (.cbz, .cbr), books (.epub), and video (.mp4, .mkv, .avi, .mov, .webm, .m4v). These are picker filters, not a verified support matrix." },
                { kind: "note", text: "A format listed in the picker is not proof that every such file opens. CBR in particular is accepted by extension, with no general in-place reader guarantee." },
                { kind: "note", text: "Until each advertised format is exercised through the production path, the safe move is to try a known-good file of the kind you want. A filename extension change does not convert a file into a supported format." }
            ]
        }),
        vault({
            id: "vault.local-failure", sourceIds: ["VLT-08", "VLT-09"], order: 40,
            title: "Why did a local file fail to open, and what can I safely do?",
            outcome: "Recognize the current local-file rejection message and take a safe next step.",
            evidence: ["qml/Main.qml", "native/engine/LocalLaunch.cpp"],
            openQuestions: ["Each rejection class not yet exercised through the production route; no-session-created not yet runtime-proven"],
            contexts: ["local-failure"],
            searchTerms: ["won't open", "failed to open", "not supported", "damaged comic", "can't play", "file no longer exists"],
            related: ["vault.open-media", "vault.local-formats"],
            blocks: [
                { kind: "paragraph", text: "When a local file is rejected, Colosseum shows one of four categorized messages: an unsupported file type, a damaged comic, a video that cannot be played, or a file that no longer exists." },
                { kind: "bullets", items: ["\"That file type isn't supported.\" - check that you selected the intended file, then try a known-good file",
                                           "\"This comic looks damaged — there's nothing to read.\" - try another known-good comic",
                                           "\"This video can't be played.\" - try another ordinary known-good local video",
                                           "\"That file no longer exists.\" - check whether the file moved or a drive disconnected, then open its current location again"] },
                { kind: "note", text: "A rejection creates no media session. Colosseum has no repair function to run; keep a known-good comparison file nearby instead." }
            ]
        })
    ];
    return fixtures.concat(production);
}
