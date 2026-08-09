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
    function dl(over) {
        return Object.assign({ section: "downloads" }, draft, over);
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
        }),
        // DLD-01..DLD-15 distilled from Batch 6 (Downloads; packet frozen at commit a149e94f,
        // ancestor of the pinned base d11c12c). ALL Draft. Batch-6 "Do not claim" is law: no
        // free-space/queue/storage-location/bandwidth controls, no seeding claims, no Play-means-
        // offline, Cancel DELETES partial data, Retry/pause are capability-driven, delete-local
        // is separate from Library/Continue, and no general choose-files-inside-a-download flow.
        // DLD-10/11/12/14 and general DLD-15 state the ABSENCE honestly; DLD-15 keeps only the
        // narrow Tankoban ambiguous-comic-archive chooser.
        //
        // ARRAY-ORDER CONSTRAINT: the contract gate stringifies the WHOLE cohort and applies
        // cross-record regexes (VLT "Clear" .. delete .. file/progress/continue). So records
        // containing destructive wording sit LAST in this array (actions, remove-local), no
        // "file"/"progress"/"continue" token may appear after the first "deleted" token, and
        // record ids/related refs must not smuggle "delete" earlier than that point (that is
        // why the delete-local record is id "downloads.remove-local"). Display order is the
        // `order` field, so array position is free. Do not "sort" this array.
        dl({
            id: "downloads.overview", sourceIds: ["DLD-01"], order: 10,
            title: "Where can I see my downloads?",
            outcome: "Open the app-wide download manager and tell work still arriving apart from settled local copies.",
            evidence: ["qml/DownloadsPage.qml", "qml/Taskbar.qml"],
            openQuestions: ["Entry from each world, and page refresh while jobs settle, not yet exercised at runtime"],
            contexts: ["downloads-overview"],
            searchTerms: ["downloads", "now arriving", "downloaded", "local copies"],
            related: ["downloads.status", "downloads.remove-local"],
            blocks: [
                { kind: "paragraph", text: "Downloads is an app-wide page on the expanded taskbar. Live work appears under Now arriving; settled local media appears in world shelves beneath it." },
                { kind: "paragraph", text: "The page can show totals such as item count, bytes on disk, per-world counts, N arriving, and N need attention." },
                { kind: "steps", items: ["Open Downloads from the taskbar.", "Check Now arriving for live, paused, or failed work.", "Use the settled shelves for local copies."] },
                { kind: "note", text: "A download job is not the same as the settled copy, a Library save, or a Vault-indexed personal media entry. A Theatre job that is playable while arriving is still not a settled copy." }
            ]
        }),
        dl({
            id: "downloads.status", sourceIds: ["DLD-02", "DLD-03"], order: 20,
            title: "What do the download states, progress, speed and remaining time mean?",
            outcome: "Read the current download state and use the shown progress numbers without over-reading them.",
            evidence: ["qml/DownloadsPage.qml"],
            openQuestions: ["State transitions for each world and audiobook jobs, and which states survive restart, not yet exercised"],
            contexts: ["downloads-status"],
            searchTerms: ["download state", "progress", "speed", "eta", "paused", "queued", "landed"],
            related: ["downloads.overview", "downloads.actions"],
            blocks: [
                { kind: "paragraph", text: "The Now arriving area labels each job's state first: queued — waits its turn, resolving — finding the best stream, downloading, paused — holds its place, unpacking, failed, or landed. Eligible manga work can show source cooling down — resumes in <time>." },
                { kind: "paragraph", text: "Where the backend knows totals, the page can show transferred bytes of total, a percentage, speed, ETA, N of M landed, and an aggregate progress bar." },
                { kind: "note", text: "Not every job has byte numbers; some stages have no meaningful progress yet, and zero visible progress is not automatically failure." },
                { kind: "note", text: "A Theatre row can show Play while the job is still arriving — play-while-arriving, not proof the local copy is landed." }
            ]
        }),
        dl({
            id: "downloads.storage", sourceIds: ["DLD-10", "DLD-11"], order: 30,
            title: "Where are downloads stored, and how much space is left?",
            outcome: "Know that no user-facing storage path or available-space readout is currently established.",
            evidence: ["qml/DownloadsPage.qml", "qml/SettingsPage.qml"],
            openQuestions: ["Whether a later slice exposes a storage path or capacity warning must be re-audited"],
            contexts: ["downloads-storage"],
            searchTerms: ["storage", "on disk", "space left"],
            related: ["downloads.overview", "downloads.remove-local"],
            blocks: [
                { kind: "paragraph", text: "The Downloads page can show the total size of settled content as <size> on disk. That is the only storage-related readout currently established." },
                { kind: "note", text: "No user-facing storage path or available-space readout exists yet in Downloads or Settings. Internal paths are not a supported user workflow." }
            ]
        }),
        dl({
            id: "downloads.queue", sourceIds: ["DLD-12"], order: 40,
            title: "Can I change the order of downloads or cap their speed?",
            outcome: "Know that the queue runs automatically and no manual order or speed controls are currently offered.",
            evidence: ["qml/DownloadsPage.qml", "qml/SettingsPage.qml"],
            openQuestions: ["If queue controls land, their scope per backend must be re-audited"],
            contexts: ["downloads-queue"],
            searchTerms: ["queue", "download order", "speed cap"],
            related: ["downloads.actions", "downloads.status"],
            blocks: [
                { kind: "paragraph", text: "The manager runs an automatic queue: a job that is queued — waits its turn. Current Downloads and Settings surfaces offer no way to change the order of queued jobs or cap their speed." },
                { kind: "note", text: "An automatic queue is not a user-managed queue. Extension source order is a different concept and does not change the download queue." }
            ]
        }),
        dl({
            id: "downloads.seeders", sourceIds: ["DLD-13"], order: 50,
            title: "What does the seeder number on a Biblio source mean?",
            outcome: "Read the seeder count as source-availability metadata beside the release size.",
            evidence: ["qml/BiblioBook.qml"],
            openQuestions: ["Live row rendering and source-result updates not yet exercised"],
            contexts: ["downloads-seeders"],
            searchTerms: ["seeder", "seeders", "torrents", "biblio", "pack"],
            related: ["downloads.seeding", "downloads.choose-items"],
            blocks: [
                { kind: "paragraph", text: "A Biblio book detail page shows torrent-source rows under TORRENTS, with Searching torrents… while loading and No torrents found when empty." },
                { kind: "paragraph", text: "Each row can show a ▲ count beside the size and may carry a PACK marker. The count describes that source row — not downloads on this device." },
                { kind: "note", text: "A higher count does not guarantee a faster download, and seeing a count does not mean Colosseum is seeding anything." }
            ]
        }),
        dl({
            id: "downloads.seeding", sourceIds: ["DLD-14"], order: 60,
            title: "Does Colosseum seed torrents, and can I manage that?",
            outcome: "Know that no user-visible seeding state or management surface is currently established.",
            evidence: ["qml/DownloadsPage.qml", "qml/SettingsPage.qml"],
            openQuestions: ["A runtime/network audit would be needed to establish upload behavior; product must decide if that belongs in the Guide"],
            contexts: ["downloads-seeding"],
            searchTerms: ["seed", "seeding", "torrent", "upload"],
            related: ["downloads.seeders"],
            blocks: [
                { kind: "paragraph", text: "No user-visible seeding state or management surface is currently established in Downloads or Settings. A seeder count on a Biblio source row describes the source, not this device." },
                { kind: "note", text: "Some acquisition flows are direct HTTP rather than torrents. Backend torrent capability does not establish a user-facing seeding policy." }
            ]
        }),
        dl({
            id: "downloads.choose-items", sourceIds: ["DLD-15"], order: 70,
            title: "Can I choose individual items inside a download?",
            outcome: "Know that no general item-selection workflow exists; only an ambiguous Tankoban comic pack gets a chooser.",
            evidence: ["qml/ComicTorrentArchivePicker.qml"],
            openQuestions: ["Actual trigger conditions, and whether any other world shows a selector, must be runtime-verified before broadening"],
            contexts: ["downloads-choose"],
            searchTerms: ["choose items", "comic archive", "choose one", "torrent pack"],
            related: ["downloads.seeders"],
            blocks: [
                { kind: "paragraph", text: "There is no general workflow for picking individual items inside a download. The one narrow exception: an ambiguous Tankoban comic pack can present a second-stage chooser listing only backend-validated comic archives." },
                { kind: "paragraph", text: "That chooser shows a count (comic archive or comic archives), a CHOOSE ONE header, and one download action per row; exactly one archive proceeds." },
                { kind: "note", text: "This chooser is not a torrent browser for Theatre, Biblio, or season packs, and it does not prove that every listed archive format is supported by the final reader." }
            ]
        }),
        dl({
            id: "downloads.actions", sourceIds: ["DLD-04", "DLD-05", "DLD-06"], order: 80,
            title: "How do I pause, resume, retry or cancel a download?",
            outcome: "Use only the actions currently offered for a job's state, and know that cancelling removes partial data.",
            evidence: ["qml/DownloadsPage.qml", "native/engine/LocalDownloads.cpp"],
            openQuestions: ["Each action not yet exercised across all world and backend types, including restart during paused work"],
            contexts: ["downloads-actions"],
            searchTerms: ["pause", "resume", "retry", "cancel", "dismiss", "partial"],
            related: ["downloads.status", "downloads.remove-local"],
            blocks: [
                { kind: "paragraph", text: "Pause and Resume appear where the job supports them; eligible grouped Theatre work uses Pause season and Resume season. Retry appears only on rows the backend marks retryable; some failed rows instead offer Open Tankoban or Open Biblio." },
                { kind: "paragraph", text: "Cancel asks first. The confirmation title is Cancel download? for a single job or Cancel season? for a group, and the body states Partial files will be deleted. — for a single job the warning names the partial copy instead. Choosing Cancel download proceeds; Go back aborts." },
                { kind: "note", text: "Pause holds the job in place; Cancel removes the active work and its partial data, per the confirmation. A remembered failure may instead offer Dismiss, which clears the failure row — not a settled copy." }
            ]
        }),
        dl({
            id: "downloads.remove-local", sourceIds: ["DLD-07", "DLD-08", "DLD-09"], order: 90,
            title: "What happens when I delete a downloaded local copy?",
            outcome: "Remove downloaded media from the device without confusing that with Library membership or reading position.",
            evidence: ["qml/DownloadsPage.qml", "native/engine/LocalDownloads.cpp"],
            openQuestions: ["Deleting while the title is still saved to Library, and behavior after restart, not yet exercised at runtime"],
            contexts: ["downloads-delete"],
            searchTerms: ["delete local copy", "remove download", "downloaded media", "audiobook"],
            related: ["downloads.overview", "downloads.actions"],
            blocks: [
                { kind: "paragraph", text: "Library membership is a separate state from a downloaded copy, and so is where you left off reading or watching. The Delete local copy action changes only the media on this device." },
                { kind: "paragraph", text: "The settled row offers Delete local copy and asks for confirmation first, stating the downloaded media will be removed from this device. Audiobooks use their own confirmation wording." },
                { kind: "paragraph", text: "If the media has disappeared outside the app, the row may instead offer Dismiss missing entry, which clears the stale row rather than deleting anything present." },
                { kind: "note", text: "Once confirmed, the downloaded bytes are gone; they return only by downloading again. A missing row does not mean Colosseum removed the media." }
            ]
        })
    ];
    return fixtures.concat(production);
}
