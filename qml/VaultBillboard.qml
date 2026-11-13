pragma ComponentBehavior: Bound
import QtQuick

// TheatreCinemaHero's cinematic composition, adapted rather than changing its public contract:
// Vault needs a gold resume action, logo, progress, full metadata, and externally tracked slide.
FocusScope {
    id: hero
    property var rows: []
    property int currentIndex: 0
    readonly property var row: rows.length ? rows[Math.min(currentIndex, rows.length - 1)] : ({})
    signal activated(var row)
    signal detailsRequested(var row)
    signal moveDown()
    height: 336
    Theme { id: theme }
    onRowsChanged: currentIndex = 0
    VaultArt { anchors.fill: parent; row: hero.row; landscape: true; radius: 18; opacity: 0.8 }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: theme.vaultScrim }
            GradientStop { position: 0.66; color: Qt.rgba(theme.vaultScrim.r, theme.vaultScrim.g, theme.vaultScrim.b, 0.5) }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.45; color: "transparent" }
            GradientStop { position: 1; color: theme.vaultBackdrop }
        }
    }
    Column {
        x: 0; y: 28; width: Math.min(470, hero.width - 24); spacing: 10
        Text { text: Number(hero.row.progressFraction || 0) > 0 ? "CONTINUE YOUR STORY" : "IN YOUR LIBRARY"; color: theme.gold; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.8 }
        Item {
            width: parent.width; height: 68
            Image { id: logo; anchors.left: parent.left; width: Math.min(320, parent.width); height: 68; source: hero.row.logo || ""; fillMode: Image.PreserveAspectFit; horizontalAlignment: Image.AlignLeft; asynchronous: true; sourceSize.width: 640 }
            Text { visible: logo.status !== Image.Ready; anchors.fill: parent; text: String(hero.row.title || "Your library"); color: theme.ink; font.family: theme.display; font.pixelSize: 42; fontSizeMode: Text.Fit; minimumPixelSize: 27; maximumLineCount: 2; wrapMode: Text.WordWrap; elide: Text.ElideRight }
        }
        Text {
            width: parent.width
            text: [hero.row.year || "", hero.row.runtimeText || "", Array.isArray(hero.row.genres) ? hero.row.genres.slice(0, 2).join(", ") : (hero.row.genres || ""), hero.row.imdbRating ? "IMDb " + hero.row.imdbRating : ""].filter(v => String(v).length).join("  ·  ")
            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; elide: Text.ElideRight
        }
        Text { width: parent.width; text: String(hero.row.blurb || hero.row.description || hero.row.subtitle || ""); color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
        Row {
            spacing: 10
            Repeater {
                model: [Number(hero.row.progressFraction || 0) > 0 ? "Resume" : (hero.row.kind === "book" || hero.row.kind === "comic" ? "Read" : "Watch"), "Details"]
                delegate: Rectangle {
                    id: cta
                    required property string modelData
                    required property int index
                    width: index === 0 ? 124 : 100; height: 42; radius: 21
                    color: index === 0 ? (action.pressed ? Qt.darker(theme.gold, 1.15) : theme.gold) : (action.interactionActive ? theme.vaultSurfaceHi : theme.vaultSurface)
                    border.color: index === 0 ? theme.gold : theme.edge
                    scale: action.pressed ? 0.97 : 1
                    Behavior on scale { NumberAnimation { duration: theme.vaultMotionDuration; easing.type: Easing.OutCubic } }
                    Text { anchors.centerIn: parent; text: cta.modelData; color: cta.index === 0 ? theme.vaultBackdrop : theme.ink; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                    KeyboardAction { id: action; anchors.fill: parent; focusRadius: 21; focusPrimary: cta.index === 0; accessibleName: cta.modelData + " " + String(hero.row.title || ""); onTriggered: cta.index === 0 ? hero.activated(hero.row) : hero.detailsRequested(hero.row) }
                }
            }
        }
        Row {
            visible: Number(hero.row.progressFraction || 0) > 0; spacing: 10
            Rectangle { width: 160; height: 3; y: 7; radius: 2; color: theme.edge
                Rectangle { height: 3; radius: 2; width: parent.width * Math.max(0, Math.min(1, Number(hero.row.progressFraction || 0))); color: theme.gold }
            }
            Text { text: String(hero.row.progressText || "In progress"); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
        }
    }
    Row {
        anchors.bottom: parent.bottom; anchors.bottomMargin: 22; spacing: 4
        Repeater {
            model: hero.rows.length
            delegate: Item {
                id: dot
                required property int index
                width: 24; height: 20
                Rectangle { anchors.centerIn: parent; width: hero.currentIndex === dot.index ? 20 : 6; height: 6; radius: 3; color: hero.currentIndex === dot.index ? theme.ink : theme.inkDimmer }
                KeyboardAction { anchors.fill: parent; accessibleName: "Featured title " + (dot.index + 1); onTriggered: hero.currentIndex = dot.index }
            }
        }
    }
    Timer { interval: 9000; repeat: true; running: hero.visible && hero.rows.length > 1 && !hero.activeFocus && !hover.hovered; onTriggered: hero.currentIndex = (hero.currentIndex + 1) % hero.rows.length }
    HoverHandler { id: hover }
    Keys.onDownPressed: hero.moveDown()
}
