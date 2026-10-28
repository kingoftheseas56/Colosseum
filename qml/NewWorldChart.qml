pragma ComponentBehavior: Bound
import QtQuick
import "UniverseContentApi.js" as UniverseContent
import QtQuick.Shapes
import "OnePieceNewWorldAtlasData.js" as Atlas
import "OnePieceNewWorldData.js" as Story
import "OnePieceNewWorldAdapter.js" as Adapter

Item {
    id: root
    readonly property int universeContentGeneration: typeof Content !== "undefined" ? (UniverseContent.reload(Content.generation), Content.generation) : 0
    width: 1680
    height: 960
    clip: true
    property string displayFamily: "Georgia"
    property string bodyFamily: "Georgia"
    property real detailLevel: 1
    readonly property string redLineCoast: "M22 22 H145 L136 58 158 87 142 116 165 153 143 187 155 222 135 257 159 292 145 327 164 362 145 393 158 425 143 458 164 494 145 529 162 565 138 601 155 637 138 671 157 707 139 741 152 779 133 815 151 851 134 886 145 937 H22 Z"

    function grain() {
        var result = ""
        for (var i = 0; i < 420; ++i) {
            var x = 30 + ((i * 239 + i * i * 3) % 1617)
            var y = 30 + ((i * 137 + i * i * 7) % 902)
            result += "M" + x + " " + y + "l" + (1 + i % 3) + " 0.4 "
        }
        return result
    }
    function rhumbs() {
        var result = ""
        var centers = [[538, 822], [1090, 315]]
        for (var c = 0; c < centers.length; ++c)
            for (var i = 0; i < 16; ++i) {
                var angle = Math.PI * i / 8
                result += "M" + centers[c][0] + " " + centers[c][1] + "l"
                        + (Math.cos(angle) * 1900).toFixed(2) + " "
                        + (Math.sin(angle) * 1900).toFixed(2) + " "
            }
        return result
    }
    function cliffs() {
        var result = ""
        for (var i = 0; i < 28; ++i) {
            var x = 48 + (i % 3) * 13
            var y = 52 + i * 31
            result += "M" + x + " " + y + "l12 -21 13 23 m-13 -23 l2 22 "
        }
        return result
    }
    function bridgeInk() {
        var forest = (root.universeContentGeneration, Atlas.location)("green-bit"), city = (root.universeContentGeneration, Atlas.location)("dressrosa")
        var x = forest.x * 1680, top = forest.y * 960 + forest.h / 2
        var bottom = city.y * 960 - city.h / 2
        var path = "M" + (x - 3) + " " + top + "V" + bottom + " M" + (x + 3) + " " + top + "V" + bottom
        for (var y = top + 6; y < bottom; y += 10)
            path += " M" + (x - 6) + " " + y + "h12"
        return path
    }
    function routeInk(group) {
        var result = ""
        for (var i = 0; i < (root.universeContentGeneration, Atlas.routeLegs).length; ++i) {
            var leg = (root.universeContentGeneration, Atlas.routeLegs)[i]
            var category = leg.route
            if (leg.kind === "ascent" || leg.kind === "descent" || leg.kind === "waterfall-ascent") category = "vertical"
            else if (leg.kind === "mirror-transit") category = "mirror"
            else if (leg.kind === "inland-journey" || leg.kind === "return") category = "inland"
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
            PathSvg { path: root.grain() }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#15877552"; strokeWidth: 0.7
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
            fillColor: "#08838b6d"; strokeColor: "#20675f44"; strokeWidth: 0.6
            PathSvg { path: "M150 68 Q850 74 1658 64 V99 Q852 106 150 98 Z M147 901 Q850 894 1658 902 V936 H147 Z" }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#258c7a53"; strokeWidth: 1
            PathSvg { path: "M233 386 Q260 369 283 386 T337 386 M247 396 Q274 380 296 396 T350 396 M951 379 Q978 364 1000 380 T1051 380 M966 391 Q993 375 1014 391 T1064 391 M718 743 Q742 730 764 744 T810 744" }
        }
    }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "#976344"; strokeWidth: 1.6
            fillGradient: LinearGradient {
                x1: 22; y1: 0; x2: 170; y2: 0
                GradientStop { position: 0; color: "#ae795b" }
                GradientStop { position: 1; color: "#c7a078" }
            }
            PathSvg { path: root.redLineCoast }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#756a4730"; strokeWidth: 0.8
            PathSvg { path: root.cliffs() }
        }
    }
    Text {
        x: 86; y: 276; width: 250; rotation: 90; transformOrigin: Item.TopLeft
        text: "RED LINE"; color: "#795337"
        font.family: root.displayFamily; font.pixelSize: 25; font.letterSpacing: 5
    }
    Column {
        x: 205; y: 90; width: 350; spacing: 6
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "THE GRAND LINE"; color: "#90734d"; font.family: root.bodyFamily; font.pixelSize: 13; font.letterSpacing: 4 }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "NEW WORLD"; color: "#6b4c31"; font.family: root.displayFamily; font.pixelSize: 34; font.letterSpacing: 3 }
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 234; height: 1; color: "#667f5b37" }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "FROM THE RED LINE TO WANO"; color: "#96764b"; font.family: root.bodyFamily; font.pixelSize: 10; font.letterSpacing: 1.2 }
    }
    Text { x: 735; y: 78; width: 300; text: "CALM BELT"; color: "#8a8062"; font.family: root.bodyFamily; font.pixelSize: 12; font.letterSpacing: 4; horizontalAlignment: Text.AlignHCenter }

    // Explicit depth section. Its rectangle is an inset footprint, not sea geography.
    Shape {
        x: 185; y: 606; width: 332; height: 247
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "#13a8b6a0"; strokeColor: "#30878a6d"; strokeWidth: 0.8
            PathSvg { path: "M3 22 Q27 8 71 17 T154 17 T240 17 T329 17 L326 222 Q286 237 243 227 T155 228 T70 228 T3 228 Z" }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#32808b73"; strokeWidth: 0.85
            PathSvg { path: "M13 42 Q33 33 48 43 T86 43 M190 47 Q207 37 224 47 T262 47 M30 204 Q55 194 75 205 T119 205 M211 207 Q235 196 254 207 T300 207" }
        }
    }
    Text { x: 182; y: 648; width: 315; text: "10,000 m beneath the Red Line"; color: "#7c775c"; font.family: root.bodyFamily; font.pixelSize: 11; font.italic: true; horizontalAlignment: Text.AlignHCenter }

    Repeater {
        model: (root.universeContentGeneration, Atlas.regions)
        delegate: Item {
            id: region
            required property var modelData
            x: modelData.x * 1680; y: modelData.y * 960
            width: modelData.width * 1680; height: modelData.height * 960
            Item {
                width: 100; height: 100
                transform: Scale { xScale: region.width / 100; yScale: region.height / 100 }
                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        fillColor: region.modelData.id === "wano-basin" ? "#087e957e" : "#088d7450"
                        strokeColor: region.modelData.id === "wano-basin" ? "#607f7459" : "#40897551"
                        strokeWidth: region.modelData.id === "wano-basin" ? 0.28 : 0.2
                        strokeStyle: region.modelData.id === "wano-basin" ? ShapePath.SolidLine : ShapePath.DashLine
                        dashPattern: [4, 5]
                        PathSvg {
                            path: region.modelData.id === "wano-basin"
                                ? "M4 23 Q0 14 17 9 L28 3 41 6 53 2 67 7 80 5 95 15 93 28 98 42 94 56 98 67 89 80 93 89 79 97 65 93 51 98 36 94 23 97 10 87 7 74 1 64 5 52 2 39 Z"
                                : "M5 28 C14 0 75 0 92 29 C114 65 94 94 60 97 C19 104 -4 76 5 28 Z"
                        }
                    }
                }
                Shape {
                    anchors.fill: parent
                    visible: region.modelData.id === "wano-basin" && root.detailLevel >= 1.25
                    ShapePath {
                        fillColor: "transparent"; strokeColor: "#7b8b734e"; strokeWidth: 0.35
                        PathSvg { path: "M8 20 L4 25 M13 14 L8 17 M23 9 L18 13 M39 7 L36 12 M57 6 L54 11 M77 9 L74 14 M88 17 L85 22 M93 35 L89 39 M94 52 L90 55 M93 69 L89 71 M86 82 L82 81 M77 92 L74 87 M60 94 L58 89 M42 93 L40 88 M25 91 L27 86 M13 82 L18 79 M9 64 L14 63 M7 44 L12 44" }
                    }
                }
            }
        }
    }

    // A few unlabelled islets imply the selected archipelago, not an exhaustive inventory.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "#c8b48a"; strokeColor: "#92714e"; strokeWidth: 0.9
            PathSvg { path: "M950 558 L960 546 976 554 972 569 958 572 Z M1140 564 L1152 557 1164 569 1157 582 1143 581 Z M1160 724 L1172 710 1185 721 1178 734 1165 734 Z M1020 800 L1032 790 1047 797 1042 811 1027 815 Z M907 682 L919 671 932 680 927 693 912 694 Z" }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#97764f"; strokeWidth: 0.8
            PathSvg { path: root.bridgeInk() }
        }
    }
    Text { x: 696; y: 556; text: "Iron bridge"; color: "#8b7652"; font.family: root.bodyFamily; font.pixelSize: 10; font.italic: true; visible: root.detailLevel >= 1.5 }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"; strokeColor: "#b28a5438"; strokeWidth: 1.8
            strokeStyle: ShapePath.DashLine; dashPattern: [5, 4]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("primary") }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#a2786446"; strokeWidth: 1.15
            strokeStyle: ShapePath.DashLine; dashPattern: [2, 4]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("vertical") }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#526c644b"; strokeWidth: 1.1
            strokeStyle: ShapePath.DashLine; dashPattern: [1, 5]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("secondary") }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#526d5860"; strokeWidth: 1.0
            strokeStyle: ShapePath.DashLine; dashPattern: [7, 4, 1, 4]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("mirror") }
        }
        ShapePath {
            fillColor: "transparent"; strokeColor: "#6f7f7355"; strokeWidth: 0.85
            strokeStyle: ShapePath.DashLine; dashPattern: [2, 5]
            PathSvg { path: "M760 338 Q785 362 820 346 T880 347" }
        }
    }
    Repeater {
        model: (root.universeContentGeneration, Atlas.locations)
        delegate: NewWorldLandform {
            required property var modelData
            objectName: "newWorldLandform-" + modelData.id
            visible: modelData.kind !== "region" && modelData.kind !== "boundary" && modelData.kind !== "waypoint"
            x: modelData.x * 1680 - width / 2
            y: modelData.y * 960 - height / 2
            width: modelData.w; height: modelData.h
            kind: modelData.kind
            fineDetail: root.detailLevel >= 1.25
        }
    }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"; strokeColor: "#6b8a6647"; strokeWidth: 1.15
            strokeStyle: ShapePath.DashLine; dashPattern: [2, 5]; capStyle: ShapePath.RoundCap
            PathSvg { path: root.routeInk("inland") }
        }
    }
    Repeater {
        model: (root.universeContentGeneration, Atlas.locations)
        delegate: Text {
            id: label
            required property var modelData
            objectName: "newWorldChartLabel-" + modelData.id
            visible: modelData.label.length > 0 && root.detailLevel >= (modelData.minZoom || 1)
            width: modelData.minor ? 180 : 286
            x: modelData.labelX * 1680 - width / 2
            y: modelData.labelY * 960
            text: modelData.label
            color: "#765336"
            font.family: root.displayFamily
            font.pixelSize: modelData.size
            font.letterSpacing: modelData.minor ? 0 : 0.5
            horizontalAlignment: Text.AlignHCenter
            style: Text.Outline; styleColor: "#ddc69a"
        }
    }
    Text { x: 718; y: 432; width: 261; text: "MOVING WITH ZUNESHA"; color: "#8d7450"; font.family: root.bodyFamily; font.pixelSize: 10; font.letterSpacing: 1.5; horizontalAlignment: Text.AlignHCenter }
    Text { visible: root.detailLevel >= 1.25; x: 1318; y: 288; width: 270; text: "CLOSED COUNTRY · RAISED INLAND SEA"; color: "#8e7350"; font.family: root.bodyFamily; font.pixelSize: 10; font.letterSpacing: 0.7; horizontalAlignment: Text.AlignHCenter }
    Text { visible: root.detailLevel >= 1.25; x: 1050; y: 803; text: "Mirror transit"; color: "#887161"; font.family: root.bodyFamily; font.pixelSize: 10; font.italic: true }
    Text { visible: root.detailLevel >= 1.25; x: 1080; y: 214; width: 171; text: "Advance party to Wano"; color: "#827557"; font.family: root.bodyFamily; font.pixelSize: 11; font.italic: true; rotation: 14 }
    Text { x: 1475; y: 424; text: "Mount Fuji"; visible: root.detailLevel >= 1.5; color: "#806849"; font.family: root.bodyFamily; font.pixelSize: 11; font.italic: true }
    Text { x: 1360; y: 532; text: "Flower Capital"; visible: root.detailLevel >= 1.5; color: "#806849"; font.family: root.bodyFamily; font.pixelSize: 11; font.italic: true }

    Item {
        x: 505; y: 789; width: 66; height: 66
        Repeater {
            model: 8
            delegate: Shape {
                required property int index
                anchors.fill: parent
                rotation: index * 45
                opacity: 0.68
                ShapePath {
                    fillColor: "#9b8054"; strokeColor: "#846542"; strokeWidth: 0.7
                    PathSvg { path: "M33 1 L38 28 33 33 28 28 Z" }
                }
            }
        }
        Rectangle { anchors.centerIn: parent; width: 16; height: 16; radius: 8; color: "#dbc494"; border.color: "#846542" }
        Text { anchors.horizontalCenter: parent.horizontalCenter; y: -18; text: "N"; color: "#846542"; font.family: root.bodyFamily; font.pixelSize: 12 }
    }
    Row {
        x: 595; y: 792; spacing: 10
        OnePieceRoundBadge {
            objectName: "newWorldReverieContextBadge"
            width: 84; height: 84
            source: Qt.resolvedUrl((root.universeContentGeneration, Adapter.contextArt)((root.universeContentGeneration, Story.worldContext)).badgeSource)
            Accessible.ignored: true
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 245
            text: (root.universeContentGeneration, Story.worldContext).title.toUpperCase()
            color: "#8b704b"; font.family: root.bodyFamily; font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }
    Column {
        x: 531; y: 872; width: 678; spacing: 7
        Text { text: "- - -   Luffy's voyage     · · ·   Parallel advance party     - · -   Mirror transit"; color: "#826947"; font.family: root.bodyFamily; font.pixelSize: 11 }
    }
    Rectangle { x: 19; y: 19; width: 1642; height: 922; color: "transparent"; border.width: 2; border.color: "#997446" }
    Rectangle { x: 25; y: 25; width: 1630; height: 910; color: "transparent"; border.width: 0.7; border.color: "#aa8e62" }
}
