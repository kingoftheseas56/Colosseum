import QtQuick
import QtQuick.Layouts
import "PorticoData.js" as Data

Item {
    id: root
    objectName: "account-highlights-content"
    required property var controller
    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    readonly property var sessions: controller.monthSessions(controller.accountMonth)
    readonly property var titles: controller.tallySessions(sessions, "id")
    readonly property var apps: controller.tallySessions(sessions, "pk")
    readonly property string monthName: Qt.formatDate(new Date(controller.accountMonth + "-01T12:00:00"), "MMMM")
    readonly property string yearName: Qt.formatDate(new Date(controller.accountMonth + "-01T12:00:00"), "yyyy")
    property string focusKey: ""
    readonly property var cards: {
        var out = []
        if (titles.length)
            out.push({key:titles[0].key, label:"Most time", value:controller.durationText(titles[0].mins), app:false})
        var often = titles.slice().sort(function(a,b) { return b.n-a.n || b.mins-a.mins }).find(function(x) {
            return !titles.length || x.key !== titles[0].key
        })
        if (often)
            out.push({key:often.key, label:"Opened most", value:often.n + " times", app:false})
        if (apps.length)
            out.push({key:apps[0].key, label:"Top app", value:controller.durationText(apps[0].mins), app:true})
        var fresh = titles.slice().reverse().find(function(x) {
            return !out.some(function(c) { return !c.app && c.key === x.key })
        })
        if (fresh)
            out.push({key:fresh.key, label:"New this month", value:"First opened here", app:false})
        return out
    }

    function shiftedMonth(delta) {
        var d = new Date(controller.accountMonth + "-01T12:00:00")
        d.setMonth(d.getMonth() + delta)
        return Qt.formatDate(d, "yyyy-MM")
    }

    function canChangeMonth(delta) {
        var all = controller.allSessions()
        var current = Qt.formatDate(new Date(), "yyyy-MM")
        var earliest = all.length
                ? Qt.formatDate(new Date(all[all.length - 1].at), "yyyy-MM")
                : current
        var key = shiftedMonth(delta)
        return key >= earliest && key <= current
    }

    function actionKeys() {
        var out = []
        if (canChangeMonth(-1))
            out.push("month-earlier")
        if (canChangeMonth(1))
            out.push("month-later")
        for (var i = 0; i < cards.length; ++i)
            out.push("card-" + i)
        return out
    }

    function itemForKey(key) {
        if (key === "month-earlier")
            return monthControls.itemAt(0)
        if (key === "month-later")
            return monthControls.itemAt(1)
        if (key.indexOf("card-") === 0) {
            var i = Number(key.slice(5))
            return highlightCards.itemAt(i)
        }
        return null
    }

    function revealItem(item) {
        if (!item)
            return
        var p = item.mapToItem(scroll.contentItem, 0, 0)
        var pad = 0.75 * u
        var top = p.y - pad
        var bottom = p.y + item.height + pad
        if (top < scroll.contentY)
            scroll.contentY = Math.max(0, top)
        else if (bottom > scroll.contentY + scroll.height)
            scroll.contentY = Math.min(Math.max(0, scroll.contentHeight - scroll.height),
                                       bottom - scroll.height)
    }

    function setFocusedKey(key) {
        var keys = actionKeys()
        if (keys.indexOf(key) < 0)
            return false
        focusKey = key
        controller.accountContentFocusActive = true
        Qt.callLater(revealFocused)
        return true
    }

    function revealFocused() {
        revealItem(itemForKey(focusKey))
    }

    function leaveToShell() {
        focusKey = ""
        controller.accountContentFocusActive = false
        controller.accountShellFocusIndex = controller.accountFocusForTab("highlights")
        return true
    }

    function normalizeFocus() {
        if (!controller.accountContentFocusActive)
            return
        var keys = actionKeys()
        if (!keys.length) {
            leaveToShell()
            return
        }
        if (keys.indexOf(focusKey) < 0)
            focusKey = keys[0]
        Qt.callLater(revealFocused)
    }

    function enterFromShell() {
        var keys = actionKeys()
        if (!keys.length)
            return false
        return setFocusedKey(keys[0])
    }

    function moveFocus(delta) {
        var keys = actionKeys()
        if (!keys.length)
            return leaveToShell()
        var index = keys.indexOf(focusKey)
        if (index < 0)
            index = 0
        var next = Math.max(0, Math.min(keys.length - 1, index + delta))
        return setFocusedKey(keys[next])
    }

    function activateFocused() {
        if (focusKey === "month-earlier" && canChangeMonth(-1)) {
            controller.changeAccountMonth(-1)
            Qt.callLater(normalizeFocus)
            return true
        }
        if (focusKey === "month-later" && canChangeMonth(1)) {
            controller.changeAccountMonth(1)
            Qt.callLater(normalizeFocus)
            return true
        }
        if (focusKey.indexOf("card-") === 0) {
            var i = Number(focusKey.slice(5))
            var card = cards[i]
            if (!card)
                return false
            controller.accountContentFocusActive = false
            if (card.app) {
                controller.accountApp = card.key
                controller.accountTab = "apps"
            } else {
                controller.openTitle(card.key)
            }
            return true
        }
        return false
    }

    function handleKey(key) {
        if (!controller.accountContentFocusActive)
            return false
        var keys = actionKeys()
        if (!keys.length) {
            leaveToShell()
            return false
        }
        var index = keys.indexOf(focusKey)
        if (index < 0) {
            setFocusedKey(keys[0])
            index = 0
        }
        if (key === Qt.Key_Up) {
            if (index === 0)
                return leaveToShell()
            return moveFocus(-1)
        }
        if (key === Qt.Key_Left)
            return moveFocus(-1)
        if (key === Qt.Key_Right || key === Qt.Key_Down)
            return moveFocus(1)
        if (key === Qt.Key_Return || key === Qt.Key_Enter)
            return activateFocused()
        return false
    }

    onVisibleChanged: {
        if (!visible)
            focusKey = ""
        else if (controller.accountContentFocusActive)
            Qt.callLater(normalizeFocus)
    }

    Flickable {
        id: scroll
        objectName: "account-highlights-scroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, main.implicitHeight + 9 * u)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: main
            x: m
            y: 1.4 * u
            width: parent.width - 2 * m
            spacing: 2.8 * u

            Column {
                width: main.width * 0.34
                spacing: 0

                Row {
                    spacing: 0.5 * u
                    Repeater {
                        id: monthControls
                        model: [{t:"‹  Earlier", d:-1, key:"month-earlier"},
                                {t:"Later  ›", d:1, key:"month-later"}]
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            property bool actionEnabled: root.canChangeMonth(modelData.d)
                            objectName: modelData.key === "month-earlier"
                                        ? "highlights-month-earlier" : "highlights-month-later"
                            width: 6.6 * u
                            height: 2.5 * u
                            radius: 0.7 * u
                            opacity: actionEnabled ? 1.0 : 0.42
                            color: Qt.rgba(1,1,1,0.05)
                            border.width: 1
                            border.color: Qt.rgba(1,1,1,0.13)

                            Text {
                                anchors.centerIn: parent
                                text: modelData.t
                                color: actionEnabled ? controller.mist : controller.slate
                                font.family: controller.uiFont
                                font.pixelSize: 0.88 * u
                            }
                            Rectangle {
                                objectName: parent.objectName + "-focus"
                                anchors.fill: parent
                                anchors.margins: -0.1875 * u
                                radius: parent.radius + 0.1875 * u
                                color: "transparent"
                                border.width: 0.1875 * u
                                border.color: controller.gold
                                visible: controller.accountContentFocusActive
                                         && root.focusKey === modelData.key
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: parent.actionEnabled
                                hoverEnabled: true
                                onEntered: root.setFocusedKey(modelData.key)
                                onClicked: {
                                    root.setFocusedKey(modelData.key)
                                    root.activateFocused()
                                }
                            }
                        }
                    }
                }

                Text {
                    topPadding: 2 * u
                    text: root.monthName
                    color: controller.ink
                    font.family: controller.displayFont
                    font.pixelSize: 5.6 * u
                    font.weight: Font.Medium
                }
                Text {
                    text: root.yearName
                    color: controller.slate
                    font.family: controller.displayFont
                    font.pixelSize: 5.6 * u
                }
                Grid {
                    topPadding: 2.6 * u
                    width: parent.width
                    columns: 2
                    rowSpacing: 1.7 * u
                    columnSpacing: 1.5 * u
                    Repeater {
                        model: [
                            {v:controller.durationText(controller.totalMins(root.sessions)), l:"Time in your apps"},
                            {v:controller.distinct(root.sessions,"id"), l:"Titles opened"},
                            {v:Array.from(new Set(root.sessions.map(function(s) {
                                return new Date(s.at).toDateString()
                            }))).length, l:"Active days"},
                            {v:controller.distinct(root.sessions,"pk"), l:"Apps used"}
                        ]
                        delegate: Column {
                            required property var modelData
                            width: (root.width * 0.34 - 1.5 * u) / 2
                            Text {
                                text: String(modelData.v)
                                color: controller.ink
                                font.family: controller.displayFont
                                font.pixelSize: 2.3 * u
                            }
                            Text {
                                text: modelData.l
                                color: controller.slate
                                font.family: controller.uiFont
                                font.pixelSize: 0.88 * u
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: 1
                height: Math.max(44 * u, main.height)
                color: Qt.rgba(1,1,1,0.13)
            }

            Column {
                width: main.width - main.width * 0.34 - 2.8 * u - 1 - 2.8 * u
                spacing: 1.3 * u
                Text {
                    text: root.monthName + " highlights"
                    color: controller.ink
                    font.family: controller.displayFont
                    font.pixelSize: 1.6 * u
                }
                Row {
                    visible: root.cards.length > 0
                    spacing: 1.4 * u
                    width: parent.width
                    Repeater {
                        id: highlightCards
                        model: root.cards
                        delegate: Item {
                            required property int index
                            required property var modelData
                            objectName: "highlights-card-" + index
                            width: (parent.width - 4.2 * u) / 4
                            height: 25 * u

                            Rectangle {
                                id: art
                                width: parent.width
                                height: width * 1.5
                                radius: 0.75 * u
                                clip: true
                                color: Qt.rgba(1,1,1,0.055)
                                border.width: 1
                                border.color: Qt.rgba(1,1,1,0.1)
                                Image {
                                    id: cover
                                    anchors.fill: parent
                                    source: modelData.app ? "" : controller.artUrl(Data.T[modelData.key], false)
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    visible: status === Image.Ready
                                }
                                PorticoCombinedGlyph {
                                    anchors.centerIn: parent
                                    width: 3 * u
                                    height: 3 * u
                                    visible: modelData.app
                                    glyphKey: modelData.app ? modelData.key : ""
                                    tone: controller.ink
                                }
                                Text {
                                    anchors { fill: parent; margins: 1.2 * u }
                                    visible: !modelData.app && cover.status !== Image.Ready
                                    text: Data.T[modelData.key] ? Data.T[modelData.key].t : ""
                                    color: controller.mist
                                    font.family: controller.displayFont
                                    font.pixelSize: 1.4 * u
                                    wrapMode: Text.WordWrap
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            Text {
                                y: art.height + 0.9 * u
                                text: modelData.label
                                color: controller.gold
                                font.family: controller.uiFont
                                font.pixelSize: 0.85 * u
                            }
                            Text {
                                y: art.height + 2.3 * u
                                width: parent.width
                                text: modelData.app ? controller.providerName(modelData.key)
                                                    : (Data.T[modelData.key] ? Data.T[modelData.key].t : "")
                                color: controller.ink
                                font.family: controller.uiFont
                                font.weight: Font.DemiBold
                                font.pixelSize: u
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Text {
                                y: art.height + 5.2 * u
                                text: modelData.value
                                color: controller.mist
                                font.family: controller.uiFont
                                font.pixelSize: 0.88 * u
                            }
                            Rectangle {
                                objectName: "highlights-card-" + index + "-focus"
                                anchors.fill: parent
                                anchors.margins: -0.1875 * u
                                radius: 0.9 * u
                                color: "transparent"
                                border.width: 0.1875 * u
                                border.color: controller.gold
                                visible: controller.accountContentFocusActive
                                         && root.focusKey === "card-" + index
                                z: 4
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onEntered: root.setFocusedKey("card-" + index)
                                onClicked: {
                                    root.setFocusedKey("card-" + index)
                                    root.activateFocused()
                                }
                            }
                        }
                    }
                }
                Text {
                    visible: root.cards.length === 0
                    text: "Nothing recorded in " + root.monthName
                    color: controller.mist
                    font.family: controller.uiFont
                    font.pixelSize: 1.15 * u
                }
            }
        }
    }
}
