// UniversesWorld — the fourth world. Unlike the other three it is not an endless catalogue:
// the set is finite and curated, so the page is two things (Hemanth 2026-10-04):
//   · a carousel of TRIBUTES — the hand-made art pages;
//   · every universe that has a standard page, as a large full-width BANNER (never a tile),
//     under one glass strip that filters by what a universe holds. The strip is the house
//     WorldTabBar, but it FILTERS one list in place; it never splits the list across pages.
// Layout 2026-10-05 from Preflight's Universes world prototype (Preflight-Architect/arcs/
// 58-universes-and-tributes/prototypes/universes-world-html-mock): "Explore Universes" with
// its count, the glass facet strip, and 178px portal banners (UniverseBanner).
// WorldPage owns the top bar, the ONE page scroller and keyboard routing. A banner's click
// or Enter emits universeRequested with the roster entry; Main routes it to that universe's
// standard page (openRosterUniverse). The carousel emits tributeRequested for the art page.
import QtQuick
import "Universes.js" as Universes
import "SagaApi.js" as Saga

WorldPage {
    id: universes
    objectName: "universesWorld"          // automation identity, like theatreWorld
    medium: "Universes"
    // the first D-pad arrow lands the first thing on the page: the Tribute carousel when
    // there is one, as in the other worlds. The list starts below the fold then, and a
    // region that is mostly off screen cannot take the first landing (in-app walk 2026-10-05:
    // with Tributes installed the arrows never left the world root)
    initialFocusName: universes.tributes.length > 0 ? "universesTributeCarouselView" : "universesLedger"

    Theme { id: theme }

    // the shell sets this (Main's world-loader binds it to installedUniverses):
    // { extensionId, name, banner, logo } per installed universe
    property var installed: []
    // a banner → the shell: installed entries carry extensionId, curated ones a page
    signal universeRequested(var entry)
    // the carousel → the shell: the hand-made art page of an installed universe
    signal tributeRequested(var entry)

    readonly property var tributes: Universes.tributes(universes.installed)

    readonly property var roster: Universes.worldRoster(universes.installed)
    // the filter: "" shows everything; otherwise only universes that hold that form
    property string facet: ""
    readonly property var facets: Universes.rosterFacets(universes.roster)
    // the strip's tabs: All, then each form some universe holds ("all" stands for the empty filter)
    readonly property var facetTabs: [{ key: "all", label: "All" }].concat(universes.facets)
    readonly property var shown: Universes.filterRoster(universes.roster, universes.facet)
    onShownChanged: {
        universes.hovered = -1
        pile.currentIndex = universes.shown.length > 0 ? 0 : -1
    }

    // Arc 63-D2A production readiness contract. Lanista waits on
    // directorReadinessState with the existing strict-equality ui-wait-for.
    // Default to every banner in the current facet plus the active Tribute.
    property int directorShotFirstBannerIndex: 0
    property int directorShotLastBannerIndex: -1
    property bool directorIncludeTributeArt: true

    property int directorReadinessEpoch: 0
    property int directorPresentedFramesInternal: 0
    property int directorDelegateRevisionInternal: 0

    function directorStableBannerKey(entry) {
        if (!entry)
            return ""
        const extensionId = String(entry.extensionId || "")
        const name = String(entry.name || "")
        return extensionId.length > 0 ? extensionId + ":" + name : name
    }

    function directorResolvedBannerRange() {
        const count = universes.shown.length
        if (count === 0)
            return { valid: true, first: 0, last: -1 }
        const first = Number(universes.directorShotFirstBannerIndex)
        const configuredLast = Number(universes.directorShotLastBannerIndex)
        const last = configuredLast < 0 ? count - 1 : configuredLast
        const valid = isFinite(first) && isFinite(last)
                && Math.floor(first) === first && Math.floor(last) === last
                && first >= 0 && first < count && last >= first && last < count
        return { valid: valid, first: first, last: last }
    }

    readonly property string directorExpectedCohortTokenInternal: {
        const range = universes.directorResolvedBannerRange()
        const members = []
        if (universes.directorIncludeTributeArt && universes.tributes.length > 0) {
            const ti = tributeCarousel.index
            if (ti >= 0 && ti < universes.tributes.length) {
                const tribute = universes.tributes[ti]
                members.push(["tribute", String(tribute.extensionId || tribute.name || ""),
                              String(tribute.banner || "")])
            } else {
                members.push(["tribute-index-invalid", String(ti), ""])
            }
        }
        if (range.valid) {
            for (let i = range.first; i <= range.last; ++i) {
                const entry = universes.shown[i]
                members.push(["banner", universes.directorStableBannerKey(entry),
                              String(entry ? (entry.banner || "") : "")])
            }
        } else {
            members.push(["range-invalid", String(universes.directorShotFirstBannerIndex),
                          String(universes.directorShotLastBannerIndex)])
        }
        return JSON.stringify({
            facet: universes.facet,
            first: universes.directorShotFirstBannerIndex,
            last: universes.directorShotLastBannerIndex,
            includeTribute: universes.directorIncludeTributeArt,
            rangeValid: range.valid,
            members: members
        })
    }

    onDirectorExpectedCohortTokenInternalChanged: {
        universes.directorReadinessEpoch += 1
        universes.directorPresentedFramesInternal = 0
    }

    function directorCohortSnapshot() {
        // Repeater item creation/removal is part of enumerability. This revision prevents
        // a temporary delegate-construction gap from being mistaken for a stable cohort.
        universes.directorDelegateRevisionInternal
        universes.directorExpectedCohortTokenInternal

        const diagnostics = []
        const observations = []
        let required = 0
        let ready = 0
        let failed = 0
        let enumerable = true

        function diagnose(key, source, status, reason) {
            if (diagnostics.length < 8)
                diagnostics.push({ key: key, source: source, status: status, reason: reason })
        }

        function account(kind, expectedKey, expectedSource, actualKey, actualSource, status, present) {
            required += 1
            observations.push([kind, expectedKey, expectedSource, actualKey, actualSource, status, present])
            if (!expectedSource) {
                failed += 1
                diagnose(expectedKey, expectedSource, "unavailable", "missing-source")
                return
            }
            if (!present) {
                enumerable = false
                diagnose(expectedKey, expectedSource, "unavailable", "delegate-unavailable")
                return
            }
            if (actualKey !== expectedKey || actualSource !== expectedSource) {
                failed += 1
                diagnose(expectedKey, expectedSource, status, "identity-or-source-mismatch")
                return
            }
            if (status === "ready") {
                ready += 1
                return
            }
            if (status === "error" || status === "unavailable") {
                failed += 1
                diagnose(expectedKey, expectedSource, status,
                         status === "error" ? "image-error" : "image-unavailable")
                return
            }
            diagnose(expectedKey, expectedSource, status, "not-ready")
        }

        if (universes.directorIncludeTributeArt && universes.tributes.length > 0) {
            const ti = tributeCarousel.index
            if (ti < 0 || ti >= universes.tributes.length) {
                required += 1
                enumerable = false
                diagnose("tribute:" + ti, "", "unavailable", "active-tribute-not-enumerable")
                observations.push(["tribute", String(ti), "", "", "", "unavailable", false])
            } else {
                const tribute = universes.tributes[ti]
                const expectedKey = String(tribute.extensionId || tribute.name || "")
                const expectedSource = String(tribute.banner || "")
                const present = tributeCarousel.directorCurrentSlide !== null
                account("tribute", expectedKey, expectedSource,
                        present ? tributeCarousel.directorCurrentAssetKey : "",
                        present ? tributeCarousel.directorCurrentArtSource : "",
                        present ? tributeCarousel.directorCurrentArtStatus : "unavailable",
                        present)
            }
        }

        const range = universes.directorResolvedBannerRange()
        if (!range.valid) {
            enumerable = false
            diagnose("banner-range", "", "unavailable", "invalid-shot-range")
            observations.push(["range-invalid", universes.directorShotFirstBannerIndex,
                               universes.directorShotLastBannerIndex])
        } else {
            for (let i = range.first; i <= range.last; ++i) {
                const entry = universes.shown[i]
                const expectedKey = universes.directorStableBannerKey(entry)
                const expectedSource = String(entry ? (entry.banner || "") : "")
                const item = ledgerRepeater.itemAt(i)
                const present = item !== null
                account("banner", expectedKey, expectedSource,
                        present ? universes.directorStableBannerKey(item.entry) : "",
                        present ? String(item.directorBannerSource || "") : "",
                        present ? String(item.directorBannerStatus || "loading") : "unavailable",
                        present)
            }
        }

        return {
            enumerable: enumerable,
            required: required,
            ready: ready,
            failed: failed,
            diagnostics: diagnostics,
            observationToken: JSON.stringify(observations)
        }
    }

    readonly property var directorCohortSnapshotInternal: universes.directorCohortSnapshot()
    readonly property string directorObservationTokenInternal:
        String(universes.directorCohortSnapshotInternal.observationToken || "")
    readonly property int directorRequiredCount: universes.directorCohortSnapshotInternal.required
    readonly property int directorReadyCount: universes.directorCohortSnapshotInternal.ready
    readonly property int directorFailedCount: universes.directorCohortSnapshotInternal.failed
    readonly property var directorReadinessDiagnostics: universes.directorCohortSnapshotInternal.diagnostics
    readonly property int directorPresentedFrameCount: universes.directorPresentedFramesInternal
    readonly property bool directorSurfaceActiveInternal: {
        const window = universes.Window.window
        return universes.visible && universes.lifecycleActive
                && window !== null && window.visible
    }
    readonly property bool directorAllRequiredReadyInternal:
        universes.directorCohortSnapshotInternal.enumerable
        && universes.directorRequiredCount > 0
        && universes.directorFailedCount === 0
        && universes.directorReadyCount === universes.directorRequiredCount
    readonly property string directorReadinessState:
        !universes.directorSurfaceActiveInternal ? "unavailable"
        : !universes.directorCohortSnapshotInternal.enumerable || universes.directorRequiredCount === 0 ? "unavailable"
        : universes.directorFailedCount > 0 ? "partial"
        : !universes.directorAllRequiredReadyInternal ? "loading"
        : universes.directorPresentedFramesInternal >= 2 ? "settled" : "loading"

    onDirectorObservationTokenInternalChanged: universes.directorPresentedFramesInternal = 0
    onDirectorSurfaceActiveInternalChanged: universes.directorPresentedFramesInternal = 0

    // Settled is a presentation fact, not an Image.status instant: require two completed
    // swaps while this exact key/source/status cohort remains fully Ready.
    Connections {
        target: universes.Window.window
        function onFrameSwapped() {
            if (!universes.directorSurfaceActiveInternal
                    || !universes.directorAllRequiredReadyInternal) {
                universes.directorPresentedFramesInternal = 0
                return
            }
            if (universes.directorPresentedFramesInternal < 2)
                universes.directorPresentedFramesInternal += 1
        }
    }

    // hover goes STILL while the page moves (the Hall's walking rule, Hemanth 2026-07-13):
    // a stationary cursor must not light banner after banner as the page scrolls under it.
    property int hovered: -1              // which banner the pointer is on (-1 = none)
    property bool walking: false
    Timer { id: walkSettle; interval: 260; onTriggered: universes.walking = false }
    Connections {
        target: universes.pageFlickable
        function onContentYChanged() {
            universes.walking = true
            universes.hovered = -1
            walkSettle.restart()
        }
    }

    // ---- WARMING: a banner that is pointed at (hovered, or selected by keyboard) for a
    //      moment has the first posters of its shelves page fetched ahead of the click, so
    //      the page does not open on empty tiles. One universe at a time, shelves pages only;
    //      same sourceSize as the page's tiles, so the decoded image is reused as it is ----
    readonly property var pointedAt: universes.hovered >= 0 ? universes.shown[universes.hovered]
            : (pile.activeFocus && pile.currentIndex >= 0 ? universes.shown[pile.currentIndex] : null)
    property string warming: ""
    onPointedAtChanged: warmDelay.restart()
    Timer {
        id: warmDelay; interval: 320
        onTriggered: if (universes.pointedAt && universes.pointedAt.page === "shelves")
                         universes.warming = universes.pointedAt.name
    }
    Repeater {
        objectName: "universesWarmer"
        model: universes.warming.length ? Saga.shelfImageUrls(universes.warming, 8) : []
        delegate: Image {
            required property string modelData
            source: modelData
            asynchronous: true; cache: true; visible: false
            sourceSize.width: 400
        }
    }

    // ---- TRIBUTES — the art pages; the second button goes to the same universe's page ----
    FeaturedCarousel {
        id: tributeCarousel
        objectName: "universesTributeCarousel"
        automationId: "universesTributeCarouselView"
        visible: universes.tributes.length > 0
        kicker: "Tribute"
        primaryLabel: "Enter tribute"
        secondaryLabel: "Browse universe"
        slides: universes.tributes.map(function(t) {
            // one button when the Tribute is the universe's only page (Cosmere)
            const slide = { id: t.extensionId, title: t.name, blurb: t.blurb, ghost: "",
                            c1: "#14161d", c2: "#0c0e11", art: t.banner }
            if (!t.standard) slide.secondaryLabel = ""
            return slide
        })
        onPrimaryClicked: (i) => universes.tributeRequested(universes.tributes[i])
        // the same universe's standard page (a Tribute with none shows no second button)
        onSecondaryClicked: (i) => {
            const tribute = universes.tributes[i]
            for (let r = 0; r < universes.roster.length; ++r)
                if (universes.roster[r].extensionId === tribute.extensionId) {
                    universes.universeRequested(universes.roster[r])
                    return
                }
            universes.universeRequested(tribute)
        }
    }

    // ---- the heading, with how many universes the filter is showing ----
    Item {
        objectName: "universesHeading"
        width: parent.width
        height: 34
        Text {
            anchors.left: parent.left; anchors.bottom: parent.bottom
            text: "Explore Universes"
            color: theme.ink
            font.family: theme.display; font.pixelSize: 26
        }
        Text {
            objectName: "universesCount"
            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.bottomMargin: 4
            text: universes.shown.length + (universes.shown.length === 1 ? " universe" : " universes")
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 13
        }
    }

    // ---- THE FILTER — the house glass tab strip: All, then each form some universe holds ----
    WorldTabBar {
        id: facetBar
        objectName: "universesFilter"
        tabPrefix: "universesFilter"
        visible: universes.facets.length > 1
        backdrop: universes.backdrop
        track: universes.viewportContentY
        tabModel: universes.facetTabs
        currentTab: universes.facet.length ? universes.facet : "all"
        onTabRequested: (tab) => universes.facet = (tab === "all" ? "" : tab)
        // Down from the strip enters the list. The list is one region far taller than the
        // window, which the page's spatial router cannot land on its own, so the strip
        // hands over by name: bring the top of the list into view, then give it the focus.
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Down && universes.shown.length > 0) {
                universes.enterLedger()
                event.accepted = true
            }
        }
    }
    function enterLedger() {
        const flick = universes.pageFlickable
        const top = pile.mapToItem(flick.contentItem, 0, 0).y
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        // leave the strip on screen above the first banner
        if (top - flick.contentY > flick.height * 0.5)
            flick.contentY = Math.min(maxY, Math.max(0, top - flick.height * 0.35))
        pile.forceActiveFocus(Qt.TabFocusReason)
    }

    // ---- THE UNIVERSES — one large banner each, alphabetical ----
    Column {
        id: pile
        objectName: "universesLedger"     // one keyboard focus region (the Hall's own law)
        width: parent.width
        spacing: 14

        property int currentIndex: universes.shown.length > 0 ? 0 : -1
        focusPolicy: universes.shown.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: (event) => ledgerNav.handle(event)

        Repeater {
            id: ledgerRepeater
            model: universes.shown
            onItemAdded: (index, item) => universes.directorDelegateRevisionInternal += 1
            onItemRemoved: (index, item) => universes.directorDelegateRevisionInternal += 1
            delegate: UniverseBanner {
                id: bar
                required property var modelData
                required property int index
                // Arc 63-C1/C2: semantic, unique named target for deterministic
                // pointer travel. The roster owns the key; generic template categories
                // (for example "anime") are not safe identities because they collide.
                objectName: "universesBanner_" + String(bar.modelData.automationKey || bar.index)
                entry: bar.modelData
                hot: universes.hovered === bar.index
                keyboardSelected: pile.activeFocus && pile.currentIndex === bar.index
                onHoverEntered: if (!universes.walking && !ledgerNav.keyboardRecentlyMoved)
                                    universes.hovered = bar.index
                onHoverExited: if (universes.hovered === bar.index) universes.hovered = -1
                onActivated: {
                    pile.currentIndex = bar.index
                    universes.universeRequested(bar.modelData)
                }
            }
        }

        Text {
            visible: universes.shown.length === 0
            text: "No universes hold this yet."
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
            topPadding: 24; bottomPadding: 24
        }

        // Up/Down walk the pile, Enter opens; an arrow at either end stays unaccepted so
        // WorldPage's spatial router can move focus off the list (the Hall's own contract).
        KeyboardCollectionController {
            id: ledgerNav
            view: pile
            orientation: "vertical"
            count: universes.shown.length
            // the pile scrolls with WorldPage's ONE page scroller — keep the landed bar
            // inside it (the Hall's own reveal math, mapped into the page's contentItem)
            positionIndexFn: function(index) {
                const item = ledgerRepeater.itemAt(index)
                if (!item) return
                const flick = universes.pageFlickable
                const top = item.mapToItem(flick.contentItem, 0, 0).y
                const bottom = top + item.height
                const maxY = Math.max(0, flick.contentHeight - flick.height)
                if (top < flick.contentY) flick.contentY = Math.max(0, top)
                else if (bottom > flick.contentY + flick.height)
                    flick.contentY = Math.min(maxY, bottom - flick.height)
            }
            onActivated: (index) => {
                if (index >= 0 && index < universes.shown.length)
                    universes.universeRequested(universes.shown[index])
            }
        }
    }
}
