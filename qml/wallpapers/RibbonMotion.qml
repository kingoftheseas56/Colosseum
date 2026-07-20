// RibbonMotion — a still, QML-drawn wallpaper in the Windows 11 "Captured Motion"
// spirit: a near-black field, a signature arc, and translucent folded ribbons in
// warm iridescent gradients. Pure Qt Quick Shapes (no bitmap, no shader, no
// animation) so it renders identically on any backend and costs nothing at rest.
// `variant` (0..2) picks a palette+composition; everything else is shared geometry.
import QtQuick
import QtQuick.Shapes

Item {
    id: root
    anchors.fill: parent

    // still wallpaper — declared for shelf-contract parity with the living scenes;
    // nothing animates, so there is nothing to freeze.
    property bool running: true
    property int variant: 0

    readonly property int v: Math.max(0, Math.min(2, variant))
    readonly property string bloom: v === 1 ? "#241640" : v === 2 ? "#3a1c10" : "#3c1421"

    // one ribbon's outline: sample the centreline cubic, offset by a tapered
    // half-width on each side (edge=1 hugs one side, for the thin sheen slivers).
    function ribbonOutline(p0, p1, p2, p3, w, prof, edge) {
        var N = 56, left = [], right = [];
        for (var i = 0; i <= N; i++) {
            var t = i / N, u = 1 - t;
            var bx = u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x;
            var by = u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y;
            var dx = 3*u*u*(p1.x-p0.x) + 6*u*t*(p2.x-p1.x) + 3*t*t*(p3.x-p2.x);
            var dy = 3*u*u*(p1.y-p0.y) + 6*u*t*(p2.y-p1.y) + 3*t*t*(p3.y-p2.y);
            var len = Math.hypot(dx, dy) || 1;
            var nx = -dy/len, ny = dx/len;
            var hw = 0.5 * w * Math.pow(Math.sin(Math.PI*t), prof);
            var lo = (edge === 1) ? (0.55*hw) : hw;
            left.push(Qt.point(bx + nx*lo, by + ny*hw));
            right.push(Qt.point(bx - nx*hw, by - ny*hw));
        }
        return left.concat(right.reverse());
    }

    // palette per variant: [arc, violet, magenta, crimson, orange, amber, irisTube, irisSliver]
    readonly property var pal: [
        [ ["#7a2a10","#ff7a24","#ffc86a"], ["#361356","#7a34c0","#b25fe0"], ["#7a1240","#e0356b","#ff86a6"],
          ["#7c1522","#f04a3a","#ff9a6a"], ["#a3400f","#ff7a24","#ffc76a"], ["#c86418","#ffb046","#ffe6b0"],
          ["#46d6e0","#c56fe0","#ffcf6a"], ["#5fe0d4","#c56fe0","#ffcf6a"] ],
        [ ["#2a1550","#6a3ad0","#a880f0"], ["#1c1048","#4a54c8","#8390ea"], ["#5a124f","#d0409a","#ff90cd"],
          ["#6a1546","#e8407a","#ff9ac6"], ["#7a2a86","#c064d8","#ff9ae0"], ["#8a4fb0","#c9a0ff","#eadcff"],
          ["#46e0d0","#6f8fe8","#c56fe0"], ["#5fe0d4","#8f7fe8","#e08fd8"] ],
        [ ["#6a2a08","#ff9a24","#ffd06a"], ["#3a0e20","#a01f3a","#e0607a"], ["#7a1220","#e0352a","#ff7a5a"],
          ["#7c2410","#ff6a1e","#ffb056"], ["#a3400f","#ff7a24","#ffc76a"], ["#c86418","#ffc046","#ffe6b0"],
          ["#ffcf6a","#ff8fb0","#c56fe0"], ["#ffb46a","#ff7f8a","#c56fe0"] ]
    ]

    readonly property var specSets: [
        [
            { p0:{x:360,y:650}, p1:{x:540,y:110}, p2:{x:1080,y:170}, p3:{x:1180,y:560}, w:120, prof:0.55, op:0.42, gi:0 },
            { p0:{x:520,y:560}, p1:{x:840,y:640}, p2:{x:980,y:300}, p3:{x:1160,y:360}, w:150, prof:0.6, op:0.42, gi:1 },
            { p0:{x:560,y:560}, p1:{x:860,y:300}, p2:{x:1000,y:560}, p3:{x:1190,y:420}, w:118, prof:0.6, op:0.58, gi:2 },
            { p0:{x:600,y:610}, p1:{x:900,y:360}, p2:{x:1030,y:600}, p3:{x:1205,y:470}, w:98, prof:0.6, op:0.66, gi:3 },
            { p0:{x:640,y:520}, p1:{x:880,y:315}, p2:{x:1025,y:470}, p3:{x:1165,y:360}, w:86, prof:0.6, op:0.66, gi:4 },
            { p0:{x:700,y:470}, p1:{x:905,y:340}, p2:{x:1015,y:445}, p3:{x:1145,y:380}, w:58, prof:0.6, op:0.8, gi:5 },
            { p0:{x:1050,y:280}, p1:{x:1150,y:380}, p2:{x:1075,y:540}, p3:{x:1180,y:620}, w:44, prof:0.7, op:0.55, gi:6 },
            { p0:{x:560,y:520}, p1:{x:820,y:380}, p2:{x:1030,y:440}, p3:{x:1180,y:355}, w:24, prof:0.6, op:0.6, gi:7 }
        ],
        [
            { p0:{x:360,y:600}, p1:{x:560,y:120}, p2:{x:1100,y:200}, p3:{x:1160,y:520}, w:126, prof:0.55, op:0.4, gi:0 },
            { p0:{x:520,y:600}, p1:{x:840,y:660}, p2:{x:1000,y:320}, p3:{x:1170,y:380}, w:150, prof:0.6, op:0.44, gi:1 },
            { p0:{x:560,y:580}, p1:{x:860,y:320}, p2:{x:1010,y:560}, p3:{x:1195,y:430}, w:120, prof:0.6, op:0.56, gi:2 },
            { p0:{x:600,y:630}, p1:{x:900,y:380}, p2:{x:1035,y:600}, p3:{x:1205,y:490}, w:96, prof:0.6, op:0.62, gi:3 },
            { p0:{x:650,y:540}, p1:{x:885,y:330}, p2:{x:1030,y:480}, p3:{x:1165,y:380}, w:84, prof:0.6, op:0.62, gi:4 },
            { p0:{x:705,y:485}, p1:{x:905,y:350}, p2:{x:1018,y:455}, p3:{x:1150,y:395}, w:56, prof:0.6, op:0.72, gi:5 },
            { p0:{x:1060,y:300}, p1:{x:1160,y:400}, p2:{x:1085,y:555}, p3:{x:1185,y:630}, w:42, prof:0.7, op:0.5, gi:6 },
            { p0:{x:560,y:540}, p1:{x:820,y:400}, p2:{x:1030,y:455}, p3:{x:1185,y:370}, w:24, prof:0.6, op:0.55, gi:7 }
        ],
        [
            { p0:{x:360,y:660}, p1:{x:520,y:180}, p2:{x:1080,y:210}, p3:{x:1190,y:600}, w:116, prof:0.55, op:0.42, gi:0 },
            { p0:{x:520,y:540}, p1:{x:840,y:620}, p2:{x:980,y:320}, p3:{x:1160,y:380}, w:146, prof:0.6, op:0.42, gi:1 },
            { p0:{x:560,y:550}, p1:{x:860,y:330}, p2:{x:1000,y:540}, p3:{x:1190,y:430}, w:120, prof:0.6, op:0.56, gi:2 },
            { p0:{x:600,y:600}, p1:{x:900,y:380}, p2:{x:1030,y:580}, p3:{x:1205,y:470}, w:100, prof:0.6, op:0.64, gi:3 },
            { p0:{x:640,y:520}, p1:{x:880,y:325}, p2:{x:1025,y:465}, p3:{x:1165,y:365}, w:88, prof:0.6, op:0.66, gi:4 },
            { p0:{x:700,y:472}, p1:{x:905,y:345}, p2:{x:1015,y:448}, p3:{x:1145,y:385}, w:58, prof:0.6, op:0.8, gi:5 },
            { p0:{x:1050,y:290}, p1:{x:1150,y:390}, p2:{x:1075,y:545}, p3:{x:1180,y:620}, w:44, prof:0.7, op:0.55, gi:6 },
            { p0:{x:560,y:520}, p1:{x:820,y:385}, p2:{x:1030,y:445}, p3:{x:1180,y:360}, w:24, prof:0.6, op:0.6, gi:7 }
        ]
    ]

    readonly property var sheenSets: [
        [ { p0:{x:660,y:520}, p1:{x:885,y:322}, p2:{x:1022,y:470}, p3:{x:1160,y:362}, w:52, prof:0.75, op:0.34, g:["#ffcf9a","#ffe6c6","#ffcf9a"] },
          { p0:{x:700,y:468}, p1:{x:905,y:340}, p2:{x:1015,y:444}, p3:{x:1145,y:380}, w:34, prof:0.82, op:0.4, g:["#ffe9c8","#fff6e6","#ffdca8"] } ],
        [ { p0:{x:665,y:530}, p1:{x:885,y:330}, p2:{x:1026,y:478}, p3:{x:1162,y:368}, w:50, prof:0.75, op:0.3, g:["#dcd0ff","#f0e8ff","#dcd0ff"] },
          { p0:{x:705,y:480}, p1:{x:905,y:350}, p2:{x:1018,y:452}, p3:{x:1150,y:392}, w:32, prof:0.82, op:0.36, g:["#e8e0ff","#ffffff","#e8dcff"] } ],
        [ { p0:{x:660,y:522}, p1:{x:885,y:326}, p2:{x:1022,y:472}, p3:{x:1160,y:366}, w:52, prof:0.75, op:0.36, g:["#ffdf9a","#fff0c6","#ffdf9a"] },
          { p0:{x:700,y:470}, p1:{x:905,y:344}, p2:{x:1015,y:446}, p3:{x:1145,y:384}, w:34, prof:0.82, op:0.42, g:["#fff0c8","#fffbe6","#ffe6a8"] } ]
    ]

    readonly property var specs: specSets[v]
    readonly property var sheens: sheenSets[v]
    readonly property var palette: pal[v]

    // the field always covers the whole surface
    Rectangle { anchors.fill: parent; color: "#0b0a12" }

    // the composition is authored on a fixed 1280x800 canvas and scaled to COVER
    // the container (like PreserveAspectCrop) — fills any screen and the tiny
    // picker tile alike without distorting the ribbons.
    Item {
        id: art
        width: 1280; height: 800
        anchors.centerIn: parent
        transformOrigin: Item.Center
        scale: Math.max(root.width / 1280, root.height / 800)

        // warm bloom behind the cluster (the light source)
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeWidth: 0
                fillGradient: RadialGradient {
                    centerX: 860; centerY: 420; centerRadius: 600; focalX: 860; focalY: 420
                    GradientStop { position: 0.0; color: root.bloom }
                    GradientStop { position: 0.55; color: "#180c15" }
                    GradientStop { position: 1.0; color: "#00000000" }
                }
                startX: 0; startY: 0
                PathLine { x: 1280; y: 0 }
                PathLine { x: 1280; y: 800 }
                PathLine { x: 0; y: 800 }
            }
        }

        Repeater {
            model: root.specs
            delegate: Shape {
                required property var modelData
                anchors.fill: parent
                opacity: modelData.op
                ShapePath {
                    strokeWidth: 0
                    fillGradient: LinearGradient {
                        x1: modelData.p0.x; y1: modelData.p0.y; x2: modelData.p3.x; y2: modelData.p3.y
                        GradientStop { position: 0.0; color: root.palette[modelData.gi][0] }
                        GradientStop { position: 0.5; color: root.palette[modelData.gi][1] }
                        GradientStop { position: 1.0; color: root.palette[modelData.gi][2] }
                    }
                    PathPolyline { path: root.ribbonOutline(modelData.p0, modelData.p1, modelData.p2, modelData.p3, modelData.w, modelData.prof, 0) }
                }
            }
        }
        Repeater {
            model: root.sheens
            delegate: Shape {
                required property var modelData
                anchors.fill: parent
                opacity: modelData.op
                ShapePath {
                    strokeWidth: 0
                    fillGradient: LinearGradient {
                        x1: modelData.p0.x; y1: modelData.p0.y; x2: modelData.p3.x; y2: modelData.p3.y
                        GradientStop { position: 0.0; color: modelData.g[0] }
                        GradientStop { position: 0.5; color: modelData.g[1] }
                        GradientStop { position: 1.0; color: modelData.g[2] }
                    }
                    PathPolyline { path: root.ribbonOutline(modelData.p0, modelData.p1, modelData.p2, modelData.p3, modelData.w, modelData.prof, 1) }
                }
            }
        }
    }
}
