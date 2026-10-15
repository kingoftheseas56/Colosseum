// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

Column {
    id: root

    property string kicker: ""
    property string title: ""
    property string copy: ""

    width: parent ? parent.width : implicitWidth
    spacing: 0

    Theme { id: theme }

    Text {
        width: parent.width
        text: root.kicker
        color: theme.gold
        font.family: theme.ui
        font.pixelSize: 10
        font.weight: Font.Bold
        font.letterSpacing: 2.4
        wrapMode: Text.WordWrap
    }

    Item { width: 1; height: 12 }

    Text {
        width: parent.width
        text: root.title
        color: theme.ink
        font.family: theme.display
        font.pixelSize: 29
        font.weight: Font.Medium
        wrapMode: Text.WordWrap
    }

    Item { width: 1; height: root.copy.length > 0 ? 10 : 0 }

    Text {
        width: parent.width
        visible: root.copy.length > 0
        text: root.copy
        color: theme.inkDimmer
        font.family: theme.ui
        font.pixelSize: 13
        lineHeight: 1.45
        wrapMode: Text.WordWrap
    }
}
