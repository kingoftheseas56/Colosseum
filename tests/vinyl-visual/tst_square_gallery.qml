import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

// Explicit capture harness, outside the unit/aggregate QML directories.
// Run from the repository root after creating artifacts/vinyl-v1/slice3.
TestCase {
    id: test
    name: "VinylSquareGallery"
    visible: true
    width: 1240; height: 680
    when: windowShown && regular.status === FontLoader.Ready && italic.status === FontLoader.Ready
    FontLoader { id: regular; source: "../../assets/fonts/Fraunces-Regular.ttf" }
    FontLoader { id: italic; source: "../../assets/fonts/Fraunces-Italic.ttf" }
    QtObject {
        id: worldStub
        property var session: null
        function artwork(value) { return value || ""; }
        function activate(item) {}
    }
    function cover(color) {
        return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(
            '<svg xmlns="http://www.w3.org/2000/svg" width="220" height="220" viewBox="0 0 220 220">'
            + '<rect width="220" height="220" fill="' + color + '"/>'
            + '<circle cx="140" cy="77" r="65" fill="#f0d2a1" opacity=".75"/>'
            + '<path d="M0 140 L220 90 V220 H0Z" fill="#1d2737"/>'
            + '<path d="M0 175 L220 125" stroke="#e3a774" stroke-width="8"/>'
            + '</svg>');
    }
    readonly property var entries: [
        {id:"gallery-song", kind:"songs", title:"Glass Hours", artist:"Fixture artist",
            record:{title:"After Dark"}, cover:cover("#8f5647")},
        {id:"gallery-album", kind:"albums", title:"After Dark", artist:"Fixture artist", cover:cover("#627765")},
        {id:"gallery-artist", kind:"artists", title:"Fixture artist", cover:cover("#655c8a")},
        {id:"gallery-mix", kind:"mixes", title:"Late Night Mix", artworkCovers:[cover("#8f5647"),cover("#627765"),cover("#655c8a")]},
        {id:"gallery-station", kind:"radio", title:"Night Radio", cover:cover("#647b87")}
    ]
    Rectangle {
        id: sheet
        anchors.fill: parent
        color: "#11151e"
        Column {
            x: 32; y: 22; spacing: 20
            Text {
                text: "Vinyl · square widget review"
                font.family: regular.name; font.pixelSize: 27; color: "#f7f7f5"
            }
            Text {
                text: "Fixture artwork · production widgets and captions · 130 and 196 logical pixels"
                font.family: "Segoe UI"; font.pixelSize: 13; color: "#a9a9b4"
            }
            Repeater {
                model: [130, 196]
                delegate: Column {
                    id: sizeRow
                    required property int modelData
                    spacing: 9
                    Text {
                        text: sizeRow.modelData + " px"
                        font.family: "Segoe UI"; font.pixelSize: 12; color: "#f0c44a"
                    }
                    Row {
                        spacing: 22
                        Repeater {
                            model: test.entries
                            delegate: Vinyl.VinylItemCard {
                                required property var modelData
                                objectName: modelData.id + "-" + sizeRow.modelData
                                width: sizeRow.modelData
                                worldData: worldStub
                                item: modelData
                            }
                        }
                    }
                }
            }
        }
    }
    function artworkReady(node) {
        if (node.visible && node.objectName === "vinylArtworkImage" && String(node.source).length
                && node.status !== Image.Ready) return false;
        var children = node.children || [];
        for (var i = 0; i < children.length; ++i) if (!artworkReady(children[i])) return false;
        return true;
    }
    function test_capture() {
        tryVerify(function() { return artworkReady(sheet); }, 15000);
        verify(waitForRendering(sheet));
        var shot = grabImage(sheet);
        verify(shot.width > 0 && shot.height > 0);
        shot.save("artifacts/vinyl-v1/slice3/widget-gallery.png");
    }
}
