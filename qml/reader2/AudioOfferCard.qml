pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: card
    objectName: "reader2AudioOfferCard"

    property string offerState: "available"
    property string label: ""
    property string meta: ""
    property real progress: 0
    property var versions: []

    signal focusRequested(var item)
    signal getRequested()
    signal versionChosen(var row)
    signal otherVersionsRequested()

    readonly property real boundedProgress: Math.max(0, Math.min(1, progress))
    readonly property bool available: offerState === "available"
    readonly property bool getting: offerState === "getting"
    readonly property bool choosing: offerState === "choose"
    readonly property int inset: 19
    readonly property int contentWidth: Math.max(0, width - inset * 2)

    implicitWidth: 290
    implicitHeight: choosing ? Math.max(158, 118 + versionColumn.implicitHeight) : 158
    radius: 14
    color: Theme.goldWash
    border.width: 1
    border.color: Qt.rgba(240 / 255, 194 / 255, 74 / 255, 0.22)
    antialiasing: true
    visible: available || getting || choosing

    component HeadphoneGlyph: Canvas {
        property color ink: Theme.gold
        antialiasing: true
        onInkChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            var w = width
            var h = height
            var s = Math.min(w, h)
            var cx = w / 2
            var cy = h / 2
            ctx.clearRect(0, 0, w, h)
            ctx.strokeStyle = ink
            ctx.lineWidth = Math.max(1.4, s / 11)
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath()
            ctx.arc(cx, cy + s * 0.03, s * 0.34, Math.PI, 0, false)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(cx - s * 0.34, cy + s * 0.03)
            ctx.lineTo(cx - s * 0.34, cy + s * 0.30)
            ctx.moveTo(cx + s * 0.34, cy + s * 0.03)
            ctx.lineTo(cx + s * 0.34, cy + s * 0.30)
            ctx.stroke()
            ctx.strokeRect(cx - s * 0.43, cy + s * 0.16, s * 0.14, s * 0.29)
            ctx.strokeRect(cx + s * 0.29, cy + s * 0.16, s * 0.14, s * 0.29)
        }
    }

    function versionPrimary(row) {
        if (!row)
            return qsTr("Audiobook")
        return String(row.title || row.name || row.label || qsTr("Audiobook"))
    }

    function versionSecondary(row) {
        if (!row)
            return ""
        var parts = []
        if (row.format)
            parts.push(String(row.format).toUpperCase())
        if (row.language)
            parts.push(String(row.language))
        if (row.sizeText)
            parts.push(String(row.sizeText))
        else if (row.size)
            parts.push(String(row.size))
        return parts.join(" · ")
    }

    Row {
        id: heading
        objectName: "reader2AudioOfferHeading"
        x: card.inset
        y: card.inset
        width: card.contentWidth
        height: 18
        spacing: 9

        HeadphoneGlyph {
            width: 18
            height: 18
        }

        Text {
            id: headingText
            objectName: "reader2AudioOfferHeaderText"
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Audiobook available")
            font.family: Theme.ui
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: Theme.ink
        }
    }

    Text {
        id: metaText
        objectName: "reader2AudioOfferMeta"
        x: card.inset
        y: 45
        width: card.contentWidth
        height: 15
        text: card.meta
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.ui
        font.pixelSize: 13
        color: Theme.inkDim
    }

    Rectangle {
        id: getButton
        objectName: "reader2AudioOfferGetButton"
        visible: card.available
        x: card.inset
        y: 74
        width: card.contentWidth
        height: 40
        radius: 11
        color: Theme.gold

        Text {
            anchors.centerIn: parent
            text: card.label.length ? card.label : qsTr("Get audiobook")
            font.family: Theme.ui
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: "#241a05"
        }

        ReaderKeyboardArea {
            id: getArea
            objectName: "reader2AudioOfferGetAction"
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            keyboardFocused: function() { card.focusRequested(getButton) }
            keyboardLabel: card.label.length ? card.label : qsTr("Get audiobook")
            onClicked: card.getRequested()
        }
    }

    Rectangle {
        id: progressTrack
        objectName: "reader2AudioOfferProgressTrack"
        visible: card.getting
        x: card.inset
        y: 74
        width: card.contentWidth
        height: 40
        radius: 11
        color: Theme.rowHover
        border.width: 1
        border.color: Qt.rgba(240 / 255, 194 / 255, 74 / 255, 0.22)
        clip: true

        Rectangle {
            id: progressFill
            objectName: "reader2AudioOfferProgressFill"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: progressTrack.width * card.boundedProgress
            color: Qt.rgba(240 / 255, 194 / 255, 74 / 255, 0.30)
        }

        Text {
            anchors.centerIn: parent
            text: card.label.length
                  ? card.label
                  : qsTr("Getting audiobook · %1%").arg(Math.round(card.boundedProgress * 100))
            font.family: Theme.ui
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: Theme.ink
        }
    }

    Item {
        id: versionsLink
        objectName: "reader2AudioOfferOtherVersions"
        visible: card.available && card.versions.length > 0
        x: card.inset
        y: 124
        width: card.contentWidth
        height: 15

        Text {
            anchors.centerIn: parent
            text: qsTr("Other versions (%1)").arg(card.versions.length)
            font.family: Theme.ui
            font.pixelSize: 12
            color: versionsArea.containsMouse ? Theme.inkDim : Theme.inkFaint
        }

        ReaderKeyboardArea {
            id: versionsArea
            objectName: "reader2AudioOfferOtherVersionsAction"
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            keyboardFocused: function() { card.focusRequested(versionsLink) }
            keyboardLabel: qsTr("Other versions")
            onClicked: card.otherVersionsRequested()
        }
    }

    Text {
        id: chooseLabel
        objectName: "reader2AudioOfferChooseLabel"
        visible: card.choosing
        x: card.inset
        y: 74
        width: card.contentWidth
        height: 17
        text: card.label.length ? card.label : qsTr("Choose audiobook")
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.ui
        font.pixelSize: 14
        font.weight: Font.DemiBold
        color: Theme.gold
    }

    Column {
        id: versionColumn
        objectName: "reader2AudioOfferVersionList"
        visible: card.choosing
        x: card.inset
        y: 99
        width: card.contentWidth
        spacing: 6

        Repeater {
            model: card.versions

            delegate: Rectangle {
                id: versionRow
                required property var modelData
                required property int index
                objectName: "reader2AudioOfferVersionRow" + index
                width: versionColumn.width
                height: 44
                radius: 9
                color: versionArea.containsMouse || versionArea.activeFocus ? Theme.rowHover : "transparent"
                border.width: 1
                border.color: Theme.barBorder

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    spacing: 2

                    Text {
                        width: parent.width
                        text: card.versionPrimary(versionRow.modelData)
                        elide: Text.ElideRight
                        font.family: Theme.ui
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: Theme.ink
                    }

                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: card.versionSecondary(versionRow.modelData)
                        elide: Text.ElideRight
                        font.family: Theme.ui
                        font.pixelSize: 11
                        color: Theme.inkFaint
                    }
                }

                ReaderKeyboardArea {
                    id: versionArea
                    objectName: "reader2AudioOfferVersionAction" + versionRow.index
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    keyboardFocused: function() { card.focusRequested(versionRow) }
                    keyboardLabel: card.versionPrimary(versionRow.modelData)
                    onClicked: card.versionChosen(versionRow.modelData)
                }
            }
        }
    }
}
