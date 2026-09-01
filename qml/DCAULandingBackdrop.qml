import QtQuick

Item {
    id: root
    objectName: "dcauLandingBackdropV11"

    Canvas {
        id: wash
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()

            var vertical = ctx.createLinearGradient(0, 0, 0, height)
            vertical.addColorStop(0.0, "#0a0d13")
            vertical.addColorStop(0.72, "#07090d")
            vertical.addColorStop(1.0, "#07090d")
            ctx.fillStyle = vertical
            ctx.fillRect(0, 0, width, height)

            var glow = ctx.createRadialGradient(width * 0.5, height * 0.45, 0,
                                                width * 0.5, height * 0.45, width * 0.35)
            glow.addColorStop(0.0, "rgba(255,255,255,0.035)")
            glow.addColorStop(1.0, "rgba(255,255,255,0)")
            ctx.fillStyle = glow
            ctx.fillRect(0, 0, width, height)
        }
    }
}
