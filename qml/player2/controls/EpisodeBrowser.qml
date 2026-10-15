import QtQuick
import "../.."
import "Player2Browser.js" as Browser

// EpisodeBrowser — the drawer's Episodes tab: season pills over the active season's episode list.
// Pure renderer in the Player 2 house style: host-resolved data in, typed taps out. It never fetches
// or ranks — SourceDrawer asks the host and feeds `episodes` in; row state (now/watched/progress) is
// derived by Player2Browser.js (headless-tested). Now-playing is a media-id compare, not a host flag.
Item {
    id: browser

    property QtObject theme
    property int seasonCount: 0
    property int activeSeason: 1
    property var episodes: []
    property string currentEpisodeId: ""
    property string state: "idle"   // idle | loading | ready

    signal episodePicked(string episodeId)
    signal seasonPicked(int season)

    readonly property color gold: theme ? theme.gold : "#f0c44a"
    readonly property color ink: theme ? theme.ink : "#f7f7f5"
    readonly property color inkDim: theme ? theme.inkDim : "#c9c8d0"
    readonly property color inkDimmer: theme ? theme.inkDimmer : "#9a99a5"
    readonly property color edge: theme ? theme.edge : Qt.rgba(1, 1, 1, 0.18)
    readonly property color goldTint: Qt.rgba(0.94, 0.77, 0.29, 0.10)
    readonly property var pills: Browser.seasonPills(browser.seasonCount)
    function episodeIndexNow() {
        for (var i = 0; i < browser.episodes.length; ++i) if (String(browser.episodes[i].mediaId) === browser.currentEpisodeId) return i
        return browser.episodes.length ? 0 : -1
    }

    // ---- season pills: only when the show actually spans more than one season ----
    Flow {
        id: pillRow
        property int keyboardIndex: Math.max(0, browser.pills.indexOf(browser.activeSeason))
        focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: function(event) {
            if (!browser.pills.length) return
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                keyboardIndex = Math.max(0, Math.min(browser.pills.length - 1, keyboardIndex + (event.key === Qt.Key_Left ? -1 : 1))); event.accepted = true
            } else if (event.key === Qt.Key_Home) { keyboardIndex = 0; event.accepted = true }
            else if (event.key === Qt.Key_End) { keyboardIndex = browser.pills.length - 1; event.accepted = true }
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { browser.seasonPicked(browser.pills[keyboardIndex]); event.accepted = true }
        }
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 6
        visible: browser.pills.length > 1
        Repeater {
            model: browser.pills
            delegate: Rectangle {
                id: pill
                required property int index
                required property int modelData
                width: pillText.implicitWidth + 20
                height: 24
                radius: 12
                color: "transparent"
                border.width: 1
                border.color: (pillRow.activeFocus && pillRow.keyboardIndex === pill.index) || browser.activeSeason === pill.modelData
                              ? Qt.rgba(0.94, 0.77, 0.29, 0.72) : browser.edge
                Text {
                    id: pillText
                    anchors.centerIn: parent
                    text: "S" + pill.modelData
                    color: browser.activeSeason === pill.modelData ? browser.gold : browser.inkDim
                    font.family: "Segoe UI"; font.pixelSize: 11
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: browser.seasonPicked(pill.modelData)
                }
            }
        }
    }

    // ---- honest loading line ----
    Text {
        id: loadingLine
        anchors.top: pillRow.visible ? pillRow.bottom : parent.top
        anchors.topMargin: pillRow.visible ? 10 : 0
        anchors.left: parent.left
        visible: browser.state === "loading" && browser.episodes.length === 0
        text: "Loading episodes…"
        color: browser.inkDimmer; font.family: "Segoe UI"; font.pixelSize: 12
    }

    // ---- the season's episodes ----
    ListView {
        id: list
        anchors.top: pillRow.visible ? pillRow.bottom : parent.top
        anchors.topMargin: pillRow.visible ? 12 : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: browser.episodes
        currentIndex: browser.episodeIndexNow()
        focusPolicy: visible && count > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: function(event) { episodeKeyboard.handle(event) }
        delegate: Item {
            id: epRow
            required property var modelData
            width: ListView.view.width
            height: 46
            readonly property var st: Browser.episodeRowState(epRow.modelData, browser.currentEpisodeId)

            Rectangle {
                anchors.fill: parent
                color: epRow.st.state === "now" ? browser.goldTint
                     : epMa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
            }
            Rectangle {   // gold now-playing edge marker
                visible: epRow.st.state === "now"
                anchors.left: parent.left; width: 2
                anchors.top: parent.top; anchors.bottom: parent.bottom
                color: browser.gold
            }
            Row {
                anchors.fill: parent
                anchors.leftMargin: 12; anchors.rightMargin: 12
                spacing: 10
                Text {
                    width: 30
                    anchors.verticalCenter: parent.verticalCenter
                    text: "E" + epRow.modelData.episode
                    color: browser.inkDimmer; font.family: "Segoe UI"; font.pixelSize: 11
                }
                Text {
                    width: parent.width - 30 - 56 - 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: epRow.modelData.title
                    elide: Text.ElideRight
                    color: epRow.st.state === "now" ? "#f0e6c8"
                         : epRow.st.state === "watched" ? browser.inkDimmer : browser.ink
                    font.family: "Segoe UI"; font.pixelSize: 13
                }
                Rectangle {   // real progress bar
                    width: 46; height: 3; radius: 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: Qt.rgba(1, 1, 1, 0.10)
                    Rectangle {
                        width: parent.width * epRow.st.frac
                        anchors.left: parent.left
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        radius: 2; color: browser.gold
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                color: Qt.rgba(1, 1, 1, 0.05) }
            MouseArea {
                id: epMa
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: browser.episodePicked(epRow.modelData.mediaId)
            }
        }
        KeyboardCollectionController {
            id: episodeKeyboard
            view: list
            orientation: "vertical"
            count: list.count
            onActivated: function(index) { if (index >= 0 && index < browser.episodes.length) browser.episodePicked(String(browser.episodes[index].mediaId)) }
        }
    }
}
