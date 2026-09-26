pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import "PorticoData.js" as Data

Item {
    id: root
    objectName: "account-stats-slice-content"
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    property int contentFocusIndex: -1
    readonly property var sessions: controller.allSessions()
    readonly property var rows: controller.tallySessions(sessions, controller.statsBy === "title" ? "id" : "pk")
    readonly property int actionableRowCount: Math.min(rows.length, controller.statsBy === "title" ? 15 : 30)
    readonly property var metrics: [
        {v:controller.durationText(controller.totalMins(sessions)),l:"Time in your apps"},
        {v:controller.distinct(sessions,"id"),l:"Titles opened"},
        {v:sessions.length,l:"Visits"},
        {v:Array.from(new Set(sessions.map(function(s){return new Date(s.at).toDateString()}))).length,l:"Active days"},
        {v:controller.distinct(sessions,"pk"),l:"Apps used"}
    ]
    function period() {
        if (!sessions.length) return "your first visit"
        return Qt.formatDate(new Date(sessions[sessions.length-1].at), "MMMM yyyy")
    }
    function clearContentFocus() { contentFocusIndex = -1 }
    function targetItem(index) {
        if (index === 0 || index === 1) return groupRepeater.itemAt(index)
        return rankingRepeater.itemAt(index - 1)
    }
    function enterContent() { setContentFocus(controller.statsBy === "app" ? 1 : 0) }
    function ensureTargetVisible(item) {
        if (!item) return
        var p = item.mapToItem(statsFlick.contentItem, 0, 0)
        var pad = 0.55 * u
        var top = p.y - pad
        var bottom = p.y + item.height + pad
        var maxY = Math.max(0, statsFlick.contentHeight - statsFlick.height)
        if (top < statsFlick.contentY) statsFlick.contentY = Math.max(0, top)
        else if (bottom > statsFlick.contentY + statsFlick.height)
            statsFlick.contentY = Math.min(maxY, bottom - statsFlick.height)
    }
    function setContentFocus(index) {
        contentFocusIndex = Math.max(0, Math.min(actionableRowCount + 1, index))
        controller.accountShellFocusIndex = -1
        Qt.callLater(function() { ensureTargetVisible(targetItem(contentFocusIndex)) })
    }
    function activateRow(row) {
        if (!row || !row.key) return
        if (controller.statsBy === "title") controller.openTitle(row.key)
        else { controller.accountApp = row.key; controller.accountTab = "apps" }
    }
    function activateFocused() {
        if (contentFocusIndex === 0) controller.statsBy = "title"
        else if (contentFocusIndex === 1) controller.statsBy = "app"
        else activateRow(rows[contentFocusIndex - 2])
    }
    function handleKey(key) {
        if (contentFocusIndex < 0) return false
        if (key === Qt.Key_Left) { if (contentFocusIndex <= 1) setContentFocus(0); return true }
        if (key === Qt.Key_Right) { if (contentFocusIndex <= 1) setContentFocus(1); return true }
        if (key === Qt.Key_Up) {
            if (contentFocusIndex <= 1) { clearContentFocus(); controller.accountShellFocusIndex = 3 }
            else if (contentFocusIndex === 2) setContentFocus(controller.statsBy === "app" ? 1 : 0)
            else setContentFocus(contentFocusIndex - 1)
            return true
        }
        if (key === Qt.Key_Down) {
            if (contentFocusIndex <= 1) { if (actionableRowCount > 0) setContentFocus(2) }
            else if (contentFocusIndex < actionableRowCount + 1) setContentFocus(contentFocusIndex + 1)
            return true
        }
        if (key === Qt.Key_Return || key === Qt.Key_Enter) { activateFocused(); return true }
        return false
    }

    Flickable {
        id: statsFlick
        objectName: "stats-flickable"
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.implicitHeight + 10*u
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: body
            x: m; y: u; width: parent.width-2*m; spacing: 1.9*u
            Rectangle {
                width: parent.width; height: 7.5*u; radius: u
                color: Qt.rgba(1,1,1,0.035); border.width: 1; border.color: Qt.rgba(1,1,1,0.10)
                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    Repeater {
                        model: root.metrics
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true; Layout.leftMargin: 1.4*u; Layout.alignment: Qt.AlignVCenter
                            spacing: 0.25*u
                            Text { text: String(modelData.v); color: controller.ink; font.family: controller.displayFont; font.pixelSize: 2.1*u }
                            Text { text: modelData.l; color: controller.slate; font.family: controller.uiFont; font.pixelSize: 0.84*u }
                        }
                    }
                }
            }
            RowLayout {
                width: parent.width
                Text { text: "Since " + root.period(); color: controller.ink; font.family: controller.displayFont; font.pixelSize: 1.6*u; Layout.fillWidth: true }
                Repeater {
                    id: groupRepeater
                    model: [{k:"title",t:"By title"},{k:"app",t:"By app"}]
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        objectName: "stats-filter-" + modelData.k
                        property bool focusedState: root.contentFocusIndex === index
                        width: 6.7*u; height: 2.5*u; radius: height/2
                        color: focusedState ? Qt.rgba(240/255,196/255,74/255,0.12) : (controller.statsBy===modelData.k ? Qt.rgba(240/255,196/255,74/255,0.15) : Qt.rgba(1,1,1,0.045))
                        border.width: focusedState ? Math.max(2,0.14*u) : 1
                        border.color: focusedState || controller.statsBy===modelData.k ? controller.gold : Qt.rgba(1,1,1,0.12)
                        Text { anchors.centerIn: parent; text: modelData.t; color: controller.statsBy===modelData.k || parent.focusedState ? controller.gold : controller.mist; font.family: controller.uiFont; font.pixelSize: 0.88*u }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: root.setContentFocus(index)
                            onClicked: { root.setContentFocus(index); controller.statsBy=modelData.k }
                        }
                    }
                }
            }
            Rectangle {
                width: parent.width; height: (Math.min(root.rows.length, controller.statsBy==="title" ? 15 : 30)+1)*3.7*u
                radius: u; clip: true; color: Qt.rgba(1,1,1,0.02)
                border.width: 1; border.color: Qt.rgba(1,1,1,0.1)
                Column {
                    width: parent.width
                    Repeater {
                        id: rankingRepeater
                        model: [{key:"",mins:0,n:0,apps:[],titles:[]}].concat(root.rows.slice(0,controller.statsBy==="title"?15:30))
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            objectName: index === 0 ? "stats-row-header" : "stats-row-" + index
                            property bool focusedState: index > 0 && root.contentFocusIndex === index + 1
                            property string rowKey: modelData.key
                            width: parent.width; height: 3.7*u
                            color: index===0 ? Qt.rgba(1,1,1,0.04) : (focusedState || rowMouse.containsMouse ? Qt.rgba(1,1,1,0.07) : "transparent")
                            border.width: focusedState ? Math.max(1,0.14*u) : 0
                            border.color: controller.gold
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(1,1,1,0.07) }
                            RowLayout {
                                anchors { fill: parent; leftMargin: 1.3*u; rightMargin: 1.3*u }
                                spacing: u
                                Text { Layout.preferredWidth: 2*u; text: index===0?"#":String(index); color: controller.slate; font.family: controller.uiFont; font.pixelSize: 0.9*u }
                                Text { Layout.fillWidth: true; text: index===0?(controller.statsBy==="title"?"Title":"App"):(controller.statsBy==="title"?(Data.T[modelData.key]?Data.T[modelData.key].t:modelData.key):controller.providerName(modelData.key)); color: index===0?controller.slate:controller.ink; font.family: controller.uiFont; font.pixelSize: 0.95*u; font.weight: index===0?Font.Normal:Font.DemiBold; elide: Text.ElideRight }
                                Text { Layout.preferredWidth: 8*u; text:index===0?"Time":controller.durationText(modelData.mins); color:controller.mist; font.family:controller.uiFont; font.pixelSize:0.92*u }
                                Text { Layout.preferredWidth: 7*u; text:index===0?(controller.statsBy==="title"?"Opened":"Visits"):String(modelData.n); color:controller.mist; font.family:controller.uiFont; font.pixelSize:0.92*u }
                                Text { Layout.preferredWidth: 16*u; text:index===0?(controller.statsBy==="title"?"On":"Titles opened"):(controller.statsBy==="title"?modelData.apps.map(function(k){return controller.providerName(k)}).join(", "):String(modelData.titles.length)); color:controller.mist; font.family:controller.uiFont; font.pixelSize:0.92*u; elide:Text.ElideRight }
                            }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: index > 0
                                onEntered: root.setContentFocus(index + 1)
                                onClicked: { root.setContentFocus(index + 1); root.activateRow(modelData) }
                            }
                        }
                    }
                }
            }
            Text { width:parent.width; text:"Time means how long a service's page stayed open in Feria, not how long something played."; color:controller.slate; font.family:controller.uiFont; font.pixelSize:0.83*u; wrapMode:Text.WordWrap }
        }
    }
}
