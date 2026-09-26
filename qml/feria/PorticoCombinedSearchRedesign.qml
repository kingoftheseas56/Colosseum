import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "PorticoData.js" as Data
import ".." as Colosseum

Item {
    id: root
    objectName: "searchVariantView"

    required property var controller

    readonly property real u: controller.unit
    readonly property real m: controller.marginX
    readonly property var hits: controller.visibleResults()
    readonly property var apps: controller.searchableApps()
    readonly property bool hasQuery: controller.query.trim().length > 0
    readonly property bool hasHits: hits.length > 0
    readonly property int totalChoices: hits.length + apps.length
    readonly property real resultGap: 1.0 * u
    readonly property int resultColumns: Math.max(3, Math.floor((scroll.width + resultGap) / (17 * u + resultGap)))
    readonly property real resultCardWidth: (scroll.width - (resultColumns - 1) * resultGap) / resultColumns

    property alias searchField: input

    function suggestionId(label) {
        var q = String(label).trim().toLowerCase()
        var ids = Object.keys(Data.T)
        var i
        for (i = 0; i < ids.length; ++i) {
            var exactTitle = Data.T[ids[i]]
            if (exactTitle.t.toLowerCase() === q)
                return ids[i]
        }
        for (i = 0; i < ids.length; ++i) {
            var exactBy = Data.T[ids[i]]
            if (exactBy.by.toLowerCase() === q)
                return ids[i]
        }
        for (i = 0; i < ids.length; ++i) {
            var item = Data.T[ids[i]]
            var hay = (item.t + " " + item.by).toLowerCase()
            if (hay.indexOf(q) >= 0)
                return ids[i]
        }
        return ""
    }

    function useSuggestion(label) {
        controller.query = label
        input.text = label
        controller.searchResultIndex = 0
        input.forceActiveFocus()
        Qt.callLater(root.revealSelection)
    }

    function moveSelection(delta) {
        if (root.totalChoices <= 0) {
            controller.searchResultIndex = -1
            return
        }
        if (controller.searchResultIndex < 0)
            controller.searchResultIndex = 0
        else
            controller.searchResultIndex = Math.max(0, Math.min(root.totalChoices - 1,
                                                                  controller.searchResultIndex + delta))
        Qt.callLater(root.revealSelection)
    }

    function revealSelection() {
        if (!root.visible || controller.searchResultIndex < 0)
            return

        var target = null
        var index = controller.searchResultIndex
        if (index < root.hits.length) {
            if (index === 0)
                target = featureCard
            else if (resultRepeater)
                target = resultRepeater.itemAt(index - 1)
        } else if (appRepeater) {
            target = appRepeater.itemAt(index - root.hits.length)
        }

        if (!target || !target.visible)
            return

        var p = target.mapToItem(scroll.contentItem, 0, 0)
        var top = p.y - 1.2 * u
        var bottom = p.y + target.height + 1.2 * u
        if (top < scroll.contentY)
            scroll.contentY = Math.max(0, top)
        else if (bottom > scroll.contentY + scroll.height)
            scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height,
                                                   bottom - scroll.height))
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: controller.dusk }
            GradientStop { position: 0.58; color: Qt.rgba(8/255, 10/255, 16/255, 0.985) }
            GradientStop { position: 1; color: controller.night }
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 12 * u
        color: Qt.rgba(86/255, 98/255, 132/255, 0.07)
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(86/255, 98/255, 132/255, 0.09) }
            GradientStop { position: 1; color: "transparent" }
        }
    }

    Item {
        id: mast
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 6.4 * u
        z: 5

        RowLayout {
            anchors {
                fill: parent
                leftMargin: m
                rightMargin: m
            }
            spacing: 1.15 * u

            Rectangle {
                Layout.preferredWidth: back.implicitWidth + 1.45 * u
                Layout.preferredHeight: 2.55 * u
                radius: height / 2
                color: Qt.rgba(0, 0, 0, 0.36)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.14)

                Colosseum.BackAction {
                    id: back
                    anchors.centerIn: parent
                    variant: "plain"
                    label: "Portico"
                    idleColor: controller.mist
                    hoverColor: controller.ink
                    labelSize: 0.88 * u
                    onTriggered: controller.back()
                }
            }

            Text {
                text: "Search"
                color: controller.ink
                font.family: controller.displayFont
                font.pixelSize: 2.2 * u
                font.weight: Font.Medium
                Layout.alignment: Qt.AlignVCenter
            }

            Rectangle {
                id: searchShell
                objectName: "search-field-shell"
                Layout.preferredWidth: Math.min(48 * u, root.width * 0.48)
                Layout.maximumWidth: 48 * u
                Layout.preferredHeight: 3.35 * u
                Layout.alignment: Qt.AlignVCenter
                radius: 0.88 * u
                color: Qt.rgba(1, 1, 1, input.activeFocus ? 0.075 : 0.05)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, input.activeFocus ? 0.22 : 0.12)
                clip: true

                RowLayout {
                    anchors {
                        fill: parent
                        leftMargin: 1.05 * u
                        rightMargin: 1.05 * u
                    }
                    spacing: 0.7 * u

                    Image {
                        Layout.preferredWidth: 1.25 * u
                        Layout.preferredHeight: 1.25 * u
                        source: Qt.resolvedUrl("../../assets/icons/search.svg")
                        opacity: 0.72
                    }

                    TextField {
                        id: input
                        objectName: "searchField"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: controller.query
                        placeholderText: "Title, artist, author, album…"
                        color: controller.ink
                        placeholderTextColor: controller.slate
                        selectionColor: Qt.rgba(240/255, 196/255, 74/255, 0.35)
                        selectedTextColor: controller.ink
                        font.family: controller.uiFont
                        font.pixelSize: 1.15 * u
                        background: null
                        selectByMouse: true
                        verticalAlignment: TextInput.AlignVCenter

                        onTextEdited: {
                            controller.query = text
                            controller.searchResultIndex = 0
                        }

                        Keys.onEscapePressed: controller.back()
                        Keys.onDownPressed: root.moveSelection(1)
                        Keys.onUpPressed: root.moveSelection(-1)
                        Keys.onReturnPressed: controller.activateSearchChoice()
                        Keys.onEnterPressed: controller.activateSearchChoice()
                    }
                }

                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: input.activeFocus ? Math.max(2, 0.13 * u) : 0
                    color: controller.gold
                    visible: input.activeFocus
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                visible: root.width >= 1180
                text: controller.searchResultIndex < 0
                      ? "↑↓ choose   Esc close"
                      : "↑↓ choose   Enter open   Esc close"
                color: controller.slate
                font.family: controller.uiFont
                font.pixelSize: 0.78 * u
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    Flickable {
        id: scroll
        objectName: "search-scroll"
        anchors {
            left: parent.left
            right: parent.right
            top: mast.bottom
            bottom: parent.bottom
            leftMargin: m
            rightMargin: m
        }
        contentWidth: width
        contentHeight: Math.max(height, body.implicitHeight + 2.4 * u)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: body
            width: scroll.width
            spacing: 1.35 * u

            Column {
                id: emptyState
                objectName: "search-empty-state"
                width: parent.width
                visible: !root.hasQuery
                spacing: 0.75 * u

                Text {
                    text: "Find something worth opening."
                    color: controller.ink
                    font.family: controller.displayFont
                    font.pixelSize: 2.45 * u
                    font.weight: Font.Medium
                }

                Text {
                    width: Math.min(parent.width, 62 * u)
                    text: "Portico searches the catalogues and charts already powering your shelves. Start with a title, creator, author, artist, or one of these catalogue-backed shortcuts."
                    color: controller.mist
                    font.family: controller.uiFont
                    font.pixelSize: 0.98 * u
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }

                Item { width: 1; height: 0.45 * u }

                Text {
                    text: "Start somewhere"
                    color: controller.ink
                    font.family: controller.uiFont
                    font.pixelSize: 1.02 * u
                    font.weight: Font.DemiBold
                }

                Flow {
                    width: parent.width
                    spacing: 0.85 * u

                    Repeater {
                        model: Data.TRY
                        delegate: suggestionDelegate
                    }
                }
            }

            Column {
                id: resultsState
                width: parent.width
                visible: root.hasQuery && root.hasHits
                spacing: 1.1 * u

                Row {
                    width: parent.width
                    spacing: 0.8 * u

                    Text {
                        text: root.hits.length + (root.hits.length === 1 ? " match" : " matches")
                        color: controller.ink
                        font.family: controller.displayFont
                        font.pixelSize: 1.8 * u
                        font.weight: Font.Medium
                    }

                    Text {
                        anchors.baseline: parent.children[0].baseline
                        text: "for “" + controller.query.trim() + "”"
                        color: controller.mist
                        font.family: controller.uiFont
                        font.pixelSize: 0.95 * u
                    }
                }

                Rectangle {
                    id: featureCard
                    objectName: "search-feature"
                    width: parent.width
                    height: 13.2 * u
                    radius: 1.25 * u
                    color: Qt.rgba(1, 1, 1, controller.searchResultIndex === 0 ? 0.07 : 0.045)
                    border.width: controller.searchResultIndex === 0 ? Math.max(2, 0.15 * u) : 1
                    border.color: controller.searchResultIndex === 0 ? controller.gold : Qt.rgba(1, 1, 1, 0.10)

                    clip: true

                    property var feature: root.hits.length ? controller.titleObj(root.hits[0]) : null

                    Row {
                        anchors {
                            fill: parent
                            margins: 1.0 * u
                        }
                        spacing: 1.25 * u

                        Rectangle {
                            width: 7.5 * u
                            height: 11.2 * u
                            radius: 0.72 * u
                            color: Qt.rgba(1, 1, 1, 0.05)
                            clip: true

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop {
                                    position: 0
                                    color: featureCard.feature ? controller.toneFor(root.hits[0])[0] : Qt.rgba(1,1,1,0.05)
                                }
                                GradientStop {
                                    position: 1
                                    color: featureCard.feature ? controller.toneFor(root.hits[0])[1] : Qt.rgba(1,1,1,0.02)
                                }
                            }

                            Image {
                                anchors.fill: parent
                                source: featureCard.feature ? controller.artUrl(featureCard.feature, false) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                visible: status === Image.Ready
                            }
                        }

                        Column {
                            width: Math.min(31 * u, parent.width * 0.34)
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0.38 * u

                            Text {
                                text: featureCard.feature
                                      ? Data.KIND[featureCard.feature.k].toUpperCase()
                                        + (featureCard.feature.y ? "   ·   " + featureCard.feature.y : "")
                                      : ""
                                color: controller.mist
                                font.family: controller.uiFont
                                font.pixelSize: 0.78 * u
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.65
                            }

                            Text {
                                width: parent.width
                                text: featureCard.feature ? featureCard.feature.t : ""
                                color: controller.ink
                                font.family: controller.displayFont
                                font.pixelSize: 2.15 * u
                                font.weight: Font.Medium
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: featureCard.feature ? featureCard.feature.by : ""
                                color: controller.mist
                                font.family: controller.uiFont
                                font.pixelSize: 0.92 * u
                                elide: Text.ElideRight
                            }

                            Text {
                                visible: controller.searchResultIndex === 0
                                text: "Enter to open"
                                color: controller.gold
                                font.family: controller.uiFont
                                font.pixelSize: 0.78 * u
                                font.weight: Font.DemiBold
                            }
                        }

                        Rectangle {
                            width: 1
                            height: 8.5 * u
                            anchors.verticalCenter: parent.verticalCenter
                            color: Qt.rgba(1,1,1,0.10)
                        }

                        Column {
                            width: Math.max(16 * u, parent.width - 41.5 * u)
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0.55 * u

                            Text {
                                width: parent.width
                                text: featureCard.feature ? featureCard.feature.s : ""
                                color: controller.mist
                                font.family: controller.uiFont
                                font.pixelSize: 0.94 * u
                                lineHeight: 1.35
                                wrapMode: Text.WordWrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }

                            Text {
                                text: "Available through"
                                color: controller.slate
                                font.family: controller.uiFont
                                font.pixelSize: 0.76 * u
                                font.weight: Font.DemiBold
                            }

                            Text {
                                width: parent.width
                                text: featureCard.feature
                                      ? controller.offers(featureCard.feature).map(function(d) {
                                            return controller.providerName(d.pk)
                                        }).join("   ·   ")
                                      : ""
                                color: controller.ink
                                font.family: controller.uiFont
                                font.pixelSize: 0.9 * u
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: controller.searchResultIndex = 0
                        onClicked: if (root.hits.length) controller.openTitle(root.hits[0])
                    }
                }

                Text {
                    visible: root.hits.length > 1
                    text: "More matches"
                    color: controller.ink
                    font.family: controller.uiFont
                    font.pixelSize: 1.02 * u
                    font.weight: Font.DemiBold
                }

                Flow {
                    id: resultFlow
                    width: parent.width
                    spacing: root.resultGap
                    visible: root.hits.length > 1

                    Repeater {
                        id: resultRepeater
                        model: root.hits.slice(1)
                        delegate: resultDelegate
                    }
                }
            }

            Column {
                id: noResultsState
                width: parent.width
                visible: root.hasQuery && !root.hasHits
                spacing: 1.0 * u

                Rectangle {
                    objectName: "search-no-results"
                    width: parent.width
                    height: 10.5 * u
                    radius: 1.25 * u
                    color: Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.09)

                    Row {
                        anchors {
                            fill: parent
                            margins: 1.25 * u
                        }
                        spacing: 1.3 * u

                        Rectangle {
                            width: 5.2 * u
                            height: width
                            radius: width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Qt.rgba(1, 1, 1, 0.055)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.10)

                            Image {
                                anchors.centerIn: parent
                                width: 2.0 * u
                                height: 2.0 * u
                                source: Qt.resolvedUrl("../../assets/icons/search.svg")
                                opacity: 0.68
                            }
                        }

                        Column {
                            width: parent.width - 7 * u
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0.45 * u

                            Text {
                                text: "No catalogue matches"
                                color: controller.ink
                                font.family: controller.displayFont
                                font.pixelSize: 2.0 * u
                                font.weight: Font.Medium
                            }

                            Text {
                                width: parent.width
                                text: "Nothing in Portico’s current catalogues matches “" + controller.query.trim()
                                      + "”. Try another title or creator, pick a shortcut below, or hand the search to one of your apps."
                                color: controller.mist
                                font.family: controller.uiFont
                                font.pixelSize: 0.96 * u
                                lineHeight: 1.35
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                Text {
                    text: "Try a catalogue shortcut"
                    color: controller.ink
                    font.family: controller.uiFont
                    font.pixelSize: 1.02 * u
                    font.weight: Font.DemiBold
                }

                Flow {
                    width: parent.width
                    spacing: 0.85 * u

                    Repeater {
                        model: Data.TRY
                        delegate: suggestionDelegate
                    }
                }
            }

            Item { width: 1; height: 0.25 * u }

            Column {
                id: appSection
                width: parent.width
                spacing: 0.75 * u

                Row {
                    width: parent.width
                    spacing: 0.8 * u

                    Text {
                        text: root.hasHits ? "Search the app instead" : "Search directly in an app"
                        color: controller.ink
                        font.family: controller.uiFont
                        font.pixelSize: 1.02 * u
                        font.weight: Font.DemiBold
                    }

                    Text {
                        anchors.baseline: parent.children[0].baseline
                        text: "Opens that provider’s own search"
                        color: controller.slate
                        font.family: controller.uiFont
                        font.pixelSize: 0.78 * u
                    }
                }

                Flow {
                    width: parent.width
                    spacing: 0.75 * u

                    Repeater {
                        id: appRepeater
                        model: root.apps

                        delegate: Rectangle {
                            required property string modelData
                            required property int index

                            objectName: "search-app-" + modelData
                            width: 16.5 * u
                            height: 4.45 * u
                            radius: 0.95 * u
                            color: selected ? Qt.rgba(1,1,1,0.085) : Qt.rgba(1,1,1,0.045)
                            border.width: selected ? Math.max(2, 0.15 * u) : 1
                            border.color: selected ? controller.gold : Qt.rgba(1,1,1,0.10)

                            property bool selected: controller.searchResultIndex === root.hits.length + index

                            Row {
                                anchors {
                                    fill: parent
                                    margins: 0.75 * u
                                }
                                spacing: 0.75 * u

                                Rectangle {
                                    width: 2.75 * u
                                    height: width
                                    radius: 0.7 * u
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: Qt.rgba(1,1,1,0.055)
                                    border.width: 1
                                    border.color: Qt.rgba(1,1,1,0.08)

                                    PorticoCombinedGlyph {
                                        anchors.centerIn: parent
                                        width: 1.55 * u
                                        height: 1.55 * u
                                        glyphKey: modelData
                                        tone: controller.ink
                                    }
                                }

                                Column {
                                    width: parent.width - 4.1 * u
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 0.08 * u

                                    Text {
                                        width: parent.width
                                        text: controller.providerName(modelData)
                                        color: controller.ink
                                        font.family: controller.uiFont
                                        font.pixelSize: 0.95 * u
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: selected ? "Enter to search" : "Provider search"
                                        color: selected ? controller.gold : controller.slate
                                        font.family: controller.uiFont
                                        font.pixelSize: 0.72 * u
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onEntered: controller.searchResultIndex = root.hits.length + index
                                onClicked: controller.openHost(modelData, "", "search")
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 2.2 * u }
        }
    }

    Component {
        id: suggestionDelegate

        Rectangle {
            required property string modelData
            required property int index

            objectName: "search-suggestion-" + index
            width: (scroll.width - 2 * 0.85 * u) / 3
            height: 7.6 * u
            radius: 1.0 * u
            color: hovered ? Qt.rgba(1,1,1,0.072) : Qt.rgba(1,1,1,0.042)
            border.width: 1
            border.color: hovered ? Qt.rgba(1,1,1,0.18) : Qt.rgba(1,1,1,0.09)
            clip: true

            property bool hovered: false
            property string matchedId: root.suggestionId(modelData)
            property var matchedItem: matchedId ? controller.titleObj(matchedId) : null

            Rectangle {
                id: suggestionArt
                x: 0.7 * u
                y: 0.7 * u
                width: 5.2 * u
                height: parent.height - 1.4 * u
                radius: 0.68 * u
                clip: true
                color: Qt.rgba(1,1,1,0.05)

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0
                        color: parent.parent.matchedId
                               ? controller.toneFor(parent.parent.matchedId)[0]
                               : Qt.rgba(68/255,78/255,104/255,0.65)
                    }
                    GradientStop {
                        position: 1
                        color: parent.parent.matchedId
                               ? controller.toneFor(parent.parent.matchedId)[1]
                               : Qt.rgba(18/255,22/255,34/255,0.75)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: modelData.slice(0, 1).toUpperCase()
                    color: controller.ink
                    opacity: 0.72
                    font.family: controller.displayFont
                    font.pixelSize: 2.0 * u
                }

                Image {
                    anchors.fill: parent
                    source: parent.parent.matchedItem
                            ? controller.artUrl(parent.parent.matchedItem, false)
                            : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    visible: status === Image.Ready
                }
            }

            Column {
                x: suggestionArt.x + suggestionArt.width + 0.9 * u
                y: 1.15 * u
                width: parent.width - x - 0.8 * u
                spacing: 0.22 * u

                Text {
                    width: parent.width
                    text: parent.parent.matchedItem
                          ? Data.KIND[parent.parent.matchedItem.k].toUpperCase()
                          : "SEARCH"
                    color: controller.slate
                    font.family: controller.uiFont
                    font.pixelSize: 0.68 * u
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.55
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: modelData
                    color: controller.ink
                    font.family: controller.displayFont
                    font.pixelSize: 1.24 * u
                    font.weight: Font.Medium
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: parent.parent.matchedItem && parent.parent.matchedItem.by
                          ? parent.parent.matchedItem.by
                          : "Search Portico"
                    color: controller.mist
                    font.family: controller.uiFont
                    font.pixelSize: 0.76 * u
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: parent.hovered = true
                onExited: parent.hovered = false
                onClicked: root.useSuggestion(modelData)
            }
        }
    }

    Component {
        id: resultDelegate

        Item {
            required property string modelData
            required property int index

            objectName: "search-result-" + (index + 1)
            width: root.resultCardWidth
            height: artBox.height + 3.45 * u

            property var resultItem: controller.titleObj(modelData)
            property string resultShape: resultItem ? controller.shapeFor(resultItem) : "poster"
            property bool selected: controller.searchResultIndex === index + 1

            Rectangle {
                id: artBox
                width: parent.width
                height: parent.resultShape === "poster" ? parent.width * 1.28 : parent.width
                radius: parent.resultShape === "circle" ? width / 2
                                                        : (parent.resultShape === "square" ? 0.62 * u : 0.78 * u)
                clip: true
                color: Qt.rgba(1,1,1,0.05)
                border.width: parent.selected ? Math.max(2, 0.15 * u) : 1
                border.color: parent.selected ? controller.gold : Qt.rgba(1,1,1,0.09)
                scale: parent.selected ? 1.025 : 1
                transformOrigin: Item.Center

                Behavior on scale {
                    NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: controller.toneFor(modelData)[0] }
                        GradientStop { position: 1; color: controller.toneFor(modelData)[1] }
                    }
                }

                Text {
                    anchors {
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                        margins: 0.75 * u
                    }
                    text: resultItem ? resultItem.t : ""
                    color: controller.ink
                    font.family: controller.displayFont
                    font.pixelSize: 1.02 * u
                    font.weight: Font.Medium
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }

                Image {
                    anchors.fill: parent
                    source: resultItem ? controller.artUrl(resultItem, false) : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    visible: status === Image.Ready
                }

                Rectangle {
                    x: 0.48 * u
                    y: 0.48 * u
                    height: 1.55 * u
                    width: kindText.implicitWidth + 0.85 * u
                    radius: height / 2
                    color: Qt.rgba(4/255, 5/255, 9/255, 0.82)
                    border.width: 1
                    border.color: Qt.rgba(1,1,1,0.12)

                    Text {
                        id: kindText
                        anchors.centerIn: parent
                        text: resultItem ? Data.KIND[resultItem.k].toUpperCase() : ""
                        color: controller.ink
                        font.family: controller.uiFont
                        font.pixelSize: 0.62 * u
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.45
                    }
                }
            }

            Text {
                id: resultTitle
                anchors {
                    left: parent.left
                    right: parent.right
                    top: artBox.bottom
                    topMargin: 0.58 * u
                }
                text: parent.resultItem ? parent.resultItem.t : ""
                color: parent.selected ? controller.ink : controller.mist
                font.family: controller.uiFont
                font.pixelSize: 0.88 * u
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                anchors {
                    left: parent.left
                    right: parent.right
                    top: resultTitle.bottom
                    topMargin: 0.15 * u
                }
                text: parent.resultItem
                      ? (parent.resultItem.y ? parent.resultItem.y + "   ·   " : "")
                        + Data.KIND[parent.resultItem.k]
                      : ""
                color: parent.selected ? controller.gold : controller.slate
                font.family: controller.uiFont
                font.pixelSize: 0.7 * u
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: controller.searchResultIndex = index + 1
                onClicked: controller.openTitle(modelData)
            }
        }
    }


    Connections {
        target: controller

        function onSearchResultIndexChanged() {
            Qt.callLater(root.revealSelection)
        }

        function onQueryChanged() {
            scroll.contentY = 0
            Qt.callLater(root.revealSelection)
        }
    }

    Component.onCompleted: if (visible) Qt.callLater(function() {
        input.forceActiveFocus()
    })

    onVisibleChanged: if (visible) Qt.callLater(function() {
        scroll.contentY = 0
        input.forceActiveFocus()
    })
}
