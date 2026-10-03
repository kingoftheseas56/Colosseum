import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "PorticoData.js" as Data
import ".." as Colosseum

Item {
    id: home
    objectName: "home-focus-variant"
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    property bool snapReveal: false
    readonly property real contentLeft: rowIndex.contentLeft
    readonly property bool tabsDocked: {
        page.contentY
        page.contentHeight
        lensRail.y
        return lensRail.mapToItem(home, 0, 0).y < 96 && page.contentY > 0
    }
    readonly property real contentTopInset: tabsDocked ? 166 : 108
    property var lensPositions: ({})
    property var pendingLensPosition: null
    property int shelfRevision: 0
    function focusRowLink(name, item) {
        item = item || home
        if (item.objectName === name) { item.forceActiveFocus(Qt.BacktabFocusReason); return true }
        for (var i = 0; i < item.children.length; ++i)
            if (focusRowLink(name, item.children[i])) return true
        return false
    }
    readonly property var rowIndexRows: {
        shelfRevision
        var rows = [
            {key:"featured", title:"Featured", target:feature},
            {key:"apps", title:"Apps", target:appsHeader}
        ]
        if (continueRow.visible) rows.push({key:"continue", title:"Continue", target:continueRow})
        for (var i = 0; i < shelfRepeater.count; ++i) {
            var shelf = shelfRepeater.itemAt(i)
            if (shelf) {
                var sh = shelf.modelData
                var provider = sh.providerId || (sh.apps && sh.apps.length === 1 ? sh.apps[0] : "")
                rows.push({key:sh.id, title:sh.title, target:shelf,
                    iconSource:Data.BRAND_FORMAT[provider] ? Qt.resolvedUrl(Data.glyphSource(provider)) : ""})
            }
        }
        return rows
    }
    function rememberLens() {
        pageGlide.cancelGlide()
        var rails = ({})
        for (var i = 0; i < shelfRepeater.count; ++i) {
            var shelf = shelfRepeater.itemAt(i)
            if (shelf) rails[shelf.modelData.id] = shelf.railContentX
        }
        lensPositions[controller.lens] = {y:page.contentY, rails:rails}
    }
    function restoreLens() {
        pendingLensPosition = lensPositions[controller.lens] || {
            y:Math.min(page.contentY, Math.max(0, lensRail.y - 96)), rails:({})
        }
        restorePosition.restart()
    }
    function parkRow(target) {
        if (!target) return
        var top = target.mapToItem(page.contentItem, 0, 0).y
        // Rows below the tabs park below the compact dock; the hero/apps keep the mast clear.
        animateContentY(top - (top >= lensRail.y + lensRail.height ? 166 : 108))
    }
    function leaveTabs(event) {
        if (event.key === Qt.Key_Up) {
            controller.focusArea = "app"
            controller.keyboardTarget.forceActiveFocus()
            revealApp(controller.focusIndex)
        } else if (event.key === Qt.Key_Down) {
            if (controller.shownShelves().length) {
                controller.focusArea = "shelf"
                controller.shelfIndex = 0
                controller.shelfCardIndex = 0
                controller.keyboardTarget.forceActiveFocus()
                revealShelf(0)
            }
        } else return
        event.accepted = true
    }
    onTabsDockedChanged: {
        var from = tabsDocked ? lensTabs : lensDock
        var to = tabsDocked ? lensDock : lensTabs
        if (from.activeFocus) {
            to.keyboardIndex = from.keyboardIndex
            to.forceActiveFocus(Qt.OtherFocusReason)
        }
    }
    Timer {
        id: restorePosition
        interval: 0
        onTriggered: {
            if (!home.pendingLensPosition) return
            var position = home.pendingLensPosition
            homeColumn.forceLayout()
            pageGlide.cancelGlide()
            page.contentY = home.boundedContentY(position.y)
            for (var i = 0; i < shelfRepeater.count; ++i) {
                var shelf = shelfRepeater.itemAt(i)
                if (shelf && position.rails[shelf.modelData.id] !== undefined)
                    shelf.restoreRail(position.rails[shelf.modelData.id])
            }
            home.pendingLensPosition = null
        }
    }

    function boundedContentY(value) {
        return Math.max(0, Math.min(Math.max(0, page.contentHeight - page.height), value))
    }
    function animateContentY(target) {
        target = boundedContentY(target)
        var distance = Math.abs(target - page.contentY)
        if (distance < 0.5)
            return
        if (snapReveal) {
            pageGlide.cancelGlide()
            page.contentY = target
            return
        }
        pageGlide.glideTo(target)
    }
    function mappedRect(item, targetItem, localRect) {
        var p1 = item.mapToItem(targetItem, localRect.x, localRect.y)
        var p2 = item.mapToItem(targetItem, localRect.x + localRect.width, localRect.y)
        var p3 = item.mapToItem(targetItem, localRect.x, localRect.y + localRect.height)
        var p4 = item.mapToItem(targetItem, localRect.x + localRect.width,
                               localRect.y + localRect.height)
        var minX = Math.min(p1.x, p2.x, p3.x, p4.x)
        var maxX = Math.max(p1.x, p2.x, p3.x, p4.x)
        var minY = Math.min(p1.y, p2.y, p3.y, p4.y)
        var maxY = Math.max(p1.y, p2.y, p3.y, p4.y)
        return Qt.rect(minX, minY, maxX - minX, maxY - minY)
    }
    function ensureVisibleRect(item, localRect) {
        if (!item)
            return
        var pad = 0.55 * u
        var rect = mappedRect(item, homeColumn, Qt.rect(localRect.x - pad,
                                                        localRect.y - pad,
                                                        localRect.width + 2 * pad,
                                                        localRect.height + 2 * pad))
        var topInset = home.contentTopInset + 8
        var bottomInset = 1.0 * u
        var target = page.contentY
        var visibleTop = page.contentY + topInset
        var visibleBottom = page.contentY + page.height - bottomInset
        if (rect.y < visibleTop)
            target = rect.y - topInset
        else if (rect.y + rect.height > visibleBottom)
            target = rect.y + rect.height - (page.height - bottomInset)
        animateContentY(target)
    }
    function ensureVisible(item) {
        ensureVisibleRect(item, Qt.rect(0, 0, item ? item.width : 0, item ? item.height : 0))
    }
    function scrollHome() { animateContentY(0) }
    function revealFeature() { ensureVisible(feature) }
    function revealApp(index) {
        var item = appRepeater.itemAt(index)
        if (!item)
            return
        var finalScale = controller.movingApp ? 1.04 : 1.0
        var currentScale = Math.max(0.01, item.scale)
        var factor = Math.max(1.0, finalScale / currentScale)
        var extraX = item.width * (factor - 1) / 2
        var extraY = item.height * (factor - 1) / 2
        ensureVisibleRect(item, Qt.rect(-extraX, -extraY,
                                        item.width + 2 * extraX,
                                        item.height + 2 * extraY))
    }
    function revealShelf(index) {
        var item = shelfRepeater.itemAt(index)
        if (item)
            ensureVisibleRect(item, Qt.rect(0, 0, item.width, item.height - controller.galleryMetrics.shelfGap))
    }
    function revealLens() { if (!tabsDocked) ensureVisible(lensRail) }
    function focusLens() {
        revealLens()
        (tabsDocked ? lensDock : lensTabs).forceActiveFocus(Qt.TabFocusReason)
    }
    function focusedItem() {
        if (controller.focusArea === "feature")
            return feature
        if (controller.focusArea === "app")
            return appRepeater.itemAt(controller.focusIndex)
        if (controller.focusArea === "lens")
            return lensRail
        if (controller.focusArea === "shelf") {
            var shelfItem = shelfRepeater.itemAt(controller.shelfIndex)
            return shelfItem
        }
        return null
    }
    function focusedViewportRect() {
        var item = focusedItem()
        if (!item)
            return Qt.rect(0, 0, 0, 0)
        var localRect = controller.focusArea === "shelf"
                ? Qt.rect(0, 0, item.width, item.height - controller.galleryMetrics.shelfGap)
                : Qt.rect(0, 0, item.width, item.height)
        var rect = mappedRect(item, homeColumn, localRect)
        return Qt.rect(rect.x, rect.y - page.contentY, rect.width, rect.height)
    }
    function revealCurrentFocus() {
        if (controller.viewState !== "home")
            return
        if (controller.focusArea === "shelf")
            revealShelf(controller.shelfIndex)
        else if (controller.focusArea === "lens")
            revealLens()
        else if (controller.focusArea === "app")
            revealApp(controller.focusIndex)
        else
            ensureVisible(focusedItem())
    }
    function snapCurrentFocus() {
        snapReveal = true
        revealCurrentFocus()
        snapReveal = false
    }
    readonly property real focusScrollY: page.contentY

    onWidthChanged: resizeReveal.restart()
    onHeightChanged: resizeReveal.restart()

    Timer {
        id: resizeReveal
        interval: 100
        repeat: false
        onTriggered: home.snapCurrentFocus()
    }

    Connections {
        target: page
        function onContentHeightChanged() {
            if (home.pendingLensPosition) restorePosition.restart()
        }
    }

    Colosseum.ScrollGlide { id: pageGlide; flick: page }

    Item {
        id: backdropLayer
        anchors.fill: parent

        Item {
            id: ambientViewport
            anchors { left:parent.left; right:parent.right; top:parent.top }
            height: parent.height * 0.82
            clip: true
            Image {
                id: ambientArt
                x: 0
                y: -2.4 * u
                width: parent.width
                height: parent.height + 2.4 * u
                source: controller.artUrl(controller.featured().item, true)
                fillMode: Image.PreserveAspectCrop
                horizontalAlignment: Image.AlignHCenter
                verticalAlignment: Image.AlignTop
                opacity: status === Image.Ready ? 0.42 : 0
                asynchronous: true
                cache: true
                Behavior on opacity { NumberAnimation { duration: 500 } }
            }
        }
        Rectangle {
            anchors.fill: ambientViewport
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position:0; color:Qt.rgba(6/255,7/255,11/255,0.86) }
                GradientStop { position:0.62; color:Qt.rgba(6/255,7/255,11/255,0.20) }
                GradientStop { position:1; color:Qt.rgba(6/255,7/255,11/255,0.50) }
            }
        }
        Rectangle {
            anchors { left:ambientViewport.left; right:ambientViewport.right; bottom:ambientViewport.bottom }
            height: ambientViewport.height * 0.55
            gradient: Gradient {
                GradientStop { position:0; color:"transparent" }
                GradientStop { position:1; color:controller.night }
            }
        }
    }

    Flickable {
        id: page
        objectName: "feriaHomeScroll"
        x: home.contentLeft
        width: Math.max(0, home.width - x)
        height: home.height
        contentWidth: width
        contentHeight: homeColumn.implicitHeight + 9 * u
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: true
        ScrollBar.vertical: Colosseum.HouseScrollBar { flick: page }

        Column {
            id: homeColumn
            width: page.width
            spacing: 0
            Item {
                id: topHalf
                width: parent.width
                height: 108 + feature.height + 70 + appsGrid.implicitHeight + 36

                Column {
                    anchors { left:parent.left; right:parent.right; top:parent.top; leftMargin:m; rightMargin:m; topMargin:108 }
                    spacing: 20

                    Item {
                        id: feature
                        width: Math.min(52 * u, parent.width)
                        height: 330
                        Column {
                            anchors { left:parent.left; right:parent.right; bottom:parent.bottom; bottomMargin:3.35*u }
                            spacing: 0.5 * u
                            Row {
                                spacing: 0.6 * u
                                PorticoCombinedGlyph {
                                    width:1.1*u; height:1.1*u
                                    glyphKey: controller.featured().app || "plus"
                                    tone: controller.mist
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: controller.featured().source
                                    color: controller.mist
                                    font.family: controller.uiFont
                                    font.pixelSize: 13
                                }
                            }
                            Text {
                                width: parent.width
                                text: controller.featured().item ? controller.featured().item.t : controller.providerName(controller.selectedApp)
                                color: controller.ink
                                font.family: controller.displayFont
                                font.pixelSize: 42
                                font.letterSpacing: 0
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Row {
                                spacing: 1.2 * u
                                visible: !!controller.featured().item
                                Repeater {
                                    model: controller.featured().item ? [controller.featured().item.y].concat(controller.featured().item.f) : []
                                    delegate: Text {
                                        required property string modelData
                                        required property int index
                                        text: modelData
                                        color: index === 0 ? controller.ink : controller.mist
                                        font.family: controller.uiFont
                                        font.pixelSize: 13
                                    }
                                }
                            }
                            Text {
                                width: Math.min(40*u, parent.width)
                                text: controller.featured().item ? controller.featured().item.s : "Browse " + controller.providerName(controller.selectedApp) + " in Feria."
                                color: controller.mist
                                font.family: controller.uiFont
                                font.pixelSize: 14
                                lineHeight: 1.55
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                visible: controller.focusArea === "feature"
                                width: 112; height: 42; radius:11
                                color: controller.gold
                                Text { anchors.centerIn:parent; text:controller.featured().item ? "Details" : "Open"; color:Qt.rgba(0,0,0,0.86); font.family:controller.uiFont; font.pixelSize:14; font.weight:Font.Bold }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: controller.focusArea = "feature"
                            onClicked: controller.featured().item ? controller.openTitle(controller.featured().item.id) : controller.openHost(controller.selectedApp,"","home")
                        }
                    }

                    Colosseum.WidgetHeader {
                        id: appsHeader
                        width: parent.width
                        title: "Your apps"
                        moreLabel: "See all"
                        automationId: "feriaAppsSeeAll"
                        onMoreClicked: controller.openApps()
                    }
                    GridLayout {
                        id: appsGrid
                        width: Math.min(parent.width, columns * 180 + (columns - 1) * columnSpacing)
                        columns: Math.max(1, Math.floor((parent.width + columnSpacing) / (180 + columnSpacing)))
                        columnSpacing: controller.galleryMetrics.cardGap
                        rowSpacing: controller.galleryMetrics.cardGap
                        Repeater {
                            id: appRepeater
                            model: controller.activeApps.length + 1
                            delegate: PorticoCombinedAppTile {
                                required property int index
                                property string pk: index < controller.activeApps.length ? controller.activeApps[index] : ""
                                Layout.fillWidth: true
                                Layout.preferredHeight: width * 0.6
                                unit: u
                                providerKey: pk
                                title: pk ? controller.providerName(pk) : "Apps"
                                numberText: index < 9 && pk ? String(index + 1) : ""
                                selected: controller.focusArea === "app" && controller.focusIndex === index
                                moving: controller.movingApp && selected
                                addMode: !pk
                                displayFont: controller.displayFont
                                uiFont: controller.uiFont
                                ink: controller.ink; mist:controller.mist; slate:controller.slate; gold:controller.gold
                                onEntered: { controller.selectApp(index) }
                                onTriggered: {
                                    controller.selectApp(index)
                                    if (pk) controller.openHost(pk,"","home"); else controller.openApps()
                                }
                            }
                        }
                    }
                }
            }

            Item {
                id: lensRail
                width: homeColumn.width
                height: 58
                Keys.onPressed: (event) => home.leaveTabs(event)
                Colosseum.WorldTabBar {
                    id: lensTabs
                    objectName: "feriaTabBar"
                    width: parent.width
                    backdrop: controller.backdrop
                    track: page.contentY
                    opacity: home.tabsDocked ? 0 : 1
                    enabled: !home.tabsDocked
                    tabModel: [
                        { key: "all", label: "All" },
                        { key: "watch", label: "Watch" },
                        { key: "listen", label: "Listen" },
                        { key: "read", label: "Read" }
                    ]
                    tabPrefix: "feriaLens"
                    currentTab: controller.lens
                    onTabRequested: (tab) => {
                        controller.focusArea = "lens"
                        controller.selectLens(tab)
                    }
                }
            }

            FeriaContinueRow {
                id: continueRow
                x: m; width: parent.width - 2 * m
                controller: home.controller
                bottomPadding: 36
            }

            Repeater {
                id: shelfRepeater
                model: controller.shownShelves()
                onItemAdded: home.shelfRevision++
                onItemRemoved: home.shelfRevision++
                delegate: Item {
                    id: shelf
                    required property var modelData
                    required property int index
                    readonly property real railContentX: rail.contentX
                    function restoreRail(value) {
                        rail.forceLayout()
                        rail.contentX = Math.max(rail.originX, Math.min(rail.originX + Math.max(0, rail.contentWidth - rail.width), value))
                    }
                    width: homeColumn.width
                    height: 50 + rail.height + controller.galleryMetrics.shelfGap

                    Colosseum.WidgetHeader {
                        x: m
                        width: parent.width - 2 * m
                        title: shelf.modelData.title
                        sub: shelf.modelData.chips ? controller.providerName(controller.chipSelection[shelf.modelData.id]) + " chart" : (shelf.modelData.src || "")
                        moreLabel: "See all"
                        automationId: "feriaSeeAll_" + shelf.modelData.id
                        onMoreClicked: controller.openSeeAll(shelf.modelData)
                    }

                    Row {
                        id: providerChips
                        visible: !!modelData.chips
                        anchors { right:parent.right; rightMargin:m + 104; top:parent.top; topMargin:0 }
                        spacing: 0.4 * u
                        Repeater {
                            model: modelData.chips ? Object.keys(modelData.chips).filter(function(pk){ return controller.activeApps.indexOf(pk)>=0 }) : []
                            delegate: PorticoCombinedPill {
                                required property string modelData
                                unit: u
                                compact: true
                                label: controller.providerName(modelData)
                                selected: controller.chipSelection[shelf.modelData.id] === modelData
                                onTriggered: {
                                    var next = ({ albums:controller.chipSelection.albums, artists:controller.chipSelection.artists })
                                    next[shelf.modelData.id] = modelData
                                    controller.chipSelection = next
                                }
                            }
                        }
                    }

                    ListView {
                        id: rail
                        anchors { left:parent.left; right:parent.right; top:parent.top; topMargin:50 }
                        height: controller.galleryMetrics.posterWidth * controller.galleryMetrics.posterRatio + 10 + controller.galleryMetrics.titleMinHeight + 24 + controller.galleryMetrics.hoverLift
                        orientation: ListView.Horizontal
                        spacing: controller.galleryMetrics.cardGap
                        leftMargin: m
                        rightMargin: m
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: controller.shelfItems(modelData)
                        delegate: PorticoCombinedMediaCard {
                            required property string modelData
                            required property int index
                            property var it: controller.titleObj(modelData)
                            unit: u
                            width: controller.galleryMetrics.posterWidth
                            title: it ? it.t : ""
                            sub: it ? (it.k === "artist" ? (it.f[0] || it.by || "") : (it.k === "album" ? (it.by || "") : (it.y ? it.y + "   " : "") + controller.kindLabel(it))) : ""
                            artSource: controller.artUrl(it, false)
                            shape: controller.shapeFor(it)
                            rankText: shelf.modelData.rank ? String(index + 1) : ""
                            selected: controller.focusArea === "shelf" && controller.shelfIndex === shelf.index && controller.shelfCardIndex === index
                            toneA: controller.toneFor(modelData)[0]
                            toneB: controller.toneFor(modelData)[1]
                            displayFont: controller.displayFont
                            ink:controller.ink; mist:controller.mist; slate:controller.slate; gold:controller.gold
                            onEntered: { controller.focusArea="shelf"; controller.shelfIndex=shelf.index; controller.shelfCardIndex=index }
                            onTriggered: controller.openTitle(modelData)
                        }
                        Connections {
                            target: controller
                            function onShelfCardIndexChanged() {
                                if (controller.focusArea === "shelf" && controller.shelfIndex === shelf.index)
                                    rail.positionViewAtIndex(controller.shelfCardIndex, ListView.Contain)
                            }
                        }
                    }
                }
            }

            Text {
                x: m
                width: parent.width - 2*m
                topPadding: 2*u
                bottomPadding: 4*u
                text: controller.liveDiscovery
                    ? "Feria combines catalogue and chart sources. Availability depends on each service, region, and account."
                    : "These listings are illustrative. Availability depends on each service, region, and account."
                color: controller.slate
                font.family: controller.uiFont
                font.pixelSize: 0.82 * u
                lineHeight: 1.5
                wrapMode: Text.WordWrap
            }
        }
    }

    Rectangle {
        anchors { left:parent.left; right:parent.right; top:parent.top }
        height: 4.25 * u
        visible: page.contentY > 40
        color: Qt.rgba(8/255,10/255,16/255,0.88)
        border.width: 0
        z: 35
    }
    Colosseum.RowIndexSidebar {
        id: rowIndex
        x: m
        z: 36
        pageFlick: page
        backdrop: controller.backdrop
        worldName: "Feria"
        contextLabel: "APP"
        automationPrefix: "feriaRowIndex"
        rows: home.rowIndexRows
        fixedTopInset: 108
        currentOffset: home.contentTopInset
        onRowRequested: (target) => home.parkRow(target)
    }
    Colosseum.WorldTabBar {
        id: lensDock
        objectName: "feriaTabDock"
        tabPrefix: "feriaTabDock"
        compact: true
        x: page.x + m
        y: 100
        width: Math.max(0, page.width - 2 * m)
        visible: home.tabsDocked
        backdrop: controller.backdrop
        track: page.contentY
        contentBackdrop: page
        contentTrack: page.contentY
        tabModel: lensTabs.tabModel
        currentTab: controller.lens
        z: 36
        onTabRequested: (tab) => lensTabs.tabRequested(tab)
        Keys.priority: Keys.AfterItem
        Keys.onPressed: (event) => home.leaveTabs(event)
    }
}
