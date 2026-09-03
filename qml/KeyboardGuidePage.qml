pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "keyboardGuidePage"

    signal backRequested()

    property bool featuresOpen: true
    property bool readingOpen: false
    property bool shortcutsOpen: false
    property QtObject keyboardRegistry: null

    readonly property string iconRoot: "assets/keyboard-guide/"

    function takeKeyboardFocus() {
        featuresHeader.forceActiveFocus(Qt.TabFocusReason)
    }

    onVisibleChanged: if (visible) Qt.callLater(root.takeKeyboardFocus)

    readonly property var featureRows: [
        { icon: "keyboard.svg", tokens: ["Tab", "/", "Shift", "+", "Tab"], action: "Next / previous region" },
        { icon: "navigation.svg", tokens: ["←", "↑", "↓", "→"], action: "Navigate" },
        { icon: "enter.svg", tokens: ["Enter", "/", "Space"], action: "Open / activate" },
        { icon: "back.svg", tokens: ["Esc"], action: "Back / close" },
        { icon: "search.svg", tokens: ["Ctrl", "+", "F"], action: "Search" },
        { icon: "more.svg", tokens: ["Shift", "+", "F10"], action: "More options" },
        { icon: "navigation.svg", tokens: ["Home", "/", "End"], action: "First / last" },
        { icon: "navigation.svg", tokens: ["PageUp", "/", "PageDown"], action: "Move one page" }
    ]

    readonly property var comicRows: [
        { tokens: ["←", "/", "→"], action: "Previous / next page" },
        { tokens: ["Space", "/", "PageDown"], action: "Next page / screen" },
        { tokens: ["PageUp"], action: "Previous page / screen" },
        { tokens: ["T"], action: "Thumbnails" },
        { tokens: ["B"], action: "Bookmark" }
    ]

    readonly property var bookRows: [
        { tokens: ["←", "/", "PageUp"], action: "Previous page" },
        { tokens: ["→", "/", "PageDown"], action: "Next page" },
        { tokens: ["Space"], action: "Next page" },
        { tokens: ["Esc"], action: "Close overlay / back" }
    ]

    readonly property var playerRows: [
        { tokens: ["Space"], action: "Play / pause" },
        { tokens: ["←", "/", "→"], action: "Seek backward / forward" },
        { tokens: ["M"], action: "Mute" },
        { tokens: ["E"], action: "Episodes & sources" },
        { tokens: ["Esc"], action: "Close menu / back" }
    ]

    function guideTokens(sequence) {
        var parts = String(sequence || "").split("+")
        var result = []
        for (var i = 0; i < parts.length; i++) {
            if (i > 0)
                result.push("+")
            result.push(parts[i])
        }
        return result
    }

    readonly property var shortcutRows: {
        var revision = root.keyboardRegistry ? root.keyboardRegistry.revision : 0
        if (!root.keyboardRegistry || revision < 0)
            return []

        var entries = root.keyboardRegistry.entriesFor("application", "Shortcuts")
        var rows = []
        for (var i = 0; i < entries.length; i++) {
            var entry = entries[i]
            var chord = entry.sequences && entry.sequences.length
                    ? String(entry.sequences[0]) : ""
            rows.push({
                semanticId: entry.semanticId,
                icon: entry.icon,
                tokens: root.guideTokens(chord),
                chord: chord,
                action: entry.label
            })
        }
        return rows
    }

    Theme { id: theme }

    Keys.onEscapePressed: function(event) {
        root.backRequested()
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: "#050505"
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        width: Math.min(parent.width * 0.46, 520)
        height: Math.min(parent.height * 0.55, 460)
        radius: width / 2
        color: Qt.rgba(0.94, 0.77, 0.29, 0.055)
        layer.enabled: true
        opacity: 0.9
    }

    component KeySequence: Row {
        id: sequence
        required property var tokens
        spacing: 5

        Repeater {
            model: sequence.tokens
            delegate: Item {
                id: tokenItem
                required property string modelData
                readonly property bool separator: modelData === "+" || modelData === "/"
                implicitWidth: separator ? 10 : Math.max(31, tokenLabel.implicitWidth + 18)
                implicitHeight: 27

                Rectangle {
                    anchors.fill: parent
                    visible: !tokenItem.separator
                    radius: 7
                    color: Qt.rgba(1, 1, 1, 0.075)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.22)

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.18)
                    }
                }

                Text {
                    id: tokenLabel
                    anchors.centerIn: parent
                    text: tokenItem.modelData
                    color: tokenItem.separator ? "#777783" : theme.ink
                    font.family: theme.ui
                    font.pixelSize: tokenItem.separator ? 10 : 11
                    font.weight: tokenItem.separator ? Font.Normal : Font.DemiBold
                }
            }
        }
    }

    component GuideRow: Item {
        id: row
        required property var tokens
        required property string action
        property string icon: ""
        property bool compact: false

        implicitHeight: compact ? 46 : 42

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Qt.rgba(1, 1, 1, 0.09)
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: row.compact ? 12 : 10
            anchors.rightMargin: row.compact ? 12 : 10
            spacing: row.compact ? 10 : 12

            Image {
                visible: !row.compact && row.icon.length > 0
                source: root.iconRoot + row.icon
                sourceSize.width: 17
                sourceSize.height: 17
                Layout.preferredWidth: visible ? 24 : 0
                Layout.preferredHeight: 24
                fillMode: Image.PreserveAspectFit
                opacity: 0.72
            }

            KeySequence {
                tokens: row.tokens
                Layout.preferredWidth: row.compact ? 118 : 220
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                text: row.action
                color: row.compact ? "#d1d0d6" : "#d4d3d8"
                font.family: theme.ui
                font.pixelSize: row.compact ? 12 : 13
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    component SectionHeader: Button {
        id: section
        required property string title
        required property string note
        required property string iconSource
        required property bool expanded

        signal toggleRequested()

        width: parent ? parent.width : 0
        height: 62
        activeFocusOnTab: true
        hoverEnabled: true
        Accessible.name: title
        Accessible.description: (expanded ? "Collapse " : "Expand ") + title

        onClicked: toggleRequested()
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                section.toggleRequested()
                event.accepted = true
            }
        }

        background: Rectangle {
            color: section.down
                   ? Qt.rgba(1, 1, 1, 0.055)
                   : section.hovered || section.activeFocus
                     ? Qt.rgba(1, 1, 1, 0.035)
                     : Qt.rgba(1, 1, 1, 0.018)
            border.width: section.activeFocus ? 1 : 0
            border.color: theme.gold
        }

        contentItem: RowLayout {
            spacing: 13

            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 9
                color: Qt.rgba(0.94, 0.77, 0.29, 0.08)
                border.width: 1
                border.color: Qt.rgba(0.94, 0.77, 0.29, 0.18)

                Image {
                    anchors.centerIn: parent
                    source: root.iconRoot + section.iconSource
                    sourceSize.width: 17
                    sourceSize.height: 17
                    width: 17
                    height: 17
                }
            }

            Column {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: section.title.toUpperCase()
                    color: theme.ink
                    font.family: theme.ui
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    font.letterSpacing: 1.3
                }

                Text {
                    text: section.note
                    color: "#777783"
                    font.family: theme.ui
                    font.pixelSize: 11
                }
            }

            Image {
                source: root.iconRoot + "chevron.svg"
                sourceSize.width: 13
                sourceSize.height: 13
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                rotation: section.expanded ? 180 : 0
                opacity: 0.8

                Behavior on rotation {
                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                }
            }
        }
    }

    component MediaColumn: Rectangle {
        id: card
        required property string title
        required property string icon
        required property var rows

        color: Qt.rgba(1, 1, 1, 0.018)
        radius: 14
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)
        implicitHeight: mediaColumn.implicitHeight

        Column {
            id: mediaColumn
            width: parent.width

            Item {
                width: parent.width
                height: 52

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 13
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    Rectangle {
                        width: 27
                        height: 27
                        radius: 8
                        color: Qt.rgba(0.94, 0.77, 0.29, 0.07)

                        Image {
                            anchors.centerIn: parent
                            source: root.iconRoot + card.icon
                            sourceSize.width: 17
                            sourceSize.height: 17
                            width: 17
                            height: 17
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: card.title.toUpperCase()
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 1.2
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.09)
                }
            }

            Repeater {
                model: card.rows
                delegate: GuideRow {
                    required property var modelData
                    width: mediaColumn.width
                    compact: true
                    tokens: modelData.tokens
                    action: modelData.action
                }
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 28
        clip: true

        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            width: Math.min(scroll.availableWidth, 900)
            implicitHeight: guide.implicitHeight
            anchors.horizontalCenter: parent.horizontalCenter

            Rectangle {
                id: guide
                width: parent.width
                implicitHeight: guideColumn.implicitHeight
                radius: 22
                color: Qt.rgba(0.055, 0.055, 0.065, 0.96)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.13)
                clip: true

                Column {
                    id: guideColumn
                    width: parent.width
                    spacing: 0

                    Item {
                        width: parent.width
                        height: 104

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 28
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 9

                            Row {
                                spacing: 10

                                Rectangle {
                                    width: 28
                                    height: 1
                                    color: theme.gold
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: "COLOSSEUM · KEYBOARD"
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 10
                                    font.letterSpacing: 1.6
                                }
                            }

                            Text {
                                text: "Essentials"
                                color: theme.ink
                                font.family: theme.display
                                font.pixelSize: 34
                                font.weight: Font.Medium
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Qt.rgba(1, 1, 1, 0.10)
                        }
                    }

                    SectionHeader {
                        id: featuresHeader
                        objectName: "keyboardGuideFeaturesHeader"
                        KeyNavigation.tab: readingHeader
                        KeyNavigation.backtab: shortcutsHeader
                        title: "Features"
                        note: "Move, open, search, go back"
                        iconSource: "navigation.svg"
                        expanded: root.featuresOpen
                        onToggleRequested: root.featuresOpen = !root.featuresOpen
                    }

                    Item {
                        width: parent.width
                        height: root.featuresOpen ? featureContent.implicitHeight + 16 : 0
                        clip: true

                        Behavior on height {
                            NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
                        }

                        Column {
                            id: featureContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            anchors.topMargin: 8

                            Repeater {
                                model: root.featureRows
                                delegate: GuideRow {
                                    required property var modelData
                                    width: featureContent.width
                                    tokens: modelData.tokens
                                    action: modelData.action
                                    icon: modelData.icon
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.10)
                    }

                    SectionHeader {
                        id: readingHeader
                        objectName: "keyboardGuideReadingHeader"
                        KeyNavigation.tab: shortcutsHeader
                        KeyNavigation.backtab: featuresHeader
                        title: "Reading & Playback"
                        note: "Comic, book and video controls"
                        iconSource: "play.svg"
                        expanded: root.readingOpen
                        onToggleRequested: root.readingOpen = !root.readingOpen
                    }

                    Item {
                        width: parent.width
                        height: root.readingOpen ? mediaGrid.implicitHeight + 32 : 0
                        clip: true

                        Behavior on height {
                            NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
                        }

                        GridLayout {
                            id: mediaGrid
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 16
                            columns: 3
                            columnSpacing: 10
                            rowSpacing: 10

                            MediaColumn {
                                title: "Comic Reader"
                                icon: "comic.svg"
                                rows: root.comicRows
                                Layout.fillWidth: true
                            }

                            MediaColumn {
                                title: "Book Reader"
                                icon: "book.svg"
                                rows: root.bookRows
                                Layout.fillWidth: true
                            }

                            MediaColumn {
                                title: "Video Player"
                                icon: "play.svg"
                                rows: root.playerRows
                                Layout.fillWidth: true
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.10)
                    }

                    SectionHeader {
                        id: shortcutsHeader
                        objectName: "keyboardGuideShortcutsHeader"
                        KeyNavigation.tab: featuresHeader
                        KeyNavigation.backtab: readingHeader
                        title: "Shortcuts"
                        note: "Open major Colosseum destinations"
                        iconSource: "keyboard.svg"
                        expanded: root.shortcutsOpen
                        onToggleRequested: root.shortcutsOpen = !root.shortcutsOpen
                    }

                    Item {
                        width: parent.width
                        height: root.shortcutsOpen ? shortcutsContent.implicitHeight + 16 : 0
                        clip: true

                        Behavior on height {
                            NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
                        }

                        Column {
                            id: shortcutsContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            anchors.topMargin: 8

                            Repeater {
                                model: root.shortcutRows
                                delegate: GuideRow {
                                    required property var modelData
                                    width: shortcutsContent.width
                                    tokens: modelData.tokens
                                    action: modelData.action
                                    icon: modelData.icon
                                    Accessible.name: modelData.chord + " " + modelData.action
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
