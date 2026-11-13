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
            backdrop: wallpaper
            androidHost: host.androidHost
            width: host.width - 108
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
    Component { id: metricsComponent; App.AdaptiveLayout {} }
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
        wait(50)
        compare(room.library.volumeRows.length, 1)
        verify(room.library.flowViewportHeight > 0)
        verify(room.library.maxScaledVolumeHeight <= room.library.flowViewportHeight,
               "The full volume art and caption must fit their own viewport")
        const readVolume = textDescendant(room, room.library.currentActionLabel).parent
        const volumeTop = readVolume.mapToItem(room, 0, 0).y
        verify(volumeTop >= 0 && volumeTop + readVolume.height <= room.height)
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
        mouseClick(readVolume, readVolume.width / 2, readVolume.height / 2)
        compare(volumeReadSpy.count, 1)
        compare(volumeReadSpy.signalArguments[0][0], "fixture-1")
        volumeReadSpy.target = null
    }
    Component { id: libraryComponent; App.LibraryPage { active: false } }
    Component { id: tankobanLibraryComponent; App.TankobanLibraryTab {} }
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
            height: 259
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
                {tag: "phone-poster", width: 955, kind: "poster"}]
    }
    function test_inset_hero_copy_clears_cover(data) {
        host.width = data.width
        host.androidHost = true
        const title = "A Court of Thorns and Roses"
        const slide = createTemporaryObject(insetHeroComponent, host.contentItem,
            {slide: {title: title, artKind: data.kind, blurb: "By Sarah J. Maas."}})
        verify(slide)
        wait(50)
        const copy = textDescendant(slide, title).parent
        const cover = findChild(slide, "featuredInsetArt_0")
        verify(copy.x + copy.width + 12 <= cover.x, "Hero copy must clear the inset cover")
        verify(copy.y >= 0 && copy.y + copy.height <= slide.height)
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
        return [{tag: "t20", width: 762}, {tag: "oneplus", width: 955},
                {tag: "t20-125", width: 914}]
    }
    function test_clock_and_controls_fit(data) {
        host.width = data.width
        const bar = createTemporaryObject(barComponent, host.contentItem)
        verify(bar)
        bar.date = "Wednesday, September 30"
        wait(50)
        const clock = findChild(bar, "topBarClock")
        const picker = findChild(bar, "topBarWorldPicker")
        const systems = findChild(bar, "topBarSystemChrome")
        const left = findChild(bar, "topBarLeftChrome")
        verify(clock.visible, "Clock must remain visible")
        verify(picker.x >= left.x + left.width + 8, "Pills must clear clock and Back")
        verify(picker.x + picker.width + 8 <= systems.x, "Pills must clear Search")
        verify(systems.x + systems.width <= bar.width + 1)
    }
}
