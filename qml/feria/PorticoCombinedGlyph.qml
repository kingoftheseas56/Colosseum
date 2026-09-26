import QtQuick

Item {
    id: root
    property string glyphKey: "plus"
    property color tone: "#c9c8d0"
    readonly property var brandFormat: ({
        netflix: "svg", prime: "png", hbomax: "svg", disney: "png",
        appletv: "svg", crunchyroll: "svg", youtube: "svg", hulu: "png",
        mubi: "svg", spotify: "svg", ytmusic: "svg", applemusic: "svg",
        kindle: "png", playbooks: "svg", mangaplus: "png", viz: "png",
        webtoon: "svg", dcui: "png", marvel: "png", kobo: "svg",
        applebooks: "png"
    })
    Image {
        id: glyphImage
        anchors.fill: parent
        source: root.brandFormat[root.glyphKey]
            ? Qt.resolvedUrl("brand-logos/" + root.glyphKey + "." + root.brandFormat[root.glyphKey])
            : Qt.resolvedUrl("portico-glyphs/" + root.glyphKey + ".svg")
        fillMode: Image.PreserveAspectFit
        sourceSize.width: Math.max(1, width * 2)
        sourceSize.height: Math.max(1, height * 2)
        visible: true
        smooth: true
        mipmap: true
    }
}
