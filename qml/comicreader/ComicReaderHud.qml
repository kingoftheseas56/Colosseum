// ComicReaderHud — the Family Gradient HUD (Task 11), built EXACTLY to approved mockup surface 01
// (docs/superpowers/mocks/2026-07-23-comicreader-visual-identity.html). "Lineage layout, player
// soul": glass over black, Segoe UI chrome text, vendored Lucide strokes, and ONE gold thread
// carrying the reading position. The page owns the screen; this chrome is a visitor that rises on
// activity and auto-hides after 3s of stillness.
//
// PRESENTATION + INTENTS ONLY. The HUD binds the shell's read-only reading state off the `reader`
// seam and:
//   * WRITES only the persisted seams (persistedMode / persistedDirection) and chromeVisible — the
//     mode/direction chips write persistedMode/persistedDirection (NOT mode/rtl) so a crossing's
//     load() never resets the toggle (carry-forward from the Task 9 review).
//   * EMITS semantic intents (prev/next page, seek, openNavigator/openThumbnails/openSettings/
//     toggleBookmark, and the window verbs) that the shell / Task 12 overlays wire.
// gold is SPARING: only the scrub fill+knob, the active mode segment, the active direction pill.
//
// Every glyph is a ComicReaderIcon (vendored white-stroke Lucide) — never a Text arrow/character
// (the semantic-icon-audit law). The counter/mode-chip/direction are text LABELS, not glyph chips.

import QtQuick
import "../"   // Theme (lives in qml/, the parent of qml/comicreader/)

Item {
    id: hud
    anchors.fill: parent

    // ================= the shell seam =================
    property var reader: null

    // ---- read-only reading state bound off the shell ----
    readonly property int    currentPage:   reader ? reader.currentPage   : 1
    readonly property int    max:           reader ? reader.max           : 1
    readonly property string mode:          reader ? reader.mode          : "long_strip"
    readonly property bool   rtl:           reader ? reader.rtl           : false
    // the single user-facing identity (Manga/Comic/Strip) — the mode chip shows + writes THIS.
    readonly property string readingMode:   reader ? reader.readingMode   : "manga"
    readonly property real   stripFraction: reader ? reader.stripFraction : 0
    readonly property int    zoomPercent:   reader ? reader.zoomPercent   : 100
    readonly property bool   hasNext:       reader ? reader.hasNext       : false
    readonly property bool   hasPrev:       reader ? reader.hasPrev       : false
    readonly property string seriesTitle:   reader ? reader.seriesTitle   : ""
    readonly property string curLabel:      reader ? reader.curLabel      : ""
    readonly property bool   chromeVisible: reader ? reader.chromeVisible : true
    readonly property bool   modalOpen:     reader ? reader.modalOpen     : false
    // bookmark 0-based page indices (Task 12 fills them; empty until then)
    property var bookmarkPages: (reader && reader.bookmarkPages !== undefined) ? reader.bookmarkPages : []

    readonly property bool prevEnabled: hasPrev
    readonly property bool nextEnabled: hasNext

    // ---- auto-hide ----
    property int autoHideMs: 3000

    // ================= semantic intents (shell / Task 12 wire these) =================
    signal seekRequested(int page)              // double-mode scrub -> snapped page
    signal scrubFractionRequested(real fraction) // strip-mode scrub -> scroll fraction
    signal prevRequested()
    signal nextRequested()
    // page-turn intents from the edge side bars (double-page). DISTINCT from prev/nextRequested,
    // which CROSS entries — these turn one page/unit WITHIN the entry (shell to pageNext/pagePrev).
    signal advancePageRequested()   // forward in reading order
    signal retreatPageRequested()   // backward in reading order
    signal openNavigator()
    signal openThumbnails()
    signal openSettings()
    signal toggleBookmark()
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()

    Theme { id: theme }

    // mock's exact glass micro-values (surface 01 is the binding contract; finer than Theme's kit)
    readonly property color cPillBg:      Qt.rgba(1, 1, 1, 0.07)
    readonly property color cEdgeSoft:    Qt.rgba(1, 1, 1, 0.09)
    readonly property color cGlass:       Qt.rgba(13 / 255, 14 / 255, 18 / 255, 0.88)
    readonly property color cGlassDeep:   Qt.rgba(9 / 255, 10 / 255, 13 / 255, 0.94)
    readonly property color cChipBg:      Qt.rgba(1, 1, 1, 0.06)
    readonly property color cGoldActive:  Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.14)
    readonly property color cGoldBorder:  Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
    readonly property color cGoldGlow:    Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.18)
    readonly property color cScrubTrack:  Qt.rgba(1, 1, 1, 0.22)
    readonly property color cTick:        Qt.rgba(1, 1, 1, 0.45)
    readonly property color cSideTrack:   Qt.rgba(1, 1, 1, 0.12)
    readonly property color cSideThumb:   Qt.rgba(1, 1, 1, 0.38)

    // ================= pure position math =================
    function _clamp01(v) { return Math.max(0, Math.min(1, v)) }
    function ratioForIndex(idx0, count) { return (count > 1) ? _clamp01(idx0 / (count - 1)) : 0 }
    // the scrub fill/knob position: DOUBLE maps (page-1)/(max-1); STRIP is the raw scroll fraction.
    function fillRatio() {
        return (mode === "double_page") ? ratioForIndex(currentPage - 1, max) : _clamp01(stripFraction)
    }
    // a bookmark tick sits at pageIndex/(max-1)
    function tickRatio(pageIndex0) { return ratioForIndex(pageIndex0, max) }

    // scrub drag -> page: DOUBLE snaps to the containing canonical unit anchor; STRIP is idx+1.
    function _unitAnchorIndex0(idx0) {
        if (reader && reader.core && reader.core.unitForPage) {
            var u = reader.core.unitForPage(idx0)
            if (u) {
                var lo = -1
                if (u.rightIndex !== undefined && u.rightIndex >= 0) lo = u.rightIndex
                if (u.leftIndex !== undefined && u.leftIndex >= 0)
                    lo = (lo < 0) ? u.leftIndex : Math.min(lo, u.leftIndex)
                if (lo >= 0) return lo
            }
        }
        return idx0
    }
    function pageForRatio(ratio) {
        var count = Math.max(1, max)
        var idx0 = (count > 1) ? Math.round(_clamp01(ratio) * (count - 1)) : 0
        if (mode === "double_page") idx0 = _unitAnchorIndex0(idx0)
        return idx0 + 1
    }

    // pair-aware counter: a real pair -> "lo-hi / max"; a single/spread -> "page / max".
    function counterText() {
        var m = Math.max(1, max)
        if (mode === "double_page" && reader && reader.core && reader.core.unitForPage) {
            var u = reader.core.unitForPage(currentPage - 1)
            if (u && u.leftIndex >= 0 && u.rightIndex >= 0 && !u.spread) {
                var lo = Math.min(u.rightIndex, u.leftIndex) + 1
                var hi = Math.max(u.rightIndex, u.leftIndex) + 1
                return lo + "–" + hi + " / " + m   // en dash, matching the mock "45-46 / 230"
            }
        }
        return Math.max(1, currentPage) + " / " + m
    }

    // ================= the single Manga/Comic/Strip identity (direction baked in) =================
    // The shell translates the identity into the internal layout+direction seams so a crossing's
    // load() honors the choice. There is no separate RTL/LTR toggle anymore.
    function setReadingMode(rm) { if (reader) reader.setReadingMode(rm) }
    function cycleReadingMode() {
        var order = ["manga", "comic", "strip"]
        var i = order.indexOf(readingMode)
        setReadingMode(order[(i < 0 ? 0 : (i + 1) % order.length)])
    }

    // ================= chrome visibility + auto-hide =================
    function toggleChrome() { if (reader) reader.chromeVisible = !reader.chromeVisible }
    function reveal() {
        if (reader) reader.chromeVisible = true
        autoHideTimer.restart()
    }
    function notifyActivity() { if (chromeVisible) autoHideTimer.restart() }
    function _autoHide() { if (reader) reader.chromeVisible = false }

    // Task 12 CARRY: PAUSE auto-hide while `modalOpen` once overlays land, else the footer fades
    // under an open settings sheet / navigator after 3s. Guard _autoHide (or stop the timer) on
    // modalOpen then. No overlay exists yet, so nothing to pause today.
    Timer {
        id: autoHideTimer
        interval: hud.autoHideMs
        repeat: false
        onTriggered: hud._autoHide()
    }
    onChromeVisibleChanged: { if (chromeVisible) autoHideTimer.restart(); else autoHideTimer.stop() }
    Component.onCompleted: if (chromeVisible) autoHideTimer.restart()

    // ================= nav pill intents (boundary-gated) =================
    function pressPrev() { if (hasPrev) prevRequested() }
    function pressNext() { if (hasNext) nextRequested() }

    // ================= edge side bars (double-page page-turn affordance) =================
    // Visible only in double-page (page turns don't apply to continuous Strip scroll). Direction-
    // aware, matching the click zones + the current reader's NavBar: LEFT bar advances in RTL /
    // retreats in LTR; RIGHT bar mirrors it. They live in the chrome layer, so they auto-hide with it.
    readonly property bool navBarsVisible: mode === "double_page" && max > 0
    function navBarTap(isLeft) {
        if (isLeft) { if (rtl) advancePageRequested(); else retreatPageRequested() }
        else        { if (rtl) retreatPageRequested(); else advancePageRequested() }
    }

    // enumerable glyph inventory — every HUD glyph is a ComicReaderIcon (semantic-icon-audit oracle)
    readonly property var iconKinds: [
        icBack.glyphKind, icPrev.glyphKind, icNext.glyphKind,
        icChapters.glyphKind, icThumbs.glyphKind, icSettings.glyphKind,
        icMin.glyphKind, icFull.glyphKind, icClose.glyphKind
    ]

    // ============================================================================================
    // inline chrome vocabulary
    // ============================================================================================
    // an icon-only glass pill / window verb
    component IconPill: Rectangle {
        id: pill
        property string glyphKindProp: ""
        property alias glyphKind: glyph.kind
        property bool active: false
        property bool enabledPill: true
        property color iconInk: theme.inkDim
        property int side: 30
        signal tapped()
        implicitWidth: side
        implicitHeight: side
        radius: 9
        color: pill.active ? hud.cGoldActive : hud.cPillBg
        border.width: 1
        border.color: pill.active ? hud.cGoldBorder : hud.cEdgeSoft
        opacity: pill.enabledPill ? 1.0 : 0.4
        ComicReaderIcon {
            id: glyph
            anchors.centerIn: parent
            kind: pill.glyphKindProp
            width: Math.round(pill.side * 0.5)
            height: width
            ink: pill.active ? theme.gold : pill.iconInk
        }
        MouseArea {
            anchors.fill: parent
            enabled: pill.enabledPill
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.tapped()
        }
    }

    // an immersive window verb (back / minimize / fullscreen / close) — Colosseum's player-chrome
    // pattern (PlayerPage RoundButton): a TRANSPARENT circular button with a BRIGHT ink glyph, a
    // faint white hover chip + a subtle scale — NO persistent glass box, NO dim ink. It reads because
    // it sits on the top gradient scrim, exactly like the footer pills sit on the footer gradient.
    component VerbButton: Item {
        id: vb
        property string glyphKindProp: ""
        property alias glyphKind: vbGlyph.kind
        property int side: 34
        signal tapped()
        implicitWidth: side
        implicitHeight: side
        scale: vbMa.pressed ? 0.94 : (vbMa.containsMouse ? 1.06 : 1.0)
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: vbMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
        }
        ComicReaderIcon {
            id: vbGlyph
            anchors.centerIn: parent
            kind: vb.glyphKindProp
            width: Math.round(vb.side * 0.5); height: width
            ink: theme.ink
        }
        MouseArea {
            id: vbMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: vb.tapped()
        }
    }

    // an icon + label glass pill (Library, Chapters)
    component LabeledPill: Rectangle {
        id: lp
        property string glyphKindProp: ""
        property alias glyphKind: lglyph.kind
        property string label: ""
        property color pillColor: hud.cGlass
        signal tapped()
        implicitHeight: 30
        implicitWidth: lrow.implicitWidth + 24
        radius: 8
        color: lp.pillColor
        border.width: 1
        border.color: theme.edge
        Row {
            id: lrow
            anchors.centerIn: parent
            spacing: 7
            ComicReaderIcon {
                id: lglyph
                anchors.verticalCenter: parent.verticalCenter
                kind: lp.glyphKindProp
                width: 15; height: 15
                ink: theme.inkDim
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: lp.label
                color: theme.inkDim
                font.family: theme.hud
                font.pixelSize: 12
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: lp.tapped()
        }
    }

    // ============================================================================================
    // the chrome layer (fades whole on auto-hide)
    // ============================================================================================
    Item {
        id: chromeLayer
        anchors.fill: parent
        opacity: hud.chromeVisible ? 1.0 : 0.0
        visible: opacity > 0.001
        Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        // ---- top gradient scrim: darkens the top edge so the back + window verbs read on ANY page,
        //      mirroring the footer gradient (and the player's playerTopScrim). The page still owns
        //      the screen — this is a soft visitor, not a bar. ----
        Rectangle {
            id: topScrim
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 108
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.58) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
            }
        }

        // ---- back to Library (top-left) — bright glyph + label on the scrim, no glass box ----
        Row {
            id: icBack
            x: 18; y: 16
            spacing: 7
            property alias glyphKind: backGlyph.kind   // enumerated by iconKinds (audit)
            ComicReaderIcon {
                id: backGlyph
                anchors.verticalCenter: parent.verticalCenter
                kind: "back"
                width: 20; height: 20
                ink: backMa.containsMouse ? theme.gold : theme.ink
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Library"
                color: backMa.containsMouse ? theme.gold : theme.ink
                font.family: theme.hud
                font.pixelSize: 14
                font.weight: Font.DemiBold
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.5)
            }
            MouseArea {
                id: backMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: hud.backRequested()
            }
        }

        // ---- window verbs (top-right) — transparent RoundButtons on the scrim, bright ink ----
        Row {
            id: verbs
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 16
            anchors.topMargin: 14
            spacing: 4
            VerbButton { id: icMin;   glyphKindProp: "minimize";   onTapped: hud.minimizeRequested() }
            VerbButton { id: icFull;  glyphKindProp: "fullscreen"; onTapped: hud.fullscreenRequested() }
            VerbButton { id: icClose; glyphKindProp: "close";      onTapped: hud.closeRequested() }
        }

        // ---- thin side scroller (right edge) ----
        Rectangle {
            id: sideScroller
            width: 3
            radius: 2
            color: hud.cSideTrack
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 70
            anchors.bottom: footer.top
            anchors.bottomMargin: 24
            Rectangle {
                id: sideThumb
                width: 8
                height: 52
                radius: 5
                color: hud.cSideThumb
                x: (parent.width - width) / 2
                readonly property real _span: Math.max(0, parent.height - height)
                y: hud.fillRatio() * _span
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.SizeVerCursor
                    drag.target: sideThumb
                    drag.axis: Drag.YAxis
                    drag.minimumY: 0
                    drag.maximumY: sideThumb._span
                    // same auto-hide pause/reset as the footer scrub — never fade the chrome mid-drag
                    onPressed: autoHideTimer.stop()
                    onReleased: hud.notifyActivity()
                    onPositionChanged: {
                        if (!pressed) return
                        var r = sideThumb._span > 0 ? hud._clamp01(sideThumb.y / sideThumb._span) : 0
                        if (hud.mode === "double_page") hud.seekRequested(hud.pageForRatio(r))
                        else hud.scrubFractionRequested(r)
                    }
                }
            }
        }

        // ---- edge side bars: a visible, direction-aware page-turn affordance (double-page only) ----
        // Chevron is PHYSICAL (left bar = a left chevron, right bar = a right); the ACTION is
        // direction-aware via navBarTap(). Glyphs are ComicReaderIcon (semantic-icon-audit law).
        component NavBar: Item {
            property bool isLeft: true
            width: 60
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            visible: hud.navBarsVisible
            // Tankoban 2's SideNavArrow scheme: a big chevron drawn TWICE — a black drop shadow, then
            // a white foreground — so it reads on ANY page (a plain white glyph vanishes on a white
            // manga page; gold was wrong too). No bg fill. Brighter on hover. ComicReaderIcon per the
            // semantic-icon-audit law (TB2's QtWidget paints a Segoe UI chevron; we colorize ours).
            ComicReaderIcon {                       // drop shadow (TB2: QColor(0,0,0,90) offset +2,+2)
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 2
                anchors.verticalCenterOffset: 2
                kind: parent.isLeft ? "prev" : "next"
                width: 34; height: 34
                ink: "black"
                opacity: 0.55
            }
            ComicReaderIcon {                       // foreground white (TB2: QColor(255,255,255,180))
                anchors.centerIn: parent
                kind: parent.isLeft ? "prev" : "next"
                width: 34; height: 34
                ink: "white"
                opacity: navMa.containsMouse ? 1.0 : 0.85
            }
            MouseArea {
                id: navMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: hud.navBarTap(parent.isLeft)
            }
        }
        NavBar { objectName: "hudNavLeft";  isLeft: true;  anchors.left: parent.left }
        NavBar { objectName: "hudNavRight"; isLeft: false; anchors.right: parent.right }

        // ---- bottom gradient footer ----
        Item {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 104

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.00; color: Qt.rgba(0, 0, 0, 0.0) }
                    GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, 0.62) }
                    GradientStop { position: 1.00; color: Qt.rgba(0, 0, 0, 0.90) }
                }
            }

            // click-swallower over the footer background (floating-panel/click-swallower house law):
            // an empty-footer click must NOT fall through to the page input beneath (bottom-corner
            // clicks turning pages, center-footer clicks toggling chrome). Declared BEFORE the scrub
            // and pill rows, so those interactive children sit above it and still receive their clicks.
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; onClicked: {} }

            // ------- gold scrub thread -------
            Item {
                id: scrub
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                anchors.top: parent.top
                anchors.topMargin: 30
                height: 12

                Rectangle {
                    id: scrubTrack
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 4
                    radius: 2
                    color: hud.cScrubTrack

                    readonly property real knobRatio: hud._scrubbing ? hud._scrubRatio : hud.fillRatio()

                    // gold fill to the current position
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * scrubTrack.knobRatio
                        radius: 2
                        color: theme.gold
                    }

                    // bookmark ticks
                    Repeater {
                        model: hud.bookmarkPages
                        delegate: Rectangle {
                            width: 2; height: 10; radius: 1
                            color: hud.cTick
                            y: -3
                            x: scrubTrack.width * hud.tickRatio(modelData) - width / 2
                        }
                    }

                    // gold knob + glow
                    Rectangle {
                        id: knobGlow
                        width: 20; height: 20; radius: 10
                        color: hud.cGoldGlow
                        x: scrubTrack.width * scrubTrack.knobRatio - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        id: knob
                        width: 12; height: 12; radius: 6
                        color: theme.gold
                        x: scrubTrack.width * scrubTrack.knobRatio - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    // page bubble above the knob (on hover / drag)
                    Rectangle {
                        id: bubble
                        visible: hud._scrubbing || scrubHover.hovered
                        height: 20
                        width: bubbleText.implicitWidth + 18
                        radius: 6
                        color: hud.cGlassDeep
                        border.width: 1
                        border.color: theme.edge
                        y: -32
                        x: Math.max(0, Math.min(scrubTrack.width - width,
                                    scrubTrack.width * scrubTrack.knobRatio - width / 2))
                        Text {
                            id: bubbleText
                            anchors.centerIn: parent
                            text: hud.pageForRatio(scrubTrack.knobRatio)
                            color: theme.gold
                            font.family: theme.hud
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }
                }

                HoverHandler { id: scrubHover }
                MouseArea {
                    anchors.fill: parent
                    anchors.topMargin: -6
                    anchors.bottomMargin: -6
                    cursorShape: Qt.PointingHandCursor
                    function _ratioAt(mx) {
                        return scrubTrack.width > 0 ? hud._clamp01(mx / scrubTrack.width) : 0
                    }
                    // PAUSE auto-hide for the whole drag, then reset on release — else scrubbing a
                    // long book for >3s fires _autoHide -> chromeVisible=false -> this MouseArea
                    // (inside the opacity-gated chromeLayer) vanishes under the cursor mid-drag.
                    onPressed: function (m) { autoHideTimer.stop(); hud._scrubbing = true; hud._scrubRatio = _ratioAt(m.x); hud._emitScrub(hud._scrubRatio) }
                    onPositionChanged: function (m) { if (hud._scrubbing) { hud._scrubRatio = _ratioAt(m.x); hud._emitScrub(hud._scrubRatio) } }
                    onReleased: { hud._scrubbing = false; hud.notifyActivity() }
                }
            }

            // ------- HUD row: left group (prev · counter · next) -------
            Row {
                id: leftGroup
                anchors.left: parent.left
                anchors.leftMargin: 22
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 14
                spacing: 9
                IconPill {
                    id: icPrev
                    glyphKindProp: "prev"
                    enabledPill: hud.prevEnabled
                    anchors.verticalCenter: parent.verticalCenter
                    onTapped: hud.pressPrev()
                }
                Text {
                    id: counter
                    anchors.verticalCenter: parent.verticalCenter
                    text: hud.counterText()
                    color: theme.ink
                    font.family: theme.hud
                    font.pixelSize: 13
                    font.bold: true
                }
                IconPill {
                    id: icNext
                    glyphKindProp: "next"
                    enabledPill: hud.nextEnabled
                    anchors.verticalCenter: parent.verticalCenter
                    onTapped: hud.pressNext()
                }
            }

            // ------- HUD row: right group (chapters · thumbnails · mode chip · direction · settings) -------
            Row {
                id: rightGroup
                anchors.right: parent.right
                anchors.rightMargin: 22
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 14
                spacing: 9
                LabeledPill {
                    id: icChapters
                    glyphKindProp: "chapters"
                    label: "Chapters"
                    pillColor: hud.cPillBg
                    anchors.verticalCenter: parent.verticalCenter
                    onTapped: hud.openNavigator()
                }
                IconPill {
                    id: icThumbs
                    glyphKindProp: "thumbnails"
                    anchors.verticalCenter: parent.verticalCenter
                    onTapped: hud.openThumbnails()
                }

                // ONE three-segment identity chip — Manga (RTL double, MangaPlus) / Comic (LTR
                // double) / Strip (vertical). Active segment gold; direction is baked into the choice.
                Rectangle {
                    id: modeChip
                    anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    width: modeRow.implicitWidth
                    radius: 9
                    color: hud.cChipBg
                    border.width: 1
                    border.color: hud.cEdgeSoft
                    clip: true
                    component ModeSeg: Rectangle {
                        property string rm: ""
                        property string label: ""
                        readonly property bool on: hud.readingMode === rm
                        height: modeRow.height
                        width: segText.implicitWidth + 24
                        color: on ? hud.cGoldActive : "transparent"
                        Text {
                            id: segText
                            anchors.centerIn: parent
                            text: parent.label
                            color: parent.on ? theme.gold : theme.inkDimmer
                            font.family: theme.hud
                            font.pixelSize: 12
                            font.bold: parent.on
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: hud.setReadingMode(parent.rm) }
                    }
                    Row {
                        id: modeRow
                        height: parent.height
                        ModeSeg { objectName: "hudModeManga"; rm: "manga"; label: "Manga" }
                        ModeSeg { objectName: "hudModeComic"; rm: "comic"; label: "Comic" }
                        ModeSeg { objectName: "hudModeStrip"; rm: "strip"; label: "Strip" }
                    }
                }

                IconPill {
                    id: icSettings
                    glyphKindProp: "settings"
                    anchors.verticalCenter: parent.verticalCenter
                    onTapped: hud.openSettings()
                }
            }
        }
    }

    // ================= scrub interaction state =================
    property bool _scrubbing: false
    property real _scrubRatio: 0
    function _emitScrub(ratio) {
        if (mode === "double_page") seekRequested(pageForRatio(ratio))
        else scrubFractionRequested(_clamp01(ratio))
    }

    // ---- toast: the one transient-feedback surface (zoom, pairing, bookmarks) ----
    // Mounted OUTSIDE the auto-hiding chrome on purpose: feedback must land even when the
    // chrome is away, which is exactly when you have no other readout to check against.
    function showToast(msg) { hudToastText.text = msg; hudToast.opacity = 1; hudToastTimer.restart() }
    Rectangle {
        id: hudToast
        objectName: "hudToast"
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(parent.height * 0.14)
        width: hudToastText.implicitWidth + 28
        height: 34
        radius: 17
        color: hud.cGlassDeep
        border.width: 1
        border.color: theme.edge
        opacity: 0
        visible: opacity > 0.001
        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        Text {
            id: hudToastText
            objectName: "hudToastText"
            anchors.centerIn: parent
            color: theme.ink
            font.family: theme.hud
            font.pixelSize: 13
            font.bold: true
        }
        Timer { id: hudToastTimer; interval: 900; onTriggered: hudToast.opacity = 0 }
    }
}
