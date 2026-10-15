pragma ComponentBehavior: Bound
import QtQuick
import "UniverseExtApi.js" as UniverseApi
import "DCAUUniverseData.js" as DCAU

Item {
    id: root
    anchors.fill: parent
    objectName: "dcauUniversePage"

    property string extensionId: ""
    property string universeName: ""
    property bool reducedMotion: false
    property var payload: null
    property string currentHubId: ""
    property int progressRevision: typeof Progress !== "undefined" ? Progress.revision : 0
    readonly property var continueItems: {
        root.progressRevision
        return root.justiceContinue()
    }

    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()
    signal watchRequested(var payload)
    signal comicRequested(var entry)
    signal continueResumeRequested(var entry)
    signal continueDetailRequested(var entry)

    Theme { id: theme }

    onExtensionIdChanged: root.reload()
    function reload() {
        if (!root.extensionId) { root.payload = null; return }
        UniverseApi.load(root.extensionId, function(p) { root.payload = p })
    }
    function justiceContinue() {
        if (typeof Progress === "undefined" || !root.payload) return []
        var recent = Progress.recent("", 100), out = []
        for (var i=0; i<recent.length && out.length<12; ++i)
            if (DCAU.progressBelongsToJustice(recent[i])) out.push(recent[i])
        return out
    }
    function navigateBack() {
        if (root.currentHubId.length) root.currentHubId = ""
        else root.backRequested()
    }
    function openHub(id) { root.currentHubId = id }

    DCAULandingBackdrop { id: pageBackdrop; anchors.fill: parent }

    Item {
        id: landing
        anchors.fill: parent
        visible: root.currentHubId.length === 0
        property int selectedIndex: 0

        function centerPortal(i) {
            var item = portalRepeater.itemAt(i)
            if (!item) return
            var target = portalRow.x + item.x + item.width / 2 - portalFlick.width / 2
            var maxX = Math.max(0, portalFlick.contentWidth - portalFlick.width)
            portalFlick.contentX = Math.max(0, Math.min(target, maxX))
        }
        function selectPortal(i, focusItem) {
            landing.selectedIndex = (i + DCAU.hubs.length) % DCAU.hubs.length
            var item = portalRepeater.itemAt(landing.selectedIndex)
            if (item && focusItem) item.forceActiveFocus()
            Qt.callLater(function() { landing.centerPortal(landing.selectedIndex) })
        }
        function syncClosestPortal() {
            var center = portalFlick.contentX + portalFlick.width / 2
            var best = landing.selectedIndex
            var distance = Number.MAX_VALUE
            for (var i = 0; i < DCAU.hubs.length; ++i) {
                var item = portalRepeater.itemAt(i)
                if (!item) continue
                var d = Math.abs(portalRow.x + item.x + item.width / 2 - center)
                if (d < distance) { distance = d; best = i }
            }
            landing.selectedIndex = best
        }

        Column {
            id: continueCol
            x: theme.margin
            y: 30
            width: parent.width - theme.margin * 2
            spacing: 14
            visible: root.continueItems.length > 0

            WidgetHeader { width: parent.width; title: "Continue"; moreLabel: "" }
            Flickable {
                id: continueFlick
                width: parent.width
                height: 148
                contentWidth: continueRow.width + 26
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: continueRow
                    spacing: 20
                    Repeater {
                        model: root.continueItems
                        delegate: ContinueTile {
                            required property var modelData
                            variant: "home"
                            entry: modelData
                            backdrop: pageBackdrop
                            track: continueFlick.contentX
                            onResumeRequested: root.continueResumeRequested(modelData)
                            onDetailRequested: root.continueDetailRequested(modelData)
                            onRemoveRequested: if (typeof Progress !== "undefined")
                                                   Progress.forget(modelData.kind, modelData.id)
                        }
                    }
                }
            }
        }

        Item {
            id: portalStage
            x: 0
            width: parent.width
            y: continueCol.visible ? continueCol.y + continueCol.implicitHeight + 18 : 30
            height: parent.height - y - 24

            Flickable {
                id: portalFlick
                anchors.fill: parent
                clip: true
                contentWidth: Math.max(width, portalRow.x + portalRow.width + portalStage.width * 0.07)
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                onMovementEnded: landing.syncClosestPortal()

                Row {
                    id: portalRow
                    x: portalStage.width * 0.07
                    y: Math.max(4, (portalFlick.height - height) / 2)
                    spacing: 24
                    Repeater {
                        id: portalRepeater
                        model: DCAU.hubs
                        delegate: DCAUWorldPortal {
                            required property var modelData
                            required property int index
                            width: portalStage.width <= 900
                                   ? portalStage.width * 0.48
                                   : Math.min(300, portalStage.width * 0.24)
                            ordinal: index < 9 ? "0" + String(index + 1) : String(index + 1)
                            title: modelData.title
                            selected: landing.selectedIndex === index
                            imageSources: [
                                Qt.resolvedUrl("../assets/universes/dcau/portals/" + modelData.portal)
                            ]
                            onSelectionRequested: landing.selectPortal(index, false)
                            onActivated: root.openHub(modelData.id)
                            onPreviousRequested: landing.selectPortal(index - 1, true)
                            onNextRequested: landing.selectPortal(index + 1, true)
                        }
                    }
                }
            }

            DCAUPortalArrow {
                z: 10
                visible: portalStage.width > 900
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                glyph: "\u2039"
                onTriggered: landing.selectPortal(landing.selectedIndex - 1, true)
            }
            DCAUPortalArrow {
                z: 10
                visible: portalStage.width > 900
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                glyph: "\u203a"
                onTriggered: landing.selectPortal(landing.selectedIndex + 1, true)
            }

        }
    }

    DCAUWorldPage {
        anchors.fill: parent
        visible: root.currentHubId.length > 0
        hub: DCAU.hub(root.currentHubId)
        payload: root.payload
        reducedMotion: root.reducedMotion
        onWatchRequested: function(entry) { root.watchRequested(entry) }
        onComicRequested: function(entry) { root.comicRequested(entry) }
    }

    Shortcut {
        sequence: "Esc"
        enabled: root.currentHubId.length > 0
        onActivated: root.currentHubId = ""
    }

    ChromeScrim { z: 16 }
    BackAction { x: theme.margin; y: 28; z: 20; onTriggered: root.navigateBack() }

    Row {
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: theme.margin
        y: 34
        spacing: 20
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"; sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit; opacity: minMa.containsMouse ? 1 : .72 }
            MouseArea { id: minMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: (typeof WindowMode !== "undefined" && WindowMode.shellWindowed) ? "../assets/icons/fullscreen.svg" : "../assets/icons/fullscreen-exit.svg"; sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit; opacity: fsMa.containsMouse ? 1 : .72 }
            MouseArea { id: fsMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.fullscreenRequested() }
        }
        Item {
            width: 22; height: 22
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"; sourceSize.width: 22; sourceSize.height: 22; fillMode: Image.PreserveAspectFit; opacity: closeMa.containsMouse ? 1 : .72 }
            MouseArea { id: closeMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
        }
    }
}
