import QtQuick

Item {
    id: chip
    objectName: "reader2AudioOfferChip"

    property string text: qsTr("Audiobook available")
    signal activated()

    implicitWidth: 164
    implicitHeight: 29

    component HeadphoneGlyph: Canvas {
        property color ink: Theme.gold
        antialiasing: true
        onInkChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            var w = width
            var h = height
            var s = Math.min(w, h)
            var cx = w / 2
            var cy = h / 2
            ctx.clearRect(0, 0, w, h)
            ctx.strokeStyle = ink
            ctx.lineWidth = Math.max(1.3, s / 10)
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath()
            ctx.arc(cx, cy + s * 0.03, s * 0.34, Math.PI, 0, false)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(cx - s * 0.34, cy + s * 0.03)
            ctx.lineTo(cx - s * 0.34, cy + s * 0.30)
            ctx.moveTo(cx + s * 0.34, cy + s * 0.03)
            ctx.lineTo(cx + s * 0.34, cy + s * 0.30)
            ctx.stroke()
            ctx.strokeRect(cx - s * 0.43, cy + s * 0.16, s * 0.14, s * 0.29)
            ctx.strokeRect(cx + s * 0.29, cy + s * 0.16, s * 0.14, s * 0.29)
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Qt.rgba(20 / 255, 16 / 255, 6 / 255, 0.60)
        border.width: 1
        border.color: Qt.rgba(240 / 255, 194 / 255, 74 / 255, 0.30)
        antialiasing: true
    }

    Row {
        anchors.centerIn: parent
        spacing: 7

        HeadphoneGlyph {
            width: 14
            height: 14
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            font.family: Theme.ui
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: Theme.gold
        }
    }

    ReaderKeyboardArea {
        id: actionArea
        objectName: "reader2AudioOfferChipAction"
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        keyboardLabel: chip.text
        onClicked: chip.activated()
    }
}
