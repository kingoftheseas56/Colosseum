pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property string kind: "wild"
    property color ink: "#74563c"
    property color wash: kind === "desert" ? "#d4b77f"
                         : kind === "ship" ? "#afa17e"
                         : kind === "prison" || kind === "fortress" || kind === "judicial" ? "#d9c7a2"
                         : kind === "drum" ? "#d8d6b6" : "#b8b58b"
    property bool fineDetail: true

    // Each silhouette is original chart-scale line work on a 100 x 100 plate.
    // These are identifying landmarks, not geographically exact coast surveys.
    function coast(type) {
        switch (type) {
        case "mountain": return "M12 94 L19 72 13 56 29 38 35 17 46 28 55 4 65 24 76 17 79 44 91 65 84 79 93 96 62 91 44 98 Z";
        case "cape": return "M5 86 Q14 70 32 76 L35 41 61 41 65 77 Q82 71 96 86 L81 92 64 89 50 96 32 90 17 94 Z";
        case "cactus": return "M5 73 Q4 62 20 59 L23 29 Q27 15 39 27 L42 51 47 48 48 17 Q53 3 65 17 L68 48 73 53 76 30 Q83 21 90 35 L91 64 97 72 88 83 65 89 51 84 34 93 19 85 7 87 Z";
        case "prehistoric": return "M6 69 L14 54 21 61 29 29 38 43 44 20 57 43 69 49 79 40 88 56 96 66 91 81 77 88 64 83 48 93 33 86 17 90 6 79 Z";
        case "drum": return "M6 84 L18 77 19 30 Q26 19 38 28 L39 70 44 62 44 15 Q56 4 66 16 L66 60 71 68 73 34 Q84 24 92 35 L91 80 99 86 84 91 65 86 51 94 37 88 17 94 Z";
        case "desert": return "M4 49 L13 35 10 24 25 17 31 6 44 13 58 8 67 18 83 20 85 32 96 40 88 55 96 67 80 74 74 87 57 85 49 97 33 89 17 91 19 78 7 72 12 58 Z";
        case "jaya": return "M9 30 L25 17 43 22 38 34 45 44 35 48 39 57 30 72 21 84 10 74 16 58 8 46 Z M53 20 L70 14 89 27 94 43 82 51 90 67 77 81 59 86 51 73 66 62 56 51 61 39 Z";
        case "sky": return "M18 38 L27 25 38 30 43 14 58 17 67 29 82 31 89 46 77 57 82 69 67 83 51 78 43 89 30 75 19 73 23 57 10 49 Z";
        case "ring": return "M5 42 Q13 25 23 34 L18 48 8 52 Z M32 18 L43 12 51 23 42 30 Z M67 22 L79 19 88 32 79 39 70 35 Z M83 51 L96 49 97 61 86 68 79 61 Z M62 78 L71 68 81 76 73 89 63 88 Z M31 79 L40 70 50 80 43 91 30 91 Z M7 66 L20 58 27 69 20 81 9 78 Z";
        case "water-city": return "M5 76 Q8 60 25 53 L26 36 40 36 40 24 60 24 61 35 74 36 76 53 Q95 60 97 77 L87 87 65 91 51 86 33 92 14 85 Z";
        case "judicial": return "M4 70 L25 57 26 42 35 42 37 29 44 29 45 9 54 9 57 29 65 29 68 42 78 42 79 58 97 71 82 81 67 76 51 88 34 80 18 84 Z";
        case "ship": return "M8 68 L17 69 18 51 26 51 27 41 34 47 39 31 45 43 48 11 51 4 54 11 56 43 65 29 69 47 77 41 80 52 86 52 86 68 95 64 Q100 87 74 96 L26 96 Q5 87 8 68 Z";
        case "platform": return "M7 54 L17 47 17 29 30 20 45 29 45 45 53 45 53 22 64 11 77 22 77 45 92 50 86 74 15 77 Z";
        case "mangrove": return "M6 80 Q17 69 21 47 Q4 39 13 24 Q23 6 38 21 Q42 2 58 7 Q78 7 79 23 Q98 18 97 36 Q97 48 80 50 L76 74 95 84 83 90 64 78 62 52 54 47 49 58 49 81 62 92 48 94 38 87 39 61 30 54 29 78 15 89 Z";
        case "fortress": return "M4 60 L12 53 15 37 30 37 33 24 43 24 44 12 58 12 60 24 68 24 74 37 89 37 93 60 Q97 85 73 94 L63 82 Q81 69 78 57 L67 51 34 51 22 58 Q20 72 39 82 L30 95 Q3 82 4 60 Z";
        case "kuja": return "M6 78 L16 68 16 55 28 42 32 18 41 26 48 6 58 14 65 36 76 43 82 66 96 78 91 89 73 84 55 94 38 88 20 94 Z";
        case "prison": return "M13 44 L17 22 30 22 31 14 44 14 46 4 56 4 58 14 69 14 71 22 84 22 88 44 Q55 55 13 44 Z";
        default: return "M5 57 L15 39 22 44 25 25 38 29 43 13 58 24 70 19 77 39 91 47 86 59 96 72 83 82 64 78 55 92 40 86 31 91 14 83 18 72 6 69 Z";
        }
    }

    function engraving(type) {
        switch (type) {
        case "mountain": return "M21 79 L36 34 43 54 56 14 65 43 73 32 84 75 M36 35 L39 74 M55 18 L57 88 M70 46 L64 77 M24 86 L31 72 M75 85 L79 76 M42 92 L48 77";
        case "cape": return "M34 41 L34 30 39 24 39 16 57 16 57 24 64 30 64 41 M31 30 L48 20 67 30 M37 45 L60 45 M37 57 L61 57 M36 67 L63 67 M45 77 L45 66 53 66 54 77 M44 32 L53 32 53 39 44 39 Z M71 67 Q85 55 98 62 M76 72 Q89 66 99 70 M45 16 L48 9 53 16";
        case "cactus": return "M23 31 Q30 38 39 30 M49 19 Q56 26 65 19 M77 36 Q82 41 89 36 M27 44 L31 44 M29 48 L33 48 M54 35 L58 35 M57 40 L61 40 M52 46 L56 46 M80 51 L84 51 M80 56 L85 56 M18 70 L25 64 33 72 M36 78 L47 68 57 77 M62 71 L70 65 79 74 M25 81 L28 74 M52 86 L56 80";
        case "drum": return "M19 31 Q27 37 38 30 M44 17 Q56 23 66 17 M73 35 Q84 41 92 35 M23 39 L24 71 M34 37 L34 66 M49 26 L48 62 M60 26 L61 57 M78 45 L77 69 M87 42 L86 74 M43 16 L49 9 49 3 53 3 53 8 57 5 61 10 67 16 M18 85 L26 79 31 84 M72 85 L79 79 85 86";
        case "desert": return "M49 14 C41 31 57 42 47 56 S43 77 48 92 M52 14 C44 31 60 42 50 56 S46 77 51 92 M16 53 Q28 43 39 51 M63 61 Q76 53 88 62 M17 71 L24 58 31 70 Z M26 71 L34 59 42 73 M59 31 L65 21 73 33 Z M63 72 L69 66 74 72 74 77 64 77 Z M24 33 L31 28 36 32 36 39 25 39 Z M66 45 L73 40 78 45 78 51 66 51 Z M27 80 Q34 76 39 81 M58 84 Q67 79 71 83";
        case "jaya": return "M13 33 Q25 23 37 29 M15 44 L24 34 31 43 M60 27 Q75 19 87 33 M70 33 L77 25 83 36 M14 65 L22 57 30 65 29 73 16 73 Z M71 63 L78 57 83 62 82 70 72 70 Z M25 38 L26 54 M76 36 L77 49";
        case "sky": return "M22 47 L31 40 41 47 39 58 24 59 Z M53 59 L62 47 71 58 Z M47 66 L69 66 M46 70 L72 70 M52 75 L67 75 M59 56 L60 69 M45 79 C51 59 35 43 52 17 M49 80 C55 59 40 44 55 18 M51 31 Q70 18 75 34 Q62 43 51 31 M46 44 Q28 30 24 44 Q31 55 46 44 M49 59 Q64 45 73 52 Q61 65 49 59";
        case "ring": return "M16 40 L14 34 M39 22 L41 17 M77 29 L80 24 M89 57 L91 53 M70 79 L73 74 M36 82 L39 76 M16 71 L20 64 M34 44 Q52 34 72 47 M34 49 Q53 39 71 51";
        case "water-city": return "M10 76 Q49 99 93 76 M15 67 Q49 86 88 67 M24 56 Q49 70 77 56 M29 44 Q50 54 72 44 M40 27 Q51 34 60 27 M44 23 L44 14 56 14 56 24 M42 14 L50 7 59 14 M50 32 L50 81 M47 33 L47 79 M31 59 L31 66 M38 62 L38 71 M60 63 L60 72 M70 59 L70 67 M20 70 L21 78 M80 70 L79 78 M36 37 L43 37 M59 37 L67 37";
        case "judicial": return "M17 69 L30 62 43 67 51 78 59 68 76 63 86 70 M28 47 L75 47 M36 35 L66 35 M45 12 L54 12 M49 10 L49 1 M50 2 L60 5 50 7 M40 52 L40 62 M50 52 L50 67 M61 52 L61 63 M6 78 L17 92 M17 83 L26 97 M87 80 L80 96 M69 83 L64 99 M36 88 L39 100 M54 91 L54 99";
        case "ship": return "M10 72 Q48 90 92 71 M15 80 Q50 96 87 80 M30 92 L75 92 M20 60 L34 59 M68 58 L83 60 M30 52 L71 52 M39 46 L63 46 M49 14 L35 33 48 29 M55 18 L72 38 55 34 M51 7 L51 69 M40 57 L40 67 M59 55 L59 69 M68 64 L68 73 M29 64 L29 74";
        case "platform": return "M8 55 L91 55 M17 63 L84 63 M20 76 L17 90 M42 75 L44 91 M64 75 L64 89 M82 75 L87 91 M16 29 L31 18 46 29 M51 22 L65 9 79 23 M22 35 L29 35 29 43 22 43 Z M59 27 L66 27 66 36 59 36 Z M70 72 L79 63";
        case "mangrove": return "M23 47 Q30 36 39 42 M38 21 Q42 31 35 36 M60 14 Q53 28 66 35 M80 26 Q72 33 80 44 M45 42 L45 82 M68 47 L69 74 M25 55 L23 77 M41 84 L32 95 M70 74 L76 91 M11 19 A5 5 0 1 1 10 19 M88 8 A4 4 0 1 1 87 8 M85 61 A6 6 0 1 1 84 61";
        case "fortress": return "M15 39 L88 39 M32 27 L70 27 M45 17 L58 17 M17 45 L21 45 M27 44 L31 44 M39 43 L43 43 M57 43 L61 43 M69 44 L73 44 M80 45 L84 45 M12 64 Q13 79 29 87 M87 64 Q87 78 73 87 M34 60 Q49 53 67 60 M37 64 Q50 59 63 64 M41 69 Q50 65 59 69";
        case "kuja": return "M25 59 L36 50 47 59 45 66 27 66 Z M56 57 L66 48 77 57 75 66 58 66 Z M31 44 L40 33 51 42 M42 31 L49 21 59 32 M39 70 L50 61 61 70 59 83 41 83 Z M37 71 L63 71 M17 82 Q27 76 34 81 M67 82 Q79 77 88 84 M29 63 L44 63 M60 63 L75 63";
        case "prison": return "M18 23 Q50 34 84 23 M14 36 Q51 48 88 36 M29 29 L29 40 M41 31 L41 43 M57 31 L57 43 M72 29 L72 41 M34 15 L67 15 M47 5 L55 5 M5 49 Q25 44 40 50 T98 49";
        default: return "M12 66 L25 46 35 62 M29 44 L44 24 55 45 M50 57 L63 33 78 60 M18 77 L26 63 35 76 M52 76 L62 58 72 77 M34 82 L44 62 53 82 M25 51 L25 76 M45 33 L44 60 M65 41 L64 66 M75 72 L84 61 89 76";
        }
    }

    Item {
        width: 100
        height: 100
        transform: Scale { xScale: root.width / 100; yScale: root.height / 100 }

        Shape {
            anchors.fill: parent
            antialiasing: true
            ShapePath {
                fillColor: "transparent"
                strokeColor: "#39836543"
                strokeWidth: 4
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
            visible: root.kind === "prison"
            opacity: 0.55
            ShapePath {
                fillColor: "transparent"
                strokeColor: root.ink
                strokeWidth: 0.9
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 3]
                PathSvg { path: "M23 52 L23 65 30 65 30 78 37 78 37 91 63 91 63 78 71 78 71 65 78 65 78 52 M24 61 L77 61 M31 73 L70 73 M38 86 L62 86" }
            }
        }
    }
}
