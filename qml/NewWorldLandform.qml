pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property string kind: "forest"
    property color ink: "#74563c"
    property color wash: kind === "zunesha" ? "#b9b29a"
                         : kind === "whole-cake" || kind === "cacao" ? "#d5bd93"
                         : kind === "onigashima" ? "#c9c1a1"
                         : kind === "dressrosa" ? "#d3c197" : "#bdbb94"
    property bool fineDetail: true

    // Original, simplified 100 x 100 engravings. These identify places, not surveyed coastlines.
    function coast(type) {
        switch (type) {
        case "fishman": return "M5 71 Q13 66 23 72 L29 65 35 72 44 65 53 72 62 65 70 72 79 66 87 72 96 70 Q90 91 67 91 L55 96 37 91 22 94 10 86 Z";
        case "punk-hazard": return "M5 56 L11 43 8 31 22 28 26 15 39 21 48 17 Q43 34 48 45 Q53 61 45 74 L48 89 33 93 24 83 11 84 15 71 5 67 Z M57 17 L69 9 75 20 87 20 89 34 97 40 91 53 96 67 86 76 84 89 72 88 64 96 55 86 Q50 70 58 58 Q62 48 56 36 Z";
        case "dressrosa": return "M6 68 Q11 54 26 51 L29 30 39 30 43 18 53 18 55 7 60 7 64 19 72 19 76 31 81 49 Q96 55 97 68 L92 82 77 89 55 93 32 90 15 84 Z";
        case "zunesha": return "M28 32 C37 18 68 17 80 27 Q91 32 89 53 L90 90 Q85 96 80 91 L78 59 68 58 65 92 56 92 58 56 47 56 43 90 34 90 37 56 Q26 56 24 47 L20 49 20 75 Q20 94 10 92 L7 87 Q14 85 13 73 L12 47 Q8 39 15 30 Q21 24 28 32 Z M33 26 L34 18 40 18 40 12 46 12 48 4 54 4 58 13 65 12 68 19 76 19 78 27 Z";
        case "whole-cake": return "M4 80 Q10 70 18 76 L18 49 Q18 43 29 43 L29 31 Q29 24 40 24 L40 12 60 12 60 24 Q72 24 72 31 L72 43 Q84 43 84 49 L84 76 95 79 91 89 79 91 72 87 59 95 48 91 38 95 25 88 12 92 Z";
        case "cacao": return "M6 73 L16 64 19 45 Q19 31 33 29 L37 17 50 12 64 20 66 37 Q82 30 87 44 L88 62 96 72 88 84 73 87 61 92 46 87 31 91 13 83 Z";
        case "wano": return "M4 54 L12 40 9 28 22 22 28 10 43 14 52 7 66 13 76 9 84 22 96 29 90 43 97 53 88 64 92 78 77 83 72 93 59 88 45 96 34 88 18 93 16 80 6 75 11 64 Z";
        case "onigashima": return "M7 84 L15 70 22 67 Q15 54 22 37 Q10 29 6 6 Q22 25 34 23 Q50 15 65 23 Q80 19 93 3 Q92 29 80 39 Q88 53 79 67 L87 72 97 83 86 94 69 90 57 96 42 91 28 95 17 89 Z";
        case "waterfall": return "M10 85 L19 65 25 21 33 8 66 8 79 18 84 51 94 81 80 94 66 86 51 95 32 91 20 95 Z";
        case "mangrove": return "M8 84 L24 71 28 46 Q9 37 20 19 Q30 8 42 19 Q53 2 69 15 Q86 10 93 29 Q95 43 74 47 L74 74 91 86 76 91 63 76 64 48 53 41 43 50 40 76 50 91 35 94 28 82 15 93 Z";
        default: return "M7 69 L15 50 22 54 Q12 35 29 29 Q25 13 44 19 Q50 4 63 20 Q78 13 85 32 Q98 38 90 55 L97 72 82 83 68 81 55 91 40 85 24 92 10 82 Z";
        }
    }
    function engraving(type) {
        switch (type) {
        case "fishman": return "M11 70 Q49 81 91 70 M19 77 Q48 91 82 78 M24 65 L25 52 35 44 43 54 43 68 M48 69 L48 48 57 39 64 47 64 67 M71 67 L72 56 79 50 86 55 86 68 M28 55 L37 55 M51 50 L61 50 M29 62 L35 62 M52 59 L59 59 M43 30 L43 20 49 13 55 20 55 30 M39 22 L49 14 60 22 M37 30 L62 30 M46 30 L46 35 M54 30 L54 35 M15 61 L14 45 M14 55 L8 50 M14 50 L20 43 M89 64 L90 47 M90 54 L96 48";
        case "punk-hazard": return "M16 62 Q10 49 21 35 Q18 48 26 45 Q23 33 34 27 Q30 43 37 46 Q48 62 36 69 M18 75 L26 62 36 77 M22 80 L30 69 40 83 M17 33 L22 30 M29 20 L34 24 M62 70 L72 39 77 61 86 31 91 70 M59 80 L68 62 73 83 M77 40 L83 49 86 32 M62 25 L67 20 73 28 M66 26 L72 24 M61 85 L65 90 M80 79 L86 84 M52 20 Q47 37 52 48 T52 83";
        case "dressrosa": return "M8 68 Q48 91 95 68 M12 75 L15 81 M22 80 L25 85 M36 84 L37 89 M51 85 L52 91 M67 83 L68 88 M81 77 L82 84 M29 33 L75 33 M38 27 L68 27 M43 18 L66 18 M55 8 L61 8 M40 38 L40 52 M68 39 L68 53 M26 54 L34 46 43 54 42 63 26 63 Z M49 48 L56 41 63 48 63 62 49 62 Z M70 57 L77 51 85 58 84 64 71 64 Z M18 69 C16 59 39 57 43 67 C44 77 20 80 18 69 Z M20 67 Q30 60 41 67 M23 70 L23 73 M30 72 L30 76 M37 70 L37 74 M55 66 L55 76 M53 72 L62 72";
        case "zunesha": return "M28 32 Q46 24 42 42 Q40 57 29 51 Q22 43 28 32 Z M17 40 L20 40 M21 49 Q25 66 35 50 M15 48 L16 77 M38 58 L37 85 M42 61 L40 80 M61 60 L59 86 M82 57 L84 86 M49 30 L68 29 M50 37 L72 36 M51 45 L70 46 M86 35 Q98 41 96 58 M92 51 L96 58 M37 23 L74 23 M43 18 L44 13 M49 17 L51 9 M57 19 L61 14 M65 22 L68 17 M47 9 Q51 5 55 10";
        case "whole-cake": return "M19 49 Q50 63 84 49 M19 61 Q51 75 84 61 M19 73 Q50 87 83 73 M29 32 Q50 43 72 32 M29 42 Q51 53 72 42 M41 13 Q51 19 60 13 M41 23 Q52 29 60 23 M23 52 Q26 64 29 54 Q32 66 36 57 Q40 69 44 58 Q48 70 52 59 Q55 70 59 58 Q62 68 65 57 Q70 65 73 54 Q77 62 80 51 M46 68 L46 76 M56 69 L56 76 M34 35 L34 43 M63 35 L63 44 M45 12 L45 5 M54 13 L54 5 M43 5 Q45 0 47 5 M52 5 Q54 0 56 5 M27 80 L32 75 M70 82 L76 76";
        case "cacao": return "M18 46 Q34 52 47 40 M38 20 L49 14 61 21 M39 29 Q49 22 63 29 M41 36 Q53 29 65 36 M21 48 L22 65 M31 51 L32 66 M58 46 Q72 36 85 45 M57 54 Q71 47 86 53 M62 61 L63 67 M76 61 L77 68 M15 72 Q49 87 88 73 M29 77 L32 72 M47 82 L50 74 M70 79 L74 73";
        case "wano": return "M32 37 L49 12 55 12 72 39 Z M41 24 L49 12 55 12 63 26 55 22 49 26 Z M36 37 L45 28 M59 28 L66 38 M23 44 Q37 37 43 43 M67 44 Q80 36 87 43 M52 42 C44 52 63 58 53 67 S48 79 57 86 M17 66 L21 54 27 64 M73 69 L78 57 85 68 M29 59 L41 51 51 59 M31 60 L31 68 47 68 47 60 M28 69 L50 69 M32 70 L32 76 M44 70 L44 76 M64 55 L72 49 79 55 77 61 65 61 Z M23 79 L30 74 37 79 M66 79 L73 74 80 80 M31 82 L42 82 M36 86 L43 86 M62 35 L64 39";
        case "onigashima": return "M25 39 Q49 22 75 40 M25 40 Q28 31 36 31 M65 30 Q73 32 76 41 M26 49 Q36 40 43 49 Q38 60 27 56 Z M58 49 Q68 40 77 49 Q72 60 60 56 Z M47 57 L51 50 55 58 Z M35 66 L35 77 65 77 66 65 M41 66 L41 77 M49 65 L49 77 M57 66 L57 77 M23 34 Q13 23 9 12 M77 34 Q88 20 90 10 M39 84 L64 84 M41 89 L62 89 M44 82 L44 96 M59 82 L59 96 M17 75 L23 70 M80 78 L87 73";
        case "waterfall": return "M29 17 Q48 23 70 18 M34 23 C37 45 25 57 24 78 M43 24 C48 43 39 58 37 84 M52 25 C58 47 50 66 50 86 M62 24 C69 48 66 68 70 81 M30 10 L33 17 M65 9 L69 17 M12 86 Q23 80 32 86 T51 86 T73 87 T94 85 M70 62 Q77 51 82 60 Q77 70 70 62 L65 70 M21 40 L17 34 20 27 M79 30 L84 37";
        case "mangrove": return "M29 43 Q35 34 43 39 M45 20 Q47 31 40 34 M67 21 Q64 36 77 39 M34 49 L32 77 M68 48 L68 75 M42 81 L46 88 M74 77 L83 86 M14 13 A5 5 0 1 1 13 13 M85 56 A5 5 0 1 1 84 56";
        default: return "M23 64 L30 37 37 64 M42 69 L50 24 59 66 M65 67 L70 35 79 66 M25 45 L34 48 M45 40 L54 44 M66 48 L76 51 M15 77 Q27 70 37 78 M61 79 Q74 71 85 77";
        }
    }

    Item {
        width: 100; height: 100
        transform: Scale { xScale: root.width / 100; yScale: root.height / 100 }
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"; strokeColor: "#35836543"; strokeWidth: 3.7
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: root.coast(root.kind) }
            }
            ShapePath {
                fillColor: root.wash; strokeColor: root.ink; strokeWidth: 1.1
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: root.coast(root.kind) }
            }
        }
        Shape {
            anchors.fill: parent
            visible: root.kind === "punk-hazard"
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "#ceb08b"; strokeColor: root.ink; strokeWidth: 1.1
                PathSvg { path: "M5 56 L11 43 8 31 22 28 26 15 39 21 48 17 Q43 34 48 45 Q53 61 45 74 L48 89 33 93 24 83 11 84 15 71 5 67 Z" }
            }
            ShapePath {
                fillColor: "#ccd0b9"; strokeColor: root.ink; strokeWidth: 1.1
                PathSvg { path: "M57 17 L69 9 75 20 87 20 89 34 97 40 91 53 96 67 86 76 84 89 72 88 64 96 55 86 Q50 70 58 58 Q62 48 56 36 Z" }
            }
        }
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"; strokeColor: root.ink
                strokeWidth: root.fineDetail ? 0.85 : 0.7
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                PathSvg { path: root.engraving(root.kind) }
            }
        }
        Shape {
            anchors.fill: parent
            visible: root.kind === "fishman"
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"; strokeColor: "#846d755f"; strokeWidth: 0.8
                PathSvg { path: "M5 62 C5 18 95 18 95 62 C95 108 5 108 5 62 Z M1 62 C1 13 99 13 99 62 C99 111 1 111 1 62 Z M34 22 C34 1 65 1 65 22 C65 44 34 44 34 22 Z" }
            }
        }
    }
}
