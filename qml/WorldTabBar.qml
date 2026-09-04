// WorldTabBar — TheatreTabBar's glass pill bar, generalized to any tab set (parameterized
// tabModel). Used by the Tankoban world (Manga|Comics); Theatre keeps TheatreTabBar for now
// and can migrate to this later. Same glass look/feel: gold active pill, ghost inactive, hover tint.
import QtQuick
import QtQuick.Window

pragma ComponentBehavior: Bound

Item {
    id: tabs

    required property Item backdrop
    property var tabModel: []            // [{ key, label }, …]
    property string currentTab: ""
    // Stem for each pill's objectName: "<tabPrefix>_<key>". Defaults to the historical
    // "worldTab" so existing consumers (Tankoban) are unchanged; a world with a tab key
    // another world also uses (e.g. both Tankoban and Biblio have a "library" tab) MUST
    // set its own prefix so resolveTarget's DFS objectName lookup can't hit the wrong
    // world's hidden, pre-warmed pill first (colosseum pre-builds other worlds on a
    // warming timer, and findItem walks them all regardless of visibility).
    property string tabPrefix: "worldTab"
    signal tabRequested(string tab)

    property int keyboardIndex: 0
    readonly property bool televisionMode: {
        const w = tabs.Window.window
        return !!(w && w["televisionMode"] === true)
    }
    focusPolicy: Qt.TabFocus
    readonly property bool compactLayout: width < 600
    readonly property int pillSpacing: compactLayout ? 3 : 6

    function syncKeyboardIndex() {
        for (var i = 0; i < tabs.tabModel.length; ++i) {
            if (tabs.tabModel[i].key === tabs.currentTab) {
                tabs.keyboardIndex = i
                return
            }
        }
        tabs.keyboardIndex = 0
    }

    function requestIndex(index, reason) {
        if (tabs.tabModel.length <= 0) return
        tabs.keyboardIndex = Math.max(0, Math.min(tabs.tabModel.length - 1, index))
        if (reason !== undefined) tabs.forceActiveFocus(reason)
        tabs.tabRequested(tabs.tabModel[tabs.keyboardIndex].key)
    }

    onCurrentTabChanged: tabs.syncKeyboardIndex()
    Component.onCompleted: tabs.syncKeyboardIndex()

    Keys.onPressed: (event) => {
        var next = tabs.keyboardIndex
        if (event.key === Qt.Key_Left) next--
        else if (event.key === Qt.Key_Right) next++
        else if (event.key === Qt.Key_Home) next = 0
        else if (event.key === Qt.Key_End) next = tabs.tabModel.length - 1
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Select || event.key === Qt.Key_Space) {
            tabs.requestIndex(tabs.keyboardIndex, Qt.ShortcutFocusReason)
            event.accepted = true
            return
        } else {
            return
        }
        if (next >= 0 && next < tabs.tabModel.length && next !== tabs.keyboardIndex) {
            tabs.requestIndex(next, next < tabs.keyboardIndex ? Qt.BacktabFocusReason : Qt.TabFocusReason)
            event.accepted = true
        }
    }

    width: parent ? parent.width : 900
    height: tabs.televisionMode ? 68 : (tabs.compactLayout ? 54 : 58)

    Theme { id: theme }

    Glass {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(parent.width, (tabs.televisionMode ? 176 : (tabs.compactLayout ? 120 : 160)) * Math.max(1, tabs.tabModel.length))
        height: tabs.televisionMode ? 64 : (tabs.compactLayout ? 50 : 54)
        backdrop: tabs.backdrop
        radius: 18
        tint: 0.08
        scrim: 0.18

        Row {
            anchors.fill: parent
            anchors.margins: tabs.compactLayout ? 4 : 6
            spacing: tabs.pillSpacing

            Repeater {
                model: tabs.tabModel
                delegate: Rectangle {
                    id: pill
                    required property var modelData
                    required property int index
                    readonly property bool keyboardFocused: tabs.activeFocus && index === tabs.keyboardIndex
                    // Per-tab stable identity for Lanista/harness addressing (additive — a name
                    // changes no behavior). The stem is the owning world's tabPrefix so two worlds
                    // that share a key (e.g. "library") can't collide under resolveTarget's DFS
                    // lookup — a hidden pre-warmed world's pill would otherwise shadow the active
                    // one (observed: Tankoban's worldTab_library ate Biblio's click 2026-08-06).
                    objectName: tabs.tabPrefix + "_" + pill.modelData.key
                    readonly property bool activeState: pill.modelData.key === tabs.currentTab

                    width: (parent.width - tabs.pillSpacing * (tabs.tabModel.length - 1)) / Math.max(1, tabs.tabModel.length)
                    height: parent.height
                    radius: 14
                    color: pill.modelData.key === tabs.currentTab ? theme.gold : ((ma.containsMouse || pill.keyboardFocused) ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                    border.width: pill.modelData.key === tabs.currentTab ? 0
                                  : (pill.keyboardFocused ? (tabs.televisionMode ? 4 : 2) : 1)
                    border.color: pill.keyboardFocused ? theme.gold : Qt.rgba(1, 1, 1, 0.10)

                    Text {
                        anchors.centerIn: parent
                        text: pill.modelData.label
                        color: pill.modelData.key === tabs.currentTab ? "#17120a" : theme.ink
                        font.family: theme.ui
                        font.pixelSize: tabs.televisionMode ? 16 : (tabs.compactLayout ? 12 : 14)
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        id: ma
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: tabs.requestIndex(pill.index, Qt.MouseFocusReason)
                    }

                    Behavior on color { ColorAnimation { duration: 140 } }
                }
            }
        }
    }
}
