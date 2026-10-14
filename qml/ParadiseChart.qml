pragma ComponentBehavior: Bound
import QtQuick
import "UniverseContentApi.js" as UniverseContent
import QtQuick.Shapes
import "OnePieceParadiseAtlasData.js" as Atlas

Item {
    id: root
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? (UniverseContent.reload(Content.generation), Content.generation) : 0
    width: 1680
    height: 960
    clip: true
    property string displayFamily: "Georgia"
    property string bodyFamily: "Georgia"
    property real detailLevel: 1
    readonly property string leftContinent: "M22 22 L131 22 120 65 142 96 126 123 149 165 121 207 136 238 120 271 140 300 125 331 139 363 117 391 139 419 126 447 150 480 136 508 146 538 123 569 137 600 114 632 132 663 112 690 128 729 103 757 126 794 109 821 123 854 101 883 120 937 22 937 Z"
    readonly property string rightContinent: "M1658 22 L1536 22 1546 68 1523 98 1546 124 1525 159 1543 193 1518 226 1543 257 1527 286 1541 312 1517 345 1534 375 1520 407 1542 438 1526 472 1545 505 1524 537 1540 568 1521 603 1546 638 1529 671 1546 704 1527 738 1546 771 1523 809 1546 843 1528 871 1542 904 1530 937 1658 937 Z"

    function paperGrain() {
        var path = "";
        for (var i = 0; i < 620; ++i) {
            var x = 29 + ((i * 239 + i * i * 3) % 1618);
            var y = 29 + ((i * 137 + i * i * 7) % 902);
            path += "M" + x + " " + y + "l" + (1 + i % 3) + " 0.4 ";
        }
        return path;
    }

    function rhumbs() {
        var path = "";
        var centers = [[337, 798], [1365, 192]];
        for (var c = 0; c < centers.length; ++c) {
            for (var i = 0; i < 16; ++i) {
                var angle = Math.PI * i / 8;
                path += "M" + centers[c][0] + " " + centers[c][1] + "l"
                        + (Math.cos(angle) * 1900).toFixed(2) + " "
                        + (Math.sin(angle) * 1900).toFixed(2) + " ";
            }
        }
        return path;
    }

    function cliffHatch() {
        var path = "";
        for (var i = 0; i < 29; ++i) {
            var y = 50 + i * 30;
            var x = 51 + (i % 3) * 12;
            path += "M" + x + " " + (y + 12) + "l12 -22 13 23 m-13 -23 l2 24 ";
            path += "M" + (1574 + (i % 2) * 13) + " " + (y + 8) + "l12 -20 15 25 m-15 -25 l1 24 ";
        }
        return path;
    }

    function routeInk(group) {
        var path = "";
        for (var i = 0; i < (root.universeContentGeneration, Atlas.routeLegs).length; ++i) {
            var leg = (root.universeContentGeneration, Atlas.routeLegs)[i];
            var category = leg.route;
            if (leg.kind === "ascent" || leg.kind === "descent") category = "altitude";
            else if (leg.kind === "sea-train") category = "train";
            else if (leg.kind === "kuma") category = "kuma";
            else if (leg.kind === "return" || leg.kind === "reunion") category = "returns";
            if (category !== group) continue;
            var from = (root.universeContentGeneration, Atlas.location)(leg.from);
            var to = (root.universeContentGeneration, Atlas.location)(leg.to);
            if (!from || !to) continue;
            path += "M" + from.x * 1680 + " " + from.y * 960 + " C"
                    + leg.c1[0] * 1680 + " " + leg.c1[1] * 960 + " "
                    + leg.c2[0] * 1680 + " " + leg.c2[1] * 960 + " "
                    + to.x * 1680 + " " + to.y * 960 + " ";
        }
        return path;
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#e7d5aa" }
            GradientStop { position: 0.44; color: "#ddc69a" }
            GradientStop { position: 1; color: "#cbb17d" }
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeWidth: 0
            fillGradient: RadialGradient {
                centerX: 830; centerY: 470; centerRadius: 1000
                focalX: 830; focalY: 470
                GradientStop { position: 0.34; color: "#00744b2b" }
                GradientStop { position: 0.78; color: "#15744b2b" }
                GradientStop { position: 1; color: "#63744b2b" }
            }
            PathSvg { path: "M0 0 H1680 V960 H0 Z" }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#17755436"; strokeWidth: 0.8
            PathSvg { path: root.paperGrain() }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#1c877552"; strokeWidth: 0.7
            PathSvg { path: root.rhumbs() }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#24734e32"; strokeWidth: 1.2
            PathSvg { path: "M833 22 C820 272 847 653 835 938 M22 478 Q787 487 1658 474" }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#3af7eacb"; strokeWidth: 2
            PathSvg { path: "M830 22 C817 272 844 653 832 938 M22 480 Q787 489 1658 476" }
        }
        ShapePath {
            fillColor: "#09838b6d"; strokeColor: "#20675f44"; strokeWidth: 0.7
            PathSvg { path: "M112 274 Q850 289 1563 274 L1563 317 Q851 329 112 315 Z M105 746 Q846 732 1553 758 L1560 939 H105 Z" }
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "#976344"; strokeWidth: 1.6
            fillGradient: LinearGradient {
                x1: 24; y1: 0; x2: 168; y2: 0
                GradientStop { position: 0; color: "#ae795b" }
                GradientStop { position: 1; color: "#c7a078" }
            }
            PathSvg { path: root.leftContinent }
        }
        ShapePath {
            strokeColor: "#976344"; strokeWidth: 1.6
            fillGradient: LinearGradient {
                x1: 1510; y1: 0; x2: 1660; y2: 0
                GradientStop { position: 0; color: "#c7a078" }
                GradientStop { position: 1; color: "#ae795b" }
            }
            PathSvg { path: root.rightContinent }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#756a4730"; strokeWidth: 0.8
            PathSvg { path: root.cliffHatch() }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#ddc69a"; strokeWidth: 5
            PathSvg { path: "M68 474 L135 516 88 565 M73 550 L135 516 91 481 M135 516 L224 528" }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#7e776057"; strokeWidth: 1.2
            PathSvg { path: "M68 474 L135 516 88 565 M73 550 L135 516 91 481 M135 516 L224 528" }
        }
    }

    Text {
        x: 63; y: 176; width: 270; rotation: 90; transformOrigin: Item.TopLeft
        text: "R E D   L I N E"; color: "#795337"
        font.family: root.displayFamily; font.pixelSize: 25
    }
    Text {
        x: 1600; y: 397; width: 270; rotation: 90; transformOrigin: Item.TopLeft
        text: "R E D   L I N E"; color: "#795337"
        font.family: root.displayFamily; font.pixelSize: 25
    }

    Column {
        x: 204; y: 88; width: 330; spacing: 6
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "THE GRAND LINE"; color: "#90734d"; font.family: root.bodyFamily; font.pixelSize: 13; font.letterSpacing: 4 }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "PARADISE"; color: "#6b4c31"; font.family: root.displayFamily; font.pixelSize: 34; font.letterSpacing: 3 }
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 198; height: 1; color: "#667f5b37" }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "FIRST HALF OF THE GRAND LINE"; color: "#96764b"; font.family: root.bodyFamily; font.pixelSize: 10; font.letterSpacing: 1.2 }
    }
    Text { x: 1110; y: 186; width: 340; text: "C A L M   B E L T"; color: "#8a8062"; font.family: root.bodyFamily; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
    Text { x: 690; y: 727; width: 410; text: "C A L M   B E L T"; color: "#8a8062"; font.family: root.bodyFamily; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }

    // The sky plate is an altitude inset, not an island in the upper Calm Belt.
    Shape {
        x: 667; y: 103; width: 383; height: 196
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "#70e6dbb9"; strokeColor: "#40968c70"; strokeWidth: 1
            PathSvg { path: "M17 97 Q-5 74 29 58 Q19 32 57 29 Q81 5 109 23 Q130 2 159 20 Q183 0 212 20 Q240 8 268 26 Q312 10 333 44 Q375 44 365 81 Q397 106 369 127 Q376 161 335 162 Q306 187 273 171 Q250 198 216 178 Q184 196 157 179 Q127 190 105 173 Q73 190 48 164 Q9 164 20 134 Q-8 119 17 97 Z" }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#54968c70"; strokeWidth: 0.7
            PathSvg { path: "M29 91 Q53 73 83 89 M11 114 Q31 101 55 111 M298 143 Q322 131 352 143 M235 43 Q263 28 292 45 M55 138 Q84 123 104 137 M295 64 Q319 48 343 65" }
        }
    }

    // Fog has area and soft edges; it is not another island marker.
    Shape {
        x: 1240; y: 505; width: 250; height: 150
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeWidth: 0
            fillGradient: RadialGradient {
                centerX: 125; centerY: 75; centerRadius: 130
                focalX: 125; focalY: 75
                GradientStop { position: 0; color: "#295c6657" }
                GradientStop { position: 0.54; color: "#185c6657" }
                GradientStop { position: 1; color: "#005c6657" }
            }
            PathSvg { path: "M0 0 H250 V150 H0 Z" }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#20657260"; strokeWidth: 1
            PathSvg { path: "M18 61 Q71 43 116 58 T267 55 M32 76 Q82 58 131 76 T272 72 M10 112 Q64 93 129 110 T278 106 M26 133 Q79 120 123 134 T267 132" }
        }
    }

    // Fixed paths scale as a group; no continuously animated coast triangulation.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"; strokeColor: "#b28a5438"; strokeWidth: 1.7
            strokeStyle: ShapePath.DashLine; dashPattern: [5, 4]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("primary") }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#a2786446"; strokeWidth: 1.1
            strokeStyle: ShapePath.DashLine; dashPattern: [2, 4]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("altitude") }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#aa6e5e49"; strokeWidth: 2.3
            strokeStyle: ShapePath.DashLine; dashPattern: [1, 2]
            PathSvg { path: root.routeInk("train") }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#526c644b"; strokeWidth: 1.1
            strokeStyle: ShapePath.DashLine; dashPattern: [1, 4]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("secondary") }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#456c644b"; strokeWidth: 1.1
            strokeStyle: ShapePath.DashLine; dashPattern: [8, 4, 1, 4]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("kuma") }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#456c644b"; strokeWidth: 1.0
            strokeStyle: ShapePath.DashLine; dashPattern: [1, 7]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("returns") }
        }
    }

    Repeater {
        model: (root.universeContentGeneration, Atlas.locations)
        delegate: ParadiseLandform {
            required property var modelData
            visible: modelData.kind !== "region"
            x: modelData.x * 1680 - width / 2
            y: modelData.y * 960 - height / 2
            width: modelData.w; height: modelData.h
            kind: modelData.kind
            fineDetail: root.detailLevel >= 1.25
        }
    }
    Repeater {
        model: (root.universeContentGeneration, Atlas.locations)
        delegate: Column {
            required property var modelData
            visible: modelData.label.length > 0
            x: modelData.labelX * 1680 - width / 2
            y: modelData.labelY * 960
            width: modelData.minor ? 204 : 230
            spacing: 4
            Text {
                width: parent.width
                text: parent.modelData.label
                color: "#765336"
                font.family: root.displayFamily
                font.pixelSize: parent.modelData.size
                font.letterSpacing: parent.modelData.minor ? 0 : 0.5
                horizontalAlignment: Text.AlignHCenter
                style: Text.Outline; styleColor: "#ddc69a"
            }
            Text {
                visible: root.detailLevel >= 1.5 && parent.modelData.id !== "skypiea" && parent.modelData.detail.length > 0
                width: parent.width
                text: parent.modelData.detail
                color: "#84694a"
                font.family: root.bodyFamily
                font.italic: true; font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                style: Text.Outline; styleColor: "#ddc69a"
            }
        }
    }

    Text { x: 766; y: 109; width: 195; text: "WHITE-WHITE SEA"; color: "#8a7a5b"; font.family: root.bodyFamily; font.pixelSize: 10; font.letterSpacing: 1.5; horizontalAlignment: Text.AlignHCenter }
    Text { visible: root.detailLevel >= 1.25; x: 760; y: 263; width: 220; text: "Upper Yard, once part of Jaya"; color: "#8a7a5b"; font.family: root.bodyFamily; font.pixelSize: 11; font.italic: true; horizontalAlignment: Text.AlignHCenter }
    Text { visible: root.detailLevel >= 1.25; x: 710; y: 161; text: "Angel Island"; color: "#8a7a5b"; font.family: root.bodyFamily; font.pixelSize: 10; font.italic: true }
    Text { visible: root.detailLevel >= 1.25; x: 843; y: 230; text: "Shandora"; color: "#8a7a5b"; font.family: root.bodyFamily; font.pixelSize: 10; font.italic: true }
    Text { visible: root.detailLevel >= 1.25; x: 796; y: 335; text: "Knock Up Stream"; rotation: -66; color: "#967853"; font.family: root.bodyFamily; font.pixelSize: 11; font.italic: true }
    Text { visible: root.detailLevel >= 1.25; x: 932; y: 322; text: "White Sea"; color: "#8b7b5b"; font.family: root.bodyFamily; font.pixelSize: 11; font.italic: true }
    Text { x: 1240; y: 673; width: 260; text: "FLORIAN TRIANGLE"; color: "#857352"; font.family: root.displayFamily; font.pixelSize: 11; font.letterSpacing: 1.5; horizontalAlignment: Text.AlignHCenter }
    Text { visible: root.detailLevel >= 1.25; x: 1145; y: 516; text: "Sea train"; rotation: -64; color: "#8b7755"; font.family: root.bodyFamily; font.pixelSize: 10; font.italic: true }

    // Compass and key remain quiet secondary ink, outside the island chain.
    Item {
        x: 303; y: 765; width: 68; height: 68
        Repeater {
            model: 8
            delegate: Shape {
                required property int index
                anchors.fill: parent
                rotation: index * 45
                opacity: 0.68
                ShapePath {
                    fillColor: "#9b8054"; strokeColor: "#846542"; strokeWidth: 0.7
                    PathSvg { path: "M34 1 L39 29 34 34 29 29 Z" }
                }
            }
        }
        Rectangle { anchors.centerIn: parent; width: 17; height: 17; radius: 8.5; color: "#dbc494"; border.color: "#846542" }
        Text { anchors.horizontalCenter: parent.horizontalCenter; y: -19; text: "N"; color: "#846542"; font.family: root.bodyFamily; font.pixelSize: 13 }
    }
    Column {
        x: 216; y: 852; width: 534; spacing: 9
        Row {
            spacing: 14
            Text { text: "- - -"; color: "#8a5438"; font.family: root.bodyFamily; font.pixelSize: 13 }
            Text { text: "Straw Hat voyage"; color: "#846b49"; font.family: root.bodyFamily; font.pixelSize: 12 }
            Text { text: "· · ·"; color: "#6c644b"; font.family: root.bodyFamily; font.pixelSize: 13 }
            Text { text: "Luffy's detour / returns"; color: "#846b49"; font.family: root.bodyFamily; font.pixelSize: 12 }
        }
    }

    Rectangle { x: 19; y: 19; width: 1642; height: 922; color: "transparent"; border.width: 2; border.color: "#997446" }
    Rectangle { x: 25; y: 25; width: 1630; height: 910; color: "transparent"; border.width: 0.7; border.color: "#aa8e62" }
}
