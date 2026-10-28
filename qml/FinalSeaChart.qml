pragma ComponentBehavior: Bound
import QtQuick
import "UniverseContentApi.js" as UniverseContent
import QtQuick.Shapes
import "OnePieceFinalSeaAtlasData.js" as Atlas

Item {
    id: root
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? (UniverseContent.reload(Content.generation), Content.generation) : 0
    width: 1680
    height: 960
    clip: true

    property string displayFamily: "Georgia"
    property string bodyFamily: "Georgia"
    property real detailLevel: 1

    function grain() {
        var result = ""
        for (var i = 0; i < 430; ++i) {
            var x = 30 + ((i * 239 + i * i * 3) % 1617)
            var y = 30 + ((i * 137 + i * i * 7) % 902)
            result += "M" + x + " " + y + "l" + (1 + i % 3) + " 0.4 "
        }
        return result
    }

    function rhumbs() {
        var result = ""
        var centers = [[292, 770], [915, 330]]
        for (var c = 0; c < centers.length; ++c) {
            for (var i = 0; i < 16; ++i) {
                var angle = Math.PI * i / 8
                result += "M" + centers[c][0] + " " + centers[c][1] + "l"
                        + (Math.cos(angle) * 1900).toFixed(2) + " "
                        + (Math.sin(angle) * 1900).toFixed(2) + " "
            }
        }
        return result
    }

    function routeInk(group) {
        var result = ""
        for (var i = 0; i < (root.universeContentGeneration, Atlas.routeLegs).length; ++i) {
            var leg = (root.universeContentGeneration, Atlas.routeLegs)[i]
            var category = leg.route
            if (leg.kind === "ascent") category = "vertical"
            if (category !== group) continue
            var a = (root.universeContentGeneration, Atlas.location)(leg.from)
            var b = (root.universeContentGeneration, Atlas.location)(leg.to)
            if (!a || !b) continue
            result += "M" + a.x * 1680 + " " + a.y * 960 + " C"
                    + leg.c1[0] * 1680 + " " + leg.c1[1] * 960 + " "
                    + leg.c2[0] * 1680 + " " + leg.c2[1] * 960 + " "
                    + b.x * 1680 + " " + b.y * 960 + " "
        }
        return result
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#e7d5aa" }
            GradientStop { position: 0.48; color: "#ddc69a" }
            GradientStop { position: 1; color: "#cbb17d" }
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: 0
            fillGradient: RadialGradient {
                centerX: 800
                centerY: 450
                centerRadius: 1020
                focalX: 800
                focalY: 450
                GradientStop { position: 0.35; color: "#00744b2b" }
                GradientStop { position: 0.80; color: "#14744b2b" }
                GradientStop { position: 1; color: "#5e744b2b" }
            }
            PathSvg { path: "M0 0 H1680 V960 H0 Z" }
        }

        ShapePath {
            fillColor: "transparent"
            strokeColor: "#17755436"
            strokeWidth: 0.8
            PathSvg { path: root.grain() }
        }

        ShapePath {
            fillColor: "transparent"
            strokeColor: "#15877552"
            strokeWidth: 0.7
            PathSvg { path: root.rhumbs() }
        }

        ShapePath {
            fillColor: "transparent"
            strokeColor: "#24734e32"
            strokeWidth: 1.1
            PathSvg { path: "M830 22 C817 270 844 652 832 938 M22 480 Q790 487 1658 476" }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: "#38f7eacb"
            strokeWidth: 2
            PathSvg { path: "M833 22 C820 270 847 652 835 938 M22 478 Q790 485 1658 474" }
        }

        // Winter currents and volatile seas, kept faint enough to preserve breathing room.
        ShapePath {
            fillColor: "transparent"
            strokeColor: "#3d7f8467"
            strokeWidth: 0.8
            PathSvg { path: "M86 384 Q116 367 142 384 T201 384 M132 397 Q164 378 191 397 T251 397 M510 662 Q543 646 575 663 T638 663 M566 679 Q601 661 631 679 T697 679 M865 326 Q900 307 932 326 T997 326" }
        }
    }

    // Product title. "Final Sea" is a Colosseum chart label, not a canon ocean name.
    Column {
        x: 176
        y: 102
        width: 430
        spacing: 8
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "THE GRAND LINE"
            color: "#90734d"
            font.family: root.bodyFamily
            font.pixelSize: 13
            font.letterSpacing: 4
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "THE FINAL SEA"
            color: "#6b4c31"
            font.family: root.displayFamily
            font.pixelSize: 45
            font.letterSpacing: 3
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 270
            height: 1
            color: "#667f5b37"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "COLOSSEUM ATLAS · POST-WANO VOYAGE"
            color: "#96764b"
            font.family: root.bodyFamily
            font.pixelSize: 10
            font.letterSpacing: 1.2
        }
    }

    // Egghead Island Cloud / Labophase plate.
    Shape {
        x: 382
        y: 300
        width: 290
        height: 202
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "#35ddd7b8"
            strokeColor: "#62958b6f"
            strokeWidth: 0.9
            PathSvg {
                path: "M10 111 Q-4 88 26 76 Q20 48 54 48 Q72 20 101 37 Q128 8 158 34 Q187 17 209 43 Q246 27 260 59 Q294 65 280 94 Q301 120 273 138 Q278 169 242 170 Q215 192 189 175 Q165 195 138 178 Q108 194 85 177 Q54 190 34 166 Q0 164 12 138 Q-7 126 10 111 Z"
            }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: "#4e857c65"
            strokeWidth: 0.7
            PathSvg {
                path: "M25 99 Q49 84 75 99 M34 128 Q63 112 91 128 M189 76 Q215 61 240 76 M202 142 Q226 129 251 142"
            }
        }
    }

    // Labophase security rings.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"
            strokeColor: "#7d76524f"
            strokeWidth: 1.0
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            PathSvg { path: "M455 474 C455 389 610 389 610 474 C610 557 455 557 455 474 Z M468 474 C468 410 598 410 598 474 C598 538 468 538 468 474 Z" }
        }
    }

    Text {
        x: 465
        y: 380
        width: 140
        text: "LABOPHASE"
        color: "#846b4a"
        font.family: root.bodyFamily
        font.pixelSize: 10
        font.letterSpacing: 1.3
        horizontalAlignment: Text.AlignHCenter
    }
    Text {
        x: 456
        y: 528
        width: 160
        text: "FABIRIOPHASE"
        color: "#846b4a"
        font.family: root.bodyFamily
        font.pixelSize: 10
        font.letterSpacing: 1.1
        horizontalAlignment: Text.AlignHCenter
    }
    Text {
        x: 463
        y: 505
        width: 150
        text: "FRONTIER DOME"
        visible: root.detailLevel >= 1.25
        color: "#8b7350"
        font.family: root.bodyFamily
        font.pixelSize: 9
        horizontalAlignment: Text.AlignHCenter
    }

    // Strange mist on the known route between Egghead and Elbaf.
    Shape {
        x: 650
        y: 334
        width: 302
        height: 222
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeWidth: 0
            fillGradient: RadialGradient {
                centerX: 151
                centerY: 111
                centerRadius: 162
                focalX: 151
                focalY: 111
                GradientStop { position: 0; color: "#4a7b8175" }
                GradientStop { position: 0.62; color: "#247b8175" }
                GradientStop { position: 1; color: "#007b8175" }
            }
            PathSvg { path: "M0 0 H302 V222 H0 Z" }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: "#3e687263"
            strokeWidth: 0.9
            PathSvg {
                path: "M15 75 Q57 54 99 73 T201 70 T289 74 M28 96 Q73 76 115 95 T220 93 T281 96 M10 131 Q56 109 103 130 T208 126 T293 132 M34 155 Q78 136 121 154 T229 151"
            }
        }
    }

    Text {
        x: 696
        y: 523
        width: 220
        text: "SLEEPING MIST"
        color: "#807458"
        font.family: root.bodyFamily
        font.pixelSize: 11
        font.letterSpacing: 1.8
        horizontalAlignment: Text.AlignHCenter
    }
    Text {
        x: 687
        y: 541
        width: 238
        text: "route atmosphere · placement interpretive"
        color: "#8a7959"
        font.family: root.bodyFamily
        font.pixelSize: 9
        font.italic: true
        horizontalAlignment: Text.AlignHCenter
    }

    // Elbaf vertical tier plates behind Adam.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: "#16839a88"
            strokeColor: "#46857c68"
            strokeWidth: 0.8
            PathSvg {
                path: "M1018 683 Q1088 642 1168 663 T1341 655 T1514 682 L1518 874 Q1413 906 1302 883 T1116 889 T1007 860 Z"
            }
        }
        ShapePath {
            fillColor: "#1aa3a87f"
            strokeColor: "#4a8f896a"
            strokeWidth: 0.7
            PathSvg {
                path: "M1004 377 Q1085 339 1167 363 T1329 355 T1517 383 L1493 574 Q1404 598 1310 578 T1130 587 T1021 561 Z"
            }
        }
        ShapePath {
            fillColor: "#16d7d2b7"
            strokeColor: "#4a918c68"
            strokeWidth: 0.6
            PathSvg {
                path: "M1091 176 Q1150 148 1216 169 T1347 164 T1457 181 Q1481 213 1450 235 Q1385 255 1319 238 T1184 245 T1098 230 Q1072 207 1091 176 Z"
            }
        }
    }

    Text {
        x: 1070
        y: 705
        width: 420
        text: "FIRST WORLD · UNDERWORLD"
        color: "#7d6d50"
        font.family: root.bodyFamily
        font.pixelSize: 11
        font.letterSpacing: 1.4
        horizontalAlignment: Text.AlignHCenter
    }
    Text {
        x: 1082
        y: 389
        width: 400
        text: "SECOND WORLD · SUN WORLD"
        color: "#7d6d50"
        font.family: root.bodyFamily
        font.pixelSize: 11
        font.letterSpacing: 1.4
        horizontalAlignment: Text.AlignHCenter
    }
    Text {
        x: 1120
        y: 189
        width: 320
        text: "THIRD WORLD · HEAVEN / ASTRAL"
        color: "#7d6d50"
        font.family: root.bodyFamily
        font.pixelSize: 10
        font.letterSpacing: 1.0
        horizontalAlignment: Text.AlignHCenter
    }

    // Voyage lines sit beneath landmarks and seals.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"
            strokeColor: "#b28a5438"
            strokeWidth: 1.8
            strokeStyle: ShapePath.DashLine
            dashPattern: [5, 4]
            capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("primary") }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: "#6f786151"
            strokeWidth: 1.15
            strokeStyle: ShapePath.DashLine
            dashPattern: [2, 5]
            capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("internal") }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: "#7c6e5a55"
            strokeWidth: 1.1
            strokeStyle: ShapePath.DashLine
            dashPattern: [2, 4]
            capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("vertical") }
        }
    }

    // Major geographic forms.
    FinalSeaLandform {
        x: 110
        y: 518
        width: 108
        height: 92
        kind: "wano-context"
        fineDetail: root.detailLevel >= 1.25
    }

    FinalSeaLandform {
        x: 385
        y: 354
        width: 296
        height: 280
        kind: "egghead"
        fineDetail: root.detailLevel >= 1.25
    }

    FinalSeaLandform {
        x: 997
        y: 114
        width: 530
        height: 760
        kind: "adam-tree"
        fineDetail: root.detailLevel >= 1.25
    }

    // Supporting Elbaf landmarks.
    Repeater {
        model: [
            (root.universeContentGeneration, Atlas.location)("land-of-gods"),
            (root.universeContentGeneration, Atlas.location)("elbaf-underworld"),
            (root.universeContentGeneration, Atlas.location)("owl-library"),
            (root.universeContentGeneration, Atlas.location)("walrus-school"),
            (root.universeContentGeneration, Atlas.location)("aurust-castle")
        ]
        delegate: FinalSeaLandform {
            required property var modelData
            visible: modelData !== null && root.detailLevel >= (modelData.minZoom || 1)
            x: modelData.x * 1680 - width / 2
            y: modelData.y * 960 - height / 2
            width: modelData.w
            height: modelData.h
            kind: modelData.kind
            fineDetail: root.detailLevel >= 1.5
        }
    }

    // Location labels.
    Repeater {
        model: (root.universeContentGeneration, Atlas.locations)
        delegate: Text {
            required property var modelData
            visible: modelData.label.length > 0
                     && modelData.id !== "egghead"
                     && modelData.id !== "elbaf"
                     && modelData.id !== "fabiriophase"
                     && modelData.id !== "labophase"
                     && modelData.id !== "frontier-dome"
                     && modelData.id !== "sleeping-mist"
                     && modelData.id !== "elbaf-underworld"
                     && modelData.id !== "sun-world"
                     && modelData.id !== "heaven-world"
                     && root.detailLevel >= (modelData.minZoom || 1)
            width: modelData.minor ? 170 : 230
            x: modelData.labelX * 1680 - width / 2
            y: modelData.labelY * 960
            text: modelData.label
            color: "#765336"
            font.family: root.displayFamily
            font.pixelSize: modelData.size
            horizontalAlignment: Text.AlignHCenter
            style: Text.Outline
            styleColor: "#ddc69a"
        }
    }

    Text {
        x: 407
        y: 658
        width: 250
        text: "EGGHEAD"
        color: "#765336"
        font.family: root.displayFamily
        font.pixelSize: 29
        font.letterSpacing: 1.2
        horizontalAlignment: Text.AlignHCenter
        style: Text.Outline
        styleColor: "#ddc69a"
    }
    Text {
        x: 1044
        y: 83
        width: 450
        text: "ELBAF"
        color: "#765336"
        font.family: root.displayFamily
        font.pixelSize: 36
        font.letterSpacing: 3
        horizontalAlignment: Text.AlignHCenter
        style: Text.Outline
        styleColor: "#ddc69a"
    }
    Text {
        x: 1081
        y: 123
        width: 380
        text: "WARLAND KINGDOM · JEWEL TREE ADAM"
        color: "#8c714d"
        font.family: root.bodyFamily
        font.pixelSize: 10
        font.letterSpacing: 1.1
        horizontalAlignment: Text.AlignHCenter
    }

    // High-zoom internal labels.
    Text {
        x: 1105
        y: 479
        text: "Owl Library"
        visible: root.detailLevel >= 1.25
        color: "#806849"
        font.family: root.bodyFamily
        font.pixelSize: 10
        font.italic: true
    }
    Text {
        x: 1194
        y: 516
        text: "Walrus School"
        visible: root.detailLevel >= 1.25
        color: "#806849"
        font.family: root.bodyFamily
        font.pixelSize: 10
        font.italic: true
    }
    Text {
        x: 1374
        y: 431
        text: "Aurust Castle"
        visible: root.detailLevel >= 1.25
        color: "#806849"
        font.family: root.bodyFamily
        font.pixelSize: 10
        font.italic: true
    }

    // Compass and legend.
    Item {
        x: 257
        y: 773
        width: 68
        height: 68
        Repeater {
            model: 8
            delegate: Shape {
                required property int index
                anchors.fill: parent
                rotation: index * 45
                opacity: 0.68
                ShapePath {
                    fillColor: "#9b8054"
                    strokeColor: "#846542"
                    strokeWidth: 0.7
                    PathSvg { path: "M34 1 L39 29 34 34 29 29 Z" }
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 17
            height: 17
            radius: 8.5
            color: "#dbc494"
            border.color: "#846542"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: -19
            text: "N"
            color: "#846542"
            font.family: root.bodyFamily
            font.pixelSize: 13
        }
    }

    Column {
        x: 347
        y: 805
        width: 520
        spacing: 7
        Text {
            text: "- - -   Straw Hat voyage       · · ·   Elbaf internal movement"
            color: "#826947"
            font.family: root.bodyFamily
            font.pixelSize: 12
        }
        Text {
            text: "Interpretive chart · not to scale · both islands remain canonically in the New World"
            color: "#907650"
            font.family: root.bodyFamily
            font.pixelSize: 10
            font.italic: true
        }
    }

    Rectangle {
        x: 1305
        y: 830
        width: 255
        height: 68
        color: "#24f0dfb4"
        border.color: "#93754d"
        border.width: 1
        Text {
            anchors.centerIn: parent
            width: parent.width - 20
            text: "ONGOING VOYAGE\nELBAF · OPEN-ENDED"
            color: "#6d5234"
            font.family: root.displayFamily
            font.pixelSize: 13
            font.letterSpacing: 0.7
            horizontalAlignment: Text.AlignHCenter
            lineHeight: 1.15
        }
    }

    Rectangle {
        x: 19
        y: 19
        width: 1642
        height: 922
        color: "transparent"
        border.width: 2
        border.color: "#997446"
    }
    Rectangle {
        x: 25
        y: 25
        width: 1630
        height: 910
        color: "transparent"
        border.width: 0.7
        border.color: "#aa8e62"
    }
}
