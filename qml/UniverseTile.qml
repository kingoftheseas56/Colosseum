// UniverseTile — one work in a universe row. 150x236 r8, index numeral, optional chip,
// 56px caption block (a 6px top margin leaves ~50px for the two-line title + year, measured
// against real Segoe UI metrics so the caption never overflows the declared 292px total height).
// Every value is the inherited saga contract (One Piece plan §3.0), not a fresh choice: a
// universe page must sit in the same family as SagaUniversePage.
//
// Art by kind: video → metahub poster/small (ONLY /small is reliable; /medium 404s across
// the long tail and Cinemeta URLs must never be upscaled — house doctrine). Non-video kinds
// resolve their cover ASYNC from the entry's {provider,id} via UniverseApi.coverFor — anilist
// (GraphQL by ID), applebooks (iTunes lookup by store ID), comic (ComicsApi.posterFor). The
// lettered plate is the fallback only while a cover loads or is unreachable.
pragma ComponentBehavior: Bound
import QtQuick
import "UniverseApi.js" as UniverseApi

Item {
    id: tile
    property var entry: ({})            // public API — left as-is; read through `e` below
    readonly property var e: tile.entry || ({})
    property string kind: "video"
    property int order: 0               // NOT "index"
    property bool focusManagedByCollection: false
    property bool keyboardSelected: false
    // NOT "index" — a delegate's own `index` would shadow it
                                         // under ComponentBehavior: Bound and freeze every tile at "1"
    signal activated()

    // Async-resolved cover for non-video kinds (UniverseApi.coverFor). Unlike `art`, this is
    // IMPERATIVE state, not a binding — so it does NOT refresh itself when the view recycles
    // this delegate onto a different entry. Two guards, both load-bearing:
    //
    //  1. ListView.onReused — the rail sets `reuseItems: true`, and on reuse Qt re-assigns the
    //     delegate's model data but does NOT re-run Component.onCompleted. Measured: scrolling a
    //     13-entry rail left 3 of 5 live tiles painting an earlier entry's cover under the correct
    //     title. onReused is the only hook that fires on rebind.
    //  2. the `forEntry` identity check — a reply can land AFTER this tile has been recycled onto
    //     something else, which would paint the wrong cover a second way. Only accept a reply that
    //     still belongs to the entry that asked for it.
    property string coverUrl: ""
    function resolveCover() {
        tile.coverUrl = ""                       // drop the previous entry's art immediately
        if (tile.kind === "video") return
        var forEntry = tile.e
        UniverseApi.coverFor(forEntry, tile.kind, function (u) {
            if (u && tile.e === forEntry) tile.coverUrl = u
        })
    }
    Component.onCompleted: tile.resolveCover()
    ListView.onReused: tile.resolveCover()

    width: 150
    height: 236 + 56

    Theme { id: theme }

    readonly property string art: {
        if (tile.e.poster) return tile.e.poster
        if (tile.kind === "video" && tile.e.id)
            return "https://images.metahub.space/poster/small/" + tile.e.id + "/img"
        return ""
    }

    Rectangle {
        id: plate
        width: 150; height: 236; radius: 8
        color: "#12141a"
        border.width: (tileKey.activeFocus || tile.keyboardSelected) ? 2 : 1
        border.color: (tileKey.activeFocus || tile.keyboardSelected) ? theme.gold : theme.edge
        clip: true

        Image {
            id: artImage
            anchors.fill: parent
            source: tile.art !== "" ? tile.art : tile.coverUrl
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            sourceSize.width: 300
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 220 } }
        }
        // The honest fallback: the work's own name, never another IP's art. Cross-fades with
        // the Image above — visible while there is no art URL at all, or while the Image is
        // still loading (status passes Loading → Ready with asynchronous: true), fading out
        // the instant it is Ready. Complementary by construction: never both fully shown or
        // both fully hidden.
        Text {
            anchors.fill: parent
            anchors.margins: 12
            opacity: ((tile.art === "" && tile.coverUrl === "") || artImage.status !== Image.Ready) ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 220 } }
            text: tile.e.title || ""
            color: theme.inkDimmer
            font.family: theme.display; font.pixelSize: 15
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            width: 26; height: 26; radius: 6
            x: 8; y: 8
            color: Qt.rgba(0, 0, 0, 0.62)
            Text {
                anchors.centerIn: parent
                text: tile.order + 1
                color: theme.gold
                font.family: theme.ui; font.pixelSize: 13; font.bold: true
            }
        }
        Text {
            anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 8
            visible: !!tile.e.note
            text: (tile.e.note || "").toUpperCase()
            color: theme.gold
            font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 2
        }
        KeyboardAction {
            id: tileKey
            anchors.fill: parent
            pointerEnabled: false
            focusEnabled: !tile.focusManagedByCollection && tile.visible && tile.enabled
            accessibleName: tile.e.title || "Universe work"
            onTriggered: tile.activated()
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: tile.activated()
        }
    }

    Column {
        anchors.top: plate.bottom
        anchors.topMargin: 6
        width: parent.width
        spacing: 2
        Text {
            width: parent.width
            text: tile.e.title || ""
            color: theme.ink
            font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: !!tile.e.year
            text: tile.e.year || ""
            color: theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 11
        }
    }
}
