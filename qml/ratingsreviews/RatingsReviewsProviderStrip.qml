import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "ratingsReviewsProviderStrip"
    width: parent ? parent.width : 1000
    height: stripRow.implicitHeight + 20

    property var controller: null
    property var presentation: ({})
    property int routeGeneration: 0
    property int profileGeneration: 0
    property var savedOrder: []
    property var visibleOrder: []
    property var rows: []
    property var orderedRows: []
    property string focusedProviderId: ""
    property string lastMoveProviderId: ""
    property string lastMoveDirection: ""
    property string lastAnnouncement: ""

    function refresh() {
        savedOrder = (presentation && presentation.savedOrder) ? presentation.savedOrder.slice() : []
        visibleOrder = (presentation && presentation.visibleOrder) ? presentation.visibleOrder.slice() : []
        rows = (presentation && presentation.aggregates) ? presentation.aggregates.slice() : []
        rebuildRows()
    }

    function rebuildRows() {
        var byId = ({})
        for (var i = 0; i < rows.length; ++i)
            byId[String(rows[i].providerId || "")] = rows[i]
        var next = []
        for (var j = 0; j < visibleOrder.length; ++j) {
            var row = byId[String(visibleOrder[j])]
            if (row)
                next.push(row)
        }
        orderedRows = next
    }

    function requestMove(providerId, direction) {
        if (!controller || !controller.moveProvider)
            return false
        var result = controller.moveProvider(providerId, direction, visibleOrder,
                                             routeGeneration, profileGeneration)
        if (!result || result.changed !== true)
            return false
        savedOrder = result.savedOrder ? result.savedOrder.slice() : savedOrder
        visibleOrder = result.visibleOrder ? result.visibleOrder.slice() : visibleOrder
        focusedProviderId = providerId
        lastMoveProviderId = providerId
        lastMoveDirection = String(result.direction || (direction < 0 ? "left" : "right"))
        lastAnnouncement = String(result.announcement || "")
        rebuildRows()
        return true
    }

    onPresentationChanged: refresh()
    Component.onCompleted: refresh()

    Item {
        id: orderState
        objectName: "rrProviderOrderState"
        visible: false
        width: 0
        height: 0
        property string savedOrderCsv: root.savedOrder.join(",")
        property string visibleOrderCsv: root.visibleOrder.join(",")
        property string focusedProviderId: root.focusedProviderId
        property string lastMoveProviderId: root.lastMoveProviderId
        property string lastMoveDirection: root.lastMoveDirection
        property string lastAnnouncement: root.lastAnnouncement
    }
    Flickable {
        anchors.fill: parent
        contentWidth: stripRow.implicitWidth
        contentHeight: stripRow.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: stripRow
            spacing: 12

            Repeater {
                model: root.orderedRows
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    required property int index
                    objectName: "rrProvider_" + String(modelData.providerId || "")
                    width: 150
                    height: 132
                    radius: 14
                    color: "#10151c"
                    border.width: activeFocus ? 2 : 1
                    border.color: activeFocus ? "#d8b56c" : "#2d333d"
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: String(modelData.providerName || modelData.providerId || "Provider")
                        + " " + (modelData.state === "error" ? "Unavailable"
                               : String(modelData.scoreDisplay || ""))

                    property real dragStartX: 0
                    property real dragLastX: 0
                    Component.onCompleted: {
                        if (String(modelData.providerId || "") === root.focusedProviderId)
                            Qt.callLater(function() { card.forceActiveFocus(Qt.OtherFocusReason) })
                    }
                    onActiveFocusChanged: if (activeFocus)
                        root.focusedProviderId = String(modelData.providerId || "")

                    Column {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 7
                        Text {
                            text: String(card.modelData.providerName || card.modelData.providerId || "")
                            color: "#9da5af"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            width: parent.width
                        }
                        Text {
                            objectName: "rrProviderScore_" + String(card.modelData.providerId || "")
                            text: card.modelData.state === "error"
                                  ? "Unavailable" : String(card.modelData.scoreDisplay || "")
                            color: card.modelData.state === "error" ? "#d78373" : "#f5f3ee"
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                        }
                        Text {
                            objectName: "rrProviderState_" + String(card.modelData.providerId || "")
                            text: String(card.modelData.state || "")
                            color: "#69727e"
                            font.pixelSize: 9
                        }
                    }

                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Left) {
                            if (root.requestMove(String(card.modelData.providerId || ""), -1))
                                event.accepted = true
                        } else if (event.key === Qt.Key_Right) {
                            if (root.requestMove(String(card.modelData.providerId || ""), 1))
                                event.accepted = true
                        }
                    }

                    DragHandler {
                        id: drag
                        target: null
                        acceptedButtons: Qt.LeftButton
                        grabPermissions: PointerHandler.CanTakeOverFromAnything
                        onTranslationChanged: {
                            if (active)
                                card.dragLastX = translation.x
                        }
                        onActiveChanged: {
                            if (active) {
                                card.dragStartX = translation.x
                                card.dragLastX = translation.x
                                card.forceActiveFocus(Qt.MouseFocusReason)
                            } else {
                                var delta = card.dragLastX - card.dragStartX
                                if (Math.abs(delta) > card.width * 0.35)
                                    root.requestMove(String(card.modelData.providerId || ""), delta < 0 ? -1 : 1)
                                card.dragStartX = 0
                                card.dragLastX = 0
                            }
                        }
                    }
                }
            }
        }
    }
}
