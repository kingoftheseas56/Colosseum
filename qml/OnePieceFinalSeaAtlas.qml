pragma ComponentBehavior: Bound
import QtQuick
import "UniverseContentApi.js" as UniverseContent
import QtQuick.Controls.Basic
import "OnePieceFinalSeaData.js" as Story
import "OnePieceFinalSeaAdapter.js" as Adapter
import "OnePieceFinalSeaAtlasData.js" as Atlas
import "FinalSeaGeometry.js" as Geometry

FocusScope {
    id: root
    function resolveContentAsset(path) { root.universeContentGeneration; return UniverseContent.asset(path) }
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? (UniverseContent.reload(Content.generation), Content.generation) : 0
    focus: true
    clip: true

    property bool reducedMotion: false
    property string displayFamily: "Georgia"
    property string bodyFamily: "Georgia"
    // Retained for public-property compatibility; never used to fetch or draw a map.
    property url mapSource: root.resolveContentAsset("../assets/universes/one-piece/final-sea/final-sea-map.png")
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

    readonly property var arcEntries: (root.universeContentGeneration, Story.arcs)
    readonly property var markerEntries: (root.universeContentGeneration, Atlas.canonMarkers)
    readonly property var selectedArc: (root.universeContentGeneration, Story.arc)(selectedArcId)
    readonly property var selectedLocation: selectedArc ? (root.universeContentGeneration, Atlas.location)(selectedArc.locationId) : null
    readonly property bool transientOpen: previewVisible || indexVisible
    // True only after a Qt Quick frame containing the native chart is presented.
    property bool _nativeArtworkFramePresented: false
    readonly property bool captureReady: root._nativeArtworkFramePresented
    readonly property var stageGeometry: Geometry.stageGeometry(mapViewport.width, mapViewport.height, zoom)
    readonly property var controlGeometry: Geometry.stageGeometry(mapViewport.width, mapViewport.height, 1)
    readonly property real sealDiameter: 84 * root.stageGeometry.scale
    // Atlas anchors are unchanged; native art contains no baked poster medallions.
    readonly property var badgePlacements: {
        var placements = {}
        for (var marker of root.markerEntries)
            placements[marker.id] = {x: marker.x, y: marker.y, visible: true}
        return placements
    }

    signal arcRequested(var arc)
    signal newWorldRequested()

    function markerItem(id) {
        var known = (root.universeContentGeneration, Atlas.marker)(id)
        if (!known) return null
        for (var i = 0; i < markerRepeater.count; ++i) {
            var item = markerRepeater.itemAt(i)
            if (item && item.objectName === "finalSeaMarker-" + id) return item
        }
        return null
    }

    function openPreview(id, arcId) {
        var marker = (root.universeContentGeneration, Atlas.marker)(id)
        if (!marker || !root.visible) return false
        var selected = arcId || marker.arcIds[0]
        var entry = (root.universeContentGeneration, Story.arc)(selected)
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
        var payload = (root.universeContentGeneration, Adapter.cataloguePayload)(entry)
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
        var entry = (root.universeContentGeneration, Story.arc)(arcId)
        var marker = (root.universeContentGeneration, Atlas.markerForArc)(arcId)
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
        var marker = (root.universeContentGeneration, Atlas.markerForLocation)(locationId)
        return marker ? root.focusArc(marker.arcIds[0]) : false
    }

    function moveMarker(id, direction) {
        var next = Geometry.nextMarker(root.markerEntries, id, direction)
        var marker = next ? (root.universeContentGeneration, Atlas.marker)(next) : null
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

    // A constructed Item is not evidence of drawing. This signal is delivered
    // after Qt Quick presents a window frame, with the native chart in its stage.
    Connections {
        target: root.Window.window
        function onFrameSwapped() {
            if (!root._nativeArtworkFramePresented && root.visible
                    && root.width > 0 && root.height > 0
                    && mapStage.visible && mapStage.width > 0 && mapStage.height > 0
                    && mapArtwork.visible && mapArtwork.width > 0 && mapArtwork.height > 0)
                root._nativeArtworkFramePresented = true
        }
    }

    Rectangle { anchors.fill: parent; color: "#151b18" }

    Flickable {
        id: mapViewport
        objectName: "finalSeaMapViewport"
        anchors.fill: parent
        anchors.margins: 14
        // The real Index control sits over the native chart, below shell chrome.
        anchors.topMargin: 64
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

            // Native authored in fixed 1672x941 coordinates. Uniform scale only.
            FinalSeaNativeArtwork {
                id: mapArtwork
                objectName: "finalSeaMapArtwork"
                x: 0
                y: 0
                width: designWidth
                height: designHeight
                transformOrigin: Item.TopLeft
                scale: root.stageGeometry.scale
                displayFontFamily: root.displayFamily
                labelFontFamily: root.bodyFamily
            }

            Repeater {
                id: markerRepeater
                model: root.markerEntries
                delegate: FinalSeaMarker {
                    required property var modelData
                    property var firstArc: (root.universeContentGeneration, Story.arc)(modelData.arcIds[0])

                    markerId: modelData.id
                    title: firstArc ? firstArc.title : modelData.id
                    sealText: firstArc ? String(firstArc.order).padStart(2, "0") : ""
                    markerSource: firstArc ? Qt.resolvedUrl((root.universeContentGeneration, Adapter.markerSourceForArc)(firstArc)) : ""
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
                            root.revealMarker((root.universeContentGeneration, Atlas.marker)(id))
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

            // Previously baked into the PNG beside the Elbaf seal.
            // Keep it as presentation only, below the genuine marker overlay.
            Rectangle {
                objectName: "finalSeaOngoingLabel"
                x: 0.89 * parent.width
                y: 0.405 * parent.height
                width: 0.088 * parent.width
                height: 0.034 * parent.height
                z: 2
                color: "#e6d2a9"
                border.color: "#8a683e"
                Text {
                    anchors.centerIn: parent
                    text: "NOW · ONGOING"
                    font.family: root.bodyFamily
                    font.pixelSize: Math.max(8, 12 * root.stageGeometry.scale)
                    color: "#63472b"
                }
            }
        }
    }

    // Visible controls stay in the viewport while the chart zooms and pans.
    Rectangle {
        objectName: "finalSeaZoomStationBacking"
        x: zoomTools.x - 3
        y: zoomTools.y - 3
        width: zoomTools.width + 6
        height: zoomTools.height + 6
        color: "#e2cc9b"
        border.color: "#927345"
        radius: 2
        z: 19
    }

    Row {
        id: zoomTools
        objectName: "finalSeaZoomTools"
        // The visible lower-left station slides right of the shell taskbar dock's
        // closed footprint (leftEdge<=80 + 130 wide + gap); at short viewports the
        // dock otherwise covers the zoom controls.
        x: Math.max(mapViewport.x + root.controlGeometry.x + 0.0299 * root.controlGeometry.width, 218)
        y: mapViewport.y + root.controlGeometry.y + 0.8831 * root.controlGeometry.height
        spacing: 0
        z: 20

        FinalSeaInkButton {
            objectName: "finalSeaZoomOut"
            text: "−"
            width: Math.max(44, 0.0353 * root.controlGeometry.width)
            height: Math.max(44, 0.0584 * root.controlGeometry.height)
            mapArtwork: false
            fontFamily: root.bodyFamily
            reducedMotion: root.reducedMotion
            Accessible.name: "Zoom out"
            onClicked: root.setZoom(root.zoom - 0.25)
        }
        FinalSeaInkButton {
            objectName: "finalSeaZoomReset"
            text: Math.round(root.zoom * 100) + "%"
            width: Math.max(44, 0.0604 * root.controlGeometry.width)
            height: Math.max(44, 0.0584 * root.controlGeometry.height)
            mapArtwork: false
            fontFamily: root.bodyFamily
            reducedMotion: root.reducedMotion
            Accessible.name: "Reset zoom to show the whole chart"
            onClicked: root.setZoom(1)
        }
        FinalSeaInkButton {
            objectName: "finalSeaZoomIn"
            text: "+"
            width: Math.max(44, 0.0359 * root.controlGeometry.width)
            height: Math.max(44, 0.0584 * root.controlGeometry.height)
            mapArtwork: false
            fontFamily: root.bodyFamily
            reducedMotion: root.reducedMotion
            Accessible.name: "Zoom in"
            onClicked: root.setZoom(root.zoom + 0.25)
        }
    }

    FinalSeaInkButton {
        id: newWorldButton
        objectName: "finalSeaToNewWorld"
        x: root.mapPoint(0.0299, 0.8055).x
        y: root.mapPoint(0.0299, 0.8055).y
        // The painted plate scales with the chart, but the interactive target keeps
        // the 44 px minimum the FinalSeaInkButton implicit size already promises.
        width: Math.max(44, 0.1441 * root.stageGeometry.width)
        height: Math.max(44, 0.0616 * root.stageGeometry.height)
        mapArtwork: false
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
        x: root.mapPoint(0.8439, 0.0372).x
        y: root.mapPoint(0.8439, 0.0372).y
        width: Math.max(44, 0.1340 * root.stageGeometry.width)
        height: Math.max(44, 0.0574 * root.stageGeometry.height)
        mapArtwork: false
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
        y: Geometry.clamp(indexButton.y + indexButton.height + 8, 88, root.height - height - 24)
        width: Math.min(388, root.width - 36)
        height: Math.max(0, Math.min(300, root.height - 112))
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
                        source: root.selectedArc ? Qt.resolvedUrl((root.universeContentGeneration, Adapter.heroSourceForArc)(root.selectedArc)) : ""
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
                          + (root.universeContentGeneration, Story.snapshot).latestObservedMangaChapter
                          + " and official anime listing "
                          + (root.universeContentGeneration, Story.snapshot).latestObservedAnimeEpisode
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
