import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: browser
    property var episodes: []
    property var seasons: []
    property int currentSeason: 1
    property string layoutMode: "list"
    property string sortMode: "oldest"
    property string query: ""
    property bool searchOpen: false
    property bool seasonMenuOpen: false
    property bool seasonMarkedWatched: false

    signal playRequested(var episode)
    signal detailsRequested(var episode)
    signal downloadRequested(var episode)

    implicitHeight: content.implicitHeight
    height: implicitHeight
    HarborTheme { id: theme }

    function currentSeasonRecord() {
        for (var i = 0; i < seasons.length; ++i)
            if (Number(seasons[i].number) === Number(currentSeason)) return seasons[i]
        return seasons.length ? seasons[0] : ({ label: "Seasons", count: episodes.length, year: "" })
    }

    function filteredEpisodes() {
        var out = []
        var needle = query.trim().toLowerCase()
        for (var i = 0; i < episodes.length; ++i) {
            var episode = episodes[i]
            if (Number(episode.season || 1) !== Number(currentSeason)) continue
            if (needle.length > 0) {
                var hay = (String(episode.title || "") + " " + String(episode.overview || "")).toLowerCase()
                if (hay.indexOf(needle) < 0) continue
            }
            out.push(episode)
        }
        if (sortMode === "newest") out.reverse()
        return out
    }

    function selectRandomEpisode() {
        var visible = filteredEpisodes()
        if (!visible.length) return
        var pick = visible[Math.floor(Math.random() * visible.length)]
        browser.playRequested(pick)
    }

    Column {
        id: content
        width: parent.width
        spacing: 24

        Row {
            width: parent.width
            spacing: 12

            Text {
                text: "Episodes"
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 22
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                id: seasonPicker
                width: Math.min(240, seasonPickerRow.implicitWidth + 28)
                height: 40
                radius: 20
                color: seasonAction.interactionActive ? Qt.rgba(1, 1, 1, 0.08)
                                                      : Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.90)
                border.width: 1
                border.color: theme.edge
                anchors.verticalCenter: parent.verticalCenter

                Row {
                    id: seasonPickerRow
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        text: browser.currentSeasonRecord().label || ("Season " + browser.currentSeason)
                        color: theme.ink
                        font.family: theme.ui
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                    Text {
                        text: browser.seasonMenuOpen ? "⌃" : "⌄"
                        color: theme.inkDim
                        font.family: theme.ui
                        font.pixelSize: 14
                    }
                }

                KeyboardAction {
                    id: seasonAction
                    anchors.fill: parent
                    accessibleName: "Choose season"
                    focusRadius: parent.radius
                    onTriggered: browser.seasonMenuOpen = !browser.seasonMenuOpen
                }
            }

            Item { width: Math.max(0, parent.width - controls.implicitWidth - seasonPicker.width - 120); height: 1 }

            Row {
                id: controls
                spacing: 10
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: randomAction.interactionActive ? Qt.rgba(1, 1, 1, 0.08) : "transparent"

                    HarborEpisodeLayoutGlyph {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        kind: "shuffle"
                        ink: randomAction.interactionActive ? theme.ink : theme.inkDimmer
                    }

                    KeyboardAction {
                        id: randomAction
                        anchors.fill: parent
                        accessibleName: "Play a random episode"
                        focusRadius: parent.radius
                        onTriggered: browser.selectRandomEpisode()
                    }
                }

                Rectangle {
                    width: 108
                    height: 40
                    radius: 20
                    color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.90)
                    border.width: 1
                    border.color: theme.edge

                    Row {
                        anchors.centerIn: parent
                        spacing: 2

                        Repeater {
                            model: [
                                { key: "list", label: "List" },
                                { key: "strip", label: "Horizontal" },
                                { key: "grid", label: "Grid" }
                            ]
                            delegate: Rectangle {
                                id: layoutChoice
                                required property var modelData
                                width: 32
                                height: 32
                                radius: 16
                                color: browser.layoutMode === modelData.key ? theme.ink : "transparent"

                                HarborEpisodeLayoutGlyph {
                                    anchors.centerIn: parent
                                    width: 16
                                    height: 16
                                    kind: layoutChoice.modelData.key
                                    ink: browser.layoutMode === layoutChoice.modelData.key ? "#17181c" : theme.inkDim
                                }

                                KeyboardAction {
                                    anchors.fill: parent
                                    accessibleName: layoutChoice.modelData.label + " view"
                                    focusRadius: parent.radius
                                    onTriggered: browser.layoutMode = layoutChoice.modelData.key
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: sortRow.implicitWidth + 26
                    height: 40
                    radius: 20
                    color: sortAction.interactionActive ? Qt.rgba(1, 1, 1, 0.08)
                                                        : Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.90)
                    border.width: 1
                    border.color: theme.edge

                    Row {
                        id: sortRow
                        anchors.centerIn: parent
                        spacing: 7

                        Text {
                            text: "⇅"
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 14
                        }
                        Text {
                            text: browser.sortMode === "oldest" ? "Oldest" : "Newest"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                        Text {
                            text: "⌄"
                            color: theme.inkDim
                            font.family: theme.ui
                            font.pixelSize: 13
                        }
                    }

                    KeyboardAction {
                        id: sortAction
                        anchors.fill: parent
                        accessibleName: "Episode sort"
                        focusRadius: parent.radius
                        onTriggered: browser.sortMode = browser.sortMode === "oldest" ? "newest" : "oldest"
                    }
                }

                HarborActionButton {
                    width: 40
                    height: 40
                    compact: true
                    iconKind: browser.seasonMarkedWatched ? "eyeOff" : "check"
                    label: browser.seasonMarkedWatched ? "Mark season unwatched" : "Mark season watched"
                    checked: browser.seasonMarkedWatched
                    onTriggered: browser.seasonMarkedWatched = !browser.seasonMarkedWatched
                }

                HarborActionButton {
                    width: 40
                    height: 40
                    compact: true
                    iconKind: "search"
                    label: "Search episodes"
                    checked: browser.searchOpen
                    onTriggered: browser.searchOpen = !browser.searchOpen
                }
            }
        }

        Rectangle {
            visible: browser.seasonMenuOpen
            width: 288
            height: Math.min(360, seasonMenuColumn.implicitHeight + 12)
            radius: 16
            color: "#0d0f14"
            border.width: 1
            border.color: theme.edge
            z: 50

            Flickable {
                anchors.fill: parent
                anchors.margins: 6
                contentWidth: width
                contentHeight: seasonMenuColumn.implicitHeight
                clip: true

                Column {
                    id: seasonMenuColumn
                    width: parent.width

                    Repeater {
                        model: browser.seasons
                        delegate: Rectangle {
                            required property var modelData
                            width: seasonMenuColumn.width
                            height: 54
                            radius: 10
                            color: Number(modelData.number) === Number(browser.currentSeason)
                                   ? Qt.rgba(1, 1, 1, 0.08)
                                   : (itemAction.interactionActive ? Qt.rgba(1, 1, 1, 0.05) : "transparent")

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2

                                Text {
                                    text: modelData.label
                                    color: theme.ink
                                    font.family: theme.ui
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                }
                                Text {
                                    text: modelData.count + " episodes" + (modelData.year ? "  ·  " + modelData.year : "")
                                    color: theme.inkDimmer
                                    font.family: theme.ui
                                    font.pixelSize: 11
                                }
                            }

                            KeyboardAction {
                                id: itemAction
                                anchors.fill: parent
                                accessibleName: modelData.label
                                focusRadius: parent.radius
                                onTriggered: {
                                    browser.currentSeason = Number(modelData.number)
                                    browser.seasonMenuOpen = false
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: browser.searchOpen
            width: parent.width
            height: 44
            radius: 12
            color: Qt.rgba(17 / 255, 18 / 255, 19 / 255, 0.90)
            border.width: 1
            border.color: searchField.activeFocus ? theme.inkDimmer : theme.edge

            HarborIcon {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                iconSize: 16
                kind: "search"
                ink: theme.inkDimmer
            }

            TextInput {
                id: searchField
                anchors.left: parent.left
                anchors.leftMargin: 42
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                color: theme.ink
                font.family: theme.ui
                font.pixelSize: 13
                selectionColor: theme.gold
                selectedTextColor: "#111111"
                clip: true
                onTextChanged: browser.query = text
                Keys.onEscapePressed: {
                    text = ""
                    browser.query = ""
                    browser.searchOpen = false
                }
            }

            Text {
                visible: searchField.text.length === 0
                anchors.left: searchField.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Search episodes"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 13
            }
        }

        Text {
            readonly property var record: browser.currentSeasonRecord()
            visible: record && (record.count || record.year)
            text: (record.count || 0) + " episodes" + (record.year ? "  ·  " + record.year : "")
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 13
        }

        Column {
            visible: browser.layoutMode === "list"
            width: parent.width
            spacing: 1

            Repeater {
                model: browser.filteredEpisodes()
                delegate: HarborEpisodeRow {
                    required property var modelData
                    width: parent.width
                    episode: modelData
                    onPlayRequested: episode => browser.playRequested(episode)
                    onDetailsRequested: episode => browser.detailsRequested(episode)
                    onDownloadRequested: episode => browser.downloadRequested(episode)
                }
            }
        }

        Flickable {
            visible: browser.layoutMode === "strip"
            width: parent.width
            height: 285
            contentWidth: stripRow.implicitWidth
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: stripRow
                spacing: 16

                Repeater {
                    model: browser.filteredEpisodes()
                    delegate: Rectangle {
                        required property var modelData
                        width: 280
                        height: 268
                        radius: 14
                        color: cardAction.interactionActive ? Qt.rgba(1, 1, 1, 0.05) : "transparent"

                        Column {
                            anchors.fill: parent
                            spacing: 9

                            Rectangle {
                                width: parent.width
                                height: 158
                                radius: 10
                                color: Qt.rgba(1, 1, 1, 0.06)
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    source: modelData.thumbnail || ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: true
                                }
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 8
                                    height: 23
                                    width: stripScore.implicitWidth + 12
                                    radius: 6
                                    color: Qt.rgba(0, 0, 0, 0.58)
                                    Text {
                                        id: stripScore
                                        anchors.centerIn: parent
                                        text: "IMDb  " + (modelData.rating || "—")
                                        color: theme.ink
                                        font.family: theme.ui
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                            Text {
                                width: parent.width
                                text: modelData.title
                                color: theme.ink
                                font.family: theme.ui
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: "S" + modelData.season + " E" + modelData.number + "  ·  " + modelData.runtime
                                color: theme.inkDimmer
                                font.family: theme.ui
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            HarborEpisodeStats {
                                width: parent.width
                                episode: modelData
                            }
                        }

                        KeyboardAction {
                            id: cardAction
                            anchors.fill: parent
                            accessibleName: "Play " + modelData.title
                            focusRadius: parent.radius
                            onTriggered: browser.playRequested(modelData)
                        }
                    }
                }
            }
        }

        Grid {
            id: grid
            visible: browser.layoutMode === "grid"
            width: parent.width
            columns: Math.max(1, Math.floor(width / 300))
            columnSpacing: 18
            rowSpacing: 22

            Repeater {
                model: browser.filteredEpisodes()
                delegate: Rectangle {
                    required property var modelData
                    width: (grid.width - (grid.columns - 1) * grid.columnSpacing) / grid.columns
                    height: 264
                    radius: 14
                    color: gridAction.interactionActive ? Qt.rgba(1, 1, 1, 0.05) : "transparent"

                    Column {
                        anchors.fill: parent
                        spacing: 8

                        Rectangle {
                            width: parent.width
                            height: Math.min(170, width * 0.5625)
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.06)
                            clip: true
                            Image {
                                anchors.fill: parent
                                source: modelData.thumbnail || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                            }
                        }
                        Text {
                            width: parent.width
                            text: modelData.title
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: "S" + modelData.season + " E" + modelData.number
                                  + "  ·  " + modelData.runtime + "  ·  IMDb " + modelData.rating
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        HarborEpisodeStats {
                            width: parent.width
                            episode: modelData
                        }
                    }

                    KeyboardAction {
                        id: gridAction
                        anchors.fill: parent
                        accessibleName: "Play " + modelData.title
                        focusRadius: parent.radius
                        onTriggered: browser.playRequested(modelData)
                    }
                }
            }
        }

        Text {
            visible: browser.filteredEpisodes().length === 0
            width: parent.width
            text: browser.query.length ? "No episodes match your search." : "No episode fixture for this season."
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            topPadding: 24
            bottomPadding: 24
        }
    }
}
