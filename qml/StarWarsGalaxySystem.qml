pragma ComponentBehavior: Bound
import QtQuick

FocusScope {
    id: root
    property var destinations: []
    property bool reducedMotion: false
    property real cameraYaw: -0.42
    property real cameraPitch: 0.73
    property real cameraZoom: 1.0
    property real orbitTime: 0
    readonly property real viewportScale: Math.min(1.0, Math.max(.72, Math.min(width/1600, height/900)))
    signal destinationActivated(string destinationId)

    Theme { id: theme }

    function seeded(i) {
        var x = Math.sin(i * 126.73 + 3.7) * 43758.5453
        return x - Math.floor(x)
    }
    function projectPoint(x, y, z) {
        var cy = Math.cos(cameraYaw), sy = Math.sin(cameraYaw)
        var cp = Math.cos(cameraPitch), sp = Math.sin(cameraPitch)
        var x1 = x * cy - z * sy
        var z1 = x * sy + z * cy
        var ry = y * cp - z1 * sp
        var rz = y * sp + z1 * cp
        var distance = 1080 / (cameraZoom * viewportScale)
        var f = 860 / Math.max(220, distance + rz)
        return { x: width/2 + x1*f, y: height/2 + ry*f, scale: f, depth: rz }
    }
    function projected(d) {
        var radius = d.node ? d.radius : d.orbit
        var angle = d.angle + (d.node ? 0 : (d.speed || 0) * orbitTime)
        return projectPoint(Math.cos(angle) * radius, d.y || 0, Math.sin(angle) * radius)
    }
    function bodyRadius(d, p) {
        return d.node ? Math.max(11, 17 * p.scale) : Math.max(5, d.size * p.scale)
    }

    // Keep the approved opening composition stable. Camera drag/zoom supplies
    // spatial motion without allowing era labels to drift into one another.
    Timer {
        interval: 40
        repeat: true
        running: false
        onTriggered: {
            root.orbitTime += interval
            sky.requestPaint()
        }
    }
    onCameraYawChanged: sky.requestPaint()
    onCameraPitchChanged: sky.requestPaint()
    onCameraZoomChanged: sky.requestPaint()
    onWidthChanged: sky.requestPaint()
    onHeightChanged: sky.requestPaint()

    Canvas {
        id: sky
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#05070b"
            ctx.fillRect(0, 0, width, height)
            for (var i = 0; i < 520; ++i) {
                var sx = root.seeded(i*3+1), sy = root.seeded(i*3+2)
                var sr = root.seeded(i*3+3) * 1.25 + .2
                var sa = root.seeded(i*5+2) * .55 + .12
                var driftX = root.cameraYaw * 17 * (.35 + sr*.2)
                var driftY = root.cameraPitch * 10 * (.35 + sr*.2)
                var px = (sx * width + driftX) % width
                var py = (sy * height + driftY) % height
                if (px < 0) px += width
                if (py < 0) py += height
                ctx.globalAlpha = sa
                ctx.fillStyle = "#ffffff"
                ctx.beginPath(); ctx.arc(px, py, sr, 0, Math.PI*2); ctx.fill()
            }
            ctx.globalAlpha = 1
            for (var d = 0; d < root.destinations.length; ++d) {
                var body = root.destinations[d]
                if (body.node) continue
                ctx.strokeStyle = d < 3 ? "rgba(210,216,225,.17)" : "rgba(210,216,225,.12)"
                ctx.lineWidth = .8
                ctx.beginPath()
                for (var j = 0; j <= 180; ++j) {
                    var a = j / 180 * Math.PI * 2
                    var p = root.projectPoint(Math.cos(a)*body.orbit, 0, Math.sin(a)*body.orbit)
                    if (j === 0) ctx.moveTo(p.x,p.y); else ctx.lineTo(p.x,p.y)
                }
                ctx.stroke()
            }
        }
    }

    Text {
        x: theme.margin; y: 88
        text: "In a galaxy far far away"
        color: theme.ink
        font.family: theme.display; font.pixelSize: Math.max(44, Math.round(58 * root.viewportScale))
        font.weight: Font.Normal
    }
    Item {
        id: sun
        property var p: root.projectPoint(0,0,0)
        property real r: Math.max(23, 66 * p.scale)
        x: p.x - r; y: p.y - r
        width: r*2; height: r*2
        z: 3
        Rectangle {
            anchors.centerIn: parent
            width: parent.width*2.7; height: width; radius: width/2
            color: Qt.rgba(240/255,196/255,74/255,.035)
        }
        Rectangle {
            anchors.fill: parent; radius: width/2
            color: "#3e240b"
            border.width: 1; border.color: Qt.rgba(255/255,225/255,128/255,.22)
        }
        Rectangle {
            width: parent.width*.86; height: width; radius: width/2
            x: parent.width*.04; y: parent.height*.04
            color: "#c99d3d"
        }
        Rectangle {
            width: parent.width*.48; height: width; radius: width/2
            x: parent.width*.10; y: parent.height*.08
            color: "#fff6cf"; opacity: .76
        }
        Row {
            x: parent.width + 10
            y: parent.height/2 - height/2 - 1
            spacing: 7
            Rectangle { width: 6; height: 6; radius: 3; color: theme.gold; opacity: .62 }
            Text {
                text: "SKYWALKER SAGA"; color: theme.ink
                font.family: theme.ui; font.pixelSize: 11; font.weight: Font.DemiBold
                font.letterSpacing: .8
            }
        }
    }
    Repeater {
        id: gates
        model: root.destinations
        delegate: FocusScope {
            id: gate
            required property var modelData
            required property int index
            property var p: root.projected(modelData)
            property real r: root.bodyRadius(modelData, p)
            property bool hot: activeFocus || ma.containsMouse
            activeFocusOnTab: true
            x: p.x - r
            y: p.y - r
            width: Math.max(r*2, 160)
            height: Math.max(r*2, 34)
            z: 4

            Item {
                id: planet
                visible: gate.modelData.node !== true
                width: gate.r*2; height: width
                Rectangle {
                    anchors.fill: parent; radius: width/2
                    color: gate.modelData.dark
                    border.width: gate.hot ? 1.5 : 1
                    border.color: gate.hot ? theme.gold : Qt.rgba(247/255,247/255,245/255,.16)
                }
                Rectangle {
                    width: parent.width*.88; height: width; radius: width/2
                    x: parent.width*.035; y: parent.height*.035
                    color: gate.modelData.mid; opacity: .92
                }
                Rectangle {
                    width: parent.width*.46; height: width; radius: width/2
                    x: parent.width*.10; y: parent.height*.08
                    color: gate.modelData.light; opacity: .38
                }
                Image {
                    anchors.centerIn: parent
                    source: gate.modelData.mark || ""
                    width: gate.modelData.id === "high" ? gate.r*1.72 : gate.r*1.35
                    height: gate.modelData.id === "high" ? gate.r*.58 : gate.r*1.35
                    sourceSize.width: Math.max(32, width*3)
                    sourceSize.height: Math.max(32, height*3)
                    fillMode: Image.PreserveAspectFit
                    smooth: true; mipmap: true
                    opacity: gate.modelData.id === "high" ? .54 : .42
                }
                Rectangle {
                    anchors.fill: parent; anchors.margins: 3
                    radius: width/2; color: "transparent"
                    border.width: 1; border.color: Qt.rgba(0,0,0,.26)
                }
            }

            Item {
                id: node
                visible: gate.modelData.node === true
                width: gate.r*2; height: width
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width*.96; height: width
                    rotation: 45; color: Qt.rgba(1,1,1,.018)
                    border.width: gate.hot ? 1.5 : 1
                    border.color: gate.hot ? theme.gold : Qt.rgba(201/255,200/255,208/255,.42)
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width*.54; height: width
                        color: "transparent"; border.width: 1
                        border.color: Qt.rgba(201/255,200/255,208/255,.48)
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width*1.15; height: width; radius: width/2
                    color: "transparent"; border.width: 1
                    border.color: Qt.rgba(1,1,1,.12)
                }
            }
            Row {
                id: labelRow
                property bool placeLeft: gate.p.x < sun.p.x
                property real verticalNudge: {
                    if (gate.modelData.id === "high") return -9
                    if (gate.modelData.id === "fall") return -12
                    if (gate.modelData.id === "empire") return 9
                    if (gate.modelData.id === "rebellion") return -7
                    if (gate.modelData.id === "beyond") return -5
                    return 0
                }
                x: placeLeft ? -width - (gate.modelData.node ? 17 : 12)
                             : gate.r*2 + (gate.modelData.node ? 17 : 12)
                y: gate.r - height/2 - Math.max(1, gate.r*.12) + verticalNudge
                spacing: 7
                Rectangle {
                    width: 5; height: 5
                    radius: gate.modelData.node ? 0 : 2.5
                    rotation: gate.modelData.node ? 45 : 0
                    color: "transparent"; border.width: 1
                    border.color: gate.hot ? theme.gold : Qt.rgba(247/255,247/255,245/255,.58)
                }
                Text {
                    text: gate.modelData.name
                    color: gate.hot ? theme.gold : Qt.rgba(247/255,247/255,245/255,.82)
                    font.family: theme.ui; font.pixelSize: 9; font.weight: Font.Bold
                    font.letterSpacing: 1.0
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
            }

            MouseArea {
                id: ma
                x: -8; y: -8
                width: Math.max(gate.r*2 + labelRow.width + 24, 44)
                height: Math.max(gate.r*2 + 16, 44)
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: {
                    gate.forceActiveFocus()
                    root.destinationActivated(gate.modelData.id)
                }
            }
            Keys.onLeftPressed: function(event) {
                var next = (index - 1 + gates.count) % gates.count
                gates.itemAt(next).forceActiveFocus(); event.accepted = true
            }
            Keys.onRightPressed: function(event) {
                var next = (index + 1) % gates.count
                gates.itemAt(next).forceActiveFocus(); event.accepted = true
            }
            Keys.onReturnPressed: function(event) { root.destinationActivated(gate.modelData.id); event.accepted = true }
            Keys.onEnterPressed: function(event) { root.destinationActivated(gate.modelData.id); event.accepted = true }
        }
    }

    MouseArea {
        id: cameraDrag
        anchors.fill: parent
        z: 1
        acceptedButtons: Qt.LeftButton
        property real lastX: 0
        property real lastY: 0
        onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
        onPositionChanged: function(mouse) {
            if (!(mouse.buttons & Qt.LeftButton)) return
            var dx = mouse.x - lastX, dy = mouse.y - lastY
            lastX = mouse.x; lastY = mouse.y
            root.cameraYaw += dx * .0028
            root.cameraPitch = Math.max(.18, Math.min(1.18, root.cameraPitch + dy * .0023))
        }
        onWheel: function(wheel) {
            root.cameraZoom = Math.max(.68, Math.min(1.42, root.cameraZoom + wheel.angleDelta.y / 2400))
            wheel.accepted = true
        }
    }
}
