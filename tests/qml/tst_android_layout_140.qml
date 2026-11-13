import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App
import "../../qml/account" as Account

TestCase {
    id: suite
    name: "AndroidLayout140"
    when: windowShown
    function init() {
        host.androidHost = true
        host.width = 762
        host.height = 457
    }

    Window {
        id: host
        visible: true
        width: 762
        height: 457
        property bool desktopPresentation: true
        property bool androidHost: true
        property real browseBottomInset: 80
        FontLoader { source: "../../assets/fonts/Fraunces-Regular.ttf" }
        Item { id: wallpaper; anchors.fill: parent }
    }
    QtObject {
        id: localAccount
        property string mode: "localOnly"
        property string username: ""
        property bool accountFeaturesAvailable: true
    }
    Component {
        id: heroComponent
        App.FeaturedCarousel {
            androidHost: host.androidHost
            width: host.width - 108
            slides: [{title: "A featured title", blurb: "The featured description", cta: "Watch"}]
        }
    }
    function test_featured_buttons_fit_first_view_data() {
        return [{tag: "t20", width: 762, height: 457},
                {tag: "oneplus", width: 955, height: 427}]
    }
    function test_featured_buttons_fit_first_view(data) {
        host.width = data.width
        host.height = data.height
        const hero = createTemporaryObject(heroComponent, host.contentItem)
        verify(hero)
        verify(hero.height <= host.height - 96 - 12 - host.browseBottomInset,
               "Whole hero must fit between the header and the dock")
    }
    Component {
        id: barComponent
        App.TopBar {
            App.AdaptiveLayout { id: headerMetrics; androidHost: host.androidHost; viewportWidth: host.width }
            backdrop: wallpaper
            androidHost: host.androidHost
            width: host.width - headerMetrics.headerMargin * 2
            activeMedium: "Theatre"
            trackersEnabled: true
            accountController: localAccount
        }
    }
    Component {
        id: tabsComponent
        App.TheatreTabBar { width: host.width - 108; backdrop: wallpaper }
    }
    Component {
        id: welcomeComponent
        Account.AccountWelcome { width: host.width; height: host.height }
    }
    Component { id: taskbarComponent; App.Taskbar { groups: [] } }
    function test_taskbar_toggle_focus_follows_input_data() {
        return [{tag: "android-touch", android: true}, {tag: "desktop-pointer", android: false}]
    }
    function test_taskbar_toggle_focus_follows_input(data) {
        host.androidHost = data.android
        host.requestActivate()
        tryCompare(host, "active", true)
        const dock = createTemporaryObject(taskbarComponent, host.contentItem)
        verify(dock)
        const home = findChild(dock, "colosseumTaskbarHomeButton")
        const action = home.children.find(child => child.accessibleName !== undefined)
        verify(action)
        function activatePointer() {
            if (data.android) {
                touchEvent(action).press(0, action, action.width / 2, action.height / 2).commit()
                touchEvent(action).release(0, action, action.width / 2, action.height / 2).commit()
            } else {
                mouseClick(action, action.width / 2, action.height / 2)
            }
        }
        activatePointer()
        compare(dock.open, true)
        wait(300)
        activatePointer()
        compare(dock.open, false)
        wait(300)
        if (!data.android) {
            mouseMove(action, action.width / 2, action.height / 2)
            tryCompare(action, "hovered", true)
        }
        mouseMove(host.contentItem, host.width - 10, 10)
        wait(50)
        compare(action.activeFocus, false, "Pointer toggles must not leave persistent keyboard focus")
        compare(action.interactionActive, false, "Pointer highlight clears after closing and leaving the taskbar")
        action.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Return)
        compare(dock.open, true)
        keyClick(Qt.Key_Return)
        compare(dock.open, false)
        verify(action.activeFocus, "Keyboard focus remains available after toggling")
        keyClick(Qt.Key_Space)
        compare(dock.open, true)
        keyClick(Qt.Key_Space)
        compare(dock.open, false)
        verify(action.interactionActive, "Keyboard navigation retains its visible focus cue")
    }
    Component { id: metricsComponent; App.AdaptiveLayout {} }
    Component {
        id: rowIndexComponent
        App.RowIndexSidebar {
            pageFlick: null
            rows: [{key: "fixture", title: "Shelf", target: wallpaper}]
        }
    }
    function test_row_index_chevron_has_phone_touch_target() {
        const rail = createTemporaryObject(rowIndexComponent, host.contentItem)
        verify(rail)
        const metrics = findChild(rail, "rowIndexTouchMetrics")
        verify(metrics)
        metrics.presentationScale = 0.830357
        wait(50)
        const action = findChild(rail, "rowIndexCollapseAction")
        verify(action)
        verify(action.width * metrics.presentationScale >= 48 - 0.001)
        verify(action.height * metrics.presentationScale >= 48 - 0.001)
        const visual = action.parent
        mouseClick(rail, visual.x + 4, visual.y - 4)
        compare(rail.collapsed, false, "A tap above the visible chevron expands the row index")
    }
    property var win: host
    QtObject {
        id: volumeService
        property int revision: 0
        function volumesForSeries(id) {
            return [{id: "fixture-1", number: 1, title: "Romance Dawn", state: "ready", cover: ""}]
        }
    }
    Component { id: bookComponent; App.BiblioBook {} }
    SignalSpy { id: volumeReadSpy; signalName: "readVolumeRequested" }
    Component {
        id: mangaRoomComponent
        App.MangaReadingRoom {
            seriesId: "fixture"
            seriesTitle: "One Piece"
            author: "Eiichiro Oda"
            status: "Publishing"
            year: 1997
            synopsis: "A long series description that remains available while the volume controls stay clear of the header."
            service: volumeService
        }
    }
    function test_title_actions_and_volume_art_fit_data() {
        return [{tag: "t20", width: 762, height: 457},
                {tag: "oneplus", width: 955, height: 427}]
    }
    function test_title_actions_and_volume_art_fit(data) {
        host.width = data.width
        host.height = data.height
        const page = createTemporaryObject(bookComponent, host.contentItem,
            {width: host.width, height: host.height - host.browseBottomInset,
             androidHost: true, book: {title: "A Court of Thorns and Roses", id: "fixture"}})
        verify(page)
        wait(50)
        const read = findChild(page, "biblioPrimaryRead")
        verify(read)
        const readTop = read.mapToItem(page, 0, 0).y
        verify(readTop >= 0 && readTop + read.height <= page.height,
               "Book Read action must fit before scrolling")
        page.destroy()
        wait(10)
        const room = createTemporaryObject(mangaRoomComponent, host.contentItem,
            {width: host.width, height: host.height - host.browseBottomInset})
        verify(room)
        const shelfMetrics = findChild(room.library, "mangaShelfLayout")
        verify(shelfMetrics)
        shelfMetrics.presentationScale = data.width > 840 ? 0.830357 : 1.5
        wait(50)
        compare(room.library.volumeRows.length, 1)
        const fallback = findChild(room, "mangaHeaderCoverFallback")
        verify(fallback)
        if (data.width < 840) {
            verify(fallback.contentWidth <= fallback.width && fallback.contentHeight <= fallback.height,
                   "The cover fallback title fits while artwork is unavailable")
        }
        verify(room.library.flowViewportHeight > 0)
        verify(room.library.maxScaledVolumeHeight <= room.library.flowViewportHeight,
               "The full volume art and caption must fit their own viewport")
        const readVolume = textDescendant(room, room.library.currentActionLabel).parent
        const volumeTop = readVolume.mapToItem(room, 0, 0).y
        verify(volumeTop >= 0 && volumeTop + readVolume.height <= room.height)
        const readPointer = readVolume.children.find(child => child.acceptedButtons !== undefined)
        verify(readPointer)
        const hitTop = readPointer.mapToItem(room, 0, 0).y
        verify(hitTop >= 0 && hitTop + readPointer.height <= room.height,
               "The entire minimum touch target stays inside the viewport")
        const scroll = findChild(room, "mangaReadingRoomScroll")
        verify(scroll)
        scroll.contentY = Math.max(0, scroll.contentHeight - scroll.height)
        wait(50)
        compare(readVolume.mapToItem(room, 0, 0).y, volumeTop,
                "Volume actions remain fixed while the full header scrolls")
        const shelfTop = room.library.mapToItem(room, 0, 0).y
        verify(shelfTop >= 0 && shelfTop + room.library.flowViewportHeight <= volumeTop,
               "The full volume viewport is reachable above the fixed actions")
        volumeReadSpy.target = room
        volumeReadSpy.clear()
        mouseClick(readVolume, readVolume.width / 2, -2)
        compare(volumeReadSpy.count, 1)
        compare(volumeReadSpy.signalArguments[0][0], "fixture-1")
        volumeReadSpy.target = null
    }
    Component { id: libraryComponent; App.LibraryPage { active: false } }
    Component { id: tankobanLibraryComponent; App.TankobanLibraryTab {} }
    Component {
        id: flowingTankobanLibraryComponent
        Flickable {
            id: viewport
            width: 847; height: 251
            contentWidth: width
            contentHeight: library.flowHeight
            App.TankobanLibraryTab {
                id: library
                objectName: "populatedLibraryFixture"
                width: viewport.width; height: flowHeight
                pageFlick: viewport
                active: false
                allRows: [{entry: {id: "fixture", title: "A saved manga", cover: ""}, state: "new"}]
            }
        }
    }
    function test_populated_tankoban_library_starts_below_tools() {
        const viewport = createTemporaryObject(flowingTankobanLibraryComponent, host.contentItem,
            {width: 1172})
        verify(viewport)
        wait(50)
        viewport.width = 847
        wait(50)
        const library = findChild(viewport, "populatedLibraryFixture")
        const grid = findChild(library, "tankobanLibraryWall")
        compare(grid.count, 1)
        const gridTop = grid.mapToItem(library, 0, 0).y
        for (const label of ["All", "In Progress", "Downloaded", "Last Read", "Recently Added", "A-Z"]) {
            const pill = textDescendant(library, label).parent
            verify(gridTop >= pill.mapToItem(library, 0, pill.height).y,
                   "Saved covers start below " + label)
        }
    }
    function test_tankoban_library_tools_do_not_overlap_data() {
        return [{tag: "t20", width: 654}, {tag: "oneplus", width: 847}]
    }
    function test_tankoban_library_tools_do_not_overlap(data) {
        const page = createTemporaryObject(tankobanLibraryComponent, host.contentItem,
            {width: data.width, height: 700})
        verify(page)
        wait(50)
        const pills = ["All", "In Progress", "Downloaded", "Last Read", "Recently Added", "A-Z"]
            .map(label => textDescendant(page, label).parent)
        for (let i = 0; i < pills.length; i++) {
            const a = pills[i].mapToItem(page, 0, 0)
            verify(a.x >= 0 && a.x + pills[i].width <= page.width)
            for (let j = i + 1; j < pills.length; j++) {
                const b = pills[j].mapToItem(page, 0, 0)
                verify(a.x + pills[i].width <= b.x || b.x + pills[j].width <= a.x
                    || a.y + pills[i].height <= b.y || b.y + pills[j].height <= a.y,
                    pills[i].label + " and " + pills[j].label + " must not overlap")
            }
        }
    }
    function test_library_filters_fit_data() {
        return [{tag: "t20", width: 654}, {tag: "oneplus", width: 847}]
    }
    function test_library_filters_fit(data) {
        const page = createTemporaryObject(libraryComponent, host.contentItem,
            {width: data.width, height: 700})
        verify(page)
        wait(50)
        for (const label of ["Last watched", "Recently added", "A–Z", "Year", "Movies", "Series", "Ongoing", "Ended"]) {
            const text = textDescendant(page, label)
            verify(text, label)
            const pill = text.parent
            const point = pill.mapToItem(page, 0, 0)
            verify(point.x >= 0 && point.x + pill.width <= page.width,
                   label + " must fit inside the library page")
        }
        host.androidHost = false
        wait(50)
        const first = textDescendant(page, "Last watched").parent
        const last = textDescendant(page, "Ended").parent
        compare(first.mapToItem(page, 0, 0).y, last.mapToItem(page, 0, 0).y,
                "Desktop retains its original single row even at narrow widths")
    }
    Component {
        id: insetHeroComponent
        App.CarouselSlide {
            width: host.width - 108
            height: heroMetrics.heroHeight
            App.AdaptiveLayout { id: heroMetrics; androidHost: host.androidHost; viewportWidth: host.width; viewportHeight: host.height; bottomInset: host.browseBottomInset; presentationScale: host.width > 940 ? 0.830357 : 1.5 }
            shortAndroid: host.androidHost
            slideIndex: 0
        }
    }
    function textDescendant(item, value) {
        if (item.text !== undefined && item.text === value) return item
        for (const child of item.children || []) {
            const found = textDescendant(child, value)
            if (found) return found
        }
        return null
    }
    function test_inset_hero_copy_clears_cover_data() {
        return [{tag: "t20-poster", width: 762, kind: "poster"},
                {tag: "t20-square", width: 762, kind: "square"},
                {tag: "phone-poster", width: 955, height: 427, kind: "poster"},
                {tag: "phone-long-copy", width: 955, height: 427, kind: "banner", longCopy: true}]
    }
    function test_inset_hero_copy_clears_cover(data) {
        host.width = data.width
        host.height = data.height || 457
        host.androidHost = true
        const title = data.longCopy ? "Mission: Impossible – The Final Reckoning" : "A Court of Thorns and Roses"
        const slide = createTemporaryObject(insetHeroComponent, host.contentItem,
            {slide: {title: title, artKind: data.kind, blurb: data.longCopy ? "A long featured description that occupies both available lines while keeping the original title and both primary actions visible in the first viewport." : "By Sarah J. Maas."}})
        verify(slide)
        wait(50)
        const copy = textDescendant(slide, title).parent
        const cover = findChild(slide, "featuredInsetArt_0")
        if (data.kind !== "banner")
            verify(copy.x + copy.width + 12 <= cover.x, "Hero copy must clear the inset cover")
        verify(copy.y >= 0 && copy.y + copy.height <= slide.height)
        const titleText = textDescendant(slide, title)
        verify(titleText.contentHeight <= titleText.height + 0.01, "The fitted title itself stays inside its bounds")
        host.androidHost = false
        host.width = 1280
        wait(50)
        compare(copy.width, 560)
        compare(copy.x, 42)
    }
    SignalSpy { id: tabSpy; signalName: "tabRequested" }
    function test_target_size_retains_48_android_dp_data() {
        return [{tag: "phone-fit", scale: 0.828125}, {tag: "comparison", scale: 1.25},
                {tag: "tablet-default", scale: 1.5}]
    }
    function test_target_size_retains_48_android_dp(data) {
        const metrics = createTemporaryObject(metricsComponent, host.contentItem,
            {androidHost: true, presentationScale: data.scale})
        verify(metrics)
        for (const size of [23, 28, 35, 38, 46])
            verify(metrics.targetSize(size) * data.scale >= 48 - 0.001)
        metrics.androidHost = false
        compare(metrics.targetSize(23), 23)
        compare(metrics.targetInset(23), 0)
    }
    function test_theatre_tabs_have_touch_height() {
        const tabs = createTemporaryObject(tabsComponent, host.contentItem)
        verify(tabs)
        wait(50)
        const pill = findChild(tabs, "theatreTab_movies")
        verify(pill)
        const hit = pill.children[0]
        verify(hit.height >= 48, "Tab hit area must meet Android minimum")
        verify(hit.y < 0, "The hit area should extend beyond the original visual")
        tabSpy.target = tabs
        tabSpy.clear()
        mouseClick(hit, hit.width / 2, 1)
        compare(tabSpy.count, 1)
        compare(tabSpy.signalArguments[0][0], "movies")
        tabSpy.target = null
    }
    function test_welcome_choices_fit_without_scrolling() {
        host.width = 762
        host.height = 457
        const welcome = createTemporaryObject(welcomeComponent, host.contentItem)
        verify(welcome)
        wait(50)
        const names = ["accountWelcomeContinueLocal", "accountWelcomeSignIn", "accountWelcomeCreateAccount"]
        for (const name of names) {
            const choice = findChild(welcome, name)
            verify(choice && choice.visible, name)
            const top = choice.mapToItem(welcome, 0, 0).y
            verify(top >= 0 && top + choice.height <= welcome.height,
                   name + " must be fully visible before scrolling")
        }
    }
    function test_clock_and_controls_fit_data() {
        return [{tag: "t20-Theatre", width: 762, medium: "Theatre"},
                {tag: "t20-Tankoban", width: 762, medium: "Tankoban"},
                {tag: "t20-Biblio", width: 762, medium: "Biblio"},
                {tag: "t20-Universes", width: 762, medium: "Universes"},
                {tag: "t20-Home", width: 762, medium: ""},
                {tag: "oneplus", width: 955, medium: "Theatre"},
                {tag: "t20-125", width: 914, medium: "Theatre"}]
    }
    function test_clock_and_controls_fit(data) {
        host.width = data.width
        const bar = createTemporaryObject(barComponent, host.contentItem)
        verify(bar)
        findChild(bar, "topBarInputMetrics").presentationScale = data.width > 940 ? 0.830357 : 1.5
        bar.activeMedium = data.medium
        bar.clock = "12:59"
        bar.date = "Wednesday, September 30"
        wait(50)
        const clock = findChild(bar, "topBarClock")
        const picker = findChild(bar, "topBarWorldPicker")
        const systems = findChild(bar, "topBarSystemChrome")
        const left = findChild(bar, "topBarLeftChrome")
        verify(clock.visible, "Clock must remain visible")
        verify(left.x + left.width + 8 <= systems.x, "Clock and Back must clear system controls")
        verify(picker.x >= left.x + left.width + 8, "Tabs clear the clock and Back")
        verify(picker.x + picker.width + 8 <= systems.x, "Tabs clear system controls: " + JSON.stringify([left.width, picker.x, picker.width, systems.x, systems.width, bar.clockFreeSpace]))
        verify(picker.x >= 0 && picker.x + picker.width <= bar.width, "All six world tabs fit")
        compare(bar.height, 56, "The header retains one row")
        compare(picker.y + picker.height / 2, bar.height / 2)
        for (const label of ["Tankoban", "Biblio", "Theatre", "Universes", "Feria", "Vinyl"]) {
            const text = textDescendant(picker, label)
            verify(text)
            compare(text.font.pixelSize, bar.width < 900 ? 13 : 14)
            const pill = text.parent.parent
            const metrics = findChild(bar, "topBarInputMetrics")
            const padding = bar.width < 900 ? 12 : 20
            compare(pill.width, Math.max(metrics.touchTargetSize, text.parent.implicitWidth + padding))
            const action = findChild(bar, "modePillFocus_" + label)
            verify(action.height * metrics.presentationScale >= 48 - 0.001)
            verify(action.width * metrics.presentationScale >= 48 - 0.001)
        }
        verify(systems.x + systems.width <= bar.width + 1)
    }
}
