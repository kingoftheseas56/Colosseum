pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "OnePieceFinalSeaData.js" as Story
import "OnePieceFinalSeaAdapter.js" as Adapter
import "OnePieceFinalSeaAtlasData.js" as Atlas
import "FinalSeaGeometry.js" as Geometry
import "OnePieceAtlasBadgeLayout.js" as BadgeLayout

FocusScope {
    id: root
    focus: true

    property bool reducedMotion: false
    property string displayFamily: "Georgia"
    property string bodyFamily: "Georgia"
    property url chartSource: Qt.resolvedUrl("FinalSeaChart.qml")
    property real shellChromeInset: 172
    property real zoom: 1
    property bool indexVisible: false
    property bool previewVisible: false
    property string selectedMarkerId: ""
    property string selectedArcId: ""
    property string hoveredMarkerId: ""
    property bool suppressFocusPreview: false
    property real previewX: 18
    property real previewY: 88
    property int viewGeneration: 0

    readonly property var arcEntries: Story.arcs
    readonly property var markerEntries: Atlas.canonMarkers
    readonly property var selectedArc: Story.arc(selectedArcId)
    readonly property var selectedLocation: selectedArc ? Atlas.location(selectedArc.locationId) : null
    readonly property bool transientOpen: previewVisible || indexVisible
    readonly property bool captureReady: chartLoader.status === Loader.Ready
    readonly property var stageGeometry: Geometry.stageGeometry(mapViewport.width, mapViewport.height, zoom)
    readonly property real sealDiameter: 66 * root.stageGeometry.scale
    readonly property var badgeProtectedAreas: BadgeLayout.protectedAreas(chartLoader.item, Atlas.locations)
    readonly property var badgePlacements: BadgeLayout.placeBadges(root.markerEntries,
        root.sealDiameter / Math.max(0.01, root.stageGeometry.scale), root.badgeProtectedAreas,
        9 / Math.max(0.01, root.stageGeometry.scale))

    signal arcRequested(var arc)
    signal newWorldRequested()

    function markerItem(id) {
        var known = Atlas.marker(id)
        if (!known) return null
        for (var i = 0; i < markerRepeater.count; ++i) {
            var item = markerRepeater.itemAt(i)
            if (item && item.objectName === "finalSeaMarker-" + id) return item
        }
        return null
    }

    function openPreview(id, arcId) {
        var marker = Atlas.marker(id)
        if (!marker || !root.visible) return false
        var selected = arcId || marker.arcIds[0]
        var entry = Story.arc(selected)
        if (!entry || marker.arcIds.indexOf(selected) < 0) return false
        closeGrace.stop()
        root.indexVisible = false
        root.selectedMarkerId = marker.id
        root.selectedArcId = entry.id
        root.previewVisible = true
        previewContent.contentY = 0
        previewGeometryRefresh.restart()
        return true
    }

    function closePreview(restoreFocus) {
        var previous = root.selectedMarkerId
        closeGrace.stop()
        root.previewVisible = false
        root.selectedMarkerId = ""
        root.selectedArcId = ""
        if (restoreFocus && root.visible) {
            var marker = root.markerItem(previous)
            root.suppressFocusPreview = true
            if (marker) marker.forceActiveFocus()
            else indexButton.forceActiveFocus()
            Qt.callLater(function() {
                if (root.visible) root.suppressFocusPreview = false
            })
        }
    }

    function closeIndex(restoreFocus) {
        root.indexVisible = false
        if (restoreFocus && root.visible) indexButton.forceActiveFocus()
    }

    function toggleIndex() {
        if (root.indexVisible) {
            root.closeIndex(true)
            return
        }
        root.closePreview(false)
        root.indexVisible = true
        indexView.currentIndex = 0
        Qt.callLater(function() {
            if (!root.indexVisible) return
            indexView.positionViewAtIndex(indexView.currentIndex, ListView.Contain)
            if (indexView.currentItem) indexView.currentItem.forceActiveFocus()
        })
    }

    function requestEscape() {
        if (root.previewVisible) {
            root.closePreview(true)
            return true
        }
        if (root.indexVisible) {
            root.closeIndex(true)
            return true
        }
        return false
    }

    function openSelectedArc() {
        var entry = root.selectedArc
        var payload = Adapter.cataloguePayload(entry)
        if (!payload) return
        root.closePreview(true)
        root.arcRequested(payload)
    }

    function focusOpenArc() {
        if (root.visible && root.previewVisible) openArcButton.forceActiveFocus()
    }

    function mapPoint(nx, ny) {
        return {
            x: mapViewport.x + root.stageGeometry.x - mapViewport.contentX + nx * root.stageGeometry.width,
            y: mapViewport.y + root.stageGeometry.y - mapViewport.contentY + ny * root.stageGeometry.height
        }
    }

    function clampViewport() {
        mapViewport.contentX = Geometry.clamp(mapViewport.contentX, 0, mapViewport.contentWidth - mapViewport.width)
        mapViewport.contentY = Geometry.clamp(mapViewport.contentY, 0, mapViewport.contentHeight - mapViewport.height)
        previewGeometryRefresh.restart()
    }

    function setZoom(value) {
        var next = Geometry.clamp(value, 1, 2)
        var scroll = Geometry.zoomScroll(mapViewport.width, mapViewport.height, root.zoom,
                                         next, mapViewport.contentX, mapViewport.contentY)
        mapViewport.cancelFlick()
        root.zoom = next
        var generation = ++root.viewGeneration
        Qt.callLater(function() {
            if (generation !== root.viewGeneration) return
            mapViewport.contentX = scroll.x
            mapViewport.contentY = scroll.y
            root.clampViewport()
        })
    }

    function revealMarker(marker) {
        if (marker) marker = root.badgePlacements[marker.id] || marker
        if (!marker) return
        var px = root.stageGeometry.x + marker.x * root.stageGeometry.width
        var py = root.stageGeometry.y + marker.y * root.stageGeometry.height
        var margin = 66
        if (px < mapViewport.contentX + margin) mapViewport.contentX = px - margin
        else if (px > mapViewport.contentX + mapViewport.width - margin)
            mapViewport.contentX = px - mapViewport.width + margin
        if (py < mapViewport.contentY + margin) mapViewport.contentY = py - margin
        else if (py > mapViewport.contentY + mapViewport.height - margin)
            mapViewport.contentY = py - mapViewport.height + margin
        root.clampViewport()
    }

    function focusArc(arcId) {
        var entry = Story.arc(arcId)
        var marker = Atlas.markerForArc(arcId)
        if (!entry || !marker || !root.visible) return false
        root.closeIndex(false)
        root.revealMarker(marker)
        root.openPreview(marker.id, arcId)
        root.suppressFocusPreview = true
        var item = root.markerItem(marker.id)
        if (item) item.forceActiveFocus()
        Qt.callLater(function() {
            if (root.visible) root.suppressFocusPreview = false
        })
        return true
    }

    function focusLocation(locationId) {
        var marker = Atlas.markerForLocation(locationId)
        return marker ? root.focusArc(marker.arcIds[0]) : false
    }

    function moveMarker(id, direction) {
        var next = Geometry.nextMarker(root.markerEntries, id, direction)
        var marker = next ? Atlas.marker(next) : null
        if (marker) root.focusArc(marker.arcIds[0])
    }

    function updatePreviewGeometry() {
        if (!root.previewVisible || !root.selectedMarkerId) return
        var marker = root.markerItem(root.selectedMarkerId)
        if (!marker) return
        var topLeft = marker.mapToItem(root, 0, 0)
        var anchor = { x: topLeft.x, y: topLeft.y, width: marker.width, height: marker.height }
        var controls = [indexButton, zoomTools, newWorldButton]
        var obstacles = []
        for (var i = 0; i < controls.length; ++i) {
            var item = controls[i]
            var point = item.mapToItem(root, 0, 0)
            obstacles.push({ x: point.x, y: point.y, width: item.width, height: item.height })
        }
        var result = Geometry.placePreview(anchor, { width: preview.width, height: preview.height },
            { x: 18, y: 88, width: Math.max(0, root.width - 36), height: Math.max(0, root.height - 158) },
            obstacles)
        root.previewX = result.x
        root.previewY = result.y
    }

    function takeKeyboardFocus() {
        root.suppressFocusPreview = true
        indexButton.forceActiveFocus()
        Qt.callLater(function() {
            if (root.visible) root.suppressFocusPreview = false
        })
    }

    function moveIndex(delta, absolute) {
        var next = absolute ? delta : indexView.currentIndex + delta
        indexView.currentIndex = Geometry.clamp(next, 0, root.arcEntries.length - 1)
        indexView.positionViewAtIndex(indexView.currentIndex, ListView.Contain)
        if (indexView.currentItem) indexView.currentItem.forceActiveFocus()
    }

    onWidthChanged: viewportRefresh.restart()
    onHeightChanged: viewportRefresh.restart()
    onZoomChanged: {
        if (!isFinite(root.zoom) || root.zoom < 1 || root.zoom > 2)
            root.zoom = Geometry.clamp(root.zoom, 1, 2)
        viewportRefresh.restart()
    }
    onVisibleChanged: {
        if (!visible) {
            root.suppressFocusPreview = true
            root.closePreview(false)
            root.indexVisible = false
            root.hoveredMarkerId = ""
        } else {
            Qt.callLater(root.takeKeyboardFocus)
        }
    }
    Component.onCompleted: Qt.callLater(root.takeKeyboardFocus)

    Timer { id: viewportRefresh; interval: 0; onTriggered: root.clampViewport() }
    Timer { id: previewGeometryRefresh; interval: 0; onTriggered: root.updatePreviewGeometry() }
    Timer {
        id: closeGrace
        interval: 160
        repeat: false
        onTriggered: {
            var marker = root.markerItem(root.selectedMarkerId)
            if (!root.previewVisible) return
            if (root.hoveredMarkerId !== root.selectedMarkerId
                    && !previewHover.hovered
                    && !(marker && marker.activeFocus)
                    && !preview.activeFocus)
                root.closePreview(false)
        }
    }

    Rectangle { anchors.fill: parent; color: "#151b18" }

    Flickable {
        id: mapViewport
        objectName: "finalSeaMapViewport"
        anchors.fill: parent
        anchors.margins: 14
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, mapStage.width)
        contentHeight: Math.max(height, mapStage.height)
        interactive: root.zoom > 1
        onContentXChanged: previewGeometryRefresh.restart()
        onContentYChanged: previewGeometryRefresh.restart()

        ScrollBar.vertical: ScrollBar {
            policy: root.zoom > 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            palette.highlight: "#a58754"
        }
        ScrollBar.horizontal: ScrollBar {
            policy: root.zoom > 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            palette.highlight: "#a58754"
        }

        Item {
            id: mapStage
            x: root.stageGeometry.x
            y: root.stageGeometry.y
            width: root.stageGeometry.width
            height: root.stageGeometry.height

            Item {
                width: 1680
                height: 960
                transform: Scale {
                    xScale: root.stageGeometry.scale
                    yScale: root.stageGeometry.scale
                }

                Loader {
                    id: chartLoader
                    width: 1680
                    height: 960
                    source: root.chartSource
                    asynchronous: true
                    onLoaded: {
                        item.displayFamily = Qt.binding(function() { return root.displayFamily })
                        item.bodyFamily = Qt.binding(function() { return root.bodyFamily })
                        item.detailLevel = Qt.binding(function() { return root.zoom })
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: chartLoader.status !== Loader.Ready
                color: "#ddc69a"
                border.color: "#8a683e"
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: chartLoader.status === Loader.Error ? "CHART UNAVAILABLE" : "PREPARING FINAL SEA"
                        color: "#63482b"
                        font.family: root.displayFamily
                        font.pixelSize: 20
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "The story index remains available."
                        color: "#806646"
                        font.family: root.bodyFamily
                        font.pixelSize: 12
                    }
                }
            }

            Repeater {
                id: markerRepeater
                model: root.markerEntries
                delegate: FinalSeaMarker {
                    required property var modelData
                    property var firstArc: Story.arc(modelData.arcIds[0])

                    markerId: modelData.id
                    title: firstArc ? firstArc.title : modelData.id
                    sealText: firstArc ? String(firstArc.order).padStart(2, "0") : ""
                    markerSource: firstArc ? Qt.resolvedUrl(Adapter.markerSourceForArc(firstArc)) : ""
                    diameter: root.sealDiameter
                    fontFamily: root.bodyFamily
                    reducedMotion: root.reducedMotion
                    selected: root.selectedMarkerId === markerId

                    readonly property var placement: root.badgePlacements[modelData.id]
                    visible: placement.visible
                    x: placement.x * mapStage.width - width / 2
                    y: placement.y * mapStage.height - height / 2
                    z: selected ? 4 : 3

                    onFocusEntered: function(id) {
                        if (!root.suppressFocusPreview && !root.indexVisible && root.visible) {
                            root.revealMarker(Atlas.marker(id))
                            root.openPreview(id)
                        }
                    }
                    onFocusLeft: closeGrace.restart()
                    onHoverEntered: function(id) {
                        root.hoveredMarkerId = id
                        if (!root.indexVisible && root.visible) root.openPreview(id)
                    }
                    onHoverLeft: function(id) {
                        if (root.hoveredMarkerId === id) root.hoveredMarkerId = ""
                        closeGrace.restart()
                    }
                    onPreviewRequested: function(id) {
                        if (root.openPreview(id)) Qt.callLater(root.focusOpenArc)
                    }
                    onMoveRequested: function(id, direction) {
                        root.moveMarker(id, direction)
                    }
                }
            }
        }
    }

    Row {
        id: zoomTools
        objectName: "finalSeaZoomTools"
        x: 28
        y: root.height - height - 26
        spacing: 4
        z: 20

        FinalSeaInkButton {
            objectName: "finalSeaZoomOut"
            text: "−"
            width: 44
            fontFamily: root.bodyFamily
            reducedMotion: root.reducedMotion
            Accessible.name: "Zoom out"
            onClicked: root.setZoom(root.zoom - 0.25)
        }
        FinalSeaInkButton {
            objectName: "finalSeaZoomReset"
            text: Math.round(root.zoom * 100) + "%"
            width: 62
            fontFamily: root.bodyFamily
            reducedMotion: root.reducedMotion
            Accessible.name: "Reset zoom to show the whole chart"
            onClicked: root.setZoom(1)
        }
        FinalSeaInkButton {
            objectName: "finalSeaZoomIn"
            text: "+"
            width: 44
            fontFamily: root.bodyFamily
            reducedMotion: root.reducedMotion
            Accessible.name: "Zoom in"
            onClicked: root.setZoom(root.zoom + 0.25)
        }
    }

    FinalSeaInkButton {
        id: newWorldButton
        objectName: "finalSeaToNewWorld"
        x: Geometry.clamp(root.mapPoint(0.11, 0.77).x - width / 2, 24, root.width - width - 24)
        y: Geometry.clamp(root.mapPoint(0.11, 0.77).y, 90, root.height - height - 90)
        width: 148
        height: 48
        z: 20
        text: "TO NEW WORLD"
        fontFamily: root.bodyFamily
        reducedMotion: root.reducedMotion
        Accessible.description: "Request the preceding New World atlas."
        onClicked: {
            root.closePreview(false)
            root.closeIndex(false)
            root.newWorldRequested()
        }
    }

    FinalSeaInkButton {
        id: indexButton
        objectName: "finalSeaIndexButton"
        x: Math.max(24, root.width - root.shellChromeInset - width)
        y: 24
        width: 116
        height: 48
        z: 30
        text: "INDEX"
        fontFamily: root.bodyFamily
        reducedMotion: root.reducedMotion
        Accessible.description: "Two current voyage arcs: Egghead and Elbaf."
        onClicked: root.toggleIndex()
    }

    FocusScope {
        id: indexPanel
        objectName: "finalSeaIndexPanel"
        visible: root.indexVisible
        x: Geometry.clamp(indexButton.x + indexButton.width - width, 18, root.width - width - 18)
        y: indexButton.y + indexButton.height + 8
        width: Math.min(388, root.width - 36)
        height: Math.min(300, root.height - y - 24)
        z: 50

        Rectangle {
            anchors.fill: parent
            color: "#efe1bd"
            border.color: "#8a6b41"
        }
        Text {
            x: 18
            y: 16
            text: "THE CURRENT VOYAGE"
            color: "#755435"
            font.family: root.displayFamily
            font.pixelSize: 19
        }
        Text {
            x: 18
            y: 44
            text: "32 Egghead · 33 Elbaf"
            color: "#8b7250"
            font.family: root.bodyFamily
            font.pixelSize: 11
        }

        ListView {
            id: indexView
            objectName: "finalSeaIndexList"
            x: 14
            y: 72
            width: parent.width - 28
            height: parent.height - y - 62
            clip: true
            spacing: 5
            boundsBehavior: Flickable.StopAtBounds
            model: root.arcEntries
            currentIndex: 0
            keyNavigationEnabled: true

            delegate: FinalSeaInkButton {
                id: indexEntry
                required property var modelData
                required property int index
                objectName: "finalSeaIndexRow-" + modelData.id
                width: indexView.width - 12
                height: 46
                fontFamily: root.bodyFamily
                reducedMotion: root.reducedMotion
                text: String(modelData.order).padStart(2, "0") + "   " + modelData.title
                Accessible.name: modelData.title
                Accessible.description: modelData.place + ". Select to preview this arc."

                onActiveFocusChanged: if (activeFocus) {
                    indexView.currentIndex = indexEntry.index
                    indexView.positionViewAtIndex(indexEntry.index, ListView.Contain)
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Down) root.moveIndex(1, false)
                    else if (event.key === Qt.Key_Up) root.moveIndex(-1, false)
                    else if (event.key === Qt.Key_Home) root.moveIndex(0, true)
                    else if (event.key === Qt.Key_End) root.moveIndex(root.arcEntries.length - 1, true)
                    else return
                    event.accepted = true
                }
                onClicked: root.focusArc(modelData.id)
            }
        }

        FinalSeaInkButton {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 14
            height: 44
            text: "CLOSE INDEX"
            fontFamily: root.bodyFamily
            reducedMotion: root.reducedMotion
            onClicked: root.closeIndex(true)
        }
    }

    FocusScope {
        id: preview
        objectName: "finalSeaArcPreview"
        visible: root.previewVisible && root.selectedArc !== null
        x: root.previewX
        y: root.previewY
        width: Math.max(0, Math.min(410, root.width - 36))
        height: Math.max(0, Math.min(350, root.height - 158))
        z: 40

        onWidthChanged: previewGeometryRefresh.restart()
        onHeightChanged: previewGeometryRefresh.restart()
        onActiveFocusChanged: if (!activeFocus) closeGrace.restart()

        Keys.onPressed: function(event) {
            var step = 0
            if (event.key === Qt.Key_PageDown) step = previewContent.height * 0.8
            else if (event.key === Qt.Key_PageUp) step = -previewContent.height * 0.8
            else if (event.key === Qt.Key_Down) step = 36
            else if (event.key === Qt.Key_Up) step = -36
            else return
            previewContent.cancelFlick()
            previewContent.contentY = Geometry.clamp(previewContent.contentY + step, 0,
                                                     previewContent.contentHeight - previewContent.height)
            event.accepted = true
        }

        Rectangle {
            anchors.fill: parent
            color: "#efe1bd"
            border.color: "#8a6b41"
            border.width: 1
        }

        HoverHandler {
            id: previewHover
            onHoveredChanged: {
                if (hovered) closeGrace.stop()
                else closeGrace.restart()
            }
        }

        Flickable {
            id: previewContent
            objectName: "finalSeaPreviewContent"
            x: 16
            y: 16
            width: parent.width - 32
            height: parent.height - 88
            clip: true
            contentWidth: width
            contentHeight: previewColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                palette.highlight: "#a58754"
            }

            Column {
                id: previewColumn
                width: previewContent.width - 10
                spacing: 10

                Item {
                    width: parent.width
                    height: 132
                    Rectangle { anchors.fill: parent; radius: 4; color: "#d6bd8b"; border.color: "#8a683e" }
                    Image {
                        id: heroImage
                        objectName: "finalSeaPreviewHero"
                        anchors.fill: parent
                        anchors.margins: 4
                        source: root.selectedArc ? Qt.resolvedUrl(Adapter.heroSourceForArc(root.selectedArc)) : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        mipmap: true
                        sourceSize.width: 720
                        sourceSize.height: 360
                        visible: status === Image.Ready
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: heroImage.status !== Image.Ready
                        text: root.selectedArc ? root.selectedArc.title.toUpperCase() : "ARC ART UNAVAILABLE"
                        color: "#65472a"
                        font.family: root.bodyFamily
                        font.pixelSize: 11
                        font.bold: true
                    }
                }

                Row {
                    width: parent.width
                    height: Math.max(82, headingColumn.implicitHeight)
                    spacing: 14

                    FinalSeaLandform {
                        width: 74
                        height: 80
                        kind: root.selectedArcId === "egghead" ? "egghead" : "adam-tree"
                    }

                    Column {
                        id: headingColumn
                        width: parent.width - 88
                        spacing: 5

                        Text {
                            width: parent.width
                            text: root.selectedArc
                                  ? String(root.selectedArc.order).padStart(2, "0") + " / "
                                    + root.selectedArc.sagaLabel.toUpperCase()
                                  : ""
                            color: "#96764d"
                            font.family: root.bodyFamily
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            width: parent.width
                            text: root.selectedArc ? root.selectedArc.title : ""
                            color: "#523a25"
                            font.family: root.displayFamily
                            font.pixelSize: 23
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            width: parent.width
                            text: root.selectedArc ? root.selectedArc.place : ""
                            color: "#896c48"
                            font.family: root.bodyFamily
                            font.pixelSize: 11
                            font.italic: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: root.selectedArc ? root.selectedArc.summary : ""
                    color: "#654b31"
                    font.family: root.bodyFamily
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    lineHeight: 1.12
                }

                Text {
                    width: parent.width
                    text: root.selectedArc
                          ? "Anime  " + root.selectedArc.anime
                            + "\nChapters  " + root.selectedArc.chapters
                            + "     Volumes  " + root.selectedArc.volumes
                          : ""
                    color: "#896c48"
                    font.family: root.bodyFamily
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    lineHeight: 1.2
                }

                Text {
                    visible: root.selectedArcId === "elbaf"
                    width: parent.width
                    text: "ONGOING · snapshot observed through manga chapter "
                          + Story.snapshot.latestObservedMangaChapter
                          + " and official anime listing "
                          + Story.snapshot.latestObservedAnimeEpisode
                          + ". The arc has no endpoint here."
                    color: "#846c4b"
                    font.family: root.bodyFamily
                    font.pixelSize: 10
                    font.italic: true
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: root.selectedLocation && root.selectedLocation.sublabels
                          ? root.selectedLocation.sublabels.join(" · ")
                          : ""
                    visible: text.length > 0
                    color: "#896c48"
                    font.family: root.bodyFamily
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: "Open the arc to browse available media. Ongoing ranges use the current catalogue snapshot."
                    color: "#8a7252"
                    font.family: root.bodyFamily
                    font.pixelSize: 10
                    font.italic: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        Row {
            x: 16
            y: parent.height - height - 14
            spacing: 8

            FinalSeaInkButton {
                id: openArcButton
                objectName: "finalSeaOpenArc"
                width: 132
                height: 44
                text: "OPEN ARC"
                enabled: root.selectedArc !== null
                fontFamily: root.bodyFamily
                reducedMotion: root.reducedMotion
                onClicked: root.openSelectedArc()
            }
            FinalSeaInkButton {
                objectName: "finalSeaClosePreview"
                width: 90
                height: 44
                text: "CLOSE"
                fontFamily: root.bodyFamily
                reducedMotion: root.reducedMotion
                onClicked: root.closePreview(true)
            }
        }
    }

    Keys.onEscapePressed: function(event) {
        event.accepted = root.requestEscape()
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)
            root.setZoom(root.zoom + 0.25)
        else if (event.key === Qt.Key_Minus)
            root.setZoom(root.zoom - 0.25)
        else if (event.key === Qt.Key_0)
            root.setZoom(1)
        else if (event.key === Qt.Key_I)
            root.toggleIndex()
        else
            return
        event.accepted = true
    }
}
