// WorldTabBar — TheatreTabBar's glass pill bar, generalized to any tab set (parameterized
// tabModel). Used by the Tankoban world (Manga|Comics); Theatre keeps TheatreTabBar for now
// and can migrate to this later. Same glass look/feel: gold active pill, ghost inactive, hover tint.
import QtQuick
import QtQuick.Effects
import QtQuick.Window

pragma ComponentBehavior: Bound

Item {
    id: tabs

    required property Item backdrop
    // The world's scroll offset. The glass re-samples the wallpaper behind the bar as the page
    // scrolls; without it the sample stayed at the bar's first position (Glass "track").
    property real track: 0
    // Docked only: the scrolling content under the bar and its scroll offset (Glass contentBackdrop).
    property Item contentBackdrop: null
    property real contentTrack: 0
    // The docked copy WorldPage pins under the TopBar is the mock's smaller bar.
    property bool compact: false
    property bool shortWindow: false
    property bool theatreStyle: false
    property bool phoneLayout: {
        const w = tabs.Window.window
        return !!(w && (Qt.platform.os === "android" || w["androidHost"] === true)
                  && Math.min(w.width, w.height) < 600)
    }
    property var tabModel: []            // [{ key, label }, …]
    property string currentTab: ""
    property bool focusOnPointer: true
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
    readonly property bool scrollingTabs: tabs.phoneLayout || tabs.compactLayout
    readonly property int pillSpacing: tabs.scrollingTabs ? 4 : 6

    function revealCurrentTab() {
        const pill = tabRepeater.itemAt(tabs.keyboardIndex)
        if (!pill) return
        const end = pill.x + pill.width
        const limit = Math.max(0, tabViewport.contentWidth - tabViewport.width)
        if (pill.x < tabViewport.contentX)
            tabViewport.contentX = Math.max(0, pill.x)
        else if (end > tabViewport.contentX + tabViewport.width)
            tabViewport.contentX = Math.min(limit, end - tabViewport.width)
        else
            tabViewport.contentX = Math.min(limit, tabViewport.contentX)
    }

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
        if (reason !== undefined && (reason !== Qt.MouseFocusReason || tabs.focusOnPointer)) tabs.forceActiveFocus(reason)
        tabs.tabRequested(tabs.tabModel[tabs.keyboardIndex].key)
    }

    onCurrentTabChanged: { tabs.syncKeyboardIndex(); Qt.callLater(tabs.revealCurrentTab) }
    onTabModelChanged: { tabs.syncKeyboardIndex(); Qt.callLater(tabs.revealCurrentTab) }
    onWidthChanged: Qt.callLater(tabs.revealCurrentTab)
    Component.onCompleted: { tabs.syncKeyboardIndex(); Qt.callLater(tabs.revealCurrentTab) }

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
            event.accepted = false
            return
        }
        if (next >= 0 && next < tabs.tabModel.length && next !== tabs.keyboardIndex) {
            tabs.requestIndex(next, next < tabs.keyboardIndex ? Qt.BacktabFocusReason : Qt.TabFocusReason)
            event.accepted = true
        } else event.accepted = false
    }

    width: parent ? parent.width : 900
    height: tabs.phoneLayout ? (tabs.shortWindow ? 48 : 56) : (tabs.theatreStyle && tabs.televisionMode ? 68 : (tabs.compact ? 50 : 58))

    Theme { id: theme }

    // The docked bar lifts off the content like the mock's dock (0 14px 30px rgba(0,0,0,.35)).
    RectangularShadow {
        visible: tabs.compact
        anchors.fill: bar
        radius: bar.radius
        offset.y: 14
        blur: 30
        color: Qt.rgba(0, 0, 0, 0.35)
    }

    Glass {
        id: bar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(parent.width, tabs.theatreStyle ? (tabs.televisionMode ? 880 : 760) : (tabs.compact ? 130 : 160) * Math.max(1, tabs.tabModel.length))
        height: tabs.phoneLayout ? (tabs.shortWindow ? 48 : 54) : (tabs.theatreStyle && tabs.televisionMode ? 64 : (tabs.compact ? 46 : 54))
        backdrop: tabs.backdrop
        track: tabs.track
        radius: tabs.compact ? 15 : 18
        tint: 0.08
        scrim: 0.18                       // the mock's .tabs film, docked or in flow
        // Docked, the bar floats over the scrolling page: frost the posters passing under it
        // too, like the mock's backdrop-filter (the wallpaper-only blur let them show sharp).
        contentBackdrop: tabs.contentBackdrop
        contentTrack: tabs.contentTrack

        Flickable {
            id: tabViewport
            objectName: tabs.tabPrefix + "Viewport"
            anchors.fill: parent
            anchors.margins: tabs.phoneLayout ? (tabs.shortWindow ? 0 : 3) : (tabs.compactLayout ? 4 : 6)
            contentWidth: pillRow.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width
            clip: true

            Row {
                id: pillRow
                width: tabs.scrollingTabs ? implicitWidth : tabViewport.width
                height: tabViewport.height
                spacing: tabs.pillSpacing

            Repeater {
                id: tabRepeater
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

                    width: tabs.scrollingTabs ? Math.max(76, pillLabel.implicitWidth + 24)
                           : Math.max(0, (pillRow.width - tabs.pillSpacing * (tabs.tabModel.length - 1)) / Math.max(1, tabs.tabModel.length))
                    height: pillRow.height
                    Accessible.role: Accessible.Button
                    Accessible.name: pill.modelData.label
                    Accessible.onPressAction: tabs.requestIndex(pill.index, Qt.OtherFocusReason)
                    radius: tabs.compact ? 11 : 14
                    color: pill.modelData.key === tabs.currentTab ? theme.gold
                         : (ma.containsMouse || ma.pressed || pill.keyboardFocused) ? Qt.rgba(1, 1, 1, 0.12)
                         : tabs.compact ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                    border.width: pill.modelData.key === tabs.currentTab ? 0 : 1
                    border.color: Qt.rgba(1, 1, 1, 0.10)

                    FocusRing { anchors.fill: parent; radius: pill.radius; shown: pill.keyboardFocused }

                    Text {
                        id: pillLabel
                        objectName: tabs.tabPrefix + "Label_" + pill.modelData.key
                        anchors.centerIn: parent
                        text: pill.modelData.label
                        color: pill.modelData.key === tabs.currentTab ? "#17120a" : theme.ink
                        font.family: theme.ui
                        font.pixelSize: theme.fontSize(tabs.theatreStyle && tabs.televisionMode ? 16 : (tabs.compact ? 13 : 14))
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
}
