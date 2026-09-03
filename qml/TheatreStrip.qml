// TheatreStrip — the Theatre HOME mode-intro widget: a cinema FILM-STRIP. One dark sprocketed band
// runs the full width of the glass bar, carrying landscape stills that drift on their own like film
// feeding a projector; resting the cursor on the band holds the frame. Trending across all three
// lanes mixed — each still wears its lane tag (Movies / Shows / Anime). Picked over the "Now Showing"
// lightbox trio on the 2026-07-04 mock review (agents/colosseum-home-biblio-theatre-mock.html).
//
// Art is LANDSCAPE: Cinemeta gives real backgrounds (metahub fallback); Jikan anime posters are
// portrait, cropped center — acceptable, tagged as the known compromise. Remote + disk-cached via
// the native launcher, exactly like Bookshelf. Data self-loads (TheatreApi read-only) so the
// Main.qml wiring stays a three-line hunk.

import QtQuick
import "TheatreApi.js" as TheatreApi

Glass {
    id: strip

    property string heading: "Theatre"
    property var stills: []            // { title, art, lane, c1, c2 } — interleaved M/S/A

    signal clicked()                   // title or a still → open the Theatre world (v1, like Bookshelf)

    radius: 18
    height: 400

    Theme { id: theme }
    KeyboardAction {
        id: stripKeyboard; anchors.fill: parent; pointerEnabled: false
        accessibleName: "Open Theatre"; focusRadius: strip.radius; onTriggered: strip.clicked()
    }

    // ---- load: top 3 per lane, interleaved deterministically (async completion order varies) ----
    property var _lanes: ({})
    property int _pending: 3
    Component.onCompleted: {
        function part(lane) {
            return function(items) {
                strip._lanes[lane] = (items || []).slice(0, 3);
                if (--strip._pending === 0)
                    strip._interleave();
            };
        }
        TheatreApi.catalogFetch("movie", null, 3, part("Movies"));
        TheatreApi.catalogFetch("series", null, 3, part("Shows"));
        TheatreApi.jikanFetch("/top/anime", { filter: "airing", page: 1 }, 3, part("Anime"));
    }
    function _interleave() {
        var order = ["Movies", "Shows", "Anime"], out = [];
        for (var i = 0; i < 3; i++)
            for (var l = 0; l < order.length; l++) {
                var lane = _lanes[order[l]] || [];
                if (i < lane.length)
                    out.push({ title: lane[i].title, art: lane[i].art, lane: order[l],
                               c1: lane[i].c1 || "#33445d", c2: lane[i].c2 || "#0c1118" });
            }
        stills = out;
    }

    // ---- main title (centered) + the one gold touch: a short marquee rule under it ----
    Text {
        id: title
        anchors.top: parent.top; anchors.topMargin: 28
        anchors.horizontalCenter: parent.horizontalCenter
        text: strip.heading; color: theme.ink
        font.family: theme.display; font.pixelSize: 33
        MouseArea {
            anchors.fill: parent; anchors.margins: -12
            cursorShape: Qt.PointingHandCursor; onClicked: strip.clicked()
        }
    }
    Rectangle {
        anchors.top: title.bottom; anchors.topMargin: 10
        anchors.horizontalCenter: parent.horizontalCenter
        width: 64; height: 2; radius: 1; color: theme.gold
    }

    // ---- corner label (Bookshelf idiom): what it is. The right-corner lane list
    //      ("Movies · Shows · Anime") was cut — read cheap (Hemanth 2026-07-18). ----
    Text {
        anchors.left: parent.left; anchors.leftMargin: 46
        anchors.top: parent.top; anchors.topMargin: 36
        text: "Trending"; color: theme.inkDim
        font.family: theme.display; font.italic: true; font.pixelSize: 22
    }

    // ---- the film band: dark, sprocketed, edge to edge ----
    Rectangle {
        id: band
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom; anchors.bottomMargin: 66
        height: 208
        color: Qt.rgba(0, 0, 0, 0.5)
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.08)
        clip: true

        HoverHandler { id: bandHover }

        // the reel — stills doubled for a seamless wrap; drifts left, holds on hover
        Row {
            id: reel
            spacing: 14
            anchors.verticalCenter: parent.verticalCenter
            // stills instantiate after the drift's running-binding flips — re-capture from/to/duration
            onWidthChanged: if (drift.running) drift.restart()
            Repeater {
                model: strip.stills.concat(strip.stills)
                delegate: Item {
                    id: frame
                    required property var modelData
                    width: 300; height: 158

                    Rectangle {
                        anchors.fill: parent
                        radius: 6; clip: true
                        gradient: Gradient {
                            GradientStop { position: 0; color: frame.modelData.c1 }
                            GradientStop { position: 1; color: frame.modelData.c2 }
                        }
                        border.width: fma.containsMouse ? 2 : 1
                        border.color: fma.containsMouse ? theme.gold : Qt.rgba(1, 1, 1, 0.12)
                        scale: fma.containsMouse ? 1.04 : 1
                        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                        Image {
                            anchors.fill: parent
                            source: frame.modelData.art || ""
                            asynchronous: true; cache: true
                            fillMode: Image.PreserveAspectCrop
                            sourceSize.width: 480; sourceSize.height: 270
                            opacity: status === Image.Ready ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 220 } }
                        }
                        // bottom scrim so the title reads over any still
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                            height: 56
                            gradient: Gradient {
                                GradientStop { position: 0; color: "transparent" }
                                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.72) }
                            }
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 10
                            anchors.top: parent.top; anchors.topMargin: 8
                            text: frame.modelData.lane.toUpperCase(); color: Qt.rgba(1, 1, 1, 0.6)
                            font.family: theme.ui; font.pixelSize: 9; font.letterSpacing: 1.6
                            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.5)
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 10
                            anchors.right: parent.right; anchors.rightMargin: 10
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                            text: frame.modelData.title; color: theme.ink
                            font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        id: fma; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: strip.clicked()
                    }
                }
            }

            // drift: one full first-half per cycle = seamless loop (Row width = n·w + (n−1)·s,
            // so the wrap point is (width + spacing) / 2). Speed pinned per-pixel, not per-cycle.
            NumberAnimation on x {
                id: drift
                running: strip.stills.length > 0
                paused: running && bandHover.hovered
                from: 0
                to: -(reel.width + reel.spacing) / 2
                duration: Math.max(1000, ((reel.width + reel.spacing) / 2) * 22)
                loops: Animation.Infinite
            }
        }

        // sprocket holes — static strips over the film, top and bottom
        Row {
            anchors.top: parent.top; anchors.topMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 20
            Repeater {
                model: Math.max(0, Math.floor((band.width - 20) / 26))
                Rectangle { width: 6; height: 6; radius: 2; color: Qt.rgba(1, 1, 1, 0.3) }
            }
        }
        Row {
            anchors.bottom: parent.bottom; anchors.bottomMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 20
            Repeater {
                model: Math.max(0, Math.floor((band.width - 20) / 26))
                Rectangle { width: 6; height: 6; radius: 2; color: Qt.rgba(1, 1, 1, 0.3) }
            }
        }
    }
}
