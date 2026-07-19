// HomeSpotlight — the AF2 Home signature. The featured title over the living wallpaper:
// a metahub logo (with a clean text-title fallback), an eyebrow, a fact line, a blurb, the
// primary + secondary actions, and a resume/progress line. It RECEDES on scroll (lifts +
// fades) as the glass board rises, driven by a 0..1 `recede`.
//
// Gold discipline: gold appears ONLY on the resume progress fill. The primary action is a
// white button (gold-free); secondaries are glass ghosts. Type is theme.displaySans (Figtree).

import QtQuick
import QtQuick.Effects

Item {
    id: root

    // ── public API ──
    property real recede: 0
    property string eyebrow: "Featured"
    property string title: ""
    property url logoUrl: ""
    property string factLine: ""
    property string blurb: ""
    property string primaryLabel: "Watch"
    property bool hasSecondary: false
    property string secondaryLabel: "Read"
    property real resumeFraction: 0
    property string resumeLabel: ""
    signal primaryRequested()
    signal secondaryRequested()
    signal detailsRequested()

    Theme { id: theme }

    implicitWidth: 600
    implicitHeight: col.implicitHeight

    opacity: 1 - Math.min(1, root.recede * 1.35)
    transform: Translate { y: -root.recede * 170 }
    enabled: opacity > 0.05

    Column {
        id: col
        width: parent.width
        spacing: 0

        // ── eyebrow: two world pips + label ──
        Row {
            spacing: 9
            bottomPadding: 22
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: -4
                Rectangle { width: 6; height: 6; radius: 3; color: theme.tintTankoban }
                Rectangle { width: 6; height: 6; radius: 3; color: theme.tintTheatre }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.eyebrow.toUpperCase()
                color: theme.inkDim
                font.family: theme.displaySans; font.pixelSize: 11
                font.weight: Font.Bold; font.letterSpacing: 3.2
            }
        }

        // ── logo (metahub) with text-title fallback ──
        Item {
            width: parent.width
            height: 128
            Image {
                id: logo
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                height: 128
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft
                source: root.logoUrl
                asynchronous: true
                cache: true
                sourceSize.height: 256
                visible: source !== "" && status === Image.Ready
                layer.enabled: visible
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Qt.rgba(0, 0, 0, 0.75)
                    shadowVerticalOffset: 22
                    shadowBlur: 1.0
                    autoPaddingEnabled: true
                }
            }
            Text {
                id: titleFallback
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                visible: logo.status !== Image.Ready
                text: root.title
                color: theme.ink
                font.family: theme.displaySans; font.pixelSize: 74
                font.weight: Font.ExtraBold; font.letterSpacing: -1.4
                lineHeight: 0.94
                elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
            }
        }

        // ── fact line ──
        Text {
            topPadding: 24
            width: parent.width
            visible: root.factLine.length > 0
            text: root.factLine
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
            elide: Text.ElideRight; maximumLineCount: 1
        }

        // ── blurb ──
        Text {
            topPadding: 16
            width: Math.min(parent.width, 500)
            visible: root.blurb.length > 0
            text: root.blurb
            color: theme.inkDim
            font.family: theme.ui; font.pixelSize: 14
            lineHeight: 1.4
            wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
        }

        // ── actions ──
        Row {
            topPadding: 28
            spacing: 11

            // primary — white, gold-free (gold is reserved for progress)
            Rectangle {
                height: 46; radius: 12
                width: primRow.implicitWidth + 48
                color: primMa.containsMouse ? Qt.rgba(1, 1, 1, 0.92) : theme.ink
                Behavior on color { ColorAnimation { duration: 130 } }
                Row {
                    id: primRow
                    anchors.centerIn: parent
                    spacing: 9
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18; height: 18; source: "../assets/icons/play.svg"
                        sourceSize.width: 36; sourceSize.height: 36
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.primaryLabel; color: "#0c0d12"
                        font.family: theme.displaySans; font.pixelSize: 15; font.weight: Font.Bold
                    }
                }
                MouseArea { id: primMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: root.primaryRequested() }
            }

            // secondary — glass ghost, world-appropriate (Read / etc.)
            Rectangle {
                visible: root.hasSecondary
                height: 46; radius: 12
                width: secRow.implicitWidth + 44
                color: secMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Row {
                    id: secRow
                    anchors.centerIn: parent
                    spacing: 9
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 17; height: 17; source: "../assets/icons/books.svg"
                        sourceSize.width: 34; sourceSize.height: 34; opacity: 0.9
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.secondaryLabel; color: theme.ink
                        font.family: theme.displaySans; font.pixelSize: 15; font.weight: Font.DemiBold
                    }
                }
                MouseArea { id: secMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: root.secondaryRequested() }
            }

            // details — glass ghost
            Rectangle {
                height: 46; radius: 12
                width: detText.implicitWidth + 40
                color: detMa.containsMouse ? theme.glassHi : theme.glassTint
                border.width: 1; border.color: theme.edge
                Text {
                    id: detText
                    anchors.centerIn: parent
                    text: "Details"; color: theme.ink
                    font.family: theme.displaySans; font.pixelSize: 15; font.weight: Font.DemiBold
                }
                MouseArea { id: detMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: root.detailsRequested() }
            }
        }

        // ── resume line — the only gold on the hero ──
        Row {
            topPadding: 20
            spacing: 12
            visible: root.resumeFraction > 0
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.resumeLabel
                color: theme.inkDimmer
                font.family: theme.ui; font.pixelSize: 13
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 200; height: 3; radius: 2
                color: Qt.rgba(1, 1, 1, 0.12)
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, root.resumeFraction))
                    height: parent.height; radius: 2; color: theme.gold
                }
            }
        }
    }
}
