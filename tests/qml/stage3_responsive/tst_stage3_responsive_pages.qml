import QtQuick
import QtQuick.Window
import QtTest
import "../../../qml" as Colosseum

TestCase {
    id: testCase
    name: "Stage3ResponsivePages"
    when: windowShown

    property string activePageName: "biblio"

    Window {
        id: testWindow
        width: 412
        height: 915
        visible: true
        color: "#080a10"

        Colosseum.BiblioBook {
            id: biblioPage
            anchors.fill: parent
            visible: testCase.activePageName === "biblio"
        }

        Colosseum.TrackerSyncCenterPage {
            id: trackerPage
            anchors.fill: parent
            visible: testCase.activePageName === "tracker"
            trackerModel: trackerFixture
            reducedMotion: true
            stremioState: ({ status: "disconnected", hasTrakt: false })
        }
    }

    QtObject {
        id: trackerFixture
        property int revision: 1
        property var connectedTrackers: []
        property var catalogue: []
        property var globalSettings: ({
            trackerSyncEnabled: true, checkOnLaunch: true, backgroundDelivery: true,
            completionMessages: false, profileAvailable: true, editable: true
        })
        property var aggregateState: ({
            status: "Empty", healthy: true, ownerHealthy: true, ownerUnavailable: false,
            syncing: false, connectedCount: 0, waitingCount: 0, unresolvedCount: 0,
            attentionProviderCount: 0, canSyncAll: false
        })
        property var dossiers: ({})
        property var importReviews: []
        property var importSnapshot: ({ accepted: false, items: [] })
        property var exportSnapshot: ({ accepted: false, items: [] })
        property var lastActionResult: ({})
        function refresh() {}
        function providerDossier(key) { return dossiers[key] || ({ found: false, providerKey: key }) }
        function deliveryRows(key) { return [] }
        function diagnoseRoute(key) { return ({ accepted: false, code: "no_current_issue" }) }
    }

    function init() {
        testWindow.width = 412
        testWindow.height = 915
        activePageName = "biblio"
        biblioPage.book = ({})
        biblioPage.editions = []
        biblioPage.torrents = []
        biblioPage.abRows = []
        biblioPage.edLoading = false
        biblioPage.torLoading = false
        biblioPage.abLoading = false
        biblioPage.readError = ""
        biblioPage.localPath = ""
        trackerFixture.revision = 1
        trackerFixture.connectedTrackers = []
        trackerFixture.catalogue = []
        trackerFixture.aggregateState = ({
            status: "Empty", healthy: true, ownerHealthy: true, ownerUnavailable: false,
            syncing: false, connectedCount: 0, waitingCount: 0, unresolvedCount: 0,
            attentionProviderCount: 0, canSyncAll: false
        })
        wait(80)
    }

    function findDescendant(root, predicate) {
        if (!root || !root.children)
            return null
        for (var i = 0; i < root.children.length; ++i) {
            var child = root.children[i]
            if (predicate(child))
                return child
            var nested = findDescendant(child, predicate)
            if (nested)
                return nested
        }
        return null
    }

    function findText(root, exactText) {
        return findDescendant(root, function(item) {
            return item.text !== undefined && String(item.text) === exactText
        })
    }

    function itemBounds(item, page) {
        var point = item.mapToItem(page, 0, 0)
        return { x: point.x, y: point.y, right: point.x + item.width,
                 bottom: point.y + item.height, width: item.width, height: item.height }
    }

    function test_biblio_phone_stacks_cover_and_keeps_long_title_readable() {
        activePageName = "biblio"
        testWindow.width = 412
        wait(120)
        var title = findDescendant(biblioPage, function(item) {
            return item.text !== undefined && item.font !== undefined
                    && Number(item.font.pixelSize) === 54
        })
        verify(title !== null, "the actual BiblioBook title Text must be present")
        var textColumn = title.parent
        var body = textColumn.parent
        var coverColumn = null
        for (var i = 0; i < body.children.length; ++i) {
            var candidate = body.children[i]
            if (candidate !== textColumn && Math.round(candidate.width) === 268) {
                coverColumn = candidate
                break
            }
        }
        verify(coverColumn !== null, "find the actual fixed-width cover/actions column")
        title.text = "A Long Fixture Title That Must Wrap Inside the Narrow Book Page Without Hiding Its Actions"
        wait(80)
        verify(textColumn.width > 0, "narrow layout must give the title column positive width")
        verify(textColumn.width >= 250, "the title column should use the available phone width")
        verify(textColumn.y >= coverColumn.y + coverColumn.height,
               "cover and actions should stack above the book text on a phone")
        var textBounds = itemBounds(textColumn, biblioPage)
        verify(textBounds.x >= -1 && textBounds.right <= biblioPage.width + 1,
               "book text must stay inside the phone viewport")
        verify(title.height > title.font.pixelSize,
               "the populated long title should wrap instead of being clipped to one line")
        var readButton = findChild(biblioPage, "biblioPrimaryRead")
        verify(readButton !== null)
        var readBounds = itemBounds(readButton, biblioPage)
        verify(readBounds.x >= -1 && readBounds.right <= biblioPage.width + 1,
               "the Read action must remain horizontally reachable")
    }

    function test_biblio_empty_loading_error_and_scroll_states_remain_reachable() {
        activePageName = "biblio"
        testWindow.width = 412
        wait(120)
        biblioPage.edLoading = true
        biblioPage.abLoading = true
        wait(50)
        verify(findText(biblioPage, "Searching LibGen…") !== null,
               "LibGen loading state remains visible")
        verify(findText(biblioPage, "Searching AudioBookBay…") !== null,
               "audiobook loading state remains visible")
        biblioPage.edLoading = false
        biblioPage.abLoading = false
        biblioPage.readError = "Fixture source error"
        wait(50)
        verify(findText(biblioPage, "No editions found") !== null,
               "empty edition state remains visible")
        verify(findText(biblioPage, "No audiobook found") !== null,
               "empty audiobook state remains visible")
        var status = findChild(biblioPage, "biblioPrimaryReadStatus")
        verify(status !== null && status.height > 0, "Read errors have a visible status region")
        verify(status.text.indexOf("Fixture source error") >= 0,
               "the fixture error reaches the existing Read status")
        var pageScroll = findDescendant(biblioPage, function(item) {
            return item.contentHeight !== undefined && item.height > 600
                    && Math.round(item.width) === testWindow.width
        })
        verify(pageScroll !== null, "find BiblioBook's real vertical Flickable")
        pageScroll.contentY = Math.max(0, pageScroll.contentHeight - pageScroll.height)
        wait(80)
        var audiobookEmpty = findText(biblioPage, "No audiobook found")
        var audiobookBounds = itemBounds(audiobookEmpty, biblioPage)
        verify(audiobookBounds.y < pageScroll.y + pageScroll.height
                    && audiobookBounds.bottom > pageScroll.y,
               "the lower audiobook state is reachable by scrolling the real page")
    }

    function test_biblio_desktop_geometry_preserves_side_by_side_columns() {
        activePageName = "biblio"
        testWindow.width = 1280
        testWindow.height = 820
        wait(140)
        var title = findDescendant(biblioPage, function(item) {
            return item.text !== undefined && item.font !== undefined
                    && Number(item.font.pixelSize) === 54
        })
        verify(title !== null)
        var textColumn = title.parent
        var body = textColumn.parent
        var coverColumn = null
        for (var i = 0; i < body.children.length; ++i) {
            var candidate = body.children[i]
            if (candidate !== textColumn && Math.round(candidate.width) === 268) {
                coverColumn = candidate
                break
            }
        }
        verify(coverColumn !== null)
        compare(Math.round(textColumn.width), 840)
        compare(Math.round(textColumn.y), Math.round(coverColumn.y))
        verify(textColumn.x >= coverColumn.x + coverColumn.width + 63,
               "desktop retains the established cover/text gap")
    }

    function test_tracker_phone_header_cards_and_stremio_action_fit_and_scroll() {
        activePageName = "tracker"
        testWindow.width = 412
        testWindow.height = 915
        wait(160)
        var preferences = findChild(trackerPage, "trackerPreferencesButton")
        var syncAll = findChild(trackerPage, "trackerSyncAllButton")
        var emptyCard = findChild(trackerPage, "trackerEmptyState")
        var stremioCard = findChild(trackerPage, "trackerStremioCard")
        var openSettings = findChild(trackerPage, "trackerMainSyncButton")
        var scroll = findChild(trackerPage, "trackerSyncCenterScroll")
        verify(preferences && syncAll && emptyCard && stremioCard && openSettings && scroll,
               "the actual tracker page header, cards, action, and Flickable must exist")
        var preferencesBounds = itemBounds(preferences, trackerPage)
        var syncBounds = itemBounds(syncAll, trackerPage)
        var pageTitle = findChild(trackerPage, "trackerSyncPageTitle")
        var emptyBounds = itemBounds(emptyCard, trackerPage)
        var stremioBounds = itemBounds(stremioCard, trackerPage)
        var settingsBounds = itemBounds(openSettings, trackerPage)
        var catalogueTitle = findChild(trackerPage, "trackerCatalogueTitle")
        var catalogueHelper = findText(trackerPage, "Capabilities are verified per provider")
        verify(pageTitle && catalogueTitle && catalogueHelper,
               "both tracker headings and the provider helper must remain present")
        var titleBounds = itemBounds(pageTitle, trackerPage)
        verify(preferencesBounds.y >= titleBounds.bottom - 1,
               "phone header actions should reflow below the title")
        verify(preferencesBounds.x >= -1 && preferencesBounds.right <= trackerPage.width + 1,
               "Preferences must fit the phone width")
        verify(syncBounds.x >= -1 && syncBounds.right <= trackerPage.width + 1,
               "Sync all must fit the phone width")
        verify(preferencesBounds.right <= syncBounds.x,
               "header actions must remain separate and ordered")
        verify(emptyBounds.x >= -1 && emptyBounds.right <= trackerPage.width + 1,
               "native library card must fit the phone width")
        verify(stremioBounds.x >= -1 && stremioBounds.right <= trackerPage.width + 1,
               "Stremio card must fit the phone width")
        verify(settingsBounds.x >= -1 && settingsBounds.right <= trackerPage.width + 1,
               "Stremio settings action must fit the phone width")
        var catalogueTitleBounds = itemBounds(catalogueTitle, trackerPage)
        var catalogueHelperBounds = itemBounds(catalogueHelper, trackerPage)
        verify(catalogueTitleBounds.right <= trackerPage.width + 1
                    && catalogueHelperBounds.right <= trackerPage.width + 1,
               "catalogue heading and helper must fit the phone width")
        verify(catalogueHelperBounds.y >= catalogueTitleBounds.bottom - 1,
               "the catalogue helper should move below its heading on a phone")
        scroll.contentY = Math.max(0, scroll.contentHeight - scroll.height)
        wait(80)
        settingsBounds = itemBounds(openSettings, trackerPage)
        verify(settingsBounds.y >= scroll.y - 1 && settingsBounds.bottom <= scroll.y + scroll.height + 1,
               "the Stremio action must remain vertically reachable inside the real Flickable")
    }

    function test_tracker_phone_long_provider_and_owner_loading_states_keep_cards_bounded() {
        activePageName = "tracker"
        testWindow.width = 412
        var provider = "A Very Long Provider Name Used To Exercise The Connected Card Layout On A Phone"
        trackerFixture.connectedTrackers = [{
            providerKey: "trakt", providerName: provider, accountLabel: "Fixture account",
            status: "Syncing", lastSuccessfulSyncAtMs: 0, pendingCount: 1,
            waitingCount: 1, unresolvedCount: 0, capabilities: ["read_history"],
            available: true, syncEnabled: false, disconnectEnabled: true
        }]
        trackerFixture.aggregateState = ({
            status: "Syncing", healthy: true, ownerHealthy: true, ownerUnavailable: false,
            syncing: true, connectedCount: 1, waitingCount: 1, unresolvedCount: 0,
            attentionProviderCount: 0, canSyncAll: false
        })
        wait(160)
        var card = findChild(trackerPage, "trackerCard_trakt")
        verify(card !== null, "connected fixture should render the real tracker card")
        var cardBounds = itemBounds(card, trackerPage)
        verify(cardBounds.x >= -1 && cardBounds.right <= trackerPage.width + 1,
               "a populated connected tracker card must fit the phone width")
        var providerTitle = findText(trackerPage, provider)
        verify(providerTitle !== null)
        var providerTitleBounds = itemBounds(providerTitle, trackerPage)
        verify(providerTitleBounds.x >= cardBounds.x - 1
                    && providerTitleBounds.right <= cardBounds.right + 1,
               "a long provider name must remain inside its connected card")
        var progress = findText(trackerPage, "Syncing · 1 connected · 1 waiting")
        verify(progress !== null, "the tracker loading status remains visible in its existing summary")
        trackerFixture.revision = -1
        wait(80)
        compare(trackerPage.aggregateState.status, "Owner unavailable")
    }

    function test_tracker_desktop_geometry_preserves_inset_and_actions() {
        activePageName = "tracker"
        testWindow.width = 1280
        testWindow.height = 820
        trackerFixture.connectedTrackers = []
        trackerFixture.aggregateState = ({
            status: "Empty", healthy: true, ownerHealthy: true, ownerUnavailable: false,
            syncing: false, connectedCount: 0, waitingCount: 0, unresolvedCount: 0,
            attentionProviderCount: 0, canSyncAll: false
        })
        wait(160)
        compare(trackerPage.pageInset, 54)
        var title = findChild(trackerPage, "trackerSyncPageTitle")
        var subtitle = findText(trackerPage, "Your external library connections")
        var preferences = findChild(trackerPage, "trackerPreferencesButton")
        var syncAll = findChild(trackerPage, "trackerSyncAllButton")
        var emptyCard = findChild(trackerPage, "trackerEmptyState")
        var stremioCard = findChild(trackerPage, "trackerStremioCard")
        verify(title && subtitle && preferences && syncAll && emptyCard && stremioCard)
        var cardBounds = itemBounds(emptyCard, trackerPage)
        var stremioBounds = itemBounds(stremioCard, trackerPage)
        verify(cardBounds.x >= 54 && cardBounds.right <= 1226,
               "desktop card remains within its established inset")
        verify(stremioBounds.x >= 54 && stremioBounds.right <= 1226,
               "desktop Stremio card remains within its established inset")
        verify(itemBounds(preferences, trackerPage).right <= 1226)
        verify(itemBounds(syncAll, trackerPage).right <= 1226)
        var titleBounds = itemBounds(title, trackerPage)
        var subtitleBounds = itemBounds(subtitle, trackerPage)
        var controlsCenter = (titleBounds.y + subtitleBounds.bottom) / 2
        verify(Math.abs(itemBounds(preferences, trackerPage).y
                        + preferences.height / 2 - controlsCenter) < 1,
               "desktop preferences retains vertical centering beside the two-line heading")
        verify(Math.abs(itemBounds(syncAll, trackerPage).y
                        + syncAll.height / 2 - controlsCenter) < 1,
               "desktop Sync all retains vertical centering beside the two-line heading")
    }
}
