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
    function src(over) {
        return Object.assign({ section: "sources" }, draft, over);
    }
    function per(over) {
        return Object.assign({ section: "personalization" }, draft, over);
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
        // ARRAY-ORDER CONSTRAINT (2026-08-09, superseded): the contract gate used to stringify the
        // whole cohort and apply cross-record regexes (VLT "Clear" .. delete .. file/progress/
        // continue), which forced destructive-wording records to the tail of this array and kept
        // record ids free of "delete"/"control"/"seeding" tokens. Commit 6b33e41 scoped the gate
        // per-lesson, so that constraint no longer exists — the array order below is simply kept
        // as-is; display order is the `order` field either way.
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
                { kind: "paragraph", text: "Cancel asks first. The confirmation title is Cancel download? for a single job or Cancel season? for a group, and the body states Partial files will be deleted. or The partial file will be deleted. Choosing Cancel download proceeds; Go back aborts." },
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
        }),
        // EXT-01..EXT-10 distilled from Batch 6 (Extensions & sources; packet frozen at a149e94f,
        // ancestor of the pinned base 6b33e41). Draft, except EXT-02 and EXT-09 which stay
        // "uncertain": EXT-02 carries a live product/repository conflict (locked rule says no
        // source is assumed enabled; current seed code enables house wells) and must state it as
        // unresolved; EXT-09's visible Settings label does not open a working surface. "Do not
        // claim" is law: worlds are not identical, not every install is an acquisition source,
        // reinstall is not a first-line fix, paste does not install before preview, no adult
        // reveal setting, no house settings sheet, disable/remove are not destructive to config
        // or media, and top rank never guarantees the best result.
        src({
            id: "sources.extensions-overview", sourceIds: ["EXT-01", "EXT-10"], order: 10,
            title: "What is an extension, and where does it apply?",
            outcome: "Know that extensions add capabilities to the existing worlds without becoming new worlds.",
            evidence: ["qml/ExtensionsPage.qml", "qml/ExtensionsSources.qml", "native/engine/ExtensionsStore.cpp"],
            openQuestions: ["At least one source-consuming flow per world, with a relevant well enabled and disabled, not yet exercised"],
            contexts: ["sources-overview"],
            searchTerms: ["extension", "sources", "installed", "catalogue", "theatre", "tankoban", "biblio"],
            related: ["sources.manage", "sources.install", "sources.order"],
            blocks: [
                { kind: "paragraph", text: "An extension supplies capabilities — catalogues, acquisition or playback sources, subtitles, or other roles — to one or more of the existing worlds. An extension is not a new world." },
                { kind: "paragraph", text: "The Extensions utility groups installed rows by job: catalogue rows fill shelves, source and well rows fetch playable or downloadable material, and other roles sit separately. World membership comes from the extension's manifest, so one extension can serve more than one world." },
                { kind: "steps", items: ["Open Extensions from the expanded taskbar.", "Use Sources to see capability rows across the worlds.", "Use Installed · and choose Theatre, Tankoban, or Biblio to see what applies in that world."] },
                { kind: "note", text: "Applied to multiple worlds does not mean identical in each world: roles, content types, and consuming surfaces differ by world. Installed also does not mean enabled everywhere." }
            ]
        }),
        src({
            id: "sources.first-run", sourceIds: ["EXT-02"], order: 20,
            title: "Why might I have no sources enabled yet?",
            outcome: "Know that the first-run source state is not yet resolved by the product.",
            status: "uncertain",
            evidence: ["native/engine/ExtensionsStore.cpp", "qml/ExtensionsPage.qml"],
            openQuestions: ["A fresh-profile runtime check must decide whether seeded house wells start enabled, then product must reconcile that with the locked no-assumed-source rule"],
            contexts: ["sources-first-run"],
            searchTerms: ["first run", "fresh install", "sources enabled", "default sources"],
            related: ["sources.extensions-overview", "sources.manage"],
            blocks: [
                { kind: "paragraph", text: "The product rule says acquisition sources are optional and none should be assumed active on first use. Current code, however, seeds house defaults when the extension index is empty, and those seed entries are created enabled." },
                { kind: "note", text: "The two descriptions disagree, and the conflict is not yet resolved. Neither fresh installs-with-sources nor fresh installs-without-sources should be treated as the confirmed state until a clean-profile run settles it." }
            ]
        }),
        src({
            id: "sources.install", sourceIds: ["EXT-03", "EXT-04"], order: 30,
            title: "How do I install an extension?",
            outcome: "Install a listed extension from the store, or add one by manifest link after previewing what it offers.",
            evidence: ["qml/ExtensionsPage.qml", "native/engine/ExtensionsStore.cpp"],
            openQuestions: ["Store install success and failure, valid and malformed link previews, and duplicate installs not yet exercised"],
            contexts: ["sources-install"],
            searchTerms: ["install extension", "browse everything", "install from a link", "manifest", "read it first"],
            related: ["sources.extensions-overview", "sources.manage"],
            blocks: [
                { kind: "paragraph", text: "Browse everything lists extensions with Install, Installing…, and Installed states, and marks built-in rows as Built-in. Choose Install on the intended extension." },
                { kind: "steps", items: ["Open Browse everything.", "Search if useful.", "Choose Install on the intended extension.", "Wait for Installing… to resolve."] },
                { kind: "paragraph", text: "Install from a link › opens a sheet that reads the manifest first. The button reads Read it first until the preview succeeds, then Install <name>; Cancel closes the sheet." },
                { kind: "steps", items: ["Choose Install from a link ›.", "Paste the extension address.", "Choose Read it first.", "Review the returned name, resources, and host.", "Choose Install <name> only for the intended extension."] },
                { kind: "note", text: "A pasted address is not installed before the preview. An arbitrary webpage address is not automatically a valid manifest, and installing never guarantees useful results." }
            ]
        }),
        src({
            id: "sources.manage", sourceIds: ["EXT-05", "EXT-06"], order: 40,
            title: "How do I turn an extension off or remove it?",
            outcome: "Temporarily stop a removable extension, or remove it deliberately with its multi-world impact made visible.",
            evidence: ["qml/ExtensionsPage.qml", "native/engine/ExtensionsStore.cpp"],
            openQuestions: ["Disable and re-enable per world, second-press multi-world removal, and reinstall-after-remove not yet exercised"],
            contexts: ["sources-manage"],
            searchTerms: ["disable extension", "enable extension", "remove extension", "locked", "installed"],
            related: ["sources.extensions-overview", "sources.install"],
            blocks: [
                { kind: "paragraph", text: "Non-core installed rows carry an on/off switch: switching it off leaves the extension installed but idle. Core catalogue rows are Locked, with no toggle and no remove action. Non-core rows expose Remove." },
                { kind: "steps", items: ["Switch a non-core row off to disable it without removing it.", "Choose Remove to remove a non-core extension.", "If a multi-world warning appears, read the named worlds, and choose Remove again only if removal is still intended."] },
                { kind: "note", text: "Removal is extension-wide, not per world: the warning reads <name> feeds <world> and <world>. Removing it takes it out of both — press Remove again to confirm." },
                { kind: "note", text: "Disabling keeps configuration and downloads in place, and removing an extension does not touch downloaded media." }
            ]
        }),
        src({
            id: "sources.order", sourceIds: ["EXT-07"], order: 50,
            title: "Why does source order matter?",
            outcome: "Change the order in which a world's sources are asked, knowing the top is asked first.",
            evidence: ["qml/ExtensionsPage.qml"],
            openQuestions: ["Reorder persistence after restart, and the observable ask-order effect in each world, not yet exercised"],
            contexts: ["sources-order"],
            searchTerms: ["source order", "reorder", "sources asked first", "top first", "rank"],
            related: ["sources.extensions-overview", "sources.manage"],
            blocks: [
                { kind: "paragraph", text: "The page states: Order matters: when you press play, sources are asked in this order, top first. Reorderable source and well rows show a world-relative numeric rank plus ▲ and ▼; catalogue rows are not ranked." },
                { kind: "steps", items: ["Open Installed ·.", "Choose the relevant world.", "Use ▲ or ▼ to move one source.", "Confirm its rank changed."] },
                { kind: "note", text: "The rank is computed among the wells relevant to the selected world, so a shared source can hold a different position in another world without becoming a second install. Being asked first does not mean a source will succeed or return the best result." }
            ]
        }),
        src({
            id: "sources.configure-external", sourceIds: ["EXT-08"], order: 60,
            title: "How do I configure an extension that has its own settings page?",
            outcome: "Open the extension-owned configuration page when the manifest marks an external extension configurable.",
            evidence: ["qml/ExtensionsPage.qml", "native/engine/ExtensionsStore.cpp"],
            openQuestions: ["One valid configurable external extension and the return-to-app behavior not yet exercised"],
            contexts: ["sources-configure"],
            searchTerms: ["configure extension", "configure", "external settings", "open extension page"],
            related: ["sources.install", "sources.settings-builtin"],
            blocks: [
                { kind: "paragraph", text: "A configurable external extension shows Configure ↗. Choosing it leaves Colosseum and opens the extension's own configuration page, derived from its manifest address." },
                { kind: "steps", items: ["Find the configurable external extension.", "Choose Configure ↗.", "Complete any configuration on the extension's page.", "Return to Colosseum and recheck the row."] },
                { kind: "note", text: "This is extension-owned configuration, not a Colosseum settings surface, and not every installed extension offers it." }
            ]
        }),
        src({
            id: "sources.settings-builtin", sourceIds: ["EXT-09"], order: 70,
            title: "How do I change settings for a built-in source?",
            outcome: "Know that the built-in source settings surface has not landed yet.",
            status: "uncertain",
            evidence: ["qml/ExtensionsPage.qml"],
            openQuestions: ["Re-audit immediately when the indexer settings sheet lands; capture exact options only from that build"],
            contexts: ["sources-settings"],
            searchTerms: ["built-in settings", "source settings", "settings", "indexer"],
            related: ["sources.configure-external", "sources.manage"],
            blocks: [
                { kind: "paragraph", text: "A configurable house or built-in source row can show the label Settings, but choosing it does not open a configuration surface. The current notice ends with settings arrive with the indexer sheet." },
                { kind: "note", text: "No in-app settings sheet exists at this commit. Do not expect indexers, languages, quality filters, or other options to be changeable in-app yet; the label is not the same route as Configure ↗." }
            ]
        }),
        // PER-01..PER-16 distilled from Batch 7 (Personalization, persistence, privacy, and
        // updates; packet frozen at commit c175c193, ancestor of the pinned base e42a5ed).
        // Draft, except PER-12/13/14 which stay "uncertain": PER-12 (privacy) — local stores and
        // network-capable clients are proven but a complete telemetry/data policy is NOT;
        // PER-13 (reset) — no global reset control exists and internal stores are not a user
        // reset contract; PER-14 (accessibility) — no dedicated global accessibility surface
        // exists and the internal reducedMotion input is not a user-facing setting. Batch-7
        // "Do not claim" is law: no nothing-leaves/no-telemetry/all-local claims, no
        // %APPDATA%/QSettings surgery, no global accessibility suite, no Explicit-Content
        // violence/horror/parental claims, no always-global wallpaper, no offline online-search,
        // no sessions-survive-restart, no cloud sync, and Continue/Library/local-copy removals
        // never delete each other's state.
        per({
            id: "personalization.wallpapers", sourceIds: ["PER-01", "PER-02"], order: 10,
            title: "How do I change a wallpaper, and can each world use a different one?",
            outcome: "Choose a built-in or searched wallpaper and apply it everywhere or only to the current world.",
            evidence: ["qml/TopBar.qml", "qml/WallpaperSearch.qml", "qml/Main.qml"],
            openQuestions: ["Persistence after restart for all four scopes, built-in animated/native apply, and online success/no-result/error, not yet exercised at runtime"],
            contexts: ["wallpapers"],
            searchTerms: ["wallpaper", "change wallpaper", "for all worlds", "per world", "animated", "native"],
            related: ["personalization.persisted-state"],
            blocks: [
                { kind: "paragraph", text: "A wallpaper picker opens from the wallpaper icon in the top bar of Home, Tankoban, Biblio, or Theatre. It offers built-in choices — Colosseum Animated and Colosseum Native — plus online search when a network connection is available." },
                { kind: "steps", items: ["Open the wallpaper picker from the top bar.", "Choose a built-in tile, or search and choose a result.", "Preview the selected wallpaper.", "Choose For All Worlds to apply the same pick to Home, Tankoban, Biblio, and Theatre, or For <target world> to change only that world.", "Close the picker and confirm the intended scope changed."] },
                { kind: "paragraph", text: "Separate picks are stored for Home, Tankoban, Biblio, and Theatre. For All Worlds writes the same choice to all four scopes; the one-world choice changes only that scope." },
                { kind: "note", text: "Wallpaper personalization is separate from the app-wide Settings page. A searched wallpaper depends on the online search result; the built-in animated and native choices do not." }
            ]
        }),
        per({
            id: "personalization.explicit-content", sourceIds: ["PER-03", "PER-04"], order: 20,
            title: "How do I show or hide sexually explicit titles, and what does that setting filter?",
            outcome: "Change the current Explicit Content preference and understand its deliberately narrow scope.",
            evidence: ["qml/SettingsPage.qml", "qml/ContentPreferences.qml", "qml/ExplicitContentPolicy.js"],
            openQuestions: ["Clean-profile default, persistence after restart, and representative filtering in Theatre, Tankoban, and Biblio, not yet exercised at runtime"],
            contexts: ["content-preferences"],
            searchTerms: ["explicit content", "content settings", "mature", "settings", "sexually explicit"],
            related: ["personalization.persisted-state"],
            blocks: [
                { kind: "paragraph", text: "The app-wide Settings page has one content preference section, CONTENT, with a single switch: Explicit Content. It reads and writes the shell's one content-preferences store." },
                { kind: "paragraph", text: "The setting's own copy reads: \"Show sexually explicit titles across Theatre, Tankoban, and Biblio. Violence, horror, mature themes, and standard age ratings are not filtered.\"" },
                { kind: "steps", items: ["Open Settings.", "Find Explicit Content under CONTENT.", "Turn the switch on to allow sexually explicit titles through the current content policy, or off to hide them where that policy applies.", "Return to the relevant world and observe its catalogue or search treatment."] },
                { kind: "note", text: "The preference is global across the three worlds rather than world-specific, and it is not a parental control or an account system. Toggling it never changes already-downloaded files." }
            ]
        }),
        per({
            id: "personalization.persisted-state", sourceIds: ["PER-05", "PER-06", "PER-07"], order: 30,
            title: "What does Colosseum remember after I close the app?",
            outcome: "Know which common user states come back after a restart and which belong only to the current run.",
            evidence: ["native/ProgressStore.h", "native/CollectionStore.h", "native/SearchHistoryStore.h", "native/SessionStore.h", "qml/Main.qml", "qml/ContentPreferences.qml"],
            openQuestions: ["Each durable state across a full restart in the target packaged build, not merely a QML reload, not yet exercised"],
            contexts: ["persistence"],
            searchTerms: ["remember", "restart", "close app", "continue", "library", "recent", "session", "persist"],
            related: ["personalization.remove-content", "vault.recent-files", "downloads.remove-local"],
            blocks: [
                { kind: "paragraph", text: "Several common states are durable and reappear after a restart: Continue progress, the Library collection, recent searches, wallpaper picks, the Explicit Content preference, Open Recent shortcuts, and downloaded files until their owning delete flow removes them." },
                { kind: "paragraph", text: "Open media-session tiles in the taskbar live in the current run's session store. They are not rebuilt as taskbar sessions after a restart; Continue is the restart-safe way to resume persisted progress." },
                { kind: "bullets", items: ["Continue — persisted reading or viewing progress", "Library — deliberately saved titles", "Open Recent — remembered local-file paths", "Downloads — local copies that stay until deleted", "Wallpaper picks and the Explicit Content preference — persisted preferences"] },
                { kind: "note", text: "An open session, a Continue entry, a Library save, an Open Recent shortcut, and a downloaded local copy are five different states. Persistence here means storage on this device; it is not a synchronization service." }
            ]
        }),
        per({
            id: "personalization.remove-content", sourceIds: ["PER-08", "PER-09", "PER-10"], order: 40,
            title: "How do I remove something from Continue, Library, or this device?",
            outcome: "Choose the correct removal action without treating progress, collection membership, and local-file ownership as the same state.",
            evidence: ["qml/ContinueTile.qml", "qml/LibraryButton.qml", "qml/DownloadsPage.qml", "native/ProgressStore.h", "native/CollectionStore.h"],
            openQuestions: ["Each action while the same title simultaneously has Continue, Library, and downloaded state, then a restart, not yet exercised at runtime"],
            contexts: ["cleanup"],
            searchTerms: ["remove", "remove from continue", "in library", "local copy", "forget progress", "unsave"],
            related: ["personalization.persisted-state", "downloads.remove-local", "vault.recent-files"],
            blocks: [
                { kind: "paragraph", text: "Three separate actions exist: Remove from Continue forgets a title's progress; using In Library again removes the title from your saved collection; Delete local copy removes the downloaded media from this device. They are not one shared delete operation." },
                { kind: "steps", items: ["To forget progress: choose Remove from Continue on the Continue tile.", "To stop saving a title: choose In Library again to remove its Library membership.", "To remove downloaded bytes: open Downloads, choose Delete local copy, and confirm the destructive file-deletion message only if that is intended."] },
                { kind: "paragraph", text: "Removing a title from Continue never touches downloaded files. Taking a title out of Library leaves its progress and any downloaded copy in place. Deleting a local copy removes the media on this device; Library membership and Continue progress remain." },
                { kind: "note", text: "Open Recent Clear is a fourth, shortcut-only action and is separate from all three." }
            ]
        }),
        per({
            id: "personalization.support-evidence", sourceIds: ["PER-11"], order: 50,
            title: "What information should I include when reporting a problem?",
            outcome: "Collect precise evidence for a reproducible support report before turning to destructive recovery.",
            evidence: ["native/engine/AppLog.cpp", "native/engine/AppLog.h", "native/main.cpp"],
            openQuestions: ["Exact packaged Windows log path and representative log contents not yet reviewed in the target build"],
            contexts: ["support"],
            searchTerms: ["report a problem", "support", "log", "error report", "what to include"],
            related: ["personalization.privacy", "personalization.reset"],
            blocks: [
                { kind: "paragraph", text: "Colosseum installs an always-on rolling application log during startup, written to a logs folder under the application-data location as colosseum.log. It rotates at roughly 5 MB and keeps numbered prior files." },
                { kind: "bullets", items: ["screen or world", "content or title", "source or extension when relevant", "the exact action you took", "the exact visible error text", "whether it happens every time", "the approximate time", "a relevant recent portion of colosseum.log when deeper evidence is needed"] },
                { kind: "steps", items: ["Reproduce the problem once if it is safe to do so.", "Record the exact screen, content, and action.", "Copy the exact visible error text.", "Note whether it happens every time.", "If deeper evidence is needed, collect only the relevant recent portion of colosseum.log.", "Keep the time of the failure so the log lines can be matched."] },
                { kind: "note", text: "There is no automatic support-submission workflow and no dedicated Report problem button established. The log is support evidence, not a reset mechanism: collecting it is not a recovery step, and a relevant log tail is not a reason to send every application file." }
            ]
        }),
        per({
            id: "personalization.privacy", sourceIds: ["PER-12"], order: 60,
            title: "What information leaves my computer?",
            outcome: "Know what the current evidence does and does not establish about local storage and network behavior.",
            status: "uncertain",
            evidence: ["native/ProgressStore.h", "native/CollectionStore.h", "native/SearchHistoryStore.h", "qml/ContentPreferences.qml", "native/engine/AppLog.cpp", "native/update/UpdateReleaseClient.cpp", "native/update/UpdateService.cpp", "qml/WallpaperApi.js"],
            openQuestions: ["A complete network trace and product-approved privacy statements remain blocking; no complete outbound-data inventory exists yet"],
            contexts: ["privacy"],
            searchTerms: ["privacy", "data", "network", "telemetry", "what leaves", "personal data"],
            related: ["personalization.persisted-state", "personalization.support-evidence"],
            blocks: [
                { kind: "paragraph", text: "This article cannot yet give a complete answer. The current evidence proves two separate categories but not a complete privacy or data policy." },
                { kind: "paragraph", text: "Known local persistence includes Continue progress, the Library collection, recent searches, wallpaper picks, the Explicit Content preference, Open Recent shortcuts, and a local rolling log. Known network-capable behavior includes catalogue, search, and discovery providers, extension registry and manifest handling, source and acquisition queries, online subtitle and wallpaper services when invoked, and updater release checks and downloads." },
                { kind: "note", text: "Those facts are not enough to list every outbound field, rule out analytics or telemetry, state a retention policy, or describe how every extension and provider handles data. The full privacy picture is unresolved until a technical network audit and a product-approved statement exist." },
                { kind: "note", text: "Before that evidence lands, treat the app's actual offline-capable local content paths as the only offline behavior you rely on, and avoid enabling online features you do not want to invoke." }
            ]
        }),
        per({
            id: "personalization.reset", sourceIds: ["PER-13"], order: 70,
            title: "How do I reset all Colosseum settings and history?",
            outcome: "Know that no supported global reset exists today, and use only the scoped controls the app offers.",
            status: "uncertain",
            evidence: ["qml/SettingsPage.qml", "native/ProgressStore.h", "native/CollectionStore.h", "native/SearchHistoryStore.h"],
            openQuestions: ["Re-audit whenever a reset or recovery UI lands; scope must state each include/exclude decision explicitly"],
            contexts: ["reset"],
            searchTerms: ["reset", "reset all", "clear all data", "factory reset", "start fresh"],
            related: ["personalization.remove-content", "personalization.support-evidence"],
            blocks: [
                { kind: "paragraph", text: "Current Settings exposes the Explicit Content preference only, and no global reset, Reset all, Clear all data, or Factory reset action exists. Colosseum state is spread across several durable stores and downloaded media, and none of them has a supported wipe-it-all control." },
                { kind: "steps", items: ["To forget progress: use Remove from Continue.", "To stop saving a title: use In Library again.", "To clear remembered local-file shortcuts: use Open Recent Clear.", "To free storage: use Delete local copy with confirmation.", "To change a preference: toggle it back."] },
                { kind: "note", text: "Internal settings keys and application-data directories are implementation details, not a user reset contract. They are not a supported way to start fresh; do not delete them to simulate a reset." }
            ]
        }),
        per({
            id: "personalization.accessibility", sourceIds: ["PER-14"], order: 80,
            title: "Which accessibility settings does Colosseum provide?",
            outcome: "Know that no dedicated global accessibility settings surface exists yet, and route comfort needs to the owning reader or player controls.",
            status: "uncertain",
            evidence: ["qml/SettingsPage.qml", "qml/UpdatePage.qml"],
            openQuestions: ["Re-audit whenever a global accessibility preferences surface lands; separately verify keyboard focus and assistive-technology behavior before any compliance claim"],
            contexts: ["accessibility"],
            searchTerms: ["accessibility", "reduced motion", "font size", "contrast", "assistive technology"],
            related: ["personalization.comfort-controls"],
            blocks: [
                { kind: "paragraph", text: "The app-wide Settings page currently contains the CONTENT preference only; no dedicated Accessibility section exists there." },
                { kind: "paragraph", text: "Reader and player surfaces offer real aids — comic layout, zoom, and page navigation; book typography, theme, and layout; audio-track selection; subtitle selection and timing. These are feature controls in their owning surfaces, not a proven global accessibility preferences area." },
                { kind: "note", text: "The update surface accepts an internal reduced-motion input, but no user-facing control changes it. Treat that input as internal plumbing, not a product preference." },
                { kind: "note", text: "Until a dedicated accessibility surface lands, route comfort and comprehension needs through the owning reader or player controls. No assistive-technology certification is established." }
            ]
        }),
        per({
            id: "personalization.comfort-controls", sourceIds: ["PER-15", "PER-16"], order: 90,
            title: "Which existing controls can make reading or video easier to follow?",
            outcome: "Find reversible reader and player controls that may improve comfort or comprehension.",
            evidence: ["qml/comicreader/ComicReaderShell.qml", "qml/reader2/AppearancePanel.qml", "qml/PlayerPage.qml", "qml/SubtitleMenu.qml", "qml/AudioMenu.qml"],
            openQuestions: ["Each owning feature remains behind its batch runtime gate; no runtime exercise was performed in this pass"],
            contexts: ["comfort"],
            searchTerms: ["comfort", "larger text", "subtitle timing", "sync", "zoom", "reading direction", "audio track", "subtitles off"],
            related: ["personalization.accessibility"],
            blocks: [
                { kind: "paragraph", text: "Inside the comic reader, book reader, and Theatre player, several reversible controls can help: comic page layout, reading direction, zoom, and page navigation; book typography, theme, layout, and keyboard navigation; video audio-track selection, subtitle selection with an Off state, and subtitle timing steps of -0.1 and +0.1 seconds beside a SYNC label." },
                { kind: "steps", items: ["Open the relevant reader or player.", "Choose the control that addresses the concrete issue: text or layout comfort, page navigation, audio track, subtitle track, or subtitle timing.", "Adjust it; every change here is reversible.", "Return to the previous value if the change does not help."] },
                { kind: "note", text: "These are feature controls owned by the comic reader, book reader, and Theatre player — not a single global comfort preset and not a certified accessibility suite. One setting does not apply to every media type." }
            ]
        })
    ];
    return fixtures.concat(production);
}
