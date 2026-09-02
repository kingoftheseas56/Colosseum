// CalendarPage — the Theatre Calendar (Stage 3, spec §5). A standalone layer over
// the shell (same grammar as ContinueSeeAllPage): the Coming-up rail is the hero
// (your next episodes in airing order, month-agnostic), the month grid is the
// parity view beneath, a day's list expands on click. Fed by your saved series'
// libCalendar slices (CalendarApi). Watched episodes never chip; silenced series
// (notify off) appear nowhere. QML paints; CalendarApi decides.
import QtQuick
import QtQuick.Controls
import "CalendarApi.js" as Cal

Item {
    id: root
    anchors.fill: parent

    property Item backdrop: null
    signal backRequested()
    signal minimizeRequested()
    signal closeRequested()
    signal playRequested(var row)        // an aired episode → resolve sources & play
    signal seriesRequested(var row)      // a future episode → open the series page
    signal discoverRequested()           // empty-shelf invitation → land on Discover

    Theme { id: theme }

    // fixed "now" for this open (recomputed each time the layer mounts)
    property double nowMs: 0
    property int viewYear: 2026
    property int viewMonth: 1            // 1-based
    property int selectedDay: 0         // 0 = none open
    property int keyboardDay: 1
    property int keyboardDayRow: 0

    function daysInViewMonth() { return new Date(viewYear, viewMonth, 0).getDate() }
    function dayHasEpisodes(day) {
        if (!root.grid || !root.grid.weeks) return false
        for (const week of root.grid.weeks)
            for (const cell of week)
                if (cell && cell.inMonth && cell.day === day)
                    return !!(cell.chips && cell.chips.length > 0)
        return false
    }
    function moveKeyboardDay(delta) {
        root.keyboardDay = Math.max(1, Math.min(root.daysInViewMonth(), root.keyboardDay + delta))
    }

    // live data (context properties absent in the headless harness → typeof guards)
    property int collectionRevision: (typeof Collection !== "undefined") ? Collection.revision : 0
    property int progressRevision: (typeof Progress !== "undefined") ? Progress.revision : 0
    property var entries: []
    property var watchedIds: ({})

    function rebuild() {
        if (typeof Collection !== "undefined") entries = Collection.items("theatre") || []
        else entries = []
        var map = {}
        if (typeof Progress !== "undefined") {
            for (var i = 0; i < entries.length; i++) {
                var vids = (entries[i].payload && entries[i].payload.libCalendar) || []
                for (var j = 0; j < vids.length; j++) {
                    var g = Progress.get("video", String(vids[j].id))
                    if (g && g.watched === true) map[String(vids[j].id)] = true
                }
            }
        }
        watchedIds = map
    }
    onCollectionRevisionChanged: rebuild()
    onProgressRevisionChanged: rebuild()

    Component.onCompleted: {
        nowMs = new Date().getTime()
        var d = new Date(nowMs)
        viewYear = d.getFullYear()
        viewMonth = d.getMonth() + 1
        keyboardDay = d.getDate()
        rebuild()
    }

    readonly property var comingRows: Cal.comingUp(entries, nowMs, 20, watchedIds)
    readonly property var grid: Cal.monthGrid(entries, viewYear, viewMonth, nowMs, watchedIds)
    readonly property int monthEpisodes: Cal.monthCount(entries, viewYear, viewMonth, nowMs, watchedIds)
    readonly property var dayRows: selectedDay > 0
        ? Cal.dayList(entries, new Date(viewYear, viewMonth - 1, selectedDay, 12, 0, 0).getTime(), watchedIds, nowMs)
        : []
    readonly property bool shelfEmpty: comingRows.length === 0 && monthEpisodes === 0

    function prevMonth() {
        selectedDay = 0
        if (viewMonth === 1) { viewMonth = 12; viewYear-- } else viewMonth--
        keyboardDay = Math.min(keyboardDay, daysInViewMonth())
    }
    function nextMonth() {
        selectedDay = 0
        if (viewMonth === 12) { viewMonth = 1; viewYear++ } else viewMonth++
        keyboardDay = Math.min(keyboardDay, daysInViewMonth())
    }
    function chipRoute(seriesId, state) {
        var row = { seriesId: seriesId }
        if (state === "aired") root.playRequested(row); else root.seriesRequested(row)
    }

    // ---- wallpaper (layer over the shell backdrop) ----
    Item {
        anchors.fill: parent
        ShaderEffectSource {
            anchors.fill: parent; sourceItem: root.backdrop; live: true; hideSource: false
            visible: root.backdrop !== null
        }
        Rectangle { anchors.fill: parent; color: Qt.rgba(0.03, 0.04, 0.07, 0.86) }
    }

    Flickable {
        id: page
        anchors.fill: parent
        activeFocusOnTab: true
        Keys.onPressed: (event) => pageKeys.handle(event)
        contentWidth: width
        contentHeight: col.implicitHeight + 60
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: HouseScrollBar { flick: page }
        ScrollGlide { flick: page }
        KeyboardScrollController { id: pageKeys; flick: page }

        Column {
            id: col
            x: theme.margin
            width: root.width - theme.margin * 2
            topPadding: 16
            spacing: 26

            // ── header band ──
            Column {
                width: parent.width
                spacing: 6
                Row {
                    spacing: 14
                    Text {
                        text: "‹"; color: backMa.containsMouse || backKey.activeFocus ? theme.gold : theme.ink
                        font.family: theme.display; font.pixelSize: 30
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea { id: backMa; anchors.fill: parent; anchors.margins: -10
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested() }
                        KeyboardAction { id: backKey; anchors.fill: parent; anchors.margins: -10; pointerEnabled: false
                            accessibleName: "Back from Calendar"; onTriggered: root.backRequested() }
                    }
                    Text { text: "THEATRE · COLLECTION"; color: theme.inkDimmer
                        font.family: theme.ui; font.pixelSize: 12; font.letterSpacing: 2.6; font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter }
                }
                Text { text: "Calendar"; color: theme.ink; font.family: theme.display; font.pixelSize: 46; font.letterSpacing: -1 }
                Text {
                    text: root.monthEpisodes === 0 ? "Nothing dated this month"
                        : root.monthEpisodes + (root.monthEpisodes === 1 ? " episode coming to your shelf this month"
                                                                        : " episodes coming to your shelf this month")
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14
                }
            }

            // ── empty shelf invitation ──
            Column {
                visible: root.shelfEmpty
                width: parent.width
                spacing: 14
                topPadding: 40
                Text { text: "Nothing scheduled"; color: theme.ink; font.family: theme.display; font.pixelSize: 28 }
                Text { text: "Save some running shows and their next episodes land here."
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
                Rectangle {
                    width: goText.implicitWidth + 34; height: 40; radius: 11; color: theme.gold
                    Text { id: goText; anchors.centerIn: parent; text: "Browse Discover"
                        color: "#17120a"; font.family: theme.ui; font.pixelSize: 14; font.weight: Font.DemiBold }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.discoverRequested() }
                    KeyboardAction { anchors.fill: parent; pointerEnabled: false
                        accessibleName: "Browse Discover"; onTriggered: root.discoverRequested() }
                }
            }

            // ── Coming-up rail (the hero) ──
            Column {
                visible: !root.shelfEmpty
                width: parent.width
                spacing: 12
                Text { text: "Coming up"; color: theme.ink; font.family: theme.display; font.pixelSize: 24 }
                Text { visible: root.comingRows.length === 0
                    text: "All caught up — nothing dated ahead."
                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 14 }
                ListView {
                    id: comingList
                    visible: root.comingRows.length > 0
                    width: parent.width; height: 176
                    orientation: ListView.Horizontal; spacing: 14; clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    activeFocusOnTab: visible
                    onActiveFocusChanged: if (activeFocus && currentIndex < 0 && count > 0) currentIndex = 0
                    Keys.onPressed: (event) => comingKeys.handle(event)
                    model: root.comingRows
                    highlight: Rectangle {
                        color: "transparent"; radius: 12; border.width: 2; border.color: theme.inkDim
                        visible: comingList.activeFocus
                    }
                    KeyboardCollectionController {
                        id: comingKeys
                        view: comingList
                        orientation: "horizontal"
                        onActivated: (index) => {
                            const row = root.comingRows[index]
                            if (!row) return
                            if (row.state === "aired") root.playRequested(row)
                            else root.seriesRequested(row)
                        }
                    }
                    delegate: Item {
                        id: rc
                        required property var modelData
                        width: 250; height: 160
                        Rectangle {
                            anchors.fill: parent; radius: 12
                            color: Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1
                            border.color: rc.modelData.state === "aired" ? theme.gold : theme.edge
                            Row {
                                anchors.fill: parent; anchors.margins: 12; spacing: 12
                                Rectangle {
                                    width: 90; height: parent.height; radius: 7; clip: true
                                    color: "#161821"; border.width: 1; border.color: theme.edge
                                    Image { anchors.fill: parent; source: rc.modelData.cover || ""
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true
                                        opacity: status === Image.Ready ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 160 } } }
                                }
                                Column {
                                    width: parent.width - 102; height: parent.height; spacing: 6
                                    Text { width: parent.width; text: rc.modelData.dayLabel
                                        color: rc.modelData.state === "aired" ? theme.gold : theme.inkDim
                                        font.family: theme.ui; font.pixelSize: 12; font.weight: Font.DemiBold }
                                    Text { width: parent.width; text: rc.modelData.title
                                        color: theme.ink; font.family: theme.display; font.pixelSize: 17
                                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                                    Text { width: parent.width
                                        text: "S" + rc.modelData.season + " · E" + rc.modelData.episode
                                              + (rc.modelData.epTitle ? "  " + rc.modelData.epTitle : "")
                                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12
                                        elide: Text.ElideRight }
                                    Item { width: 1; height: 2 }
                                    Text { text: rc.modelData.state === "aired" ? "▶ Play" : "View series"
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12 }
                                }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: rc.modelData.state === "aired" ? root.playRequested(rc.modelData)
                                                                          : root.seriesRequested(rc.modelData) }
                        }
                    }
                }
            }

            // ── month grid ──
            Column {
                visible: !root.shelfEmpty
                width: parent.width
                spacing: 12
                Row {
                    width: parent.width
                    Text { text: root.grid ? root.grid.label : ""
                        color: theme.ink; font.family: theme.display; font.pixelSize: 24
                        anchors.verticalCenter: parent.verticalCenter }
                    Item { width: parent.width - monthTitleNav.childrenRect.width - (root.grid ? 200 : 0); height: 1 }
                    Row {
                        id: monthTitleNav
                        spacing: 8; anchors.verticalCenter: parent.verticalCenter
                        Rectangle { width: 34; height: 34; radius: 10; color: prevMa.containsMouse || prevKey.activeFocus ? Qt.rgba(1,1,1,0.12) : Qt.rgba(1,1,1,0.05)
                            border.width: 1; border.color: theme.edge
                            Text { anchors.centerIn: parent; text: "‹"; color: theme.ink; font.family: theme.display; font.pixelSize: 20 }
                            MouseArea { id: prevMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.prevMonth() }
                            KeyboardAction { id: prevKey; anchors.fill: parent; pointerEnabled: false; accessibleName: "Previous month"; onTriggered: root.prevMonth() } }
                        Rectangle { width: 34; height: 34; radius: 10; color: nextMa.containsMouse || nextKey.activeFocus ? Qt.rgba(1,1,1,0.12) : Qt.rgba(1,1,1,0.05)
                            border.width: 1; border.color: theme.edge
                            Text { anchors.centerIn: parent; text: "›"; color: theme.ink; font.family: theme.display; font.pixelSize: 20 }
                            MouseArea { id: nextMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.nextMonth() }
                            KeyboardAction { id: nextKey; anchors.fill: parent; pointerEnabled: false; accessibleName: "Next month"; onTriggered: root.nextMonth() } }
                    }
                }
                // weekday header
                Row {
                    width: parent.width
                    Repeater {
                        model: ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"]
                        delegate: Item {
                            required property var modelData
                            width: (col.width) / 7; height: 22
                            Text { anchors.left: parent.left; anchors.leftMargin: 4; text: modelData
                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1 }
                        }
                    }
                }
                // weeks — one spatial focus unit; arrows move the day cursor, Enter/Space opens a populated day.
                Column {
                    id: monthGrid
                    width: parent.width; spacing: 6
                    activeFocusOnTab: visible
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Left) root.moveKeyboardDay(-1)
                        else if (event.key === Qt.Key_Right) root.moveKeyboardDay(1)
                        else if (event.key === Qt.Key_Up) root.moveKeyboardDay(-7)
                        else if (event.key === Qt.Key_Down) root.moveKeyboardDay(7)
                        else if (event.key === Qt.Key_Home) root.keyboardDay = 1
                        else if (event.key === Qt.Key_End) root.keyboardDay = root.daysInViewMonth()
                        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                            if (root.dayHasEpisodes(root.keyboardDay))
                                root.selectedDay = root.selectedDay === root.keyboardDay ? 0 : root.keyboardDay
                            else return
                        } else return
                        event.accepted = true
                    }
                    Repeater {
                        model: root.grid ? root.grid.weeks : []
                        delegate: Row {
                            required property var modelData
                            width: col.width; spacing: 0
                            Repeater {
                                model: modelData
                                delegate: Item {
                                    required property var modelData
                                    width: (col.width) / 7; height: 84
                                    Rectangle {
                                        anchors.fill: parent; anchors.margins: 3; radius: 9
                                        visible: modelData.inMonth
                                        color: modelData.day === root.selectedDay ? Qt.rgba(0.94,0.77,0.29,0.10) : Qt.rgba(1,1,1,0.035)
                                        border.width: 1
                                        border.color: modelData.isToday ? theme.gold
                                            : modelData.day === root.selectedDay ? Qt.rgba(0.94,0.77,0.29,0.5) : theme.edge
                                        Rectangle {
                                            anchors.fill: parent; anchors.margins: -1; radius: 10
                                            color: "transparent"; border.width: 2; border.color: theme.inkDim
                                            visible: monthGrid.activeFocus && modelData.inMonth && modelData.day === root.keyboardDay
                                        }
                                        Text { anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 7
                                            text: modelData.day
                                            color: modelData.isToday ? theme.gold : theme.inkDim
                                            font.family: theme.ui; font.pixelSize: 12; font.weight: modelData.isToday ? Font.DemiBold : Font.Normal }
                                        // chips
                                        Row {
                                            anchors.bottom: parent.bottom; anchors.left: parent.left
                                            anchors.margins: 7; spacing: 4
                                            Repeater {
                                                model: modelData.chips
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    width: 22; height: 30; radius: 4; clip: true
                                                    color: "#161821"
                                                    border.width: 1
                                                    border.color: modelData.state === "aired" ? theme.gold : theme.edge
                                                    Image { anchors.fill: parent; source: modelData.cover || ""
                                                        fillMode: Image.PreserveAspectCrop; asynchronous: true }
                                                }
                                            }
                                            Text { visible: modelData.overflow > 0; text: "+" + modelData.overflow
                                                color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                                                anchors.verticalCenter: parent.verticalCenter }
                                        }
                                        MouseArea { anchors.fill: parent
                                            enabled: modelData.chips && modelData.chips.length > 0
                                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                            onClicked: {
                                                root.keyboardDay = modelData.day
                                                root.selectedDay = (root.selectedDay === modelData.day ? 0 : modelData.day)
                                            } }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── selected-day list ──
            Column {
                id: dayListFocus
                visible: root.selectedDay > 0 && root.dayRows.length > 0
                width: parent.width
                spacing: 10
                activeFocusOnTab: visible
                onVisibleChanged: if (visible) root.keyboardDayRow = Math.max(0, Math.min(root.keyboardDayRow, root.dayRows.length - 1))
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Up) root.keyboardDayRow = Math.max(0, root.keyboardDayRow - 1)
                    else if (event.key === Qt.Key_Down) root.keyboardDayRow = Math.min(root.dayRows.length - 1, root.keyboardDayRow + 1)
                    else if (event.key === Qt.Key_Home) root.keyboardDayRow = 0
                    else if (event.key === Qt.Key_End) root.keyboardDayRow = root.dayRows.length - 1
                    else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        const row = root.dayRows[root.keyboardDayRow]
                        if (row) root.chipRoute(row.seriesId, row.state)
                    } else return
                    event.accepted = true
                }
                Text { text: root.grid ? (root.grid.label.split(" ")[0] + " " + root.selectedDay) : ""
                    color: theme.ink; font.family: theme.display; font.pixelSize: 20 }
                Repeater {
                    model: root.dayRows
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: parent.width; height: 52; radius: 10
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.width: dayListFocus.activeFocus && index === root.keyboardDayRow ? 2 : 1
                        border.color: dayListFocus.activeFocus && index === root.keyboardDayRow ? theme.inkDim
                            : modelData.state === "aired" ? Qt.rgba(0.94,0.77,0.29,0.4) : theme.edge
                        Row {
                            anchors.left: parent.left; anchors.leftMargin: 16
                            anchors.verticalCenter: parent.verticalCenter; spacing: 12
                            Text { text: modelData.title; color: theme.ink; font.family: theme.ui
                                font.pixelSize: 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "S" + modelData.season + "E" + modelData.episode
                                   + (modelData.epTitle ? " · " + modelData.epTitle : "")
                                color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Text { anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
                            text: modelData.state === "aired" ? "▶ Play" : "Upcoming"
                            color: modelData.state === "aired" ? theme.gold : theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 13 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.keyboardDayRow = index
                                root.chipRoute(modelData.seriesId, modelData.state)
                            } }
                    }
                }
            }

            Item { width: 1; height: 20 }
        }
    }

    // ── minimize · power cluster (every layer carries it) ──
    Row {
        anchors.right: parent.right; anchors.top: parent.top
        anchors.rightMargin: 25; anchors.topMargin: 18
        spacing: 18
        Item {
            width: 17; height: 17
            Image { anchors.fill: parent; source: "../assets/icons/minimize.svg"; opacity: minMa.hovered || minKey.activeFocus ? 1 : 0.7 }
            HoverHandler { id: minMa }
            MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: root.minimizeRequested() }
            KeyboardAction { id: minKey; anchors.fill: parent; anchors.margins: -6; pointerEnabled: false
                accessibleName: "Minimize"; onTriggered: root.minimizeRequested() }
        }
        Item {
            width: 17; height: 17
            Image { anchors.fill: parent; source: "../assets/icons/power.svg"; opacity: powMa.hovered || closeKey.activeFocus ? 1 : 0.7 }
            HoverHandler { id: powMa }
            MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: root.closeRequested() }
            KeyboardAction { id: closeKey; anchors.fill: parent; anchors.margins: -6; pointerEnabled: false
                accessibleName: "Close Colosseum"; onTriggered: root.closeRequested() }
        }
    }
}
