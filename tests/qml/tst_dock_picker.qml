import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml" as App

Item {
    id: root
    width: 1280
    height: 720

    Rectangle { id: scene; anchors.fill: parent; color: "#26354e" }
    FontLoader { id: fraunces; source: "../../assets/fonts/Fraunces-Regular.ttf" }
    Component { id: mapComponent; Item {} }
    Component { id: nativeMapComponent; App.DockWorldMap {} }
    App.DockPickerCatalog { id: catalogue }
    Component { id: catalogueComponent; App.DockPickerCatalog {} }
    Component {
        id: taskbarComponent
        App.Taskbar {
            pickerScene: scene
            localMediaAvailable: true
            pickerPicks: ({
                tv: [
                    { title: "First", releaseInfo: "2020", genres: ["Mystery"], cover: Qt.resolvedUrl("../../assets/wallpaper/captured-motion.jpg") },
                    { title: "Second", releaseInfo: "2023", genres: ["Drama"], cover: Qt.resolvedUrl("../../assets/wallpaper/cold-ripple.jpg") },
                    { title: "Third", cover: Qt.resolvedUrl("../../assets/wallpaper/sagas/dune.jpg") }
                ]
            })
        }
    }

    TestCase {
        id: suite
        name: "DesktopDockPicker"
        when: windowShown
        property Item bar
        property real widthAtChooser: 0
        Connections {
            target: suite.bar
            function onOpenMediaClicked() {
                suite.widthAtChooser = suite.findChild(suite.bar, "taskbarDock").width
            }
        }
        SignalSpy { id: picks; target: suite.bar; signalName: "pickerPickRequested" }
        SignalSpy { id: wallpapers; target: suite.bar; signalName: "pickerWallpaperRequested" }
        SignalSpy { id: files; target: suite.bar; signalName: "openMediaClicked" }

        function init() {
            root.width = 1280
            root.height = 720
            bar = createTemporaryObject(taskbarComponent, scene.parent)
            verify(bar !== null)
            picks.clear()
            wallpapers.clear()
            files.clear()
            widthAtChooser = 0
        }

        function test_dragTucksTheSameCardAndPreservesSelection() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            const deck = findChild(picker, "dockDeck_tv")
            const card = findChild(deck, "dockPickCard_tv")
            verify(waitForRendering(picker))
            verify(picker.visible)
            const dock = findChild(bar, "taskbarDock")
            tryCompare(dock, "width", picker.width)
            compare(picker.x, dock.x)
            compare(picker.x + picker.width, bar.width - picker.x)
            compare(picker.y, 20)
            const title = findChild(deck, "dockPickTitle_tv")
            const front = findChild(deck, "dockPickFace_0")
            const rightEdge = front.mapToItem(deck, front.width, front.height / 2).x
            for (let layer = 1; layer < 3; ++layer) {
                const rear = findChild(deck, "dockPickFace_" + layer)
                const exposed = rear.mapToItem(deck, rear.width, rear.height / 2).x - rightEdge
                verify(exposed >= layer * 5.5, "Each rear cover exposes a wider real right edge")
            }
            const x = card.width / 2
            const y = card.height / 2
            mouseMove(card, card.width * .8, card.height * .2)
            tryVerify(() => card.hoverTilt > 0 && card.hoverPitch > 0)
            verify(title.visible)
            compare(title.text, "First")
            mousePress(card, x, y)
            compare(card.hoverTilt, 0)
            compare(card.hoverPitch, 0)
            verify(card.pressed, "Press reaches the front cover")
            mouseMove(card, x - 11, y, 20, Qt.LeftButton)
            verify(front.x < deck.coverLeft)
            mouseRelease(card, x - 11, y)
            tryCompare(deck, "circulating", -1)
            compare(deck.current, 0)
            compare(picks.count, 0)

            mousePress(card, x, y)
            mouseMove(card, x - 48, y, 20, Qt.LeftButton)
            compare(deck.circulating, 0)
            checkDeckBounds(deck)
            mouseRelease(card, x - 48, y)
            verify(deck.revealTime < deck.duration, "Release continues from the artwork's dragged position")
            tryVerify(() => deck.turning && deck.current === 1)
            checkDeckBounds(deck)
            compare(title.text, "Second")
            tryCompare(deck, "circulating", -1)
            compare(picks.count, 0)

            compare(deck.current, 1)
            mouseClick(card)
            compare(picks.count, 1)
            compare(picks.signalArguments[0][0], "tv")
            compare(picks.signalArguments[0][1].title, "Second")
            verify(!picker.visible)
        }

        function test_circulationSharesOnePhysicalPath_data() {
            return [
                { tag: "pair", count: 2, width: 1264, height: 656, medium: "anime" },
                { tag: "fan", count: 3, width: 1264, height: 656, medium: "tv" },
                { tag: "deepStack", count: 7, width: 900, height: 720, medium: "movies" },
                { tag: "tribute", count: 5, width: 1264, height: 656, medium: "tributes" }
            ]
        }
        function test_circulationSharesOnePhysicalPath(data) {
            root.width = data.width
            root.height = data.height
            const covers = ["captured-motion.jpg", "cold-ripple.jpg", "sagas/dune.jpg"]
            const entries = Array.from({ length: data.count }, (_, i) => ({
                id: String(i), title: "Title " + i,
                cover: Qt.resolvedUrl("../../assets/wallpaper/" + covers[i % covers.length])
            }))
            bar.pickerPicks = { [data.medium]: entries }
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            if (data.medium === "tributes") picker.selectDeckPage(2)
            const deck = findChild(picker, "dockDeck_" + data.medium)
            const grab = findChild(deck, "dockPickCard_" + data.medium)
            const outgoing = findChild(deck, "dockPickFace_0")
            const incoming = findChild(deck, "dockPickFace_1")
            const viewport = findChild(picker, "dockPickViewport")
            const row = findChild(picker, "dockPickPage_" + picker.deckPage)
            tryVerify(() => grab.visible && grab.enabled && Math.abs(row.mapToItem(viewport, 0, 0).y) < .1)
            verify(waitForRendering(picker))

            const x = grab.width / 2
            const y = grab.height / 2
            mouseMove(grab, x, y)
            tryVerify(() => Math.abs(grab.hoverTilt) < .02 && Math.abs(grab.hoverPitch) < .02)
            const original = faceBounds(outgoing, deck)
            const waiting = faceBounds(incoming, deck)
            function compareRest() {
                const restored = faceBounds(outgoing, deck)
                fuzzyCompare(restored.x, original.x, .2)
                fuzzyCompare(restored.y, original.y, .2)
                fuzzyCompare(restored.width, original.width, .2)
                fuzzyCompare(restored.height, original.height, .2)
                compare(faceBounds(incoming, deck), waiting)
            }
            const distance = Math.floor(deck.leftTravel / 2)
            mousePress(grab, x, y, Qt.LeftButton, Qt.NoModifier, 0)
            mouseMove(grab, x - distance, y, 1, Qt.LeftButton)
            const pulled = faceBounds(outgoing, deck)
            const revealed = faceBounds(incoming, deck)
            verify(pulled.x + pulled.width / 2 < original.x + original.width / 2
                   && revealed.x + revealed.width / 2 > waiting.x + waiting.width / 2,
                   "Both cover centers move during the held pull: " + pulled + " / " + revealed)
            mouseRelease(grab, x - distance, y, Qt.LeftButton, Qt.NoModifier, 0)
            compare(faceBounds(outgoing, deck), pulled, "Cancel begins from the held outgoing pose")
            compare(faceBounds(incoming, deck), revealed, "Cancel begins from the held incoming pose")
            tryCompare(deck, "turning", false)
            mouseMove(grab, x, y)
            tryVerify(() => Math.abs(grab.hoverTilt) < .02 && Math.abs(grab.hoverPitch) < .02)
            compareRest()
            compare(deck.current, 0)

            mousePress(grab, x, y, Qt.LeftButton, Qt.NoModifier, 0)
            mouseMove(grab, x - Math.ceil(deck.leftTravel + 1), y, 1, Qt.LeftButton)
            compare(deck.progress, .5)
            const leftAtEdge = faceBounds(outgoing, deck)
            const rightAtEdge = faceBounds(incoming, deck)
            verify(leftAtEdge.x + leftAtEdge.width < rightAtEdge.x)
            verify(leftAtEdge.width >= deck.width * .36, "The real outgoing art still has room at the handoff")
            const carrier = Array.from({ length: Math.min(data.count - 1, 3) }, (_, i) =>
                findChild(deck, "dockPickFace_" + (i + 1)))
            verify(carrier.every(face => leftAtEdge.x + leftAtEdge.width < faceBounds(face, deck).x))
            checkDeckBounds(deck)

            mouseRelease(grab, x - Math.ceil(deck.leftTravel + 1), y, Qt.LeftButton, Qt.NoModifier, 0)
            compare(faceBounds(outgoing, deck), leftAtEdge, "Commit begins from the held outgoing pose")
            compare(faceBounds(incoming, deck), rightAtEdge, "Commit begins from the held incoming pose")
            tryVerify(() => deck.turning && deck.current === 1 && outgoing.z < incoming.z)
            checkDeckBounds(deck)
            tryCompare(deck, "turning", false)
            compare(deck.current, 1)
            compare(outgoing.visible, data.count <= 3)

            mousePress(grab, x, y, Qt.LeftButton, Qt.NoModifier, 0)
            mouseMove(grab, x + Math.ceil(deck.rightTravel + 1), y, 1, Qt.LeftButton)
            compare(deck.current, 1)
            compare(deck.circulating, 0, "The exact tucked cover comes around again")
            compare(faceBounds(outgoing, deck), leftAtEdge, "Reverse uses the same edge and pose")
            compare(faceBounds(incoming, deck), rightAtEdge)
            checkDeckBounds(deck)
            mouseRelease(grab, x + Math.ceil(deck.rightTravel + 1), y, Qt.LeftButton, Qt.NoModifier, 0)
            tryCompare(deck, "turning", false)
            compare(deck.current, 0)
            mouseMove(grab, x, y)
            tryVerify(() => Math.abs(grab.hoverTilt) < .02 && Math.abs(grab.hoverPitch) < .02)
            compareRest()
            compare(picks.count, 0)
        }

        function test_catalogueRefreshWaitsForPickerFrame() {
            const state = createTemporaryObject(catalogueComponent, root)
            state.active = true
            verify(!state.picks.universes)
            state.active = false
            wait(200)
            verify(!state.picks.universes)
            state.active = true
            bar.open = true
            tryVerify(() => state.picks.universes && state.picks.universes.length > 1)
        }

        function test_universesPreferColourAndTributesUseInstalledRoster() {
            const state = createTemporaryObject(catalogueComponent, root, {
                installedUniverses: [
                    { extensionId: "custom.starwars", name: "Star Wars", banner: "hero.jpg", logo: "logo.png" },
                    { extensionId: "custom.dexter", name: "Dexter", banner: "dexter.jpg" }
                ]
            })
            state.updateUniverses()
            state.updateTributes()
            const universes = state.picks.universes
            compare(universes.length, 10)
            const firstMonochrome = universes.findIndex(entry => !entry.raw.logoColored)
            verify(firstMonochrome > 0)
            verify(universes.slice(firstMonochrome).every(entry => !entry.raw.logoColored))
            compare(state.picks.tributes.map(entry => entry.id), ["custom.starwars", "custom.dexter"])
            compare(state.picks.tributes[0].cover, "hero.jpg")
            compare(state.picks.tributes[0].coverCandidates, ["logo.png"])
            verify(!state.picks.audiobooks)
        }

        function test_cosmereTributeLoadsBundledPortraitCrop() {
            const state = createTemporaryObject(catalogueComponent, root, {
                installedUniverses: [{
                    extensionId: "com.colosseum.universe.cosmere", name: "Cosmere",
                    banner: "remote-cosmere-banner.jpg"
                }]
            })
            state.updateTributes()
            bar.pickerPicks = state.picks
            const expected = Qt.resolvedUrl("../../assets/extensions/house/cosmere.jpg")
            compare(state.picks.tributes[0].cover, expected)

            bar.open = true
            const picker = findChild(bar, "dockPicker")
            picker.selectDeckPage(2)
            const deck = findChild(picker, "dockDeck_tributes")
            tryVerify(() => findChild(deck, "dockPosterArt_img") !== null)
            const image = findChild(deck, "dockPosterArt_img")
            tryCompare(image, "status", Image.Ready)
            compare(image.source, expected)
            compare(image.fillMode, Image.PreserveAspectCrop)
        }

        function test_coversRemainReadyAcrossTurnsAndPageChanges() {
            const covers = ["captured-motion.jpg", "cold-ripple.jpg", "sagas/dune.jpg"]
            const entries = Array.from({ length: 7 }, (_, i) => ({
                id: String(i), title: "Title " + i,
                cover: Qt.resolvedUrl("../../assets/wallpaper/" + covers[i % covers.length])
            }))
            bar.pickerPicks = { tv: entries, manga: entries, books: entries }
            const picker = findChild(bar, "dockPicker")
            const tv = findChild(picker, "dockDeck_tv")
            const manga = findChild(picker, "dockDeck_manga")
            const books = findChild(picker, "dockDeck_books")
            const stage = findChild(tv, "dockPickStage_tv")
            function art(deck, i) {
                return findChild(findChild(deck, "dockPickFace_" + i), "dockPosterArt_img")
            }
            function ready(deck, i) {
                const image = art(deck, i)
                return image !== null && image.status === Image.Ready
            }

            verify(art(tv, 0) === null)
            bar.open = true
            compare(picker.artStage, 0)
            tryVerify(() => [0, 1, 2, 5, 6].every(i => ready(tv, i)))
            verify(art(tv, 3) === null && art(tv, 4) === null)
            tryVerify(() => ready(manga, 0) && ready(manga, 1) && ready(manga, 6) && ready(books, 0))
            verify(art(manga, 2) === null && art(books, 1) === null)

            bar.pickerReducedMotion = true
            stage.forceActiveFocus(Qt.TabFocusReason)
            const initialArtwork = [0, 1, 6].map(i => [i, art(tv, i)])
            for (const [key, index] of [[Qt.Key_Left, 6], [Qt.Key_Right, 0], [Qt.Key_Right, 1], [Qt.Key_Right, 2]]) {
                keyClick(key)
                tryCompare(tv, "current", index)
                verify(ready(tv, index))
                const previous = initialArtwork.find(([i]) => i === index)
                if (previous) compare(art(tv, index), previous[1])
            }
            const front = art(tv, 2)
            const bookFront = art(books, 0)
            bar.pickerReducedMotion = false
            picker.selectDeckPage(2)
            compare(books.artRadius, 0, "Only the preloaded front is needed during the slide")
            tryCompare(books, "artRadius", 2)
            compare(art(books, 0), bookFront)
            verify(ready(books, 0))
            picker.selectDeckPage(0)
            tryCompare(tv, "artRadius", 2)
            verify(ready(tv, 2))
            compare(art(tv, 2), front)
            bar.open = false
            bar.open = true
            compare(art(tv, 2), front)
            verify(ready(tv, 2))
            bar.open = false
            tryVerify(() => art(tv, 2) === null)
        }

        function test_returnRestoresSlideCardAndKeyboardFocusAfterRefresh() {
            bar.pickerReducedMotion = true
            bar.pickerPicks = { comics: [{ id: "a", title: "First" }, { id: "b", title: "Chosen" }] }
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            picker.selectDeckPage(1)
            const stage = findChild(picker, "dockPickStage_comics")
            const title = findChild(picker, "dockPickTitle_comics")
            stage.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Right)
            tryCompare(title, "text", "Chosen")
            keyClick(Qt.Key_Return)
            verify(!picker.visible)
            const state = picks.signalArguments[0][2]
            compare(state.slide, 1)
            compare(state.key, "b")
            bar.pickerPicks = { comics: [{ id: "c", title: "New catalogue pick" }] }
            verify(bar.restorePicker(state))
            verify(picker.visible)
            compare(picker.deckPage, 1)
            tryCompare(title, "text", "Chosen")
            tryVerify(() => stage.activeFocus)

            bar.open = false
            bar.open = true
            compare(picker.deckPage, 0)
            picker.selectDeckPage(1)
            compare(title.text, "New catalogue pick")
        }

        function test_verticalDragAndWheelTurnOnePage() {
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            const deck = findChild(picker, "dockDeck_tv")
            const card = findChild(deck, "dockPickCard_tv")
            verify(waitForRendering(picker))
            verify(findChild(picker, "dockDeckDot_0") !== null)
            verify(findChild(picker, "dockDeckDot_2") !== null)
            verify(findChild(picker, "dockDeckDot_3") === null)
            const viewport = findChild(picker, "dockPickViewport")
            const heading = findChild(picker, "dockPickHeading")
            const headingPosition = heading.mapToItem(root, 0, 0)
            const start = card.mapToItem(viewport, card.width / 2, card.height * .8)

            mousePress(viewport, start.x, start.y)
            mouseMove(viewport, start.x + 1, start.y - 15, 20, Qt.LeftButton)
            mouseMove(viewport, start.x - 60, start.y - 75, 20, Qt.LeftButton)
            compare(picker.deckPage, 0)
            mouseRelease(viewport, start.x - 60, start.y - 75)
            compare(picker.deckPage, 1)
            compare(deck.current, 0)
            compare(deck.circulating, -1)
            compare(picks.count, 0)
            compare(heading.mapToItem(root, 0, 0), headingPosition)

            mouseWheel(viewport, viewport.width / 2, viewport.height / 2, 0, -120)
            tryCompare(picker, "deckPage", 2)
            verify(!findChild(picker, "dockMapNext").visible)
        }

        function test_headerArrowsOpenMapAndRetainDeckPage() {
            bar.pickerMapSlide = mapComponent
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            const next = findChild(picker, "dockMapNext")
            const back = findChild(picker, "dockMapBack")
            const heading = findChild(picker, "dockPickHeading")
            const mapHeading = findChild(picker, "dockMapHeading")
            const viewport = findChild(picker, "dockPickViewport")
            const map = findChild(picker, "dockPickMapPage")
            const lastDot = findChild(picker, "dockDeckDot_2")
            verify(waitForRendering(picker))
            verify(next.visible && !back.visible)
            const homePosition = heading.mapToItem(picker, 0, 0)

            // Keyboard activation works from the first row and hands focus to Back.
            next.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            tryVerify(() => back.activeFocus)
            compare(picker.deckPage, 0)
            verify(map.visible)
            keyClick(Qt.Key_Return)
            tryVerify(() => next.activeFocus)
            verify(!map.visible)

            mouseClick(lastDot)
            tryVerify(() => findChild(picker, "dockDeck_books").enabled)
            mouseWheel(viewport, viewport.width / 2, viewport.height / 2, 0, -120)
            compare(picker.deckPage, 2)
            verify(!picker.showingMap && !map.visible)

            mouseClick(next)
            tryVerify(() => back.visible && back.enabled)
            verify(!next.visible && !lastDot.visible)
            compare(mapHeading.mapToItem(picker, 0, 0), homePosition)
            mouseWheel(map, map.width / 2, map.height / 2, 0, 120)
            verify(picker.showingMap)
            compare(picker.deckPage, 2)

            mouseClick(back)
            tryVerify(() => next.visible && next.enabled)
            compare(picker.deckPage, 2)
            verify(findChild(picker, "dockDeck_books").visible && !map.visible)
            compare(heading.mapToItem(picker, 0, 0), homePosition)
        }

        function test_nativeMapUsesFullPaneAndRoutesOnlyAvailableLand() {
            root.width = 1264
            root.height = 656
            bar.pickerReducedMotion = true
            bar.pickerMapSlide = nativeMapComponent
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            mouseClick(findChild(picker, "dockMapNext"))
            tryCompare(picker, "showingMap", true)
            const loader = findChild(picker, "dockPickMapPage")
            tryCompare(loader, "status", Loader.Ready)
            const map = loader.item
            compare(map.height, picker.height)
            compare(map.width + map.mapToItem(picker, 0, 0).x, picker.width)
            compare(map.mapToItem(picker, 0, 0).y, 0)
            const destinations = createTemporaryObject(worldSpy, map, { target: map })
            verify(destinations.valid)

            const keys = ["tankoban", "theatre", "biblio", "universes", "vault", "feria", "vinyl"]
            for (let i = 0; i < keys.length; ++i) {
                const key = keys[i]
                const label = findChild(map, "dockWorldLabel_" + key)
                const point = label.mapToItem(map, label.width / 2, label.height / 2)
                mouseClick(map, point.x, point.y)
                compare(destinations.count, Math.min(i + 1, 5), key)
                if (i < 5)
                    compare(destinations.signalArguments[i][0], key)
            }
            mouseClick(map, 10, 10)
            compare(destinations.count, 5)
            map.focusFirstWorld()
            keyClick(Qt.Key_Return)
            compare(destinations.signalArguments[5][0], "tankoban")
            mouseWheel(map, map.width / 2, 12, 0, 120)
            compare(picker.showingMap, true)
            compare(picker.deckPage, 0)
        }

        Component {
            id: worldSpy
            SignalSpy { signalName: "worldRequested" }
        }

        function test_trackpadMomentumDoesNotSkipPages() {
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            picker.pageWheel(0, 0, Qt.ScrollBegin)
            picker.pageWheel(12, -4, Qt.ScrollUpdate)
            picker.pageWheel(0, -18, Qt.ScrollUpdate)
            picker.pageWheel(0, -18, Qt.ScrollUpdate)
            compare(picker.deckPage, 0)
            picker.pageWheel(0, -9, Qt.ScrollUpdate)
            compare(picker.deckPage, 1)
            picker.pageWheel(0, -200, Qt.ScrollUpdate)
            picker.pageWheel(0, -160, Qt.ScrollMomentum)
            picker.pageWheel(0, 0, Qt.ScrollEnd)
            picker.pageWheel(0, -100, Qt.ScrollMomentum)
            compare(picker.deckPage, 1)
            picker.pageWheel(0, 0, Qt.ScrollBegin)
            picker.pageWheel(0, -48, Qt.ScrollUpdate)
            compare(picker.deckPage, 2)
            picker.pageWheel(0, 0, Qt.ScrollEnd)
            picker.pageWheel(0, 40, Qt.NoScrollPhase)
            picker.pageWheel(0, 120, Qt.NoScrollPhase)
            compare(picker.deckPage, 1)
        }

        function test_railActionsAndCompactMediaHandoff() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            mouseClick(findChild(picker, "dockShortcut_wallpaper"))
            compare(wallpapers.count, 1)
            verify(!picker.visible)

            bar.open = true
            tryCompare(findChild(bar, "taskbarDock"), "width", bar.pickerWidth)
            const shortcut = findChild(picker, "dockShortcut_media")
            mouseClick(shortcut)
            tryCompare(files, "count", 1)
            compare(widthAtChooser, bar.closedWidth)
            verify(!bar.open)

            bar.open = true
            verify(!shortcut.activeFocus)
            shortcut.forceActiveFocus(Qt.TabFocusReason)
            verify(shortcut.activeFocus)
            keyClick(Qt.Key_Return)
            tryCompare(files, "count", 2)
            compare(widthAtChooser, bar.closedWidth)
            mouseClick(findChild(bar, "taskbarToggleAction"))
            verify(picker.visible)
            verify(!shortcut.activeFocus, "Reopening does not restore a stale shortcut focus")
            keyClick(Qt.Key_Tab)
            verify(shortcut.activeFocus, "The keyboard can still reach the first shortcut")
        }

        function test_pageIconsFollowExistingDestinationState() {
            const extensions = findChild(bar, "taskbarExtensions")
            const activity = findChild(bar, "taskbarHistoryStats")
            verify(waitForRendering(bar))
            verify(!extensions.visible && !activity.visible)
            bar.extensionsActive = true
            verify(extensions.visible && !activity.visible)
            tryCompare(findChild(bar, "taskbarDock"), "width", 310)
            bar.extensionsActive = false
            bar.historyStatsActive = true
            verify(!extensions.visible && activity.visible)
            bar.historyStatsActive = false
            bar.focusHistoryStatsAction()
            verify(!activity.visible)
            verify(!bar.open)
            verify(findChild(bar, "taskbarToggleAction").activeFocus)
        }

        function test_manhwaAndUniverseLogoDecks() {
            catalogue.updateUniverses()
            const entries = catalogue.picks.universes
            verify(entries.length > 1)
            verify(entries.every(entry => entry.raw.page && entry.raw.logo))
            bar.pickerPicks = { universes: entries, manhwa: [{ id: "123", title: "Manhwa pick" }] }
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const manhwa = findChild(picker, "dockPickCard_manhwa")
            const universes = findChild(picker, "dockPickCard_universes")
            compare(universes.width, manhwa.width)
            verify(findChild(picker, "dockDeck_music") === null)
            verify(findChild(picker, "dockDeck_audiobooks") === null)
            mouseClick(findChild(picker, "dockDeckDot_2"))
            const logo = findChild(picker, "dockUniverseLogo")
            tryCompare(logo, "status", Image.Ready)
            compare(logo.fillMode, Image.PreserveAspectFit)
            const franchises = findChild(picker, "dockPickStage_universes")
            compare(franchises.Accessible.name, "Open " + entries[0].title + " (Franchises)")
            franchises.forceActiveFocus(Qt.TabFocusReason)
            mouseClick(universes)
            compare(picks.signalArguments[0][0], "universes")
            compare(picks.signalArguments[0][1].raw.page, entries[0].raw.page)
            bar.open = true
            mouseClick(findChild(picker, "dockDeckDot_1"))
            mouseClick(manhwa)
            compare(picks.signalArguments[1][0], "manhwa")
        }

        function faceBounds(face, deck) {
            return face.mapToItem(deck, Qt.rect(0, 0, face.width, face.height))
        }

        function checkDeckBounds(deck) {
            let left = deck.width
            let right = 0
            for (let i = 0; i < deck.count; ++i) {
                const face = findChild(deck, "dockPickFace_" + i)
                if (!face.visible) continue
                const bounds = faceBounds(face, deck)
                verify(bounds.x >= -1 && bounds.x + bounds.width <= deck.width + 1,
                    deck.medium + " card " + i + " has bounds " + bounds + " within " + deck.width + " px")
                verify(bounds.y >= -1 && bounds.y + bounds.height <= deck.artHeight + 1,
                    "Card stays above the caption for " + deck.medium)
                left = Math.min(left, bounds.x)
                right = Math.max(right, bounds.x + bounds.width)
            }
            if (!deck.turning)
                fuzzyCompare((left + right) / 2, deck.width / 2, 1)
        }

        function checkSlides(picker, rows) {
            const viewport = findChild(picker, "dockPickViewport")
            const heading = findChild(picker, "dockPickHeading")
            for (let index = 0; index < rows.length; ++index) {
                picker.selectDeckPage(index)
                compare(picker.deckPage, index)
                verify(heading.visible && !heading.truncated)
                verify(heading.contentHeight <= heading.height && heading.contentWidth <= heading.width)
                const row = findChild(picker, "dockPickPage_" + index)
                row.forceLayout()
                compare(row.mapToItem(viewport, 0, 0).y, 0)
                compare(row.height, viewport.height)
                let deckRight = 0
                for (const key of rows[index]) {
                    const deck = findChild(picker, "dockDeck_" + key)
                    const label = findChild(deck, "dockPickMedium_" + key)
                    const title = findChild(deck, "dockPickTitle_" + key)
                    verify(deck.visible && deck.coverHeight > 0 && label.visible && title.visible)
                    verify(!label.truncated && !title.truncated)
                    compare(label.text, key === "tv" ? "TV"
                                                    : key === "universes" ? "Franchises"
                                                                         : key[0].toUpperCase() + key.slice(1))
                    const image = findChild(deck, "dockPosterArt_img")
                    if (key !== "universes")
                        compare(image.fillMode, key === "tributes" ? Image.PreserveAspectCrop : Image.PreserveAspectFit)
                    checkDeckBounds(deck)
                    verify(label.parent.x >= 0 && label.parent.x + label.width <= deck.width + 1)
                    verify(title.parent.y + title.y + title.height <= deck.height + 1)
                    compare(deck.y, 0)
                    verify(deck.x >= deckRight && deck.height === row.height)
                    deckRight = deck.x + deck.width
                }
                fuzzyCompare(deckRight, row.width, 1)
                for (let other = 0; other < rows.length; ++other) {
                    const offscreen = findChild(picker, "dockPickPage_" + other)
                    compare(offscreen.visible, other === index)
                    compare(offscreen.enabled, other === index)
                }
            }
        }

        function test_fullSlidesKeepReadableCaptionsAndLargeArtwork() {
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const heading = findChild(picker, "dockPickHeading")
            tryCompare(fraunces, "status", FontLoader.Ready)
            tryVerify(() => heading.fontInfo.family === "Fraunces")
            compare(heading.text, "Pick Something")
            compare(heading.horizontalAlignment, Text.AlignHCenter)
            compare(heading.width, heading.parent.width)
            compare(heading.font.weight, Font.DemiBold)
            mouseMove(heading)

            const rows = [["movies", "tv", "anime"], ["manga", "comics", "manhwa"], ["books", "universes", "tributes"]]
            const entries = bar.pickerPicks.tv
            const allPicks = {}
            for (const row of rows)
                for (const key of row)
                    allPicks[key] = entries
            bar.pickerPicks = allPicks
            const tv = findChild(picker, "dockPickCard_tv")
            const stdHeight = tv.height
            verify(stdHeight >= 300, "TV poster height at 1280x720: " + stdHeight)
            checkSlides(picker, rows)
            const tribute = findChild(picker, "dockPickCard_tributes")
            compare(tribute.width, tv.width)
            compare(tribute.height, tv.height)
            const universe = findChild(picker, "dockPickCard_universes")
            compare(universe.height, tv.height)
            root.width = 1264
            root.height = 656
            tryCompare(bar, "width", 1264)
            const windowedHeadingSize = heading.font.pixelSize
            verify(windowedHeadingSize >= 35 && windowedHeadingSize <= 40)
            console.info("Resolved heading font:", heading.fontInfo.family, heading.fontInfo.weight)
            fuzzyCompare(tv.height, 333.6, 1)
            checkSlides(picker, rows)
            const windowedHeight = tv.height
            root.width = 1600
            root.height = 900
            tryVerify(() => tv.height > stdHeight)
            checkSlides(picker, rows)
            console.info("TV poster height (1264x656 / 1280x720 / 1600x900):",
                windowedHeight.toFixed(1), "/", stdHeight.toFixed(1), "/", tv.height.toFixed(1))
            root.width = 900
            root.height = 720
            tryVerify(() => tv.height < windowedHeight)
            verify(heading.font.pixelSize < windowedHeadingSize)
            checkSlides(picker, rows)
            console.info("Pick Something font px (1264x656 / 900x720):",
                windowedHeadingSize, "/", heading.font.pixelSize)
        }

        function test_visibleLabelAndAccessibleTitleReplaceTooltip() {
            const fullTitle = "The remarkably long title of the illustrated television series"
            root.width = 1264
            root.height = 656
            bar.pickerPicks = { tv: [{ title: fullTitle }] }
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const card = findChild(picker, "dockPickCard_tv")
            const tv = findChild(picker, "dockPickStage_tv")
            const tribute = findChild(picker, "dockPickStage_tributes")
            const label = findChild(picker, "dockPickTitle_tv")
            compare(label.text, fullTitle)
            verify(label.visible && label.lineCount > 1)
            verify(!label.truncated, "Title width " + label.width + ", line count " + label.lineCount
                + " of " + label.maximumLineCount + ", font px " + label.font.pixelSize)
            mouseMove(card)
            tv.forceActiveFocus(Qt.TabFocusReason)
            wait(400)
            verify(!card.ToolTip.toolTip.visible)
            compare(tv.Accessible.name, "Open " + fullTitle + " (TV)")
            compare(tribute.Accessible.name, "Tributes")
            compare(tribute.Accessible.description, "No picks available")
            picker.selectDeckPage(2)
            const emptyTitle = findChild(picker, "dockPickTitle_tributes")
            compare(emptyTitle.text, "No picks available")
            verify(emptyTitle.visible && !emptyTitle.truncated)
            verify(emptyTitle.parent.y + emptyTitle.y + emptyTitle.height <= tribute.height)
        }

        function compareCaption(picker, key, medium, title) {
            const labels = findChild(picker, "dockPickTitle_" + key).parent.children
            compare(labels.length, 2)
            compare(labels[0].text, medium)
            compare(labels[1].text, title)
        }

        function test_captionsShowOnlyMediumAndTitle() {
            catalogue.updateUniverses()
            const franchise = catalogue.picks.universes.find(entry => entry.raw.blurb.length > 0)
            verify(franchise !== undefined, "Use an actual franchise entry with a source description")
            bar.pickerReducedMotion = true
            bar.pickerPicks = {
                anime: [{ title: "Anime pick", releaseInfo: "2024", genres: ["Drama"], blurb: "The crew follows a signal from a lost city." }],
                manga: [{ title: "Manga pick", year: 1997, raw: { authors: [{ name: "An author" }] } }],
                comics: [{ title: "Comic pick", publisher: "A publisher", year: 2016 }],
                manhwa: [{ title: "Manhwa pick", raw: { synopsis: "A short source synopsis." } }],
                books: [{ title: "Book pick", author: "A novelist", year: 1988 }],
                tributes: [{ title: "A Tribute" }],
                universes: [franchise]
            }
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            compareCaption(picker, "anime", "Anime", "Anime pick")
            picker.selectDeckPage(1)
            compareCaption(picker, "manga", "Manga", "Manga pick")
            compareCaption(picker, "comics", "Comics", "Comic pick")
            compareCaption(picker, "manhwa", "Manhwa", "Manhwa pick")
            picker.selectDeckPage(2)
            compareCaption(picker, "books", "Books", "Book pick")
            compareCaption(picker, "tributes", "Tributes", "A Tribute")
            compareCaption(picker, "universes", "Franchises", franchise.title)
        }

        function test_keyboardTurnsPagesAndFocusesOnlyTheLandingDeck() {
            bar.pickerPicks = {
                movies: [{ title: "Movie pick" }],
                tv: [{ title: "First" }, { title: "Second" }],
                comics: [{ title: "Comic pick" }],
                tributes: [{ title: "A Tribute" }]
            }
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const viewport = findChild(picker, "dockPickViewport")
            const tv = findChild(picker, "dockPickStage_tv")
            const tribute = findChild(picker, "dockPickStage_tributes")
            const comics = findChild(picker, "dockPickStage_comics")
            findChild(picker, "dockPickStage_movies").forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            verify(tv.activeFocus)
            keyClick(Qt.Key_Right)
            tryCompare(findChild(picker, "dockPickTitle_tv"), "text", "Second")
            keyClick(Qt.Key_Down)
            tryVerify(() => comics.activeFocus)
            keyClick(Qt.Key_PageUp)
            tryVerify(() => tv.activeFocus)
            keyClick(Qt.Key_PageDown)
            keyClick(Qt.Key_PageDown)
            tryVerify(() => tribute.activeFocus)
            const rect = tribute.mapToItem(viewport, Qt.rect(0, 0, tribute.width, tribute.height))
            verify(rect.y >= -1 && rect.y + rect.height <= viewport.height + 1)
            verify(!tv.activeFocus && !comics.activeFocus && !tv.enabled && !comics.enabled)
            keyClick(Qt.Key_Return)
            compare(picks.signalArguments[0][0], "tributes")
            compare(picks.signalArguments[0][1].title, "A Tribute")
        }

        function test_touchTurnsOnePageWithoutSelectingACover() {
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const viewport = findChild(picker, "dockPickViewport")
            const card = findChild(picker, "dockPickCard_tv")
            const start = card.mapToItem(viewport, card.width / 2, card.height * .8)
            const finger = touchEvent(viewport)
            finger.press(0, viewport, start.x, start.y).commit()
            finger.move(0, viewport, start.x, start.y - 20).commit()
            finger.move(0, viewport, start.x, start.y - 85).commit()
            compare(picker.deckPage, 0)
            finger.release(0, viewport, start.x, start.y - 85).commit()
            tryCompare(picker, "deckPage", 1)
            compare(picks.count, 0)
            compare(findChild(picker, "dockDeck_tv").current, 0)
        }

        function test_utilityShortcutsOpenTheirActions() {
            const routes = []
            bar.pickerUtilityShortcuts = ["sync", "dlna"].map(key => ({
                key: key, title: key, icon: "update.svg", available: true, utility: true,
                run: invoker => routes.push({ key: key, invoker: invoker.objectName, open: bar.open })
            }))
            root.height = 500
            const picker = findChild(bar, "dockPicker")
            for (const key of ["sync", "dlna"]) {
                bar.open = true
                verify(waitForRendering(picker))
                const shortcut = findChild(picker, "dockShortcut_" + key)
                verify(shortcut !== null)
                mouseClick(shortcut)
                tryCompare(bar, "pickerShown", false)
            }
            compare(routes, ["sync", "dlna"].map(key => ({
                key: key, invoker: "dockShortcut_" + key, open: false
            })))
        }

        function test_leftClicksOnPickerGlassKeepItOpen() {
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const dock = findChild(bar, "taskbarDock")
            tryCompare(dock, "width", picker.width)
            // Empty space beside the left shortcuts, then the left window gutter.
            mouseClick(picker, 12, Math.floor(picker.height / 2))
            verify(bar.pickerShown)
            mouseClick(bar, Math.floor(picker.x / 2), Math.floor(bar.height / 2))
            verify(bar.pickerShown)
            mouseClick(findChild(bar, "taskbarToggleAction"))
            verify(!bar.pickerShown)
        }

        function test_autoRevealKeepsPickerClosed() {
            bar.reveal()
            verify(bar.open)
            verify(!bar.pickerShown)
            bar.open = false
            mouseClick(findChild(bar, "taskbarToggleAction"))
            verify(bar.pickerShown)
            bar.reveal()
            verify(bar.pickerShown)
        }
    }
}
