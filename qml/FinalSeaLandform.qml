pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property string kind: "island"
    property color ink: "#74563c"
    property color wash: kind === "egghead" ? "#c6c5a8"
                         : kind === "adam-tree" ? "#b6b38b"
                         : kind === "underworld" ? "#cbc8b5"
                         : "#c8b58d"
    property bool fineDetail: true

    function coast(type) {
        switch (type) {
        case "wano-context":
            return "M5 59 L14 43 11 30 25 24 31 12 44 17 54 8 67 16 77 12 86 24 96 32 90 45 97 57 87 69 91 82 77 87 65 94 51 88 38 95 25 88 12 91 14 77 5 72 Z";
        case "egghead":
            return "M5 70 Q11 52 28 49 L30 39 42 35 43 21 52 13 62 18 64 35 76 39 78 51 Q93 54 97 70 L89 84 73 87 59 95 44 90 31 95 17 87 8 84 Z";
        case "adam-tree":
            return "M44 98 L45 72 40 72 38 60 28 60 23 53 8 52 12 43 30 43 34 36 43 36 44 25 37 22 39 14 46 15 50 3 55 15 63 14 65 22 57 25 57 36 66 36 71 43 91 43 94 52 77 53 72 60 62 60 59 72 55 72 56 98 Z";
        case "diorama":
            return "M8 80 L15 28 27 18 72 18 84 28 92 80 80 91 20 91 Z";
        case "underworld":
            return "M5 73 L17 57 24 64 31 40 40 54 49 24 60 53 69 38 78 60 86 49 96 72 87 86 69 84 55 94 40 88 24 92 11 84 Z";
        case "sun-world":
            return "M6 57 Q18 43 31 49 Q38 28 51 39 Q64 24 73 45 Q88 39 96 58 L87 72 69 70 55 82 39 76 22 82 9 72 Z";
        case "heaven-world":
            return "M7 61 Q18 45 32 51 Q41 31 54 44 Q68 29 77 49 Q91 47 96 62 Q88 78 69 74 Q54 89 40 79 Q24 86 12 74 Z";
        case "library":
            return "M10 81 L18 37 31 31 31 18 69 18 70 31 83 37 91 81 74 88 27 88 Z";
        case "school":
            return "M8 78 L18 50 31 43 32 26 47 18 61 28 71 42 85 48 93 78 79 88 22 88 Z";
        case "castle":
            return "M7 80 L16 64 17 38 29 38 31 24 42 24 44 11 57 11 59 24 70 24 73 38 84 38 86 64 96 80 83 89 18 89 Z";
        default:
            return "M7 66 L15 48 24 54 29 31 42 38 49 18 60 36 72 27 80 49 94 58 88 74 75 83 59 79 49 91 35 84 21 90 10 80 Z";
        }
    }

    function engraving(type) {
        switch (type) {
        case "wano-context":
            return "M30 48 L48 18 56 18 75 49 M39 34 L48 18 56 18 66 35 55 30 48 35 M20 58 Q34 50 43 57 M64 58 Q78 49 86 57 M50 54 C43 65 61 68 52 79";
        case "egghead":
            return "M13 70 Q49 91 90 70 M18 77 Q49 96 84 79 M30 51 L30 63 M39 47 L39 64 M66 48 L66 64 M76 52 L76 65 M40 40 Q51 32 63 40 M48 36 L48 25 55 19 62 25 62 36 M47 25 L55 18 64 25 M55 18 L55 8 M55 9 Q72 10 76 20 Q68 28 57 24 M22 67 L29 58 35 67 Z M66 68 L73 58 80 68 Z";
        case "adam-tree":
            return "M49 97 L49 29 M53 97 L54 28 M49 67 Q31 61 20 52 M53 65 Q70 58 84 50 M48 48 Q34 41 28 33 M55 45 Q67 38 74 31 M47 29 Q44 22 45 17 M54 29 Q60 22 61 17 M24 47 Q41 49 47 53 M56 52 Q72 49 86 50 M39 74 L31 86 M59 74 L69 87 M44 81 L41 96 M57 81 L61 96";
        case "diorama":
            return "M15 80 L85 80 M20 70 L80 70 M25 31 L75 31 M22 45 L78 45 M29 58 L72 58 M32 26 L39 20 M67 26 L61 20";
        case "underworld":
            return "M12 74 L27 51 37 70 M34 64 L50 29 62 67 M59 66 L71 45 86 72 M23 82 L29 72 M50 84 L55 70 M73 81 L78 72";
        case "sun-world":
            return "M14 58 Q31 47 45 58 M48 58 Q63 45 82 57 M27 52 L28 68 M66 51 L66 67 M20 70 Q50 82 86 70 M35 74 L40 67 M57 75 L61 68";
        case "heaven-world":
            return "M15 62 Q31 49 45 61 M49 61 Q65 47 83 60 M26 69 Q49 80 77 69 M42 45 L50 34 59 46 M50 34 L50 25";
        case "library":
            return "M20 39 L80 39 M24 52 L76 52 M27 65 L73 65 M34 22 L66 22 M43 18 L50 10 58 18 M35 73 L35 84 M49 72 L49 84 M63 73 L63 84";
        case "school":
            return "M22 52 L80 52 M27 64 L75 64 M34 43 L34 30 47 23 60 31 60 43 M42 31 L52 31 M31 72 L31 83 M50 70 L50 84 M69 72 L69 83";
        case "castle":
            return "M19 39 L82 39 M31 26 L70 26 M44 13 L58 13 M26 48 L26 72 M40 46 L40 73 M54 46 L54 73 M68 47 L68 72 M18 78 L85 78";
        default:
            return "M18 67 L31 43 42 65 M39 71 L50 31 61 68 M59 68 L72 43 84 69";
        }
    }

    Item {
        width: 100
        height: 100
        transform: Scale { xScale: root.width / 100; yScale: root.height / 100 }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"
                strokeColor: "#35836543"
                strokeWidth: 3.7
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: root.coast(root.kind) }
            }
            ShapePath {
                fillColor: root.wash
                strokeColor: root.ink
                strokeWidth: 1.1
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: root.coast(root.kind) }
            }
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.ink
                strokeWidth: root.fineDetail ? 0.85 : 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: root.engraving(root.kind) }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.kind === "egghead"
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"
                strokeColor: "#806a4a"
                strokeWidth: 0.8
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 2]
                PathSvg { path: "M20 48 C20 32 81 32 81 48 C81 66 20 66 20 48 Z M15 53 C15 27 86 27 86 53" }
            }
        }
    }
}
