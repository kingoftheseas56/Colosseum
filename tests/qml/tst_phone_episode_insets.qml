import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneEpisodeInsets"
    when: windowShown

    readonly property var fixtureEpisodes: [
        { id: "fixture-series:1:1", streamId: "fixture-series:1:1", season: 1, episode: 1,
          name: "Failure's Contagious", overview: "A local description long enough to exercise the episode copy bounds." },
        { id: "fixture-series:1:2", streamId: "fixture-series:1:2", season: 1, episode: 2,
          name: "Work Drinks", overview: "A second local episode keeps numbering and window presentation live." }
    ]

    Window {
        id: host
        width: 354
        height: 792
        visible: true
        property bool androidHost: true
    }

    Component { id: classicComponent; App.TheatreSeries {} }
    Component { id: rebuiltComponent; App.TheatreSeriesView {} }
    property var mountedPage: null

    function findItem(root, wanted) {
        if (!root) return null
        if (root.objectName === wanted) return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findItem(children[i], wanted)
            if (found) return found
        }
        return null
    }

    function mount(component, width, height, androidHost) {
        host.width = width
        host.height = height
        host.androidHost = androidHost
        wait(30)
        const episodes = fixtureEpisodes
        mountedPage = component.createObject(host.contentItem, { width: host.width, height: host.height })
        verify(mountedPage !== null, "the production Theatre title page instantiates")
        mountedPage.mediaType = "series"
        mountedPage.title = "Local episode fixture"
        mountedPage.activeSeason = 1
        mountedPage.sourceVideos = episodes
        mountedPage.animeOrder = ({ status: "unavailable", episodes: episodes,
                                     seasons: [{ number: 1 }], absoluteComplete: false,
                                     defaultOrder: "seasons" })
        mountedPage.loading = false
        tryCompare(mountedPage, "liveEpisodeDelegateCount", episodes.length, 1500)
        return mountedPage
    }

    function checkGeometry(page, label, phone, hasDivider) {
        compare(page.phoneNarrow, phone, label + " selects the expected responsive branch")
        if (page.episodeRowInset !== undefined)
            compare(page.episodeRowInset, page.bodyInset, label + " uses the body inset as its row edge")
        compare(page.episodes.length, 2, label + " keeps the local episodes in presentation")
        compare(page.nextUpEpisodeIndex, 0, label + " marks the first unwatched fixture as next up")

        const windowItem = findItem(page, "theatreEpisodeWindow")
        verify(windowItem && windowItem.count > 0, label + " keeps the virtual episode window populated")
        const row = windowItem.itemAt(0)
        verify(row !== null, label + " renders a real episode delegate")
        compare(row.width, findItem(page, "theatreEpisodeVirtualSpace").width,
                label + " delegate spans the virtual episode row")
        const inset = page.bodyInset
        const edge = row.width - inset
        const background = findItem(row, "theatreEpisodeRowBackground")
        const accent = findItem(row, "theatreEpisodeNextUpAccent")
        const number = findItem(row, "theatreEpisodeNumber")
        const thumb = findItem(row, "theatreEpisodeThumbnail")
        const copy = findItem(row, "theatreEpisodeCopy")
        const actions = findItem(row, "theatreEpisodeActions")

        verify(background && accent && number && thumb && copy && actions,
               label + " exposes the rendered row geometry")
        compare(background.x, inset, label + " background starts at the row inset")
        compare(background.x + background.width, edge,
                label + " background ends at the matching right inset")
        compare(accent.x, inset, label + " next-up accent starts at the row inset")
        verify(accent.visible, label + " keeps the next-up accent visible")
        verify(number.x >= inset && number.x + number.width <= edge,
               label + " episode numbering stays inside the row bounds")
        verify(thumb.x >= inset && thumb.x + thumb.width <= edge,
               label + " thumbnail stays inside the row bounds")
        compare(copy.x, phone ? inset + 8 : thumb.x + thumb.width + 18,
                label + " copy keeps its responsive text inset")
        verify(copy.x + copy.width <= background.x + background.width,
               label + " title and description remain inside the highlight")
        verify(actions.x >= inset && actions.x + actions.width <= edge,
               label + " play and download actions stay inside the row bounds")

        if (hasDivider) {
            const divider = findItem(row, "theatreEpisodeDivider")
            verify(divider !== null, label + " renders the classic episode divider")
            compare(divider.x, inset, label + " divider starts at the row inset")
            compare(divider.x + divider.width, edge,
                    label + " divider ends at the matching right inset")
        }
        verify(findItem(row, "theatreEpisodePlay_fixture-series:1:1") !== null,
               label + " preserves the first episode play action identity")
    }

    function cleanup() {
        if (mountedPage) mountedPage.destroy()
        mountedPage = null
        wait(0)
    }

    function test_phone_and_default_episode_row_geometry() {
        const routes = [
            { component: classicComponent, label: "classic", hasDivider: true },
            { component: rebuiltComponent, label: "rebuilt", hasDivider: false }
        ]
        const viewports = [
            { width: 320, height: 720, android: true, phone: true, label: "320px portrait" },
            { width: 354, height: 792, android: true, phone: true, label: "354px portrait" },
            { width: 792, height: 354, android: true, phone: true, label: "Android phone landscape" },
            { width: 792, height: 354, android: false, phone: false, label: "landscape default" },
            { width: 1280, height: 800, android: true, phone: false, label: "wide default" }
        ]
        for (let v = 0; v < viewports.length; ++v) {
            for (let r = 0; r < routes.length; ++r) {
                const viewport = viewports[v]
                const route = routes[r]
                const page = mount(route.component, viewport.width, viewport.height, viewport.android)
                checkGeometry(page, route.label + " at " + viewport.label,
                              viewport.phone, route.hasDivider)
                if (!viewport.phone)
                    verify(page.bodyInset > 20, route.label + " preserves the wider theme margin")
                page.destroy()
                mountedPage = null
                wait(0)
            }
        }
    }
}
