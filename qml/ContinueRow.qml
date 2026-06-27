// ContinueRow — the resume row for a world page. A row IS the right shape for resume (per doctrine).
// Each tile has the SAME dual-action as the home Continue cards: a center play/read button RESUMES
// into the content; clicking anywhere else opens the series / detail view. Carries the chapter /
// episode sub-label, and falls back to an AniList cover when a manga entry was saved without art.

import QtQuick
import "ContinueCovers.js" as ContinueCovers

Column {
    id: cont

    property string title: "Continue"
    property var items: []              // Progress entries: { kind, caption/title, sub, cover, progress, resume }
    signal resumeRequested(var item)    // center icon → resume INTO the content
    signal detailRequested(var item)    // anywhere else → the series / detail view

    width: parent ? parent.width : 800
    spacing: 14
    // The resume shelf only exists when there's something on it. Its parent board is a Column, which
    // skips invisible children — so an empty Continue leaves no gap.
    visible: cont.items.length > 0

    WidgetHeader { width: parent.width; title: cont.title }

    Flickable {
        width: parent.width; height: 224
        contentWidth: row.width; contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: row
            spacing: 18
            Repeater {
                model: cont.items
                delegate: Column {
                    id: cell
                    required property var modelData
                    required property int index
                    width: 132
                    spacing: 6

                    property string mkind: modelData.kind !== undefined ? modelData.kind : ""
                    property string mtitle: (modelData.title !== undefined && modelData.title.length)
                                            ? modelData.title : (modelData.caption || "")
                    // saved cover, or — for a manga saved without art — an AniList fallback by title
                    property string resolvedCover: (modelData.cover !== undefined && ("" + modelData.cover).length)
                                                   ? modelData.cover : ""
                    Component.onCompleted: if (!resolvedCover && (mkind === "manga" || mkind === "comic"))
                        ContinueCovers.fetch(mtitle, function(u) { cell.resolvedCover = u })

                    Item {
                        width: 132; height: 196
                        PortraitTile {
                            anchors.fill: parent
                            caption: cell.mtitle
                            cover: cell.resolvedCover
                            c1: modelData.c1 !== undefined ? modelData.c1 : "#444"
                            c2: modelData.c2 !== undefined ? modelData.c2 : "#111"
                            progress: modelData.progress !== undefined ? modelData.progress : -1
                            onClicked: cont.detailRequested(modelData)
                        }
                        // center play / read button — resumes INTO the content (on top of the tile)
                        Rectangle {
                            width: 46; height: 46; radius: 23
                            anchors.centerIn: parent
                            color: rbHov.hovered ? Qt.rgba(0,0,0,0.80) : Qt.rgba(0,0,0,0.55)
                            border.width: 1.5; border.color: Qt.rgba(1,1,1,0.9)
                            scale: rbHov.hovered ? 1.08 : 1.0
                            Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutBack } }
                            Image {
                                anchors.centerIn: parent
                                width: cell.mkind === "video" ? 18 : 21; height: width
                                source: cell.mkind === "video" ? "../assets/icons/play.svg"
                                      : cell.mkind === "book"  ? "../assets/icons/books.svg"
                                      : "../assets/icons/manga.svg"
                                fillMode: Image.PreserveAspectFit
                            }
                            HoverHandler { id: rbHov }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: cont.resumeRequested(modelData) }
                        }
                    }

                    // the detail line: which chapter / episode (Progress saved it in `sub`)
                    Text {
                        width: 132
                        text: modelData.sub !== undefined ? modelData.sub : ""
                        visible: text.length > 0
                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                        elide: Text.ElideRight; maximumLineCount: 1
                    }
                }
            }
        }
    }

    Theme { id: theme }
}
