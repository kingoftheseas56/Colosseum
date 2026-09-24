pragma ComponentBehavior: Bound

import QtQuick
import QtCore
import QtQuick.Dialogs
import QtQuick.Controls as Controls
import "Subtitles.js" as Subtitles
import "SubtitleGroups.js" as SubtitleGroups
import "PlayerFocusContainment.js" as FocusContainment

Item {
    id: menu
    width: faceChip.width
    height: faceChip.height

    property bool panelOpen: false
    property var player: null
    property var tracks: []
    property alias delegateModel: menu.tracks
    property string selectedId: ""
    property real delay: 0
    property alias syncValue: menu.delay
    property string chipValue: ""      // native chrome: live value shown on the chip face
    property bool loading: false
    property int count: (tracks || []).length
    property int panelWidth: 760
    property int panelHeight: 520
    property string icon: ""
    property string title: ""
    property string emptyText: ""
    property bool offRow: true
    property bool active: selectedId !== ""
    property string searchType: ""
    property string searchId: ""
    // Feature 6: one-line neutral automation status, hidden when empty.
    property string autoStatusText: ""
    property bool showAutoStatus: autoStatusText.length > 0

    property string lang: "__all__"
    property string source: "all"
    property bool hi: true
    property bool forced: false
    property string filterMode: "all"
    property bool appearanceOpen: false
    property var fontFamilies: {
        var families = Qt.fontFamilies()
        return families && families.length ? families : ["Arial", "Segoe UI", "Verdana", "Tahoma", "Georgia"]
    }
    property bool searching: false
    property bool searchLoading: false
    property var searchResults: null
    property string searchError: ""

    // Click-through fix (Agent 0 on A4's behalf, 2026-07-15). The popover is hosted on the
    // full-screen chrome layer (like Speed/Fill) so rows above the short bottom dock stay
    // clickable — Qt bounds pointer delivery to the dock ancestor otherwise. When overlayParent
    // is null the popover falls back to its in-menu parent (keeps the component standalone/testable).
    property Item overlayParent: null
    // Confirm-before-close: a pick stays PENDING until mpv confirms the switch (via selectedId),
    // so a slow or rejected selection never looks identical to success.
    property string pendingId: ""
    property bool pendingOff: false
    property bool pendingOnline: false
    property string pendingBaseId: ""
    property string selectionError: ""
    readonly property bool pending: pendingId.length > 0 || pendingOff || pendingOnline

    readonly property var groups: SubtitleGroups.groupByLanguage(tracks)
    readonly property var visibleTracks: filteredTracks()
    readonly property int allCount: (tracks || []).length
    readonly property int embeddedCount: countSource(false)
    readonly property int externalCount: countSource(true)
    property var focusReturnItem: null
    function filteredTracks() {
        var rows = SubtitleGroups.filterTracks(tracks, {
            "lang": lang,
            "source": source === "all" ? undefined : source,
            "hi": true,
            "forced": false
        })
        if (filterMode === "hi")
            return rows.filter(function(track) { return !!track.hearingImpaired })
        if (filterMode === "forced")
            return rows.filter(function(track) { return !!track.forced })
        return rows
    }

    Settings {
        id: subtitleStylePrefs
        category: "subtitleStyle"
        property string fontFamily: "Arial"
        property real scale: 1.0
        property string textColor: "#FFFFFF"
        property real outlineSize: 2.0
        property string outlineColor: "#000000"
        property int position: 94
        property string assOverride: "scale"
        property bool customized: false
    }

    function setStyleOption(key, value) {
        subtitleStylePrefs.customized = true
        if (menu.player && menu.player.setSubOption)
            menu.player.setSubOption(key, value)
    }
    function applyStyle() {
        if (!menu.player || !menu.player.setSubOption || !subtitleStylePrefs.customized)
            return
        menu.player.setSubOption("sub-font", subtitleStylePrefs.fontFamily)
        menu.player.setSubOption("sub-scale", subtitleStylePrefs.scale)
        menu.player.setSubOption("sub-color", subtitleStylePrefs.textColor)
        menu.player.setSubOption("sub-border-size", subtitleStylePrefs.outlineSize)
        menu.player.setSubOption("sub-border-color", subtitleStylePrefs.outlineColor)
        menu.player.setSubOption("sub-pos", subtitleStylePrefs.position)
        menu.player.setSubOption("sub-ass-override", subtitleStylePrefs.assOverride)
    }
    function resetAppearance() {
        subtitleStylePrefs.fontFamily = "Arial"
        subtitleStylePrefs.scale = 1.0
        subtitleStylePrefs.textColor = "#FFFFFF"
        subtitleStylePrefs.outlineSize = 2.0
        subtitleStylePrefs.outlineColor = "#000000"
        subtitleStylePrefs.position = 94
        subtitleStylePrefs.assOverride = "scale"
        subtitleStylePrefs.customized = true
        applyStyle()
    }
    onPlayerChanged: applyStyle()
    Component.onCompleted: applyStyle()
    function restoreFocus() {
        var target = menu.focusReturnItem; menu.focusReturnItem = null
        Qt.callLater(function() { if (target && target.visible && target.enabled && target.forceActiveFocus) target.forceActiveFocus(Qt.TabFocusReason) })
    }

    signal toggleRequested(bool wasOpen)
    signal trackPicked(string trackId)
    signal offPicked()
    signal delaySet(real value)
    signal delayStep(real delta)
    signal resetDelay()
    signal styleRequested()
    signal fileLoaded(url fileUrl)
    signal onlinePicked(string fileUrl, string title, string lang)

    function movePanelFocus(forward) {
        var w = menu.Window.window
        var item = w ? w.activeFocusItem : null
        if (!item || !item.nextItemInFocusChain) return false
        var next = item.nextItemInFocusChain(forward)
        if (!next || next === item) return false
        next.forceActiveFocus(Qt.TabFocusReason)
        return true
    }

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
        // Tier 2 rich rows (2026-07-20): builder pre-computes `tech` (codec · embedded/
        // external, or "provider · fetched" for online). Salient state moves to the
        // right-aligned tag; this line stays the source facts.
        if (track.tech && String(track.tech).length) {
            var lead = (track.lang && String(track.lang).trim() !== "")
                       ? String(track.lang).toUpperCase() + " · " : "";
            return lead + String(track.tech);
        }
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

    function languageOptions() {
        var out = [{ "key": "__all__", "label": "All languages", "count": allCount }]
        for (var i = 0; i < groups.length; ++i) {
            var g = groups[i]
            out.push({ "key": g.key, "label": g.label + " · " + g.count, "count": g.count })
        }
        return out
    }
    function languageIndex() {
        var opts = languageOptions()
        for (var i = 0; i < opts.length; ++i)
            if (opts[i].key === lang)
                return i
        return 0
    }
    function toggleSubtitleState() {
        if (active) {
            pickOff()
            return
        }
        if (!tracks || !tracks.length)
            return
        var candidate = visibleTracks.length ? visibleTracks[0] : tracks[0]
        if (candidate && candidate.id !== undefined)
            pickTrack(String(candidate.id))
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

    function clampX(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
    function clearPending() { pendingId = ""; pendingOff = false; pendingOnline = false; }

    // Route a subtitle pick: emit the request ONCE, mark it pending, and wait for mpv to confirm
    // via selectedId before closing. Callable (not inline in the delegate) so the interaction is
    // drivable headlessly, and so a delayed/failed switch can't masquerade as success.
    function pickTrack(id) {
        selectionError = "";
        pendingId = String(id); pendingOff = false; pendingOnline = false;
        trackPicked(String(id));
        pendingTimer.restart();
        resolvePending();               // re-picking the current track confirms at once
    }
    function pickOff() {
        selectionError = "";
        pendingOff = true; pendingId = ""; pendingOnline = false;
        offPicked();
        pendingTimer.restart();
        resolvePending();
    }
    function pickOnline(fileUrl, title, lang) {
        selectionError = "";
        pendingBaseId = selectedId;
        pendingOnline = true; pendingId = ""; pendingOff = false;
        onlinePicked(fileUrl, title, lang);
        pendingTimer.restart();         // online waits for a NEW selected track (no immediate resolve)
    }
    function resolvePending() {
        if (!pending)
            return;
        var sel = selectedId;
        var done = (pendingId.length > 0 && sel === pendingId)
                || (pendingOff && sel === "")
                || (pendingOnline && sel.length > 0 && sel !== pendingBaseId);
        if (done) {
            clearPending();
            pendingTimer.stop();
            panelOpen = false;
        }
    }
    function failPending() {
        if (!pending)
            return;
        clearPending();                 // stay OPEN — surface the error instead of vanishing
        pendingTimer.stop();
        selectionError = "Couldn't switch subtitle. Try again.";
    }
    // Rise on the full-screen overlay centered-right on the chip, clamped to the window; falls
    // back to the in-menu placement when no overlay is wired (standalone/tests).
    function positionPanel(panel) {
        if (overlayParent) {
            var p = menu.mapToItem(overlayParent, 0, 0);
            panel.x = clampX(p.x + menu.width - panel.width, 10, overlayParent.width - panel.width - 10);
            panel.y = p.y - panel.height - 10;
        } else {
            panel.x = menu.width - panel.width;
            panel.y = -panel.height - 10;
        }
    }

    onSelectedIdChanged: resolvePending()
    onPanelOpenChanged: {
        if (!panelOpen) {
            clearPending(); pendingTimer.stop(); selectionError = ""
            menu.searching = false
            menu.appearanceOpen = false
            if (menu.focusReturnItem) menu.restoreFocus()
            return
        }
        var w = menu.Window.window; menu.focusReturnItem = w ? w.activeFocusItem : null
        Qt.callLater(function() { panel.forceActiveFocus(Qt.PopupFocusReason) })
    }

    Timer { id: pendingTimer; interval: 8000; onTriggered: menu.failPending() }

    Theme { id: theme }

    // Icon face (semantic audit 2026-07-19): the subtitles control is the Lucide `captions` glyph
    // (Lucide 0.460 has no 'subtitles') with a small gold ACTIVE DOT when subtitles are on. No text.
    Item {
        id: faceChip
        width: 40
        height: 40
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: menu.panelOpen ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.16)
                 : launchMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
            border.width: (menu.panelOpen || menu.active) ? 1 : 0
            border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
        }
        PlayerIcon {
            anchors.fill: parent
            kind: "subtitle"
            ink: (menu.panelOpen || menu.active) ? theme.gold : theme.inkDim
            accessibleName: menu.title
        }
        // active dot — subtitles are ON
        Rectangle {
            visible: menu.active && !menu.panelOpen
            width: 6
            height: 6
            radius: 3
            color: theme.gold
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 7
            anchors.topMargin: 7
        }
        MouseArea {
            id: launchMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: menu.toggleRequested(menu.panelOpen)
        }
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            accessibleName: menu.title.length ? menu.title : "Subtitles"
            onTriggered: menu.toggleRequested(menu.panelOpen)
        }
    }

    Rectangle {
        // appletTail — pointer at the chip (sibling of the popover so clip stays on).
        visible: menu.panelOpen
        z: 31
        width: 8; height: 8
        rotation: 45
        x: menu.width / 2 - width / 2
        y: -14
        color: Qt.rgba(0.04, 0.05, 0.07, 0.94)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)
    }

    Rectangle {
        id: panel
        parent: menu.overlayParent ? menu.overlayParent : menu
        visible: menu.panelOpen
        z: menu.overlayParent ? 40 : 30
        width: menu.panelWidth
        height: menu.panelHeight
        onVisibleChanged: if (visible) menu.positionPanel(panel)
        radius: 22
        color: Qt.rgba(10 / 255, 12 / 255, 17 / 255, 0.96)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.14)
        clip: true
        focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                if (menu.appearanceOpen) menu.appearanceOpen = false
                else if (menu.searching) menu.searching = false
                else menu.panelOpen = false
                event.accepted = true
            } else if (event.key === Qt.Key_Down) event.accepted = menu.movePanelFocus(true)
            else if (event.key === Qt.Key_Up) event.accepted = menu.movePanelFocus(false)
        }
        Keys.onTabPressed: function(event) { event.accepted = FocusContainment.move(menu.Window.window, panel, true) }
        Keys.onBacktabPressed: function(event) { event.accepted = FocusContainment.move(menu.Window.window, panel, false) }

        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: {} }

        Item {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 72

            Text {
                id: title
                anchors.left: parent.left
                anchors.leftMargin: 22
                anchors.verticalCenter: parent.verticalCenter
                text: "Subtitles"
                color: theme.ink
                font.family: theme.hud
                font.pixelSize: 19
                font.weight: Font.DemiBold
            }
            Text {
                anchors.left: title.right
                anchors.leftMargin: 8
                anchors.baseline: title.baseline
                text: menu.allCount
                color: theme.inkDimmer
                font.family: theme.hud
                font.features: ({ "tnum": 1 })
                font.pixelSize: 13
            }
            Text {
                id: autoStatus
                visible: menu.selectionError.length > 0
                anchors.right: searchButton.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(260, implicitWidth)
                text: menu.selectionError
                color: "#ff8a8a"
                font.family: theme.hud
                font.pixelSize: 12
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
            }
            HeaderButton {
                id: searchButton
                anchors.right: styleButton.left
                anchors.rightMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                icon: "search"
                active: menu.searching
                accessibleName: "Find more subtitles"
                onClicked: {
                    menu.appearanceOpen = false
                    menu.searching = !menu.searching
                    if (menu.searching) menu.searchError = ""
                }
            }
            HeaderButton {
                id: styleButton
                anchors.right: closeButton.left
                anchors.rightMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                icon: "fit"
                active: menu.appearanceOpen
                accessibleName: "Subtitle appearance"
                onClicked: {
                    menu.searching = false
                    menu.appearanceOpen = !menu.appearanceOpen
                }
            }
            HeaderButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                icon: "cancel"
                accessibleName: "Close subtitles"
                onClicked: menu.panelOpen = false
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }
        }

        Item {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: parent.bottom

            Item {
                id: mainPane
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: menu.appearanceOpen ? parent.width - 280 : parent.width

                Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

                Item {
                    id: tracksPage
                    anchors.fill: parent
                    visible: !menu.searching

                    Row {
                        id: toolbar
                        x: 20
                        y: 18
                        width: parent.width - 40
                        height: 36
                        spacing: 10

                        ToggleButton {
                            active: menu.active
                            enabled: menu.allCount > 0 || menu.active
                            onClicked: menu.toggleSubtitleState()
                        }

                        Row {
                            id: sourceStrip
                            spacing: 4
                            height: 36
                            property int keyboardIndex: menu.source === "embedded" ? 1 : menu.source === "external" ? 2 : 0
                            focusPolicy: menu.panelOpen ? Qt.TabFocus : Qt.NoFocus
                            function enabledAt(index) { return index === 0 || (index === 1 ? menu.embeddedCount > 0 : menu.externalCount > 0) }
                            function activate(index) {
                                if (!enabledAt(index)) return
                                menu.source = index === 0 ? "all" : index === 1 ? "embedded" : "external"
                            }
                            function step(delta) {
                                var next = keyboardIndex + delta
                                while (next >= 0 && next < 3 && !enabledAt(next)) next += delta
                                if (next >= 0 && next < 3) { keyboardIndex = next; return true }
                                return false
                            }
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                                    event.accepted = step(event.key === Qt.Key_Left ? -1 : 1)
                                else if (event.key === Qt.Key_Home) { keyboardIndex = 0; event.accepted = true }
                                else if (event.key === Qt.Key_End) { keyboardIndex = menu.externalCount > 0 ? 2 : menu.embeddedCount > 0 ? 1 : 0; event.accepted = true }
                                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) { activate(keyboardIndex); event.accepted = true }
                            }

                            SegmentButton {
                                text: "All " + menu.allCount
                                selected: menu.source === "all"
                                keyboardEnabled: false
                                keyboardHighlighted: sourceStrip.activeFocus && sourceStrip.keyboardIndex === 0
                                onClicked: menu.source = "all"
                            }
                            SegmentButton {
                                text: "Embedded " + menu.embeddedCount
                                selected: menu.source === "embedded"
                                enabled: menu.embeddedCount > 0
                                keyboardEnabled: false
                                keyboardHighlighted: sourceStrip.activeFocus && sourceStrip.keyboardIndex === 1
                                onClicked: menu.source = "embedded"
                            }
                            SegmentButton {
                                text: "External " + menu.externalCount
                                selected: menu.source === "external"
                                enabled: menu.externalCount > 0
                                keyboardEnabled: false
                                keyboardHighlighted: sourceStrip.activeFocus && sourceStrip.keyboardIndex === 2
                                onClicked: menu.source = "external"
                            }
                        }
                    }

                    Controls.ComboBox {
                        id: languageCollection
                        anchors.right: parent.right
                        anchors.rightMargin: 20
                        y: 77
                        width: 142
                        height: 36
                        model: menu.languageOptions()
                        textRole: "label"
                        currentIndex: menu.languageIndex()
                        font.family: theme.hud
                        font.pixelSize: 14
                        onActivated: function(index) {
                            var row = menu.languageOptions()[index]
                            if (row) menu.lang = row.key
                        }
                        contentItem: Text {
                            leftPadding: 12
                            rightPadding: 28
                            verticalAlignment: Text.AlignVCenter
                            text: languageCollection.displayText
                            color: theme.inkDim
                            font.family: theme.hud
                            font.pixelSize: 14
                            elide: Text.ElideRight
                        }
                        indicator: Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 11
                            anchors.verticalCenter: parent.verticalCenter
                            text: "v"
                            color: theme.inkDimmer
                            font.family: theme.hud
                            font.pixelSize: 14
                        }
                        background: Rectangle {
                            radius: 11
                            color: Qt.rgba(1, 1, 1, 0.055)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)
                        }
                    }

                    Row {
                        id: filters
                        x: 20
                        y: 79
                        height: 32
                        spacing: 7

                        FilterPill { text: "All"; selected: menu.filterMode === "all"; onClicked: menu.filterMode = "all" }
                        FilterPill { text: "HI / SDH"; selected: menu.filterMode === "hi"; onClicked: menu.filterMode = menu.filterMode === "hi" ? "all" : "hi" }
                        FilterPill { text: "Forced"; selected: menu.filterMode === "forced"; onClicked: menu.filterMode = menu.filterMode === "forced" ? "all" : "forced" }
                    }

                    ListView {
                        id: variants
                        x: 20
                        y: 126
                        width: parent.width - 40
                        height: Math.max(40, footer.y - y - 8)
                        clip: true
                        spacing: 6
                        boundsBehavior: Flickable.StopAtBounds
                        model: menu.visibleTracks
                        focusPolicy: menu.panelOpen && count > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: function(event) { variantKeyboard.handle(event) }
                        delegate: VariantRow {
                            required property var modelData
                            width: variants.width
                            track: modelData
                            selected: String(modelData.id) === menu.selectedId || modelData.selected === true
                            pending: menu.pendingId.length > 0 && String(modelData.id) === menu.pendingId
                            keyboardEnabled: false
                            onClicked: menu.pickTrack(String(modelData.id))
                        }
                    }
                    KeyboardCollectionController {
                        id: variantKeyboard
                        view: variants
                        orientation: "vertical"
                        count: variants.count
                        onActivated: function(index) {
                            if (index >= 0 && index < menu.visibleTracks.length)
                                menu.pickTrack(String(menu.visibleTracks[index].id))
                        }
                    }

                    Text {
                        visible: menu.visibleTracks.length === 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 184
                        width: parent.width - 80
                        text: menu.loading ? "Finding subtitles..." : "No tracks match these filters."
                        color: theme.inkDimmer
                        font.family: theme.hud
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        id: footer
                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 13
                        width: Math.min(320, parent.width - 40)
                        height: 48
                        radius: 13
                        color: Qt.rgba(1, 1, 1, 0.025)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.07)

                        Row {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 6

                            FooterButton {
                                width: (parent.width - 6) / 2
                                height: parent.height
                                text: "Find more"
                                icon: "search"
                                onClicked: {
                                    menu.appearanceOpen = false
                                    menu.searching = true
                                    menu.searchError = ""
                                }
                            }
                            FooterButton {
                                width: (parent.width - 6) / 2
                                height: parent.height
                                text: "Load file"
                                icon: "folder"
                                onClicked: subtitleDialog.open()
                            }
                        }
                    }
                }

                Item {
                    id: searchPage
                    anchors.fill: parent
                    visible: menu.searching

                    Row {
                        id: searchHead
                        x: 20
                        y: 20
                        width: parent.width - 40
                        height: 38
                        spacing: 8

                        Rectangle {
                            width: parent.width - 86
                            height: 38
                            radius: 11
                            color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.05)
                            border.width: 1
                            border.color: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.28)
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: menu.searchId.length ? ("IMDb " + menu.searchId) : "Online search needs a matched title"
                                color: menu.searchId.length ? theme.ink : theme.inkDimmer
                                font.family: theme.hud
                                font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                        }
                        Rectangle {
                            width: 78
                            height: 38
                            radius: 11
                            color: menu.searchId.length ? theme.gold : Qt.rgba(1, 1, 1, 0.07)
                            opacity: menu.searchLoading ? 0.7 : 1
                            Text {
                                anchors.centerIn: parent
                                text: menu.searchLoading ? "..." : "Search"
                                color: menu.searchId.length ? "#111111" : theme.inkDimmer
                                font.family: theme.hud
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: menu.searchId.length > 0 && !menu.searchLoading
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: menu.runSearch()
                            }
                            KeyboardAction {
                                anchors.fill: parent
                                pointerEnabled: false
                                focusEnabled: menu.searchId.length > 0 && !menu.searchLoading
                                accessibleName: "Search subtitles"
                                onTriggered: menu.runSearch()
                            }
                        }
                    }

                    ListView {
                        id: searchList
                        x: 20
                        y: 76
                        width: parent.width - 40
                        height: Math.max(40, searchFooter.y - y - 8)
                        clip: true
                        spacing: 6
                        boundsBehavior: Flickable.StopAtBounds
                        model: menu.searchResults || []
                        focusPolicy: menu.panelOpen && count > 0 ? Qt.TabFocus : Qt.NoFocus
                        Keys.onPressed: function(event) { searchKeyboard.handle(event) }
                        delegate: VariantRow {
                            required property var modelData
                            width: searchList.width
                            track: ({
                                "id": modelData.id || modelData.url,
                                "label": modelData.title || modelData.label || "OpenSubtitles",
                                "lang": modelData.lang || "",
                                "tech": "OpenSubtitles \u00B7 fetched",
                                "external": true,
                                "tag": ""
                            })
                            selected: false
                            pending: menu.pendingOnline
                            keyboardEnabled: false
                            onClicked: menu.pickOnline(modelData.url, modelData.title || modelData.label || "OpenSubtitles", modelData.lang || "")
                        }
                    }
                    KeyboardCollectionController {
                        id: searchKeyboard
                        view: searchList
                        orientation: "vertical"
                        count: searchList.count
                        onActivated: function(index) {
                            var row = (menu.searchResults || [])[index]
                            if (!row) return
                            menu.pickOnline(row.url, row.title || row.label || "OpenSubtitles", row.lang || "")
                        }
                    }

                    Text {
                        visible: !menu.searchId.length || menu.searchResults === null || (menu.searchResults && menu.searchResults.length === 0)
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 150
                        width: parent.width - 80
                        text: !menu.searchId.length ? "Online search needs a matched title (IMDb id). Use Load file instead."
                             : menu.searchResults === null ? "Press Search to find subtitles online."
                             : "No subtitles found."
                        color: theme.inkDimmer
                        font.family: theme.hud
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }

                    FooterButton {
                        id: searchFooter
                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 13
                        width: 146
                        text: "Back to tracks"
                        icon: "back"
                        onClicked: menu.searching = false
                    }
                }

                DelayRow {
                    id: delayRow
                    visible: !menu.appearanceOpen
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 13
                    value: menu.delay
                    onStep: function(delta) {
                        menu.delaySet(Math.round((menu.delay + delta) * 100) / 100)
                        menu.delayStep(delta)
                    }
                    onReset: {
                        menu.delaySet(0)
                        menu.resetDelay()
                    }
                }
            }

            Rectangle {
                id: appearanceDrawer
                visible: menu.appearanceOpen
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 280
                color: Qt.rgba(5 / 255, 7 / 255, 10 / 255, 0.72)
                border.width: 0

                Rectangle {
                    anchors.left: parent.left
                    width: 1
                    height: parent.height
                    color: Qt.rgba(1, 1, 1, 0.08)
                }

                Text {
                    id: appearanceTitle
                    x: 18
                    y: 18
                    text: "Appearance"
                    color: theme.ink
                    font.family: theme.hud
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Text {
                    x: 18
                    y: 42
                    text: "Changes apply live and persist."
                    color: theme.inkDimmer
                    font.family: theme.hud
                    font.pixelSize: 11
                }

                Text {
                    x: 18
                    y: 78
                    text: "FONT"
                    color: theme.inkDimmer
                    font.family: theme.hud
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    y: 78
                    text: subtitleStylePrefs.fontFamily
                    color: theme.inkDim
                    font.family: theme.hud
                    font.pixelSize: 11
                }
                Controls.ComboBox {
                    id: fontPicker
                    x: 18
                    y: 98
                    width: parent.width - 36
                    height: 38
                    model: menu.fontFamilies
                    currentIndex: Math.max(0, menu.fontFamilies.indexOf(subtitleStylePrefs.fontFamily))
                    font.family: theme.hud
                    font.pixelSize: 13
                    onActivated: function(index) {
                        subtitleStylePrefs.fontFamily = menu.fontFamilies[index]
                        menu.setStyleOption("sub-font", subtitleStylePrefs.fontFamily)
                    }
                    contentItem: Text {
                        leftPadding: 10
                        rightPadding: 26
                        verticalAlignment: Text.AlignVCenter
                        text: fontPicker.displayText
                        color: theme.ink
                        font.family: subtitleStylePrefs.fontFamily
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    indicator: Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "v"
                        color: theme.inkDimmer
                        font.family: theme.hud
                        font.pixelSize: 14
                    }
                    background: Rectangle {
                        radius: 10
                        color: Qt.rgba(1, 1, 1, 0.055)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.08)
                    }
                }

                StyleSlider {
                    id: sizeSlider
                    x: 18
                    y: 153
                    width: parent.width - 36
                    label: "SIZE"
                    valueText: Math.round(subtitleStylePrefs.scale * 100) + "%"
                    from: 0.5
                    to: 2.0
                    stepSize: 0.1
                    value: subtitleStylePrefs.scale
                    onMoved: function(v) {
                        subtitleStylePrefs.scale = v
                        menu.setStyleOption("sub-scale", v)
                    }
                }

                Text {
                    x: 18
                    y: 216
                    text: "TEXT COLOR"
                    color: theme.inkDimmer
                    font.family: theme.hud
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }
                Swatches {
                    id: textSwatches
                    x: 18
                    y: 235
                    width: parent.width - 36
                    label: ""
                    selected: subtitleStylePrefs.textColor
                    colors: ["#FFFFFF", "#F0C44A", "#EDE7D1", "#9FE7FF"]
                    onPicked: function(color) {
                        subtitleStylePrefs.textColor = color
                        menu.setStyleOption("sub-color", color)
                    }
                }

                StyleSlider {
                    id: outlineSlider
                    x: 18
                    y: 285
                    width: parent.width - 36
                    label: "OUTLINE"
                    valueText: Number(subtitleStylePrefs.outlineSize).toFixed(1)
                    from: 0
                    to: 6
                    stepSize: 0.5
                    value: subtitleStylePrefs.outlineSize
                    onMoved: function(v) {
                        subtitleStylePrefs.outlineSize = v
                        menu.setStyleOption("sub-border-size", v)
                    }
                }

                StyleSlider {
                    id: positionSlider
                    x: 18
                    y: 347
                    width: parent.width - 36
                    label: "VERTICAL POSITION"
                    valueText: String(subtitleStylePrefs.position)
                    from: 0
                    to: 100
                    stepSize: 1
                    value: subtitleStylePrefs.position
                    onMoved: function(v) {
                        subtitleStylePrefs.position = Math.round(v)
                        menu.setStyleOption("sub-pos", subtitleStylePrefs.position)
                    }
                }

                Rectangle {
                    id: preview
                    x: 18
                    y: 410
                    width: parent.width - 36
                    height: 62
                    radius: 12
                    color: Qt.rgba(0, 0, 0, 0.28)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                    clip: true

                    Text {
                        anchors.centerIn: parent
                        text: "Subtitle preview"
                        color: subtitleStylePrefs.textColor
                        font.family: subtitleStylePrefs.fontFamily
                        font.pixelSize: Math.max(12, 19 * subtitleStylePrefs.scale)
                        style: Text.Outline
                        styleColor: subtitleStylePrefs.outlineColor
                    }
                }

                FooterButton {
                    x: 18
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 14
                    width: parent.width - 36
                    height: 34
                    text: "Reset appearance"
                    icon: "reset"
                    onClicked: menu.resetAppearance()
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
        property bool active: false
        property string accessibleName: ""
        signal clicked()
        width: 44
        height: 44
        Rectangle {
            anchors.fill: parent
            radius: 14
            color: button.active ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.14)
                                 : mouse.containsMouse ? Qt.rgba(1, 1, 1, 0.10)
                                                       : Qt.rgba(1, 1, 1, 0.045)
            border.width: 1
            border.color: button.active ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.30)
                                        : Qt.rgba(1, 1, 1, 0.055)
        }
        PlayerIcon {
            anchors.centerIn: parent
            width: 20
            height: 20
            kind: button.icon
            ink: button.active ? theme.gold : mouse.containsMouse ? theme.ink : theme.inkDim
            accessibleName: button.accessibleName
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            accessibleName: button.accessibleName
            onTriggered: button.clicked()
        }
    }

    component ToggleButton: Rectangle {
        id: button
        property bool active: false
        signal clicked()
        width: 78
        height: 36
        radius: 11
        color: toggleMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.075) : Qt.rgba(1, 1, 1, 0.055)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
        opacity: enabled ? 1 : 0.45
        Row {
            anchors.centerIn: parent
            spacing: 9
            Rectangle {
                width: 18
                height: 18
                radius: 9
                color: button.active ? theme.gold : Qt.rgba(1, 1, 1, 0.16)
                Text {
                    anchors.centerIn: parent
                    text: button.active ? "\u2713" : ""
                    color: "#111111"
                    font.family: theme.hud
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: button.active ? "On" : "Off"
                color: button.active ? theme.ink : theme.inkDimmer
                font.family: theme.hud
                font.pixelSize: 14
                font.weight: Font.Medium
            }
        }
        MouseArea {
            id: toggleMouse
            anchors.fill: parent
            enabled: button.enabled
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: button.clicked()
        }
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            focusEnabled: button.enabled
            accessibleName: button.active ? "Turn subtitles off" : "Turn subtitles on"
            onTriggered: button.clicked()
        }
    }

    component SegmentButton: Rectangle {
        id: button
        property string text: ""
        property bool selected: false
        property bool keyboardEnabled: true
        property bool keyboardHighlighted: false
        signal clicked()
        width: Math.max(74, label.implicitWidth + 22)
        height: 36
        radius: 9
        color: button.selected ? Qt.rgba(1, 1, 1, 0.09)
                               : segmentMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : "transparent"
        border.width: button.keyboardHighlighted ? 2 : 0
        border.color: theme.gold
        opacity: enabled ? 1 : 0.42
        Text {
            id: label
            anchors.centerIn: parent
            text: button.text
            color: button.selected ? theme.ink : theme.inkDimmer
            font.family: theme.hud
            font.pixelSize: 14
            font.weight: Font.Medium
        }
        MouseArea {
            id: segmentMouse
            anchors.fill: parent
            enabled: button.enabled
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: button.clicked()
        }
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            focusEnabled: button.enabled && button.keyboardEnabled
            accessibleName: button.text
            onTriggered: button.clicked()
        }
    }

    component FilterPill: Rectangle {
        id: pill
        property string text: ""
        property bool selected: false
        signal clicked()
        width: Math.max(52, label.implicitWidth + 22)
        height: 32
        radius: 16
        color: pill.selected ? theme.gold
                             : filterMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.025)
        border.width: pill.selected ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
        Text {
            id: label
            anchors.centerIn: parent
            text: pill.text
            color: pill.selected ? "#171306" : theme.inkDimmer
            font.family: theme.hud
            font.pixelSize: 12
            font.weight: pill.selected ? Font.DemiBold : Font.Medium
        }
        MouseArea {
            id: filterMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.clicked()
        }
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            accessibleName: pill.text
            onTriggered: pill.clicked()
        }
    }

    component StyleSlider: Item {
        id: control
        property string label: ""
        property string valueText: ""
        property real from: 0
        property real to: 1
        property real stepSize: 0.1
        property real value: 0
        signal moved(real value)
        height: 54

        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            text: control.label
            color: theme.inkDimmer
            font.family: theme.hud
            font.pixelSize: 10
            font.weight: Font.Bold
            font.letterSpacing: 1.2
        }
        Text {
            anchors.right: parent.right
            anchors.top: parent.top
            text: control.valueText
            color: theme.inkDim
            font.family: theme.hud
            font.pixelSize: 11
        }
        Controls.Slider {
            id: slider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 30
            from: control.from
            to: control.to
            stepSize: control.stepSize
            value: control.value
            onMoved: control.moved(value)
            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: 3
                radius: 2
                color: Qt.rgba(1, 1, 1, 0.12)
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: parent.radius
                    color: theme.gold
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: 14
                height: 14
                radius: 7
                color: theme.ink
                border.width: 2
                border.color: theme.gold
            }
        }
    }

    component Swatches: Item {
        id: swatches
        property string label: ""
        property string selected: ""
        property var colors: []
        property int keyboardIndex: Math.max(0, colors.map(function(c) { return String(c).toLowerCase() }).indexOf(String(selected).toLowerCase()))
        signal picked(string color)
        focusPolicy: menu.panelOpen && colors.length > 0 ? Qt.TabFocus : Qt.NoFocus
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                var next = keyboardIndex + (event.key === Qt.Key_Left ? -1 : 1)
                if (next >= 0 && next < colors.length) { keyboardIndex = next; event.accepted = true }
            } else if (event.key === Qt.Key_Home) { keyboardIndex = 0; event.accepted = true }
            else if (event.key === Qt.Key_End) { keyboardIndex = colors.length - 1; event.accepted = true }
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                swatches.picked(String(colors[keyboardIndex])); event.accepted = true
            }
        }
        height: 34
        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Repeater {
                model: swatches.colors
                delegate: Rectangle {
                    id: dot
                    required property int index
                    required property string modelData
                    width: 28
                    height: 28
                    radius: 8
                    color: modelData
                    border.width: (swatches.activeFocus && swatches.keyboardIndex === dot.index)
                                  || swatches.selected.toLowerCase() === modelData.toLowerCase() ? 2 : 1
                    border.color: (swatches.activeFocus && swatches.keyboardIndex === dot.index)
                                  || swatches.selected.toLowerCase() === modelData.toLowerCase()
                                  ? theme.gold : Qt.rgba(1, 1, 1, 0.18)
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: swatches.picked(dot.modelData)
                    }
                }
            }
        }
    }

    component VariantRow: Rectangle {
        id: row
        property var track
        property bool selected: false
        property bool pending: false
        property bool keyboardEnabled: true
        signal clicked()
        height: 66
        radius: 14
        color: row.selected ? Qt.rgba(1, 1, 1, 0.07)
                            : rowMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.045) : "transparent"
        border.width: row.selected ? 1 : 0
        border.color: Qt.rgba(1, 1, 1, 0.10)

        Rectangle {
            x: 18
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 20
            radius: 10
            color: row.selected ? theme.gold
                                : row.pending ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.45)
                                              : Qt.rgba(1, 1, 1, 0.12)
            Text {
                anchors.centerIn: parent
                text: row.selected ? "\u2713" : row.pending ? "..." : ""
                color: "#111111"
                font.family: theme.hud
                font.pixelSize: 11
                font.weight: Font.Bold
            }
        }

        Text {
            x: 68
            y: 13
            width: parent.width - 96 - (subRowTag.visible ? subRowTag.width + 12 : 0)
            text: menu.rowLabel(row.track)
            color: theme.ink
            font.family: theme.hud
            font.pixelSize: 14
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Rectangle {
            id: subRowTag
            visible: !!(row.track.tag && String(row.track.tag).length)
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            width: tagText.implicitWidth + 16
            height: 28
            radius: 14
            color: String(row.track.tag || "").toLowerCase() === "default"
                   ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.08) : "transparent"
            border.width: 1
            border.color: String(row.track.tag || "").toLowerCase() === "default"
                          ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.22)
                          : Qt.rgba(1, 1, 1, 0.08)
            Text {
                id: tagText
                anchors.centerIn: parent
                text: String(row.track.tag || "")
                color: String(row.track.tag || "").toLowerCase() === "default" ? theme.gold : theme.inkDimmer
                font.family: theme.hud
                font.pixelSize: 10
            }
        }

        Text {
            x: 68
            y: 37
            width: parent.width - 96
            text: menu.rowMeta(row.track)
            color: theme.inkDimmer
            font.family: theme.hud
            font.pixelSize: 11
            font.letterSpacing: 0.2
            elide: Text.ElideRight
        }

        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.clicked()
        }
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            focusEnabled: row.keyboardEnabled
            accessibleName: menu.rowLabel(row.track)
            onTriggered: row.clicked()
        }
    }

    component FooterButton: Rectangle {
        id: button
        property string text: ""
        property string icon: ""
        signal clicked()
        height: 36
        radius: 10
        color: footerMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)

        Row {
            anchors.centerIn: parent
            spacing: 8
            PlayerIcon {
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                iconSize: 15
                kind: button.icon
                ink: footerMouse.containsMouse ? theme.ink : theme.inkDim
                accessibleName: button.text
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: button.text
                color: footerMouse.containsMouse ? theme.ink : theme.inkDim
                font.family: theme.hud
                font.pixelSize: 14
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
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            accessibleName: button.text
            onTriggered: button.clicked()
        }
    }

    component DelayRow: Item {
        id: delayControl
        property real value: 0
        signal step(real delta)
        signal reset()
        width: 210
        height: 36

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "SYNC"
                color: theme.inkDimmer
                font.family: theme.hud
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.1
            }

            Rectangle {
                width: 128
                height: 34
                radius: 10
                color: Qt.rgba(1, 1, 1, 0.035)

                Row {
                    anchors.centerIn: parent
                    spacing: 2

                    CompactIconButton {
                        icon: "chevronLeft"
                        accessibleName: "Move subtitles earlier by 0.1 seconds"
                        onClicked: delayControl.step(-0.1)
                    }

                    Rectangle {
                        width: 58
                        height: 28
                        radius: 7
                        color: delayValueMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.055) : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: menu.fmtSigned(delayControl.value)
                            color: theme.ink
                            font.family: theme.hud
                            font.features: ({ "tnum": 1 })
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: delayValueMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: delayControl.reset()
                        }
                        KeyboardAction {
                            anchors.fill: parent
                            pointerEnabled: false
                            accessibleName: "Reset subtitle sync"
                            onTriggered: delayControl.reset()
                        }
                    }

                    CompactIconButton {
                        icon: "chevronRight"
                        accessibleName: "Move subtitles later by 0.1 seconds"
                        onClicked: delayControl.step(0.1)
                    }
                }
            }
        }
    }

    component CompactIconButton: Rectangle {
        id: button
        property string icon: ""
        property string accessibleName: ""
        signal clicked()
        width: 28
        height: 28
        radius: 8
        color: compactMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.075) : "transparent"

        PlayerIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            iconSize: 14
            kind: button.icon
            ink: compactMouse.containsMouse ? theme.ink : theme.inkDimmer
            accessibleName: button.accessibleName
        }
        MouseArea {
            id: compactMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
        KeyboardAction {
            anchors.fill: parent
            pointerEnabled: false
            accessibleName: button.accessibleName
            onTriggered: button.clicked()
        }
    }

}
