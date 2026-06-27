pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import "Subtitles.js" as Subtitles
import "SubtitleGroups.js" as SubtitleGroups

Item {
    id: menu
    width: 48
    height: 48

    property bool panelOpen: false
    property var tracks: []
    property alias delegateModel: menu.tracks
    property string selectedId: ""
    property real delay: 0
    property alias syncValue: menu.delay
    property bool loading: false
    property int count: (tracks || []).length
    property int panelWidth: 500
    property int panelHeight: 400
    property string icon: ""
    property string title: ""
    property string emptyText: ""
    property bool offRow: true
    property bool active: selectedId !== ""
    property string searchType: ""
    property string searchId: ""

    property string lang: "__all__"
    property string source: "all"
    property bool hi: true
    property bool forced: false
    property bool searching: false
    property bool searchLoading: false
    property var searchResults: null
    property string searchError: ""

    readonly property var groups: SubtitleGroups.groupByLanguage(tracks)
    readonly property var visibleTracks: SubtitleGroups.filterTracks(tracks, {
        "lang": lang,
        "source": source === "all" ? undefined : source,
        "hi": hi,
        "forced": forced
    })
    readonly property int allCount: (tracks || []).length
    readonly property int embeddedCount: countSource(false)
    readonly property int externalCount: countSource(true)

    signal toggleRequested(bool wasOpen)
    signal trackPicked(string trackId)
    signal offPicked()
    signal delaySet(real value)
    signal delayStep(real delta)
    signal resetDelay()
    signal styleRequested()
    signal fileLoaded(url fileUrl)
    signal onlinePicked(string fileUrl, string title, string lang)

    function countSource(external) {
        var n = 0;
        for (var i = 0; i < (tracks || []).length; i++)
            if (!!tracks[i].external === external)
                n++;
        return n;
    }

    function fmtSigned(value) {
        return (value >= 0 ? "+" : "") + Number(value).toFixed(2) + "s";
    }

    function rowLabel(track) {
        return track.label || track.title || track.lang || track.id || "Subtitle";
    }

    function rowMeta(track) {
        var parts = [];
        if (track.lang && String(track.lang).trim() !== "")
            parts.push(String(track.lang).toUpperCase());
        parts.push(track.external ? "EXTERNAL" : "EMBEDDED");
        if (track.codec && String(track.codec).trim() !== "")
            parts.push(String(track.codec).toUpperCase());
        if (track.forced)
            parts.push("Forced");
        if (track.hearingImpaired)
            parts.push("HI/SDH");
        if (track.default)
            parts.push("Default");
        return parts.join(" · ");
    }

    function runSearch() {
        if (!searchId.length || searchLoading)
            return;
        searchLoading = true;
        searchError = "";
        var reqId = searchId;
        Subtitles.fetch(searchType || "movie", searchId, function(list) {
            if (menu.searchId !== reqId)
                return;
            menu.searchLoading = false;
            menu.searchResults = list || [];
        });
    }

    Theme { id: theme }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: menu.panelOpen ? Qt.rgba(1, 1, 1, 0.22)
                              : launchMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
    }
    IconGlyph {
        anchors.centerIn: parent
        width: 24
        height: 24
        kind: "subtitle"
        ink: menu.panelOpen || menu.active ? theme.gold : theme.ink
    }
    Rectangle {
        visible: menu.active
        width: 6
        height: 6
        radius: 3
        anchors.top: parent.top
        anchors.topMargin: 9
        anchors.right: parent.right
        anchors.rightMargin: 9
        color: "#4ad07a"
    }
    MouseArea {
        id: launchMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: menu.toggleRequested(menu.panelOpen)
    }

    Rectangle {
        visible: menu.panelOpen
        z: 30
        width: 500
        height: 400
        x: parent.width - width
        y: -height - 10
        radius: 16
        color: Qt.rgba(12 / 255, 14 / 255, 18 / 255, 0.94)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)
        clip: true

        Text {
            id: title
            x: 16
            y: 15
            text: "Subtitles"
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Text {
            anchors.left: title.right
            anchors.leftMargin: 8
            anchors.verticalCenter: title.verticalCenter
            text: menu.allCount
            color: theme.inkDimmer
            font.family: "Consolas"
            font.pixelSize: 12
        }
        HeaderButton {
            anchors.right: closeButton.left
            anchors.rightMargin: 2
            anchors.top: parent.top
            anchors.topMargin: 7
            icon: "sliders"
            onClicked: menu.styleRequested()
        }
        HeaderButton {
            id: closeButton
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 7
            icon: "x"
            onClicked: menu.panelOpen = false
        }
        Rectangle {
            x: 0
            y: 50
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }

        Rectangle {
            id: aside
            x: 0
            y: 51
            width: 128
            height: parent.height - y
            color: Qt.rgba(1, 1, 1, 0.025)
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Qt.rgba(1, 1, 1, 0.08)
            }
            Flickable {
                anchors.fill: parent
                anchors.margins: 8
                contentWidth: width
                contentHeight: asideColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: asideColumn
                    width: parent.width
                    spacing: 3

                    AsideItem {
                        width: parent.width
                        text: menu.active ? "On" : "Off"
                        selected: menu.active
                        radio: true
                        countText: ""
                        onClicked: {
                            menu.offPicked();
                            menu.panelOpen = false;
                        }
                    }
                    Text {
                        width: parent.width
                        topPadding: 8
                        leftPadding: 4
                        text: "LANGUAGES"
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 1.6
                    }
                    AsideItem {
                        visible: menu.groups.length > 1
                        width: parent.width
                        text: "All"
                        selected: menu.lang === "__all__"
                        countText: ""
                        iconText: "Aa"
                        onClicked: menu.lang = "__all__"
                    }
                    Repeater {
                        model: menu.groups
                        delegate: AsideItem {
                            required property var modelData
                            width: asideColumn.width
                            text: modelData.label
                            selected: menu.lang === modelData.key
                            countText: modelData.count
                            onClicked: menu.lang = modelData.key
                        }
                    }
                }
            }
        }

        Item {
            id: pane
            x: 128
            y: 51
            width: parent.width - x
            height: parent.height - y

            Item {
                visible: !menu.searching
                anchors.fill: parent

                Row {
                    id: tabs
                    x: 12
                    y: 8
                    width: parent.width - 24
                    height: 27
                    spacing: 6
                    TabPill {
                        text: "All " + menu.allCount
                        selected: menu.source === "all"
                        onClicked: menu.source = "all"
                    }
                    TabPill {
                        text: "Embedded " + menu.embeddedCount
                        selected: menu.source === "embedded"
                        enabled: menu.embeddedCount > 0
                        onClicked: menu.source = "embedded"
                    }
                    TabPill {
                        text: "External " + menu.externalCount
                        selected: menu.source === "external"
                        enabled: menu.externalCount > 0
                        onClicked: menu.source = "external"
                    }
                    Item { width: Math.max(0, tabs.width - 288); height: 1 }
                    TabPill {
                        text: "HI"
                        selected: menu.hi
                        compact: true
                        onClicked: menu.hi = !menu.hi
                    }
                    TabPill {
                        text: "Forced"
                        selected: menu.forced
                        compact: true
                        onClicked: menu.forced = !menu.forced
                    }
                }
                Rectangle {
                    x: 0
                    y: 43
                    width: parent.width
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.08)
                }
                ListView {
                    id: variants
                    x: 8
                    y: 52
                    width: parent.width - 16
                    height: parent.height - y - 82
                    clip: true
                    spacing: 4
                    boundsBehavior: Flickable.StopAtBounds
                    model: menu.visibleTracks
                    delegate: VariantRow {
                        required property var modelData
                        width: variants.width
                        track: modelData
                        selected: String(modelData.id) === menu.selectedId || modelData.selected === true
                        onClicked: {
                            menu.trackPicked(String(modelData.id));
                            menu.panelOpen = false;
                        }
                    }
                }
                Text {
                    visible: menu.visibleTracks.length === 0
                    x: 20
                    y: 94
                    width: parent.width - 40
                    text: menu.loading ? "Finding subtitles..." : "No tracks match these filters. Try toggling HI/SDH or Forced."
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Row {
                    id: footer
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: delayRow.top
                    height: 38
                    FooterButton {
                        width: parent.width - 112
                        text: "Find more subtitles"
                        icon: "search"
                        onClicked: {
                            menu.searching = true;
                            menu.searchError = "";
                        }
                    }
                    Rectangle { width: 1; height: parent.height; color: Qt.rgba(1, 1, 1, 0.08) }
                    FooterButton {
                        width: 111
                        text: "Load file"
                        icon: "folder"
                        onClicked: subtitleDialog.open()
                    }
                }
            }

            Item {
                visible: menu.searching
                anchors.fill: parent

                Row {
                    id: searchHead
                    x: 12
                    y: 10
                    width: parent.width - 24
                    height: 34
                    spacing: 8
                    Rectangle {
                        width: parent.width - 52
                        height: 34
                        radius: 8
                        color: Qt.rgba(1, 1, 1, 0.07)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.09)
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: menu.searchId.length ? ("IMDb " + menu.searchId) : "Search OpenSubtitles..."
                            color: menu.searchId.length ? theme.ink : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        width: 44
                        height: 34
                        radius: 8
                        color: menu.searchId.length ? theme.gold : Qt.rgba(1, 1, 1, 0.07)
                        opacity: menu.searchLoading ? 0.7 : 1
                        Text {
                            anchors.centerIn: parent
                            text: menu.searchLoading ? "..." : "Go"
                            color: menu.searchId.length ? "#111111" : theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: menu.searchId.length > 0 && !menu.searchLoading
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: menu.runSearch()
                        }
                    }
                }

                ListView {
                    id: searchList
                    x: 8
                    y: 54
                    width: parent.width - 16
                    height: parent.height - y - 44
                    clip: true
                    spacing: 4
                    boundsBehavior: Flickable.StopAtBounds
                    model: menu.searchResults || []
                    delegate: Rectangle {
                        id: resultRow
                        required property int index
                        required property var modelData
                        width: searchList.width
                        height: (index === 0 || SubtitleGroups.langKey(modelData) !== SubtitleGroups.langKey(menu.searchResults[index - 1])) ? 62 : 42
                        radius: 8
                        color: resultMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : "transparent"

                        Text {
                            visible: resultRow.index === 0 || SubtitleGroups.langKey(resultRow.modelData) !== SubtitleGroups.langKey(menu.searchResults[resultRow.index - 1])
                            x: 4
                            y: 2
                            text: resultRow.modelData.label + " · " + menu.groupCount(resultRow.modelData.lang)
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            font.letterSpacing: 1.6
                        }
                        Text {
                            x: 10
                            y: parent.height - 35
                            width: parent.width - 20
                            text: resultRow.modelData.title || resultRow.modelData.label || "OpenSubtitles"
                            color: theme.ink
                            font.family: theme.ui
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Text {
                            x: 10
                            y: parent.height - 18
                            width: parent.width - 20
                            text: String(resultRow.modelData.lang || "UNKNOWN").toUpperCase() + " · " + (resultRow.modelData.downloads || 0) + " dl"
                            color: theme.inkDimmer
                            font.family: theme.ui
                            font.pixelSize: 10
                            font.letterSpacing: 0.6
                        }
                        MouseArea {
                            id: resultMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                menu.onlinePicked(resultRow.modelData.url, resultRow.modelData.title || resultRow.modelData.label || "OpenSubtitles", resultRow.modelData.lang || "");
                                menu.panelOpen = false;
                            }
                        }
                    }
                }
                Text {
                    visible: !menu.searchId.length || menu.searchResults === null || (menu.searchResults && menu.searchResults.length === 0)
                    x: 20
                    y: 96
                    width: parent.width - 40
                    text: !menu.searchId.length ? "Online search needs a matched title (IMDb id). Use Load file instead."
                         : menu.searchResults === null ? "Press search to find subtitles online."
                         : "No subtitles found."
                    color: theme.inkDimmer
                    font.family: theme.ui
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                FooterButton {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: delayRow.top
                    height: 38
                    text: "Back to tracks"
                    icon: "back"
                    onClicked: menu.searching = false
                }
            }

            DelayRow {
                id: delayRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                value: menu.delay
                onStep: function(delta) {
                    menu.delaySet(Math.round((menu.delay + delta) * 100) / 100);
                    menu.delayStep(delta);
                }
                onReset: {
                    menu.delaySet(0);
                    menu.resetDelay();
                }
            }
        }
    }

    FileDialog {
        id: subtitleDialog
        title: "Load subtitle file"
        nameFilters: ["Subtitle files (*.srt *.ass *.ssa *.vtt *.sub)", "All files (*)"]
        onAccepted: menu.fileLoaded(selectedFile)
    }

    function groupCount(langCode) {
        var key = ("" + (langCode || "")).trim().toLowerCase() || "unknown";
        var count = 0;
        for (var i = 0; i < (searchResults || []).length; i++)
            if (SubtitleGroups.langKey(searchResults[i]) === key)
                count++;
        return count;
    }

    component HeaderButton: Item {
        id: button
        property string icon: ""
        signal clicked()
        width: 36
        height: 36
        Rectangle {
            anchors.fill: parent
            radius: 18
            color: mouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        }
        Text {
            anchors.centerIn: parent
            text: button.icon === "x" ? "x" : "≡"
            color: mouse.containsMouse ? theme.ink : theme.inkDim
            rotation: button.icon === "sliders" ? 90 : 0
            font.family: theme.ui
            font.pixelSize: button.icon === "x" ? 16 : 18
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }

    component AsideItem: Rectangle {
        id: item
        property string text: ""
        property string countText: ""
        property string iconText: ""
        property bool selected: false
        property bool radio: false
        signal clicked()
        height: 32
        radius: 8
        color: selected ? Qt.rgba(1, 1, 1, 0.10) : itemMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : "transparent"
        border.width: selected ? 1 : 0
        border.color: Qt.rgba(1, 1, 1, 0.11)
        Rectangle {
            visible: item.radio || item.iconText === ""
            x: 8
            y: 8
            width: item.radio ? 16 : 18
            height: item.radio ? 16 : 13
            radius: item.radio ? 8 : 2
            color: item.radio && item.selected ? theme.gold : Qt.rgba(1, 1, 1, 0.13)
            Text {
                anchors.centerIn: parent
                text: item.radio && item.selected ? "✓" : ""
                color: "#111111"
                font.family: theme.ui
                font.pixelSize: 10
                font.weight: Font.Bold
            }
        }
        Text {
            visible: item.iconText !== ""
            x: 8
            width: 20
            anchors.verticalCenter: parent.verticalCenter
            text: item.iconText
            color: theme.inkDim
            font.family: theme.ui
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
        Text {
            x: 34
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 52
            text: item.text
            color: item.selected ? theme.ink : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 12
            font.weight: item.selected ? Font.DemiBold : Font.Medium
            elide: Text.ElideRight
        }
        Text {
            visible: item.countText !== ""
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: item.countText
            color: theme.inkDimmer
            font.family: "Consolas"
            font.pixelSize: 10
        }
        MouseArea {
            id: itemMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: item.clicked()
        }
    }

    component TabPill: Rectangle {
        id: pill
        property string text: ""
        property bool selected: false
        property bool compact: false
        signal clicked()
        width: compact ? 48 : Math.max(72, label.implicitWidth + 20)
        height: 24
        radius: 12
        color: selected ? theme.gold : Qt.rgba(1, 1, 1, 0.08)
        opacity: enabled ? 1 : 0.4
        Text {
            id: label
            anchors.centerIn: parent
            text: pill.text
            color: pill.selected ? "#111111" : theme.inkDim
            font.family: theme.ui
            font.pixelSize: 11
            font.weight: pill.selected ? Font.DemiBold : Font.Medium
        }
        MouseArea {
            anchors.fill: parent
            enabled: pill.enabled
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: pill.clicked()
        }
    }

    component VariantRow: Rectangle {
        id: row
        property var track
        property bool selected: false
        signal clicked()
        height: 54
        radius: 8
        color: selected ? Qt.rgba(1, 1, 1, 0.10) : rowMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : "transparent"
        border.width: selected ? 1 : 0
        border.color: Qt.rgba(1, 1, 1, 0.11)
        Rectangle {
            x: 10
            y: 11
            width: 16
            height: 16
            radius: 8
            color: row.selected ? theme.gold : Qt.rgba(1, 1, 1, 0.12)
            Text {
                anchors.centerIn: parent
                text: row.selected ? "✓" : ""
                color: "#111111"
                font.family: theme.ui
                font.pixelSize: 10
                font.weight: Font.Bold
            }
        }
        Text {
            x: 36
            y: 8
            width: parent.width - 48
            text: menu.rowLabel(row.track)
            color: theme.ink
            font.family: theme.ui
            font.pixelSize: 12
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            x: 36
            y: 28
            width: parent.width - 48
            text: menu.rowMeta(row.track)
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 10
            font.letterSpacing: 0.6
            elide: Text.ElideRight
        }
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.clicked()
        }
    }

    component FooterButton: Rectangle {
        id: button
        property string text: ""
        property string icon: ""
        signal clicked()
        height: 38
        color: footerMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : "transparent"
        Row {
            anchors.centerIn: parent
            spacing: 6
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: button.icon === "search" ? "⌕" : button.icon === "folder" ? "□" : "<"
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: button.text
                color: theme.inkDim
                font.family: theme.ui
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
        MouseArea {
            id: footerMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }

    component DelayRow: Rectangle {
        id: delayControl
        property real value: 0
        signal step(real delta)
        signal reset()
        height: 44
        color: "transparent"
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }
        Row {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 12
            spacing: 6
            Text {
                width: 116
                anchors.verticalCenter: parent.verticalCenter
                text: "SYNC"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.4
            }
            StepButton { text: "-0.1"; onClicked: delayControl.step(-0.1) }
            Text {
                width: 58
                anchors.verticalCenter: parent.verticalCenter
                text: menu.fmtSigned(delayControl.value)
                color: theme.ink
                font.family: "Consolas"
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
            StepButton { text: "+0.1"; onClicked: delayControl.step(0.1) }
            StepButton {
                visible: Math.abs(delayControl.value) > 0.0001
                width: 32
                text: "0"
                onClicked: delayControl.reset()
            }
        }
    }

    component StepButton: Rectangle {
        id: button
        property string text: ""
        signal clicked()
        width: 44
        height: 24
        radius: 6
        color: stepMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)
        Text {
            anchors.centerIn: parent
            text: button.text
            color: theme.inkDim
            font.family: "Consolas"
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
        MouseArea {
            id: stepMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }

    component IconGlyph: Canvas {
        id: glyph
        property string kind: ""
        property color ink: theme.ink
        antialiasing: true
        onKindChanged: requestPaint()
        onInkChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d");
            var w = width;
            var h = height;
            var s = Math.min(w, h);
            var cx = w / 2;
            var cy = h / 2;
            ctx.clearRect(0, 0, w, h);
            ctx.strokeStyle = ink;
            ctx.lineWidth = Math.max(1.6, s / 15);
            ctx.lineCap = "round";
            ctx.lineJoin = "round";
            function line(x1, y1, x2, y2) {
                ctx.beginPath();
                ctx.moveTo(cx + x1 * s, cy + y1 * s);
                ctx.lineTo(cx + x2 * s, cy + y2 * s);
                ctx.stroke();
            }
            ctx.strokeRect(cx - 0.31 * s, cy - 0.21 * s, 0.62 * s, 0.42 * s);
            line(-0.20, 0.02, -0.02, 0.02);
            line(0.08, 0.02, 0.22, 0.02);
            line(-0.20, 0.14, 0.20, 0.14);
        }
    }
}
