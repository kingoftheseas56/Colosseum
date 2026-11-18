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
    App.TooltipStyle { backdrop: root }
    Component { id: mapComponent; Item {} }
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
            const facts = findChild(deck, "dockPickFacts_tv")
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
            compare(facts.text, "2020 · Mystery")
            mousePress(card, x, y)
            compare(card.hoverTilt, 0)
            compare(card.hoverPitch, 0)
            verify(card.pressed, "Press reaches the front cover")
            mouseMove(card, x - 11, y, 20, Qt.LeftButton)
            verify(deck.shift < 0)
            mouseRelease(card, x - 11, y)
            tryCompare(deck, "moving", -1)
            compare(deck.current, 0)
            compare(picks.count, 0)

            mousePress(card, x, y)
            mouseMove(card, x - 48, y, 20, Qt.LeftButton)
            compare(deck.moving, 0)
            checkDeckBounds(deck)
            mouseRelease(card, x - 48, y)
            verify(deck.revealTime < deck.duration, "Release continues from the artwork's dragged position")
            tryVerify(() => deck.behind && deck.moving === 0 && deck.current === 1)
            checkDeckBounds(deck)
            compare(title.text, "Second")
            compare(facts.text, "2023 · Drama")
            tryCompare(deck, "moving", -1)
            compare(picks.count, 0)

            compare(deck.current, 1)
            mouseClick(card)
            compare(picks.count, 1)
            compare(picks.signalArguments[0][0], "tv")
            compare(picks.signalArguments[0][1].title, "Second")
            verify(!picker.visible)
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
            tryVerify(() => ready(manga, 0) && ready(books, 0))
            verify(art(manga, 1) === null && art(books, 1) === null)

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
            picker.selectSlide(2)
            tryCompare(books, "artRadius", 2)
            compare(art(books, 0), bookFront)
            verify(ready(books, 0))
            picker.selectSlide(0)
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
            picker.selectSlide(1)
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
            compare(picker.slide, 1)
            tryCompare(title, "text", "Chosen")
            tryVerify(() => stage.activeFocus)

            bar.open = false
            bar.open = true
            compare(picker.slide, 0)
            picker.selectSlide(1)
            compare(title.text, "New catalogue pick")
        }

        function test_verticalDragAndWheelTurnOnePage() {
            bar.pickerReducedMotion = true
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            const deck = findChild(picker, "dockDeck_tv")
            const card = findChild(deck, "dockPickCard_tv")
            verify(waitForRendering(picker))
            verify(findChild(picker, "dockSlideDot_0") !== null)
            verify(findChild(picker, "dockSlideDot_2") !== null)
            verify(findChild(picker, "dockSlideDot_3") === null)
            const viewport = findChild(picker, "dockPickViewport")
            const heading = findChild(picker, "dockPickHeading")
            const headingPosition = heading.mapToItem(root, 0, 0)
            const start = card.mapToItem(viewport, card.width / 2, card.height * .8)

            mousePress(viewport, start.x, start.y)
            mouseMove(viewport, start.x + 1, start.y - 15, 20, Qt.LeftButton)
            mouseMove(viewport, start.x - 60, start.y - 75, 20, Qt.LeftButton)
            compare(picker.slide, 0)
            mouseRelease(viewport, start.x - 60, start.y - 75)
            compare(picker.slide, 1)
            compare(deck.current, 0)
            compare(deck.moving, -1)
            compare(picks.count, 0)
            compare(heading.mapToItem(root, 0, 0), headingPosition)

            mouseWheel(viewport, viewport.width / 2, viewport.height / 2, 0, -120)
            tryCompare(picker, "slide", 2)
            bar.pickerMapSlide = mapComponent
            tryVerify(() => findChild(picker, "dockSlideDot_3") !== null)
            mouseClick(findChild(picker, "dockSlideDot_3"))
            tryCompare(picker, "slide", 3)
            verify(findChild(picker, "dockPickMapPage").visible)
            verify(!heading.visible)
            const firstDot = findChild(picker, "dockSlideDot_0")
            mouseMove(firstDot)
            tryVerify(() => firstDot.hovered, 2000, "First dot remains reachable on the map page at "
                + firstDot.mapToItem(root, firstDot.width / 2, firstDot.height / 2))
            mouseClick(firstDot)
            tryCompare(picker, "slide", 0)
            compare(deck.current, 0)
            verify(!findChild(picker, "dockPickMapPage").visible)
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
            compare(picker.slide, 0)
            picker.pageWheel(0, -9, Qt.ScrollUpdate)
            compare(picker.slide, 1)
            picker.pageWheel(0, -200, Qt.ScrollUpdate)
            picker.pageWheel(0, -160, Qt.ScrollMomentum)
            picker.pageWheel(0, 0, Qt.ScrollEnd)
            picker.pageWheel(0, -100, Qt.ScrollMomentum)
            compare(picker.slide, 1)
            picker.pageWheel(0, 0, Qt.ScrollBegin)
            picker.pageWheel(0, -48, Qt.ScrollUpdate)
            compare(picker.slide, 2)
            picker.pageWheel(0, 0, Qt.ScrollEnd)
            picker.pageWheel(0, 40, Qt.NoScrollPhase)
            picker.pageWheel(0, 120, Qt.NoScrollPhase)
            compare(picker.slide, 1)
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
            verify(findChild(picker, "dockDeck_tributes") === null)
            mouseClick(findChild(picker, "dockSlideDot_2"))
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
            mouseClick(findChild(picker, "dockSlideDot_1"))
            mouseClick(manhwa)
            compare(picks.signalArguments[1][0], "manhwa")
        }

        function checkDeckBounds(deck) {
            let left = deck.width
            let right = 0
            const motion = deck.moving >= 0
            const area = motion ? findChild(bar, "dockPickViewport") : deck
            for (let i = 0; i < Math.min(3, deck.count); ++i) {
                const face = findChild(deck, "dockPickFace_" + i)
                const bounds = face.mapToItem(area, Qt.rect(0, 0, face.width, face.height))
                verify(bounds.x >= -1 && bounds.x + bounds.width <= area.width + 1,
                    deck.medium + " card " + i + " has bounds " + bounds + " within " + area.width
                    + " px (cover width " + deck.coverWidth + ", rotation " + face.rotation + ")")
                verify(bounds.y >= -1 && bounds.y + bounds.height <= area.height + 1,
                    "Card stays inside " + (motion ? "the picker" : "its row") + ": " + deck.medium)
                left = Math.min(left, bounds.x)
                right = Math.max(right, bounds.x + bounds.width)
            }
            if (!motion)
                fuzzyCompare((left + right) / 2, deck.width / 2, 1)
        }

        function checkSlides(picker, rows) {
            const viewport = findChild(picker, "dockPickViewport")
            const heading = findChild(picker, "dockPickHeading")
            for (let index = 0; index < rows.length; ++index) {
                picker.selectSlide(index)
                compare(picker.slide, index)
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
                        compare(image.fillMode, Image.PreserveAspectFit)
                    checkDeckBounds(deck)
                    verify(label.parent.x >= 0 && label.parent.x + label.width <= deck.width + 1)
                    verify(title.parent.y + title.y + title.height <= deck.height + 1)
                    const facts = findChild(deck, "dockPickFacts_" + key)
                    if (facts.visible)
                        verify(facts.parent.y + facts.y + facts.height <= deck.height + 1)
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

            const rows = [["anime", "tv", "movies"], ["manga", "comics", "manhwa"], ["books", "audiobooks", "universes"]]
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
            const audio = findChild(picker, "dockPickCard_audiobooks")
            compare(audio.width, audio.height)
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
            const windowedSquare = audio.height
            root.width = 1600
            root.height = 900
            tryVerify(() => tv.height > stdHeight)
            checkSlides(picker, rows)
            console.info("TV poster height (1264x656 / 1280x720 / 1600x900):",
                windowedHeight.toFixed(1), "/", stdHeight.toFixed(1), "/", tv.height.toFixed(1))
            console.info("Audiobook square (1264x656):", windowedSquare.toFixed(1))
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
            const audio = findChild(picker, "dockPickStage_audiobooks")
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
            verify(!findChild(picker, "dockPickFacts_tv").visible)
            compare(audio.Accessible.name, "Audiobooks")
            compare(audio.Accessible.description, "No picks available")
            picker.selectSlide(2)
            const emptyTitle = findChild(picker, "dockPickTitle_audiobooks")
            compare(emptyTitle.text, "No picks available")
            verify(emptyTitle.visible && !emptyTitle.truncated)
            verify(emptyTitle.parent.y + emptyTitle.y + emptyTitle.height <= audio.height)
        }

        function compareCaption(picker, key, medium, title, facts) {
            const labels = findChild(picker, "dockPickTitle_" + key).parent.children
            compare(labels.length, 3)
            compare(labels[0].text, medium)
            compare(labels[1].text, title)
            compare(labels[2].text, facts)
            compare(labels[2].visible, facts.length > 0)
        }

        function test_captionsShowFactsWithoutProviderDescriptions() {
            catalogue.updateUniverses()
            const franchise = catalogue.picks.universes.find(entry => entry.raw.blurb.length > 0)
            verify(franchise !== undefined, "Use an actual franchise entry with a source description")
            bar.pickerReducedMotion = true
            bar.pickerPicks = {
                anime: [{ title: "Anime pick", releaseInfo: "2024", blurb: "The crew follows a signal from a lost city." }],
                manga: [{ title: "Manga pick", year: 1997, raw: { authors: [{ name: "An author" }] } }],
                comics: [{ title: "Comic pick", publisher: "A publisher", year: 2016 }],
                manhwa: [{ title: "Manhwa pick", raw: { synopsis: "A short source synopsis." } }],
                books: [{ title: "Book pick", author: "A novelist", year: 1988 }],
                audiobooks: [{ title: "Audiobook pick", raw: { author: "An audiobook author" } }],
                universes: [franchise]
            }
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            compareCaption(picker, "anime", "Anime", "Anime pick", "2024")
            picker.selectSlide(1)
            compare(findChild(picker, "dockPickFacts_manga").text, "An author · 1997")
            compare(findChild(picker, "dockPickFacts_comics").text, "A publisher · 2016")
            compareCaption(picker, "manhwa", "Manhwa", "Manhwa pick", "")
            picker.selectSlide(2)
            compare(findChild(picker, "dockPickFacts_books").text, "A novelist · 1988")
            compare(findChild(picker, "dockPickFacts_audiobooks").text, "An audiobook author")
            compareCaption(picker, "universes", "Franchises", franchise.title, "")
        }

        function test_keyboardTurnsPagesAndFocusesOnlyTheLandingDeck() {
            bar.pickerPicks = {
                anime: [{ title: "Anime pick" }],
                tv: [{ title: "First" }, { title: "Second" }],
                comics: [{ title: "Comic pick" }],
                audiobooks: [{ title: "An audiobook" }]
            }
            bar.open = true
            const picker = findChild(bar, "dockPicker")
            verify(waitForRendering(picker))
            const viewport = findChild(picker, "dockPickViewport")
            const tv = findChild(picker, "dockPickStage_tv")
            const audio = findChild(picker, "dockPickStage_audiobooks")
            const comics = findChild(picker, "dockPickStage_comics")
            findChild(picker, "dockPickStage_anime").forceActiveFocus(Qt.TabFocusReason)
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
            tryVerify(() => audio.activeFocus)
            const rect = audio.mapToItem(viewport, Qt.rect(0, 0, audio.width, audio.height))
            verify(rect.y >= -1 && rect.y + rect.height <= viewport.height + 1)
            verify(!tv.activeFocus && !comics.activeFocus && !tv.enabled && !comics.enabled)
            keyClick(Qt.Key_Return)
            compare(picks.signalArguments[0][0], "audiobooks")
            compare(picks.signalArguments[0][1].title, "An audiobook")
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
            compare(picker.slide, 0)
            finger.release(0, viewport, start.x, start.y - 85).commit()
            tryCompare(picker, "slide", 1)
            compare(picks.count, 0)
            compare(findChild(picker, "dockDeck_tv").current, 0)
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
