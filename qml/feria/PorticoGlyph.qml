import QtQuick

Item {
    id: root
    property string glyphKey: "plus"
    property color tone: "#c9c8d0"
    Image {
        id: glyphImage
        anchors.fill: parent
        source: Qt.resolvedUrl("portico-glyphs/" + root.glyphKey + ".svg")
        fillMode: Image.PreserveAspectFit
        sourceSize.width: Math.max(1, width * 2)
        sourceSize.height: Math.max(1, height * 2)
        visible: true
        smooth: true
        mipmap: true
    }
}
