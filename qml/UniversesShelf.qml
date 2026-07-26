// UniversesShelf — the installed universes, in ONE component that serves both forms.
//
// Hemanth's brief (2026-07-26): "Use the same QML for the carasel and the 'list of
// universes' sub page containing the list of all universes." So this is not a rail plus a
// separate hall page: it is one delegate, one data path, and a `form` switch. A universe
// that gains a medium, or a new universe installed, changes both surfaces at once because
// there is only one surface to change.
//
//   form: "carousel"  — a horizontal strip, for a home row or the Extensions page.
//   form: "list"      — full-width bars stacked down the page, for the see-all sub page.
//
// The list form is deliberately NOT a tile grid. That is a standing constraint of the
// house, and it is also what Hemanth ratified on 2026-07-12 when the horizontal spine
// failed his hand at 21 worlds — "it's just one long row". A carousel is right for three;
// it is wrong for thirty, and the same component has to survive both.
//
// DATA: installed extensions whose manifest declares the `universe` resource, enabled, in
// store order. Classification is by ROLE and derived, never stored — a universe declares
// types across every medium it spans, so deriving from content would put One Piece in
// manga AND anime AND film AND its own row. Checked before the type derivation.
// (Universes design §5.1a; guarded by tests/extension_worlds_derivation_test.mjs.)
//
// A universe supplies identity and ordering only — never sources (§5.3). Nothing here
// ranks anything, and nothing here is asked for a file.
pragma ComponentBehavior: Bound
import QtQuick
import "ExtensionsCatalog.js" as Catalog
import "Universes.js" as Universes

Item {
    id: root

    // "carousel" | "list"
    property string form: "carousel"
    property var installedList: []
    property bool includeDisabled: false

    signal universeActivated(var entry)

    Theme { id: theme }

    readonly property var universes: {
        var out = [];
        for (var i = 0; i < installedList.length; i++) {
            var e = installedList[i];
            if (!Catalog.isUniverse(e)) continue;
            if (!includeDisabled && e.enabled !== true) continue;
            out.push(e);
        }
        return out;
    }
    readonly property int count: universes.length
    function nameOf(e) { return (e && e.manifest && e.manifest.name) || (e && e.id) || "" }
    // THE JOIN. The extension supplies identity — name, installed, enabled. The payload
    // (art, blurb, the curated works) lives at the curation point. Matching them by name
    // is what makes an installed universe a real page rather than an empty row; without it
    // these are placeholders, which is exactly what Hemanth called them.
    //
    // The spec's end state serves this payload as universe.json over HTTPS (§5.5). Reading
    // it from the in-repo curation is the honest interim: the data is real and verified,
    // and no server is being pretended into existence.
    function payloadFor(e) {
        var n = nameOf(e);
        if (!n) return null;
        var all = Universes.universes || [];
        for (var i = 0; i < all.length; i++)
            if (all[i] && all[i].name === n) return all[i];
        return null;
    }
    // Manifest art wins when a served universe carries its own; otherwise the curated
    // banner. Absent both, the tile degrades to its name on the plate — never to a
    // stand-in borrowed from another IP.
    function artOf(e) {
        var m = (e && e.manifest) || {};
        if (m.background) return m.background;
        if (m.logo) return m.logo;
        var p = payloadFor(e);
        return (p && p.banner) || "";
    }
    // A universe with no payload yet is still installed and still opens — it just has
    // nothing to show. Saying so on the tile beats a silent empty page.
    function isDressed(e) { return payloadFor(e) !== null }

    implicitHeight: form === "list" ? listCol.implicitHeight : strip.height

    // ── carousel ────────────────────────────────────────────────────────────
    ListView {
        id: strip
        visible: root.form === "carousel"
        width: parent.width
        height: visible ? 132 : 0
        orientation: ListView.Horizontal
        spacing: 14
        clip: true
        model: root.universes
        boundsBehavior: Flickable.StopAtBounds

        // A horizontal list ignores the vertical wheel by default; map it to sideways
        // travel or the strip is unreachable with a mouse. (Same fix the KDE shelf needed.)
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (ev) => {
                var d = (ev.angleDelta.y !== 0 ? ev.angleDelta.y : ev.angleDelta.x)
                strip.contentX = Math.max(0, Math.min(
                    Math.max(0, strip.contentWidth - strip.width), strip.contentX - d))
            }
        }

        delegate: Rectangle {
            id: tile
            required property var modelData
            width: 232
            height: 124
            radius: 14
            color: "#0b0c14"
            border.width: 1
            border.color: tileMa.containsMouse ? Qt.rgba(1, 1, 1, 0.3) : theme.edge
            clip: true

            Image {
                anchors.fill: parent
                source: root.artOf(tile.modelData)
                visible: source != ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                opacity: 0.55
            }
            // Washed dark so the name always wins, whatever the art behind it.
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.15) }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.78) }
                }
            }
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 14
                spacing: 2
                Text {
                    width: parent.width
                    text: root.nameOf(tile.modelData)
                    color: theme.ink
                    font.family: theme.display
                    font.pixelSize: 20
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: !root.isDressed(tile.modelData)
                    text: "No works yet"
                    color: theme.inkDimmer
                    font.family: theme.ui; font.pixelSize: 11
                }
            }
            MouseArea {
                id: tileMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.universeActivated(tile.modelData)
            }
        }
    }

    // ── list ────────────────────────────────────────────────────────────────
    Column {
        id: listCol
        visible: root.form === "list"
        width: parent.width
        spacing: 10

        Repeater {
            model: root.form === "list" ? root.universes : []
            delegate: Rectangle {
                id: bar
                required property var modelData
                required property int index
                width: listCol.width
                height: 96
                radius: 14
                color: "#0b0c14"
                border.width: 1
                border.color: barMa.containsMouse ? Qt.rgba(1, 1, 1, 0.3) : theme.edge
                clip: true

                Image {
                    anchors.fill: parent
                    source: root.artOf(bar.modelData)
                    visible: source != ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    opacity: 0.4
                }
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.85) }
                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.35) }
                    }
                }
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    spacing: 20
                    // The index is real information here — it is the order the shelf is
                    // read in, not decoration bolted onto a list that has no sequence.
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 34
                        text: bar.index + 1
                        color: theme.gold
                        font.family: theme.ui; font.pixelSize: 13; font.bold: true
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.nameOf(bar.modelData)
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 26
                        elide: Text.ElideRight
                        width: parent.width - 34 - 20 - 90
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Enter →"
                        color: barMa.containsMouse ? theme.ink : theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 13
                    }
                }
                MouseArea {
                    id: barMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.universeActivated(bar.modelData)
                }
            }
        }
    }
}
