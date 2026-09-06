import QtQuick

Item {
    id: root

    property string countryCode: ""
    implicitWidth: 42
    implicitHeight: 42

    Accessible.role: Accessible.Graphic
    Accessible.name: root.countryCode.length ? root.countryCode + " flag" : "Country flag"

    Rectangle {
        anchors.fill: parent
        radius: Math.min(width, height) * 0.22
        color: "#151821"
        clip: true

        Canvas {
            id: flagCanvas
            anchors.fill: parent
            anchors.margins: 1
            antialiasing: true

            onPaint: {
                var ctx = getContext("2d")
                var w = width
                var h = height
                var code = String(root.countryCode || "").toUpperCase()

                ctx.clearRect(0, 0, w, h)
                ctx.fillStyle = "#151821"
                ctx.fillRect(0, 0, w, h)

                function fillRect(x, y, width, height, color) {
                    ctx.fillStyle = color
                    ctx.fillRect(x, y, width, height)
                }
                function polygon(points, color) {
                    ctx.beginPath()
                    ctx.moveTo(points[0][0], points[0][1])
                    for (var i = 1; i < points.length; ++i)
                        ctx.lineTo(points[i][0], points[i][1])
                    ctx.closePath()
                    ctx.fillStyle = color
                    ctx.fill()
                }
                function diagonal(stroke, lineWidth) {
                    ctx.strokeStyle = stroke
                    ctx.lineWidth = lineWidth
                    ctx.beginPath()
                    ctx.moveTo(0, 0)
                    ctx.lineTo(w, h)
                    ctx.moveTo(w, 0)
                    ctx.lineTo(0, h)
                    ctx.stroke()
                }
                function cross(stroke, lineWidth) {
                    ctx.strokeStyle = stroke
                    ctx.lineWidth = lineWidth
                    ctx.beginPath()
                    ctx.moveTo(w / 2, 0)
                    ctx.lineTo(w / 2, h)
                    ctx.moveTo(0, h / 2)
                    ctx.lineTo(w, h / 2)
                    ctx.stroke()
                }

                if (code === "GB" || code === "UK") {
                    fillRect(0, 0, w, h, "#1f4ba5")
                    diagonal("#ffffff", h * 0.30)
                    diagonal("#d72229", h * 0.13)
                    cross("#ffffff", h * 0.38)
                    cross("#d72229", h * 0.16)
                } else if (code === "ES") {
                    fillRect(0, 0, w, h / 4, "#aa151b")
                    fillRect(0, h / 4, w, h / 2, "#f1bf00")
                    fillRect(0, h * 0.75, w, h / 4, "#aa151b")
                } else if (code === "BR") {
                    fillRect(0, 0, w, h, "#009739")
                    polygon([[w / 2, h * 0.12], [w * 0.90, h / 2],
                             [w / 2, h * 0.88], [w * 0.10, h / 2]], "#f7d117")
                    ctx.beginPath()
                    ctx.arc(w / 2, h / 2, Math.min(w, h) * 0.22, 0, Math.PI * 2)
                    ctx.fillStyle = "#22408c"
                    ctx.fill()
                } else if (code === "FR") {
                    fillRect(0, 0, w / 3, h, "#0055a4")
                    fillRect(w / 3, 0, w / 3, h, "#ffffff")
                    fillRect(w * 2 / 3, 0, w / 3, h, "#ef4135")
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.min(width, height) * 0.22
        color: "transparent"
        border.width: 1
        border.color: theme.edge
    }

    Theme { id: theme }

    onCountryCodeChanged: flagCanvas.requestPaint()
}
