import QtQuick
import QtCore
import QtTest
import "../../qml"
import "../../qml/AddonClient.js" as AddonClient
import "../../qml/EpisodeBrowser.js" as EpisodeBrowser

Item {
    id: fixture
    width: 1280; height: 900
    Settings {
        id: prefs
        location: typeof SmartPlayTestSettingsUrl !== "undefined" ? SmartPlayTestSettingsUrl
                  : Qt.resolvedUrl("../../smartplay-test-player.ini")
        category: "transport"
    }
    QtObject {
        id: stremio
        property var requests: []
        property bool enabled: true
        function installed() { return enabled ? [{ enabled: true, id: "fixture", transportUrl: "https://example.org",
            manifest: { name: "Fixture", resources: ["stream"], types: ["movie", "series"] } }] : [] }
        function load(exts, type, id, partial, done) { requests = requests.concat([{ partial, done }]) }
    }
    QtObject {
        id: nuvio
        property int lastId: 0
        signal streamsReady(int requestId, var streams)
        signal finished(int requestId)
        function resolve() { return ++lastId }
        function cancel() {}
    }
    SourcesSheet { id: sheet; extensionsService: stremio; loadStremioStreams: stremio.load; nuvioSourceService: nuvio }
    SmartPickToggle { id: frontToggle; sourcePicker: sheet; y: 750 }
    SmartPickToggle { id: mirroredToggle; sourcePicker: sheet; x: 300; y: 750 }
    SignalSpy { id: playSpy; target: sheet; signalName: "playRequested" }
    SignalSpy { id: downloadSpy; target: sheet; signalName: "downloadRequested" }
    SignalSpy { id: seasonSpy; target: sheet; signalName: "seasonNoPacks" }
    SignalSpy { id: pickingSpy; target: sheet; signalName: "smartPickingChanged" }

    TestCase {
        name: "SmartPlay"
        when: windowShown
        property var selectionHarness: null
        function init() {
            prefs.setValue("smartPlayQuality", "1080p"); prefs.sync()
            prefs.setValue("smartPlayLastQuality", "1080p"); prefs.sync()
            prefs.setValue("preferredAudioLanguages", "eng,jpn"); prefs.sync()
            sheet.mediaMetadata = ({})
            var settings = findChild(sheet, "smartPlaySettings")
            if (settings) settings.location = prefs.location
            sheet.refreshSmartPlayQuality()
            stremio.requests = []; stremio.enabled = true
            playSpy.clear(); downloadSpy.clear(); seasonSpy.clear()
        }
        function cleanup() {
            sheet.hide()
            fixture.Window.window.width = 1280
            fixture.width = 1280
            findChild(frontToggle, "smartPickExplanation").close()
            findChild(frontToggle, "smartPickInfo").focus = false
            if (selectionHarness) { selectionHarness.destroy(); selectionHarness = null }
        }
        function row(index, quality, source, episode) {
            return AddonClient.parseStream({ infoHash: String(index + 1).repeat(40), fileIdx: index,
                title: "Fixture.2020." + (episode || "") + quality + "." + (source || "WEB-DL") + ".x265.mkv\n👤 100 💾 2 GB",
                behaviorHints: { filename: "Fixture.2020." + (episode || "") + quality + "." + (source || "WEB-DL") + ".mkv" }
            }, "Torrentio", 0, "fixture")
        }
        function show(mode) { sheet.show("movie", "tt12345", "Fixture", { title: "Fixture", year: 2020 }, mode) }
        function finish(rows) { stremio.requests[0].done(rows); nuvio.finished(nuvio.lastId) }

        function test_play_activatesPick_withoutReordering() {
            var rows = [row(0, "720p"), row(1, "1080p")]
            show(); finish(rows)
            tryCompare(playSpy, "count", 1)
            compare(playSpy.signalArguments[0][0], "2".repeat(40))
            compare(sheet.visibleRows.map(r => r.infoHash), ["1".repeat(40), "2".repeat(40)])
            compare(sheet.smartPlayRow.infoHash, "2".repeat(40))
            tryVerify(() => findChild(sheet, "sourceBestMatch_1") !== null)
            compare(findChild(sheet, "sourceBestMatch_1").visible, false)
            compare(sheet.smartPicking, true)
        }
        function test_noPick_keepsSheetOpen() {
            show(); finish([row(0, "1080p", "CAM")])
            wait(0); compare(playSpy.count, 0); compare(sheet.open, true)
            compare(sheet.smartPlayStatus, "No source passed the quality check. Choose one below.")
        }
        function test_manualTap_duringLoading_wins() {
            var rows = [row(0, "720p"), row(1, "1080p")]
            show(); stremio.requests[0].partial(rows)
            var manual = findChild(sheet, "smartPickingManualAction")
            verify(manual !== null, "Pending screen must offer a manual escape")
            mouseClick(manual, 60, 24)
            tryVerify(() => findChild(sheet, "sourceRow_0") !== null)
            mouseClick(findChild(sheet, "sourceRow_0"), 160, 60)
            compare(playSpy.count, 1); compare(playSpy.signalArguments[0][0], "1".repeat(40))
            finish(rows); wait(0); compare(playSpy.count, 1)
        }
        function test_pendingScreen_hidesInteractiveSources_untilHandoff() {
            show()
            var rows = [row(0, "720p"), row(1, "1080p")]
            stremio.requests[0].partial(rows)
            var list = findChild(sheet, "sourcesSheetList")
            compare(list.visible, false); compare(list.enabled, false)
            compare(sheet.smartPicking, true)
            var screen = findChild(sheet, "smartPickingScreen")
            verify(screen !== null); compare(screen.visible, true)
            compare(findChild(sheet, "sourcesSheetTable").visible, false)
            compare(findChild(sheet, "extPickerTrigger").visible, false)
            compare(findChild(sheet, "sourceQuality_all").visible, false)
            verify(!list.activeFocus)
            compare(sheet.visibleRows.map(r => r.infoHash), rows.map(r => r.infoHash))
            pickingSpy.clear()
            finish(rows); tryCompare(playSpy, "count", 1)
            compare(pickingSpy.count, 0, "Picking screen must remain continuously visible through playback handoff")
            compare(sheet.smartPicking, true)
            compare(list.visible, false)
            compare(screen.allowManual, false)
            sheet.hide(); compare(screen.visible, false)
        }
        function test_manualEscape_cancelsLateCompletionAndTimeout_onlyThisVisit() {
            show()
            var request = stremio.requests[0]
            var manual = findChild(sheet, "smartPickingManualAction")
            verify(manual !== null, "Pending screen must offer a manual escape")
            manual.forceActiveFocus(); keyClick(Qt.Key_Space)
            compare(sheet.smartPicking, false)
            compare(sheet.smartPlayCancelled, true)
            compare(sheet.loading, true)
            prefs.sync(); compare(prefs.value("smartPlayQuality"), "1080p")
            request.partial([row(0, "1080p")])
            compare(findChild(sheet, "sourcesSheetList").visible, true)
            var timeout = findChild(sheet, "sourcesTimeout")
            timeout.interval = 1; timeout.restart(); wait(10)
            timeout.interval = 22000
            nuvio.finished(nuvio.lastId)
            request.done([row(0, "1080p")])
            sheet.finishSmartPlay(true)
            wait(0); compare(playSpy.count, 0)
            sheet.hide(); show()
            compare(sheet.smartPicking, true)
            request.done([row(0, "1080p")]); compare(playSpy.count, 0)
            stremio.requests[1].done([row(1, "1080p")]); nuvio.finished(nuvio.lastId)
            tryCompare(playSpy, "count", 1)
        }
        function test_pendingBack_cancelsLateReply_andReopenStartsFresh() {
            show()
            var stale = stremio.requests[0]
            verify(sheet.requestEscape())
            compare(sheet.open, false); compare(sheet.smartPicking, false)
            compare(sheet.smartPlayCancelled, true)
            stale.done([row(0, "1080p")]); nuvio.finished(nuvio.lastId)
            compare(playSpy.count, 0)
            show(); compare(sheet.smartPicking, true)
            stremio.requests[1].done([row(1, "1080p")]); nuvio.finished(nuvio.lastId)
            tryCompare(playSpy, "count", 1)
        }
        function test_pendingScreen_narrowBounds_andManualFallback() {
            fixture.Window.window.width = 320
            fixture.width = 320
            show(); wait(0)
            var manual = findChild(sheet, "smartPickingManualAction")
            var screen = findChild(sheet, "smartPickingScreen")
            verify(screen !== null, "Automatic picking must replace the browser with its pending screen")
            compare(screen.width, 320)
            verify(manual.width >= 44); verify(manual.height >= 44)
            var position = manual.mapToItem(screen, 0, 0)
            verify(position.x >= 0); verify(position.x + manual.width <= screen.width)
            finish([row(0, "1080p", "CAM")]); wait(0)
            compare(screen.visible, false); compare(playSpy.count, 0)
            compare(findChild(sheet, "sourcesSheetTable").visible, true)
            compare(sheet.smartPlayStatus, "No source passed the quality check. Choose one below.")
        }
        function test_pendingScreen_doesNotReplaceOffDownloadOrSeason() {
            prefs.setValue("smartPlayQuality", "off"); prefs.sync()
            show(); compare(sheet.smartPicking, false); sheet.hide()
            prefs.setValue("smartPlayQuality", "1080p"); prefs.sync()
            show("download"); compare(sheet.smartPicking, false); sheet.hide()
            sheet.show("series", "tt12345:1:1", "Fixture", { season: 1 }, "season")
            compare(sheet.smartPicking, false)
        }
        function test_off_preservesManualSelection() {
            prefs.setValue("smartPlayQuality", "off"); prefs.sync()
            show(); finish([row(0, "1080p")]); wait(0)
            compare(playSpy.count, 0); compare(sheet.open, true)
            compare(sheet.smartPlayStatus, "")
            sheet.activateSource(0); compare(playSpy.count, 1)
        }
        function test_settingChanges_applyOnReopen() {
            var uhd = row(1, "2160p"); uhd.size = "10 GB"
            var rows = [row(0, "1080p"), uhd]
            show(); finish(rows); tryCompare(playSpy, "count", 1)
            compare(playSpy.signalArguments[0][0], "1".repeat(40)); sheet.hide()
            prefs.setValue("smartPlayQuality", "4k"); prefs.sync()
            show(); stremio.requests[1].done(rows); nuvio.finished(nuvio.lastId)
            tryCompare(playSpy, "count", 2); compare(playSpy.signalArguments[1][0], "2".repeat(40)); sheet.hide()
            prefs.setValue("smartPlayQuality", "off"); prefs.sync()
            show(); stremio.requests[2].done(rows); nuvio.finished(nuvio.lastId); wait(0)
            compare(playSpy.count, 2); compare(sheet.smartPlayEnabled, false)
        }
        function test_frontToggle_savesOff_andRestores4k() {
            prefs.setValue("smartPlayQuality", "4k"); prefs.sync()
            sheet.refreshSmartPlayQuality()
            sheet.setSmartPickEnabled(false)
            compare(sheet.smartPlayQuality, "off")
            prefs.sync(); compare(prefs.value("smartPlayQuality"), "off")
            show(); finish([row(0, "1080p")]); wait(0)
            compare(playSpy.count, 0); compare(sheet.open, true)
            sheet.hide()
            sheet.setSmartPickEnabled(true)
            compare(sheet.smartPlayQuality, "4k")
            prefs.sync(); compare(prefs.value("smartPlayQuality"), "4k")
            var uhd = row(1, "2160p"); uhd.size = "10 GB"
            show(); stremio.requests[1].done([row(0, "1080p"), uhd]); nuvio.finished(nuvio.lastId)
            tryCompare(playSpy, "count", 1)
            compare(playSpy.signalArguments[0][0], "2".repeat(40))
        }
        function test_control_pointerAndSpace_persistAndMirror() {
            prefs.setValue("smartPlayQuality", "4k"); prefs.sync()
            sheet.refreshSmartPlayQuality()
            var action = findChild(frontToggle, "smartPickSwitch")
            mouseClick(action, 40, 22)
            compare(frontToggle.checked, false)
            compare(mirroredToggle.checked, false)
            prefs.sync(); compare(prefs.value("smartPlayQuality"), "off")
            show(); finish([row(0, "1080p")]); wait(0)
            compare(playSpy.count, 0); compare(sheet.open, true)
            sheet.hide()
            action.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(frontToggle.checked, true)
            compare(mirroredToggle.checked, true)
            prefs.sync(); compare(prefs.value("smartPlayQuality"), "4k")
        }
        function test_control_info_isSeparateAndBounded() {
            var info = findChild(frontToggle, "smartPickInfo")
            var explanation = findChild(frontToggle, "smartPickExplanation")
            fixture.Window.window.width = 320
            frontToggle.width = 240
            wait(0)
            findChild(frontToggle, "smartPickSwitch").forceActiveFocus()
            mouseMove(info, 22, 22)
            var tooltip = findChild(frontToggle, "smartPickHoverExplanation")
            verify(tooltip !== null)
            tryCompare(tooltip, "visible", true)
            verify(tooltip.width <= 296)
            verify(tooltip.x >= 0)
            verify(tooltip.x + tooltip.width <= tooltip.parent.width)
            for (var i = 0; i < 5; ++i) {
                compare(info.hovered, true)
                compare(tooltip.visible, true)
                wait(100)
            }
            mouseClick(info, 22, 22)
            tryCompare(explanation, "visible", true)
            compare(frontToggle.checked, true)
            prefs.sync(); compare(prefs.value("smartPlayQuality"), "1080p")
            verify(info.x + info.width <= frontToggle.width)
            verify(explanation.width <= explanation.parent.width - 24)
            verify(explanation.width <= 296)
            verify(explanation.x >= 0)
            verify(explanation.x + explanation.width <= explanation.parent.width)
            explanation.close()
            findChild(frontToggle, "smartPickSwitch").forceActiveFocus()
            info.forceActiveFocus()
            keyClick(Qt.Key_Space)
            tryCompare(explanation, "visible", true)
            keyClick(Qt.Key_Escape)
            tryCompare(explanation, "visible", false)
            frontToggle.width = frontToggle.implicitWidth
        }
        function test_control_hover_staysStable_andLeavesPopoverClosed() {
            var info = findChild(frontToggle, "smartPickInfo")
            var popover = findChild(frontToggle, "smartPickExplanation")
            findChild(frontToggle, "smartPickSwitch").forceActiveFocus()
            mouseMove(info, 22, 22)
            tryCompare(info, "hovered", true)
            wait(450)
            compare(popover.visible, false, "Hover must not open the interactive popover")
            var tooltip = findChild(frontToggle, "smartPickHoverExplanation")
            verify(tooltip !== null)
            for (var i = 0; i < 10; ++i) {
                compare(info.hovered, true)
                compare(tooltip.visible, true)
                compare(popover.visible, false)
                wait(100)
            }
            mouseMove(fixture, 10, 650)
            tryCompare(tooltip, "visible", false)
        }
        function test_control_clickedExplanation_survivesPointerLeaving() {
            var info = findChild(frontToggle, "smartPickInfo")
            var popover = findChild(frontToggle, "smartPickExplanation")
            mouseClick(info, 22, 22)
            tryCompare(popover, "visible", true)
            mouseMove(fixture, 10, 650)
            wait(500)
            compare(popover.visible, true)
            compare(frontToggle.checked, true)
            mouseClick(fixture, 10, 650)
            tryCompare(popover, "visible", false)
        }
        function test_control_keyboardExplanation_staysSeparateFromHover() {
            var info = findChild(frontToggle, "smartPickInfo")
            var popover = findChild(frontToggle, "smartPickExplanation")
            var tooltip = findChild(frontToggle, "smartPickHoverExplanation")
            mouseMove(fixture, 10, 650)
            info.forceActiveFocus()
            wait(450)
            compare(info.hovered, false)
            compare(tooltip.visible, false)
            keyClick(Qt.Key_Return)
            tryCompare(popover, "visible", true)
            keyClick(Qt.Key_Escape)
            tryCompare(popover, "visible", false)
            wait(450)
            compare(tooltip.visible, false)
            keyClick(Qt.Key_Return)
            tryCompare(popover, "visible", true)
            mouseClick(fixture, 10, 650)
            tryCompare(popover, "visible", false)
            wait(450)
            compare(tooltip.visible, false)
        }
        function test_titlePages_compileWithControl() {
            for (var name of ["TheatreSeries.qml", "TheatreSeriesView.qml"]) {
                var component = Qt.createComponent(Qt.resolvedUrl("../../qml/" + name))
                tryVerify(() => component.status !== Component.Loading)
                compare(component.status, Component.Ready, component.errorString())
                component.destroy()
            }
        }
        function test_download_and_season_unchanged() {
            show("download"); stremio.requests[0].done([row(0, "720p"), row(1, "1080p")]); wait(0)
            compare(playSpy.count, 0); compare(downloadSpy.count, 0)
            sheet.activateSource(1); compare(downloadSpy.count, 1); compare(sheet.open, false)
            sheet.show("series", "tt12345:1:1", "Fixture", { title: "Fixture", season: 1, episode: 1 }, "season")
            stremio.requests[1].done([row(0, "1080p", "WEB-DL", "S01.")]); wait(0)
            compare(playSpy.count, 0); compare(downloadSpy.count, 1); compare(sheet.rowCount, 1)
            sheet.activateSource(0); compare(downloadSpy.count, 2)
            sheet.show("series", "tt12345:1:1", "Fixture", { season: 1 }, "season")
            stremio.requests[2].done([]); compare(seasonSpy.count, 1); compare(sheet.open, false)
        }
        function test_waitsForNuvio_andPicksItsRow() {
            show(); stremio.requests[0].done([row(0, "720p")])
            compare(playSpy.count, 0)
            nuvio.streamsReady(nuvio.lastId, [{ url: "https://example.org/video.m3u8", name: "1080p",
                title: "Fixture.2020.1080p.WEB-DL", pluginName: "Fixture plugin", repoId: "repo", pluginId: "plugin" }])
            compare(playSpy.count, 0); nuvio.finished(nuvio.lastId)
            tryCompare(playSpy, "count", 1)
            compare(playSpy.signalArguments[0][0], "url:https://example.org/video.m3u8")
        }
        function test_nuvioFinishesFirst_stillWaitsForStremio() {
            show(); nuvio.finished(nuvio.lastId); compare(playSpy.count, 0)
            stremio.requests[0].done([row(0, "1080p")]); tryCompare(playSpy, "count", 1)
        }
        function test_nuvioOnly_waitsForCompletion() {
            stremio.enabled = false; show(); compare(playSpy.count, 0)
            nuvio.streamsReady(nuvio.lastId, [{ url: "https://example.org/video.m3u8", name: "1080p",
                title: "Fixture.2020.1080p.WEB-DL", pluginName: "Fixture plugin", repoId: "repo", pluginId: "plugin" }])
            nuvio.finished(nuvio.lastId); tryCompare(playSpy, "count", 1)
        }
        function test_timeout_usesPartialRows_once() {
            show(); stremio.requests[0].partial([row(0, "1080p")])
            var timeout = findChild(sheet, "sourcesTimeout")
            verify(timeout !== null); timeout.interval = 1; timeout.restart()
            tryCompare(playSpy, "count", 1)
            finish([row(1, "1080p")]); wait(0); compare(playSpy.count, 1)
            timeout.interval = 22000
        }
        function test_pickUsesAllRows_withActiveFilter() {
            show(); stremio.requests[0].partial([row(0, "720p")]); sheet.qualityFilter = "720p"
            finish([row(0, "720p"), row(1, "1080p")]); tryCompare(playSpy, "count", 1)
            compare(playSpy.signalArguments[0][0], "2".repeat(40))
            compare(sheet.qualityFilter, "720p"); compare(sheet.visibleRows.length, 1)
        }
        function selectionPlayer() {
            verify(SmartPlayTestPlayerPickFunction.length > 0)
            selectionHarness = Qt.createQmlObject('import QtQuick\nimport "'
                + Qt.resolvedUrl("../../qml/EpisodeBrowser.js") + '" as EpisodeBrowser\nimport "'
                + Qt.resolvedUrl("../../qml/TrackLanguage.js") + '" as TrackLanguage\nimport "'
                + Qt.resolvedUrl("../../qml/SmartPlayPreferences.js") + '" as SmartPlayPreferences\nQtObject {\n'
                + 'id: root; property string mediaTitle: "Fixture"; property var mediaMetadata: ({})\n'
                + 'property var persistedStore: null\n'
                + 'property QtObject playerSettings: QtObject { property string smartPlayQuality: "1080p"; property string preferredAudioLanguages: "eng,jpn"; function sync() { if (root.persistedStore) root.persistedStore.sync() } function value(key, fallback) { return root.persistedStore ? root.persistedStore.value(key, fallback) : key === "smartPlayQuality" ? smartPlayQuality : fallback } }\n'
                + SmartPlayTestPlayerPickFunction + '\n}', this)
            return selectionHarness
        }
        function test_nextEpisode_readsPersistedOff_despiteStaleCache() {
            var player = selectionPlayer()
            player.persistedStore = prefs
            var rows = [row(0, "720p", "WEB-DL", "S01E02."), row(1, "1080p", "WEB-DL", "S01E02.")]
            var context = { title: "Fixture", type: "series", season: 1, episode: 2 }
            compare(player.pickEpisodeSource(rows, {}, context).infoHash, "2".repeat(40))
            sheet.setSmartPickEnabled(false)
            compare(player.playerSettings.smartPlayQuality, "1080p")
            compare(player.pickEpisodeSource(rows, {}, context).infoHash, "1".repeat(40))
        }
        function test_nextEpisode_continuity_thenPicker_thenOldFallback() {
            var player = selectionPlayer()
            var rows = [row(0, "720p", "WEB-DL", "S01E02."), row(1, "1080p", "WEB-DL", "S01E02.")]
            var context = { title: "Fixture", type: "series", season: 1, episode: 2 }
            compare(player.pickEpisodeSource(rows, { infoHash: "1".repeat(40), fileIdx: 9 }, context).infoHash, "1".repeat(40))
            compare(player.pickEpisodeSource(rows, { infoHash: "9".repeat(40), fileIdx: 9 }, context).infoHash, "2".repeat(40))
            var rough = [row(0, "1080p", "CAM", "S01E02.")]
            compare(player.pickEpisodeSource(rough, {}, context).infoHash, "1".repeat(40))
            player.playerSettings.smartPlayQuality = "off"
            compare(player.pickEpisodeSource(rows, {}, context).infoHash, "1".repeat(40))
        }
        function test_nextEpisode_queuedIdentity_withoutImdbId() {
            var player = selectionPlayer()
            var target = EpisodeBrowser.queueFrom([{ id: "tmdb:123:1:2", season: 1, num: 2 }], "Fixture", "")[0]
            target.context = { year: 2020, episodeQueue: [target], episodeIndex: 0 }
            var rows = [row(0, "1080p", "WEB-DL", "S01.COMPLETE."), row(1, "720p", "WEB-DL", "S01E02.")]
            compare(player.pickEpisodeSource(rows, {}, target).infoHash, "2".repeat(40))
            target.title = "Fixture - Episode 1 - S1E2"
            compare(player.pickEpisodeSource(rows, {}, target).infoHash, "2".repeat(40))
            var wrongShow = row(0, "1080p", "WEB-DL", "S01E02.")
            wrongShow.release = wrongShow.release.replace("Fixture", "Unrelated")
            wrongShow.filename = wrongShow.filename.replace("Fixture", "Unrelated")
            compare(player.pickEpisodeSource([wrongShow, rows[1]], {}, target).infoHash, "2".repeat(40))
        }
        function test_animeId_keepsReviewedAnimeTrustContext() {
            var anime = row(0, "1080p", "WEB-DL", "S02E02.")
            delete anime.fileIdx
            var context = { type: "series", id: "kitsu:123:1", title: "Fixture", season: 1, episode: 1 }
            compare(SmartPlay.pick([anime], "1080p", context).index, 0)
            context.id = "tt12345:1:1"
            compare(SmartPlay.pick([anime], "1080p", context).index, -1)
            context.id = "kitsu:123:1"; context.title = "Frieren Beyond Journey's End"
            anime.release = anime.release.replace("Fixture", "Sousou.no.Frieren")
            anime.filename = anime.filename.replace("Fixture", "Sousou.no.Frieren")
            compare(SmartPlay.pick([anime], "1080p", context).index, -1)
            context.metadata = { aliases: ["Sousou no Frieren"] }
            compare(SmartPlay.pick([anime], "1080p", context).index, 0)
            context.title = "Sousou no Frieren"
            context.metadata.aliases = ["Frieren Beyond Journey's End"]
            compare(SmartPlay.pick([anime], "1080p", context).index, 0)
            var canonical = row(0, "1080p", "WEB-DL", "S02E02.")
            delete canonical.fileIdx
            context.title = "Fixture"; context.metadata.aliases = ["Alternate"]
            compare(SmartPlay.pick([canonical], "1080p", context).index, 0)
        }
        function test_metadata_usesReleaseTimestamp_andRuntime() {
            var rows = [row(0, "1080p")]
            var context = { type: "movie", title: "Fixture", year: 2020,
                metadata: { released: Qt.formatDate(new Date(), "yyyy-MM-dd") + "T00:00:00.000Z" } }
            compare(SmartPlay.pick(rows, "1080p", context).index, -1)
            context.metadata.released = "2020-01-01T00:00:00.000Z"
            compare(SmartPlay.pick(rows, "1080p", context).index, 0)
            var large = row(0, "1080p"); large.size = "5 GB"; large.seeders = 1000
            var small = row(1, "1080p"); small.size = "1 GB"
            rows = [large, small]
            context.metadata.runtime = "120 min"
            compare(SmartPlay.pick(rows, "1080p", context).index, 0)
            context.metadata.runtime = "30 min"
            compare(SmartPlay.pick(rows, "1080p", context).index, 1)
        }
        function test_existingAudioPreference_usesIsoLanguageCodes() {
            var english = row(0, "1080p"), french = row(1, "1080p")
            english.release += ".English"; english.filename += ".English"
            french.release += ".FRENCH"; french.filename += ".FRENCH"
            var context = { type: "movie", title: "Fixture", year: 2020, preferredLanguages: ["fra"] }
            compare(SmartPlay.pick([english, french], "1080p", context).index, 1)
            context.preferredLanguages = ["eng"]
            compare(SmartPlay.pick([english, french], "1080p", context).index, 0)
        }
        function test_sheetForwardsMetadata_andExistingAudioPreference() {
            sheet.mediaMetadata = { released: Qt.formatDate(new Date(), "yyyy-MM-dd") + "T00:00:00.000Z" }
            show(); finish([row(0, "1080p")]); wait(0); compare(playSpy.count, 0); sheet.hide()
            sheet.mediaMetadata = { released: "2020-01-01T00:00:00.000Z" }
            prefs.setValue("preferredAudioLanguages", "fra"); prefs.sync()
            var english = row(0, "1080p"), french = row(1, "1080p")
            english.release += ".English"; english.filename += ".English"
            french.release += ".FRENCH"; french.filename += ".FRENCH"
            show(); stremio.requests[1].done([english, french]); nuvio.finished(nuvio.lastId)
            tryCompare(playSpy, "count", 1); compare(playSpy.signalArguments[0][0], "2".repeat(40))
            compare(playSpy.signalArguments[0][7].metadata.released, "2020-01-01T00:00:00.000Z")
        }
        function test_nextEpisode_keepsAvailableMetadata() {
            var player = selectionPlayer()
            // Both fit the 8Mbps budget at 20min; only the smaller file fits at 10min.
            var large = row(0, "1080p", "WEB-DL", "S01E02."); large.size = "1 GB"; large.sizeBytes = 1073741824; large.seeders = 1000
            var small = row(1, "1080p", "WEB-DL", "S01E02."); small.size = "0.5 GB"; small.sizeBytes = 536870912
            var target = { type: "series", id: "tt12345:1:2", title: "Fixture - S1E2", season: 1, episode: 2 }
            compare(SmartPlay.pick([small], "1080p", { type: "series", title: "Fixture", id: target.id,
                metadata: { runtime: "20 min" } }).index, 0, "Smaller fixture remains eligible at the longer runtime")
            compare(SmartPlay.pick([small], "1080p", { type: "series", title: "Fixture", id: target.id,
                metadata: { runtime: "10 min" } }).index, 0, "Smaller fixture remains eligible at the shorter runtime")
            player.mediaMetadata = { runtime: "20 min" }
            compare(player.pickEpisodeSource([large, small], {}, target).infoHash, "1".repeat(40))
            player.mediaMetadata = { runtime: "10 min" }
            compare(player.pickEpisodeSource([large, small], {}, target).infoHash, "2".repeat(40))
        }
    }
}
