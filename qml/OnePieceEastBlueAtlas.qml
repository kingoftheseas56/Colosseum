pragma ComponentBehavior: Bound
import QtQuick
import "UniverseContentApi.js" as UniverseContent
import QtQuick.Controls
import QtQuick.Controls.Basic
import "OnePieceEastBlueAtlasData.js" as AtlasData
import "OnePieceEastBlueData.js" as EastBlue

FocusScope {
    id: root
    function resolveContentAsset(path) { root.universeContentGeneration; return UniverseContent.asset(path) }
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? (UniverseContent.reload(Content.generation), Content.generation) : 0

    property bool reducedMotion: false
    property real zoom: 1.0
    property string selectedMarkerId: ""
    property var selectedArc: null
    property bool previewVisible: false
    property bool indexVisible: false
    property bool nonCanonMode: false
    property real shellChromeInset: 172
    property bool androidHost: Qt.platform.os === "android"
    readonly property bool phoneLayout: root.androidHost && Math.min(width, height) < 600
    property real previewX: 18
    property real previewY: 88
    readonly property real shellChromeReservedLeft: root.width - 54 - (22 * 3 + 20 * 2)
    readonly property real shellChromeReservedRight: root.width - 54
    readonly property bool transientOpen: root.previewVisible || root.indexVisible
    // Contain (min), not cover: at tall or narrow windows a cover fit overflows the
    // viewport at base zoom while the Flickable is not interactive, cropping Arlong
    // and the next-sea gate beyond the window edge with no panning recovery.
    readonly property real fitScale: Math.max(0.01, Math.min(mapViewport.width / 1680, mapViewport.height / 960))
    readonly property real badgeDiameter: 84 * root.fitScale * root.zoom
    readonly property real badgeHitDiameter: Math.max(44, root.badgeDiameter)
    readonly property bool fontsReady: atlasDisplayFont.status === FontLoader.Ready
            && atlasBodyFont.status === FontLoader.Ready
    readonly property bool captureReady: root.fontsReady && mapPlate.status === Loader.Ready

    signal arcRequested(var arc)
    signal mediaRequested(var entry)
    signal paradiseRequested()

    FontLoader {
        id: atlasDisplayFont
        objectName: "eastBlueAtlasDisplayFont"
        source: "../assets/fonts/Fraunces-Regular.ttf"
    }
    FontLoader {
        id: atlasBodyFont
        objectName: "eastBlueAtlasBodyFont"
        source: "../assets/fonts/Literata-Regular.ttf"
    }

    function arcFor(id) {
        var result = (root.universeContentGeneration, EastBlue.arc)(id)
        return result && result.id === id ? result : null
    }

    function openPreview(id) {
        var arc = root.arcFor(id)
        if (!arc) return
        root.selectedMarkerId = id
        root.selectedArc = arc
        root.previewVisible = true
        root.previewMarkerHovered = false
        root.previewMarkerFocused = false
        root.previewPointerInside = false
        closeGrace.stop()
        root.schedulePreviewGeometry()
    }

    function closePreviewSoon() {
        closeGrace.restart()
    }

    function closePreview() {
        closeGrace.stop()
        root.previewVisible = false
        root.selectedMarkerId = ""
        root.selectedArc = null
        root.previewMarkerHovered = false
        root.previewMarkerFocused = false
        root.previewPointerInside = false
    }

    function requestEscape() {
        if (root.previewVisible) {
            root.closePreview()
            return true
        }
        if (root.indexVisible) {
            root.indexVisible = false
            root.nonCanonMode = false
            return true
        }
        return false
    }

    onVisibleChanged: {
        if (!visible) {
            root.closePreview()
            root.indexVisible = false
            root.nonCanonMode = false
        }
    }

    function openSelectedArc() {
        var arc = root.selectedArc
        var restoreId = root.selectedMarkerId
        if (!arc) return
        // Close the preview and park scope focus on the visible selected marker, the
        // same contract as the procedural seas. Otherwise the clicked Open Arc button
        // keeps the scope's focus flag while hidden behind the arc page, and the
        // arc-return forceActiveFocus lands on no visible item and escapes to the shell.
        root.closePreview()
        var restoreMarker = root.markerForId(restoreId)
        // The marker's activeFocusChanged handler runs synchronously inside
        // forceActiveFocus, so the suppression only spans this call.
        root.suppressFocusPreview = true
        if (restoreMarker) restoreMarker.forceActiveFocus()
        else root.forceActiveFocus()
        root.suppressFocusPreview = false
        root.arcRequested(arc)
    }

    function focusMarker(id) {
        root.openPreview(id)
        for (var i = 0; i < markerRepeater.count; ++i) {
            var marker = markerRepeater.itemAt(i)
            if (marker && marker.objectName === "eastBlueBadge-" + id) {
                marker.forceActiveFocus()
                break
            }
        }
    }

    function atlasRect(item) {
        if (!item) return null
        var center = item.mapToItem(root, item.width / 2, item.height / 2)
        return { x: center.x - item.width / 2, y: center.y - item.height / 2,
            width: item.width, height: item.height }
    }

    function rectanglesOverlap(a, b) {
        return a && b && a.x < b.x + b.width && a.x + a.width > b.x
                && a.y < b.y + b.height && a.y + a.height > b.y
    }

    function candidateOverlaps(candidate, obstacles) {
        for (var i = 0; i < obstacles.length; ++i) {
            if (rectanglesOverlap(candidate, obstacles[i])) return true
        }
        return false
    }

    function updatePreviewGeometry() {
        if (!root.previewVisible || !root.selectedMarkerId || !preview)
            return
        var marker = root.markerForId(root.selectedMarkerId)
        if (!marker) return

        var markerCenter = marker.mapToItem(root, marker.width / 2, marker.height / 2)
        var markerRect = {
            x: markerCenter.x - marker.width / 2,
            y: markerCenter.y - marker.height / 2,
            width: marker.width,
            height: marker.height
        }
        if (!markerRect) return
        var gap = 16
        var safeLeft = 18
        var safeTop = 88
        var safeRight = Math.max(safeLeft, root.width - 18)
        var safeBottom = Math.max(safeTop, root.height - 18)
        var controls = [indexButton, zoomTools, paradiseButton]
        if (indexPanel.visible) controls.push(indexPanel)
        var obstacles = [markerRect]
        for (var i = 0; i < controls.length; ++i) {
            var rect = root.atlasRect(controls[i])
            if (rect && controls[i].visible) obstacles.push(rect)
        }

        var candidates = [
            { x: markerRect.x + (markerRect.width - preview.width) / 2,
              y: markerRect.y - preview.height - gap },
            { x: markerRect.x + (markerRect.width - preview.width) / 2,
              y: markerRect.y + markerRect.height + gap },
            { x: markerRect.x + markerRect.width + gap,
              y: markerRect.y + (markerRect.height - preview.height) / 2 },
            { x: markerRect.x - preview.width - gap,
              y: markerRect.y + (markerRect.height - preview.height) / 2 }
        ]
        var maxX = Math.max(safeLeft, safeRight - preview.width)
        var maxY = Math.max(safeTop, safeBottom - preview.height)
        for (var c = 0; c < candidates.length; ++c) {
            var candidate = {
                x: Math.max(safeLeft, Math.min(maxX, candidates[c].x)),
                y: Math.max(safeTop, Math.min(maxY, candidates[c].y)),
                width: preview.width,
                height: preview.height
            }
            if (candidate.x >= safeLeft && candidate.y >= safeTop
                    && candidate.x <= maxX && candidate.y <= maxY
                    && !root.candidateOverlaps(candidate, obstacles)) {
                root.previewX = candidate.x
                root.previewY = candidate.y
                return
            }
        }

        // At a constrained edge, clamp the nearest candidate into the safe
        // viewport. The normal target sizes above always have a collision-free
        // candidate; this fallback keeps keyboard/focus previews usable when a
        // caller intentionally shrinks the atlas below its normal stage.
        var nearest = candidates[0]
        var nearestDistance = Number.MAX_VALUE
        for (var n = 0; n < candidates.length; ++n) {
            var dx = candidates[n].x - markerRect.x
            var dy = candidates[n].y - markerRect.y
            var distance = dx * dx + dy * dy
            if (distance < nearestDistance) {
                nearestDistance = distance
                nearest = candidates[n]
            }
        }
        root.previewX = Math.max(safeLeft, Math.min(maxX, nearest.x))
        root.previewY = Math.max(safeTop, Math.min(maxY, nearest.y))
    }

    function schedulePreviewGeometry() {
        if (root.previewVisible) previewGeometryRefresh.restart()
    }

    function markerForId(id) {
        var target = String(id).indexOf("eastBlueBadge-") === 0 ? String(id) : "eastBlueBadge-" + id
        for (var i = 0; i < markerRepeater.count; ++i) {
            var marker = markerRepeater.itemAt(i)
            if (marker && marker.objectName === target)
                return marker
        }
        return null
    }

    // Deterministic inspection seam used by the Qt Test harness; production
    // interaction still travels through the native controls themselves.
    function markerForTest(id) {
        return root.markerForId(id)
    }

    function nonCanonActionForTest(id) {
        for (var i = 0; i < nonCanonRepeater.count; ++i) {
            var row = nonCanonRepeater.itemAt(i)
            if (!row) continue
            for (var j = 0; j < row.children.length; ++j) {
                var child = row.children[j]
                if (child && child.objectName === "eastBlueNonCanonAction-" + id)
                    return child
            }
        }
        return null
    }

    Timer {
        id: closeGrace
        interval: 160
        repeat: false
        onTriggered: {
            if (!root.previewPointerInside && !root.previewMarkerHovered
                    && !root.previewMarkerFocused && !preview.activeFocus)
                root.closePreview()
        }
    }

    Timer {
        id: previewGeometryRefresh
        interval: 0
        repeat: false
        onTriggered: root.updatePreviewGeometry()
    }

    property bool previewMarkerHovered: false
    property bool previewMarkerFocused: false
    property bool previewPointerInside: false
    property bool suppressFocusPreview: false

    onWidthChanged: root.schedulePreviewGeometry()
    onHeightChanged: root.schedulePreviewGeometry()
    onZoomChanged: root.schedulePreviewGeometry()
    onSelectedMarkerIdChanged: root.schedulePreviewGeometry()
    onSelectedArcChanged: {
        if (previewPoster) {
            previewPoster.canonPosterFallback = false
            previewPoster.posterFallback = false
        }
        root.schedulePreviewGeometry()
    }

    Rectangle { anchors.fill: parent; color: "#101816" }

    // A single logical 1680×960 stage keeps artwork and controls aligned. The stage
    // always contains at base zoom; the Flickable provides panning at higher zoom.
    Flickable {
        id: mapViewport
        anchors.fill: parent
        anchors.margins: 22
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(mapStage.width, width)
        contentHeight: Math.max(mapStage.height, height)
        interactive: root.zoom > 1.0
        onContentXChanged: root.schedulePreviewGeometry()
        onContentYChanged: root.schedulePreviewGeometry()

        Item {
            id: mapStage
            width: 1680 * root.fitScale * root.zoom
            height: 960 * root.fitScale * root.zoom
            x: Math.max(0, (mapViewport.width - width) / 2)
            y: Math.max(0, (mapViewport.height - height) / 2)

            // Keep native chart coordinates at 1680x960; scale only the drawing layer.
            // The stage and all marker placements retain their existing geometry.
            Loader {
                id: mapPlate
                objectName: "eastBlueAtlasPlate"
                width: 1680
                height: 960
                asynchronous: true
                source: Qt.resolvedUrl("EastBlueChart.qml")
                transform: Scale {
                    origin.x: 0
                    origin.y: 0
                    xScale: root.fitScale * root.zoom
                    yScale: root.fitScale * root.zoom
                }
            }
            Rectangle {
                anchors.fill: parent
                visible: mapPlate.status !== Loader.Ready
                color: "#e7d5aa"
                border.color: "#80633e"
                Text { anchors.centerIn: parent; text: "EAST BLUE ATLAS ART UNAVAILABLE"; color: "#80633e"; font.family: atlasBodyFont.name; font.pixelSize: 12; font.bold: true }
            }

            Repeater {
                id: markerRepeater
                model: (root.universeContentGeneration, AtlasData.canonMarkers)

                delegate: FocusScope {
                    id: marker
                    required property var modelData
                    property string markerId: modelData.id
                    property var markerArc: root.arcFor(markerId)
                    property bool markerHovered: markerHover.hovered
                    objectName: "eastBlueBadge-" + markerId
                    Accessible.role: Accessible.Button
                    Accessible.name: markerArc ? markerArc.title : markerId
                    Accessible.description: "Hover or focus to preview this East Blue arc"

                    activeFocusOnTab: true
                    Keys.onReturnPressed: root.openPreview(marker.markerId)
                    Keys.onEnterPressed: root.openPreview(marker.markerId)
                    onActiveFocusChanged: {
                        if (marker.markerId === root.selectedMarkerId)
                            root.previewMarkerFocused = activeFocus
                        if (activeFocus) {
                            if (!root.suppressFocusPreview)
                                root.openPreview(marker.markerId)
                            root.previewMarkerFocused = true
                        } else {
                            root.closePreviewSoon()
                        }
                    }

                    x: modelData.x * mapStage.width - width / 2
                    y: modelData.y * mapStage.height - height / 2
                    width: root.badgeHitDiameter
                    height: root.badgeHitDiameter
                    z: 4

                    HoverHandler {
                        id: markerHover
                        onHoveredChanged: {
                            if (hovered) {
                                root.openPreview(marker.markerId)
                                root.previewMarkerHovered = true
                            } else {
                                if (root.selectedMarkerId === marker.markerId)
                                    root.previewMarkerHovered = false
                                root.closePreviewSoon()
                            }
                        }
                    }
                    // Deliberately consumes no pointer buttons: badge clicks are no-ops.
                    TapHandler { acceptedButtons: Qt.NoButton }

                    Button {
                        id: badgeButton
                        objectName: "eastBlueBadgeImage-" + marker.markerId
                        width: root.badgeDiameter
                        height: root.badgeDiameter
                        anchors.centerIn: parent
                        activeFocusOnTab: false
                        // Keyboard-only badges follow focusPolicy: Qt.TabFocus
                        // semantics on the delegate FocusScope. Keeping
                        // this visual Button unfocusable makes pointer clicks
                        // inert without opening a preview through focus changes.
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: false
                        padding: 0
                        background: null
                        contentItem: Image {
                            id: badgeImage
                            source: (root.universeContentGeneration, AtlasData.badgeSource)(marker.markerId)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: badgeImage.status === Image.Error
                            radius: width / 2
                            color: "#d8bd8a"
                            border.color: "#80633e"
                            Text { anchors.centerIn: parent; text: "ARC"; color: "#80633e"; font.family: atlasBodyFont.name; font.pixelSize: 9; font.bold: true }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: width / 2
                        color: "transparent"
                        border.width: badgeButton.activeFocus ? 2 : 0
                        border.color: "#f4d282"
                    }
                }
            }

            Button {
                id: paradiseButton
                objectName: "eastBlueToParadise"
                parent: root.phoneLayout ? root : mapStage
                z: 20
                x: root.phoneLayout ? root.width - width - 16 : 0.8381 * mapStage.width - width
                // The design coordinate sits beside Reverse Mountain. At short
                // viewports the 48 px native hit target would otherwise fall
                // through the Flickable clip by a few pixels; keep it in the
                // same mapped lower-right region while clamping its bottom edge.
                y: root.phoneLayout ? root.height - height - 16 : Math.min(0.8938 * mapStage.height,
                            Math.max(0, mapViewport.height - mapStage.y - height))
                width: root.phoneLayout ? 148 : 148 * root.zoom
                height: root.phoneLayout ? 48 : 48 * root.zoom
                text: "TO PARADISE"
                activeFocusOnTab: true
                focusPolicy: Qt.StrongFocus
                font.family: atlasBodyFont.name
                font.pixelSize: Math.max(8, 11 * root.zoom)
                font.bold: true
                palette.buttonText: "#584126"
                contentItem: Text {
                    text: paradiseButton.text
                    color: paradiseButton.palette.buttonText
                    font.family: atlasBodyFont.name
                    font.pixelSize: Math.max(8, 11 * root.zoom)
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    radius: 3
                    color: paradiseButton.activeFocus ? "#f2dfb2" : "#ead2a0"
                    border.width: 1
                    border.color: "#795a35"
                }
                onClicked: root.paradiseRequested()
            }
        }
    }

    Row {
        id: zoomTools
        objectName: "eastBlueZoomTools"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        // Clear of the shell taskbar dock's closed footprint: the dock spans
        // leftEdge..leftEdge+130 with leftEdge clamped to 80, plus a gap. Under it
        // the zoom clicks hit the dock instead of the chart controls.
        anchors.leftMargin: root.phoneLayout ? 16 : 218
        anchors.bottomMargin: root.phoneLayout ? 16 : 30
        spacing: 4
        z: 20
        Button {
            id: zoomOutButton
            objectName: "eastBlueZoomOut"
            text: "−"; width: 44; height: 44
            Accessible.name: "Zoom out"
            font.family: atlasBodyFont.name
            contentItem: Text { text: zoomOutButton.text; font.family: atlasBodyFont.name; font.pixelSize: 14; color: zoomOutButton.palette.buttonText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            onClicked: root.zoom = Math.max(1.0, root.zoom - 0.25)
        }
        Button {
            id: zoomResetButton
            objectName: "eastBlueZoomReset"
            text: "100%"; width: 58; height: 44
            font.family: atlasBodyFont.name
            contentItem: Text { text: zoomResetButton.text; font.family: atlasBodyFont.name; font.pixelSize: 11; color: zoomResetButton.palette.buttonText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            onClicked: root.zoom = 1.0
        }
        Button {
            id: zoomInButton
            objectName: "eastBlueZoomIn"
            text: "+"; width: 44; height: 44
            Accessible.name: "Zoom in"
            font.family: atlasBodyFont.name
            contentItem: Text { text: zoomInButton.text; font.family: atlasBodyFont.name; font.pixelSize: 14; color: zoomInButton.palette.buttonText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            onClicked: root.zoom = Math.min(2.0, root.zoom + 0.25)
        }
    }

                        Button {
        id: indexButton
        objectName: "eastBlueIndexButton"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 24
        anchors.rightMargin: root.phoneLayout ? 16 : root.shellChromeInset
        width: 116
        height: 48
        z: 30
        text: ""
        font.family: atlasBodyFont.name
        activeFocusOnTab: true
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.role: Accessible.Button
        Accessible.name: "INDEX"
        Accessible.description: "Open the East Blue arc index"
        background: Rectangle {
            id: indexBackground
            objectName: "eastBlueIndexBackground"
            radius: 3
            color: indexButton.pressed ? "#d8ba7c"
                   : indexButton.activeFocus ? "#f6e5bb"
                   : indexButton.hovered ? "#f0d9a4" : "#ead2a0"
            border.width: 1
            border.color: indexButton.activeFocus ? "#5f4322" : "#795a35"
        }
        contentItem: Row {
            anchors.centerIn: parent
            spacing: 8
            Item {
                width: 18
                height: 16
                Rectangle { x: 0; y: 1; width: 18; height: 2; radius: 1; color: "#584126" }
                Rectangle { x: 0; y: 7; width: 18; height: 2; radius: 1; color: "#584126" }
                Rectangle { x: 0; y: 13; width: 18; height: 2; radius: 1; color: "#584126" }
            }
            Text {
                text: "INDEX"
                color: "#584126"
                font.family: atlasBodyFont.name
                font.pixelSize: 12
                font.bold: true
            }
        }
        onClicked: {
            root.indexVisible = !root.indexVisible
            root.nonCanonMode = false
            if (root.indexVisible) root.closePreview()
        }
    }

    Rectangle {
        id: indexPanel
        objectName: "eastBlueIndexPanel"
        visible: root.indexVisible
        anchors.top: indexButton.bottom
        anchors.right: parent.right
        anchors.rightMargin: root.phoneLayout ? 18 : root.shellChromeInset
        anchors.topMargin: 8
        width: Math.min(root.width - 36, 420, Math.max(290, root.width * 0.34))
        height: Math.min(root.height - indexButton.y - indexButton.height - 30, 610)
        color: "#efe1bd"
        border.color: "#8a6b41"
        border.width: 1
        z: 29

        Rectangle {
            objectName: "eastBlueIndexCaret"
            width: 12
            height: 12
            x: indexPanel.width - 24
            y: -6
            rotation: 45
            color: indexPanel.color
            border.color: indexPanel.border.color
            border.width: 1
            z: -1
        }

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 7
            Text { text: root.nonCanonMode ? "EAST BLUE / INDEX" : "THE EAST BLUE SAGA"; color: "#9b774a"; font.family: atlasBodyFont.name; font.pixelSize: 10; font.letterSpacing: 1.4 }
            Text { text: root.nonCanonMode ? "Non-canon material" : "Ports of the voyage"; color: "#523a25"; font.family: atlasDisplayFont.name; font.pixelSize: 23 }

            Flickable {
                id: indexFlick
                width: parent.width
                height: parent.height - (root.nonCanonMode ? 110 : 66)
                clip: true
                contentWidth: width
                contentHeight: indexContent.height
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    id: indexContent
                    width: indexFlick.width
                    spacing: 6

                    Repeater {
                        model: root.nonCanonMode ? 0 : (root.universeContentGeneration, AtlasData.canonMarkers).length
                        delegate: Button {
                            id: canonRowButton
                            required property int index
                            objectName: "eastBlueCanonRow-" + (root.universeContentGeneration, AtlasData.canonMarkers)[index].id
                            width: indexContent.width
                            height: 46
                            text: String(index + 1).padStart(2, "0") + "   " + (root.universeContentGeneration, EastBlue.arc)((root.universeContentGeneration, AtlasData.canonMarkers)[index].id).title
                            font.family: atlasBodyFont.name
                            font.pixelSize: 14
                            palette.buttonText: "#523a25"
                            activeFocusOnTab: true
                            focusPolicy: Qt.StrongFocus
                            hoverEnabled: true
                            background: Rectangle {
                                objectName: "eastBlueCanonRowBackground-" + (root.universeContentGeneration, AtlasData.canonMarkers)[canonRowButton.index].id
                                radius: 2
                                color: canonRowButton.pressed ? "#d8ba7c"
                                       : canonRowButton.activeFocus ? "#f6e5bb"
                                       : canonRowButton.hovered ? "#f0d9a4" : "transparent"
                                border.width: 1
                                border.color: canonRowButton.activeFocus ? "#5f4322" : "#b89a68"
                            }
                            contentItem: Text {
                                text: canonRowButton.text
                                font.family: atlasBodyFont.name
                                font.pixelSize: 14
                                color: canonRowButton.palette.buttonText
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            onClicked: {
                                root.indexVisible = false
                                root.focusMarker((root.universeContentGeneration, AtlasData.canonMarkers)[index].id)
                            }
                        }
                    }

                    Button {
                        id: nonCanonToggleButton
                        objectName: "eastBlueNonCanonToggle"
                        visible: !root.nonCanonMode
                        width: indexContent.width
                        height: 44
                        text: "NON-CANON MATERIAL"
                        font.family: atlasBodyFont.name
                        font.pixelSize: 11
                        font.bold: true
                        contentItem: Text {
                            text: nonCanonToggleButton.text
                            font.family: atlasBodyFont.name
                            font.pixelSize: 11
                            font.bold: true
                            color: nonCanonToggleButton.palette.buttonText
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        onClicked: root.nonCanonMode = true
                    }

                    Repeater {
                        id: nonCanonRepeater
                        model: root.nonCanonMode ? (root.universeContentGeneration, AtlasData.nonCanonEntries) : []
                        delegate: Row {
                            id: nonCanonRow
                            required property var modelData
                            width: indexContent.width
                            height: 98
                            spacing: 8

                            Item {
                                width: 64; height: 82
                                Image {
                                    id: nonCanonPoster
                                    anchors.fill: parent
                                    source: nonCanonRow.modelData.poster
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    visible: nonCanonPoster.status === Image.Error
                                    color: "#d8bd8a"
                                    border.color: "#80633e"
                                    Text { anchors.centerIn: parent; text: "POSTER\nUNAVAILABLE"; color: "#80633e"; font.family: atlasBodyFont.name; font.pixelSize: 8; horizontalAlignment: Text.AlignHCenter }
                                }
                            }
                            Column {
                                width: Math.max(110, parent.width - 160)
                                spacing: 2
                                Text { width: parent.width; text: nonCanonRow.modelData.title; color: "#523a25"; font.family: atlasBodyFont.name; font.pixelSize: 13; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                                Text { text: nonCanonRow.modelData.year + " · " + nonCanonRow.modelData.kindLabel; color: "#896c48"; font.family: atlasBodyFont.name; font.pixelSize: 10 }
                                Text { width: parent.width; text: nonCanonRow.modelData.placement; color: "#896c48"; font.family: atlasBodyFont.name; font.pixelSize: 10; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                            }
                            Button {
                                id: nonCanonActionButton
                                objectName: "eastBlueNonCanonAction-" + nonCanonRow.modelData.id
                                width: 76; height: 44
                                text: nonCanonRow.modelData.entry ? "OPEN" : "UNAVAILABLE"
                                enabled: !!nonCanonRow.modelData.entry
                                contentItem: Text {
                                    text: nonCanonActionButton.text
                                    font.family: atlasBodyFont.name
                                    font.pixelSize: 12
                                    color: nonCanonActionButton.enabled ? nonCanonActionButton.palette.buttonText : "#9c927e"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                Accessible.description: nonCanonRow.modelData.entry ? "Open media detail" : "No verified local media identity is available"
                                onClicked: if (nonCanonRow.modelData.entry) {
                                    root.indexVisible = false
                                    root.mediaRequested(nonCanonRow.modelData.entry)
                                }
                            }
                        }
                    }

                    Button {
                        id: backToIndexRowButton
                        objectName: "eastBlueBackToIndexRow"
                        visible: root.nonCanonMode
                        width: indexContent.width
                        height: 44
                        text: "BACK TO INDEX"
                        font.family: atlasBodyFont.name
                        contentItem: Text {
                            text: backToIndexRowButton.text
                            font.family: atlasBodyFont.name
                            font.pixelSize: 12
                            color: backToIndexRowButton.palette.buttonText
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        onClicked: root.nonCanonMode = false
                    }
                }
            }

            Button {
                id: backToIndexButton
                objectName: "eastBlueBackToIndex"
                visible: root.nonCanonMode
                width: parent.width
                height: 44
                z: 2
                text: "BACK TO INDEX"
                contentItem: Text {
                    text: backToIndexButton.text
                            font.family: atlasBodyFont.name
                            font.pixelSize: 11
                            font.bold: true
                    color: backToIndexButton.palette.buttonText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                onClicked: root.nonCanonMode = false
            }
        }

        Keys.onEscapePressed: root.requestEscape()
    }

    Rectangle {
        id: preview
        visible: root.previewVisible && root.selectedArc !== null
        objectName: "eastBlueArcPreview"
        x: root.previewX
        y: root.previewY
        width: Math.min(root.width - 36, 500, Math.max(360, root.width * 0.38))
        height: Math.min(230, Math.max(180, root.height - 48))
        color: "#efe1bd"
        border.color: "#8a6b41"
        border.width: 1
        z: 40

        onVisibleChanged: if (visible) root.schedulePreviewGeometry()

        HoverHandler {
            id: bannerHover
            enabled: root.previewVisible && !root.previewMarkerFocused
            onHoveredChanged: {
                root.previewPointerInside = hovered
                if (!hovered) root.closePreviewSoon()
            }
        }
        activeFocusOnTab: true

        Flickable {
            id: previewContent
            objectName: "eastBluePreviewContent"
            x: 12
            y: 12
            width: parent.width - 24
            height: parent.height - (root.phoneLayout ? 84 : 24)
            contentWidth: width
            contentHeight: previewBody.height
            clip: root.phoneLayout
            interactive: root.phoneLayout && contentHeight > height
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: previewBody
                width: previewContent.width
                height: Math.max(previewArt.height, previewCopy.height)
                spacing: 14
                Item {
                    id: previewArt
                    width: root.phoneLayout ? 74 : Math.min(135, preview.width * 0.34)
                    height: root.phoneLayout ? 112 : Math.max(130, preview.height - 24)
                    Image {
                        id: previewPoster
                        anchors.fill: parent
                        objectName: "eastBlueArcPreviewPoster"
                        property bool canonPosterFallback: false
                        property bool posterFallback: false
                        source: {
                            var marker = root.selectedArc ? (root.universeContentGeneration, AtlasData.canonMarker)(root.selectedArc.id) : null
                            return marker ? (posterFallback ? marker.badge
                                           : canonPosterFallback ? marker.poster
                                           : (root.universeContentGeneration, AtlasData.bannerSource)(marker.id)) : ""
                        }
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        smooth: true
                        mipmap: true
                        sourceSize: Qt.size(540, 816)
                        onStatusChanged: {
                            if (status === Image.Error && !posterFallback) {
                                if (!canonPosterFallback) canonPosterFallback = true
                                else posterFallback = true
                            }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: previewPoster.status === Image.Error
                        color: "#d8bd8a"
                        border.color: "#80633e"
                        Text { anchors.centerIn: parent; text: "POSTER\nUNAVAILABLE"; color: "#80633e"; font.family: atlasBodyFont.name; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
                    }
                }
                Column {
                    id: previewCopy
                    width: root.phoneLayout ? parent.width - previewArt.width - 14 : parent.width - 150
                    spacing: 6
                    Text { text: root.selectedArc ? String(root.selectedArc.order).padStart(2, "0") + " / EAST BLUE" : ""; color: "#9b774a"; font.family: atlasBodyFont.name; font.pixelSize: 10; font.letterSpacing: 1.2 }
                    Text { width: parent.width; text: root.selectedArc ? root.selectedArc.title : ""; color: "#523a25"; font.family: atlasDisplayFont.name; font.pixelSize: 24; wrapMode: Text.WordWrap }
                    Text { width: parent.width; text: root.selectedArc ? root.selectedArc.place : ""; color: "#896c48"; font.family: atlasBodyFont.name; font.pixelSize: 12; font.italic: true; wrapMode: Text.WordWrap }
                    Text { width: parent.width; text: root.selectedArc ? root.selectedArc.summary : ""; color: "#654b31"; font.family: atlasBodyFont.name; font.pixelSize: 12; wrapMode: Text.WordWrap; maximumLineCount: 4; elide: Text.ElideRight }
                }
            }
        }

        Row {
            id: previewActions
            x: root.phoneLayout ? 12 : previewContent.x + previewCopy.x
            y: root.phoneLayout ? preview.height - height - 12 : previewContent.y + previewCopy.height + 6
            width: root.phoneLayout ? preview.width - 24 : implicitWidth
            height: root.phoneLayout ? 48 : 44
            spacing: 8
            Button {
                id: openArcButton
                objectName: "eastBlueOpenArc"
                text: "OPEN ARC"
                width: root.phoneLayout ? (previewActions.width - 8) / 2 : 104
                height: previewActions.height
                // Clicking must not park the scope's focus flag on a control
                // that hides with the preview; keyboard users still reach it by Tab.
                focusPolicy: Qt.TabFocus
                contentItem: Text { text: openArcButton.text; font.family: atlasBodyFont.name; font.pixelSize: 11; font.bold: true; color: openArcButton.palette.buttonText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: root.openSelectedArc()
            }
            Button {
                id: closePreviewButton
                objectName: "eastBlueClosePreview"
                text: "CLOSE"
                width: root.phoneLayout ? (previewActions.width - 8) / 2 : 80
                height: previewActions.height
                focusPolicy: Qt.TabFocus
                contentItem: Text { text: closePreviewButton.text; font.family: atlasBodyFont.name; font.pixelSize: 11; color: closePreviewButton.palette.buttonText; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: root.closePreview()
            }
        }

        Keys.onEscapePressed: root.requestEscape()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                root.requestEscape()
                event.accepted = true
            }
        }
    }

    Keys.onEscapePressed: {
        root.requestEscape()
    }
}
