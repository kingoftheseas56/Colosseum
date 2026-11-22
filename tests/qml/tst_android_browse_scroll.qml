import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    name: "AndroidBrowseScroll"
    when: windowShown

    Window {
        id: host
        visible: true
        width: 1040
        height: 520
        property bool androidHost: true
        property bool desktopPresentation: true
        property real browseBottomInset: 96
    }

    Component {
        id: worldComponent
        App.WorldPage {
            anchors.fill: parent
            medium: "Theatre"
            androidHost: host.androidHost
            contentBottomInset: host.browseBottomInset
            Rectangle { objectName: "lastRow"; width: parent.width; height: 1800 }
        }
    }

    function barsOf(item) {
        const bars = item instanceof ScrollBar && item.visible && item.orientation === Qt.Vertical
                     && item.policy !== ScrollBar.AlwaysOff ? [item] : []
        for (const child of item.children)
            bars.push(...barsOf(child))
        return bars
    }

    function test_world_thumb_already_tracks_its_page() {
        const world = createTemporaryObject(worldComponent, host.contentItem)
        const flick = world.pageFlickable
        const bars = barsOf(world)
        compare(bars.length, 1)
        const bar = bars[0]
        compare(bar.flick, flick)
        const trackY = bar.background.mapToItem(world, 0, 0).y
        const thumbY = bar.contentItem.mapToItem(world, 0, 0).y
        flick.contentY = 350
        tryCompare(bar, "position", flick.visibleArea.yPosition)
        tryVerify(() => bar.contentItem.mapToItem(world, 0, 0).y > thumbY)
        compare(bar.background.mapToItem(world, 0, 0).y, trackY)
    }

    function test_world_underlays_dock_but_last_row_clears_it() {
        const world = createTemporaryObject(worldComponent, host.contentItem)
        const flick = world.pageFlickable
        const row = findChild(world, "lastRow")
        compare(flick.y + flick.height, world.height)
        flick.contentY = flick.contentHeight - flick.height
        verify(row.mapToItem(world, 0, row.height).y <= world.height - world.contentBottomInset)
        const bars = barsOf(world)
        compare(bars.length, 1)
        compare(bars[0].background.visible, false)
    }

    function dragUp(item, x, y) {
        const points = touchEvent(item)
        points.press(0, item, x, y).commit()
        for (let i = 1; i <= 4; ++i) {
            wait(30)
            points.move(0, item, x, y - 18 * i).commit()
        }
        points.release(0, item, x, y - 72).commit()
    }

    function createSeries(file) {
        const component = Qt.createComponent(Qt.resolvedUrl("../../qml/" + file + ".qml"))
        compare(component.status, Component.Ready, component.errorString())
        const episodes = Array.from({length: 120}, (_, i) => ({
            id: "ttfixture:1:" + (i + 1), season: 1, episode: i + 1, title: "Episode " + (i + 1)
        }))
        const page = createTemporaryObject(component, host.contentItem, {
            width: host.width, height: host.height
        })
        verify(page !== null)
        page.mediaType = "series"
        page.errorMsg = ""
        page.videos = episodes
        page.episodes = episodes
        page.seasons = [1, 2]
        page.loading = false
        return page
    }

    function test_series_single_surface_data() {
        return [{ tag: "classic", file: "TheatreSeries" }, { tag: "rebuilt", file: "TheatreSeriesView" }]
    }

    function test_series_single_surface(data) {
        const page = createSeries(data.file)
        const flick = findChild(page, "theatreSeriesScroll")
        const ledger = findChild(page, "theatreEpisodeVirtualSpace")
        verify(flick && ledger)
        tryVerify(() => ledger.visible && flick.contentHeight > flick.height)
        tryVerify(() => ledger.mapToItem(flick, 0, 0).y > 0)
        const initial = page.episodeVirtualContentY
        flick.contentY = initial
        wait(0)
        const bars = barsOf(page)
        compare(bars.length, 1)
        const bar = bars[0]
        compare(bar.flick, flick)
        const initialPosition = bar.position
        // Begin over a virtualized episode row, below the pinned controls.
        dragUp(flick, 290, flick.height - 65)
        verify(flick.contentY > initial, "the ledger must scroll on the page Flickable")
        tryVerify(() => bar.position > initialPosition)
        compare(barsOf(page).length, 1)

        flick.cancelFlick()
        tryCompare(flick, "moving", false)
        const start = flick.contentY
        // Begin on the pinned season control, not in the episode delegates.
        const season = findChild(page, "theatreSeason_1")
        verify(season && season.visible)
        const point = season.mapToItem(flick, season.width / 2, season.height / 2)
        dragUp(flick, point.x, point.y)
        verify(flick.contentY > start, "a vertical drag over season chrome must reach the same page Flickable")
        const rangeStrip = findChild(page, "theatreLedgerRangeStrip")
        if (rangeStrip) {
            verify(page.longRun)
            compare(rangeStrip.visible, false)
        }
    }

    function test_series_dock_clearance_data() { return test_series_single_surface_data() }

    function test_series_dock_clearance(data) {
        const page = createSeries(data.file)
        const flick = findChild(page, "theatreSeriesScroll")
        const ledger = findChild(page, "theatreEpisodeVirtualSpace")
        const sheet = findChild(page, "sourcesSheet")
        compare(flick.height, host.height)
        tryVerify(() => flick.contentHeight > flick.height)
        flick.contentY = flick.contentHeight - flick.height
        verify(ledger.mapToItem(page, 0, ledger.height).y <= host.height - host.browseBottomInset)
        compare(sheet.height, host.height)
    }
}
