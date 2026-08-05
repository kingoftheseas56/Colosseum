// ComicReaderHud — the approved sidebar-free reading chrome (Task 5, plan 2026-07-28), replacing
// the Family Gradient pill HUD it grew out of. Hemanth approved this shape section by section; the
// decision ledger (docs/superpowers/specs/2026-07-28-comic-reader-overhaul-design.md) is verbatim:
//
//   * Thin title strip with Back and book title.
//   * One flat top command bar: Bookmark, Pages, Loupe, Image, current Layout, and current Order.
//   * No reader sidebar.
//   * No permanent settings drawer.
//   * One gold bottom rail with current position, total pages, and scrub affordance.
//   * The comic remains visible behind temporary surfaces and never shifts to make room for them.
//   * Toolbar, title toast, progress rail, and cursor sleep together after 2.5 seconds of inactivity.
//   * Any plain mouse movement restores HUD and cursor together. Escape explicitly toggles chrome
//     when no temporary surface is open.
//
// The shorthand the arc runs on: "Cover's calm, YACReader's flow, Colosseum's brain." Calm here is
// ONE shallow layer, not a drawer: no pill soup, no nested control architecture, no side panel.
//
// PRESENTATION + INTENTS ONLY. The HUD binds the shell's read-only reading state off the `reader`
// seam and EMITS semantic intents (openPages / openLoupe / openImage / openLayout / toggleOrder /
// toggleBookmark / seek / crossing / window verbs). It never writes core state: the shell's ONE
// overlay coordinator decides what actually opens (the one-temporary-surface rule).
//
// Gold is SPARING and structural: the progress rail, the scrub fill/knob, and the single active
// command. Nothing else.
//
// Every glyph is a ComicReaderIcon (white-stroke SVG, tinted) — never a Text arrow/character (the
// semantic-icon-audit law). The counter and the command names are text LABELS, not glyph chips.

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
    // LAYOUT + ORDER are the persisted truth (Task 3). The command bar SHOWS both, so it reads them
    // straight rather than through the lossy combined identity the retired mode pills spoke — that
    // identity cannot express Single Page, which is exactly why those pills are gone.
    readonly property string layout:        reader ? reader.layout        : "long_strip"
    readonly property string order:         reader ? reader.order         : "ltr"
    readonly property real   stripFraction: reader ? reader.stripFraction : 0
    readonly property int    zoomPercent:   reader ? reader.zoomPercent   : 100
    readonly property bool   hasNext:       reader ? reader.hasNext       : false
    readonly property bool   hasPrev:       reader ? reader.hasPrev       : false
    readonly property string seriesTitle:   reader ? reader.seriesTitle   : ""
    readonly property string curLabel:      reader ? reader.curLabel      : ""
    readonly property bool   chromeVisible: reader ? reader.chromeVisible : true
    readonly property bool   modalOpen:     reader ? reader.modalOpen     : false
    // which temporary surface the shell currently owns ("" = none). The command bar reads THIS to
    // decide which single command is gold.
    readonly property string activeOverlay: (reader && reader.activeOverlay !== undefined)
                                            ? reader.activeOverlay : ""
    // bookmark 0-based page indices — the rail's ticks and the Bookmark command's gold both read it
    property var bookmarkPages: (reader && reader.bookmarkPages !== undefined) ? reader.bookmarkPages : []

    readonly property bool prevEnabled: hasPrev
    readonly property bool nextEnabled: hasNext
    readonly property bool bookmarkedHere: bookmarkPages && bookmarkPages.indexOf(currentPage - 1) >= 0

    // ---- auto-hide: 2.5s, the approved dial. Chrome and cursor sleep TOGETHER (the shell's
    //      cursorIdleMs carries the same number). ----
    property int autoHideMs: 2500

    // ================= semantic intents (the shell wires these) =================
    // --- the six direct commands ---
    signal openPages()                          // Pages    -> the temporary filmstrip (Task 6)
    signal openLoupe()                          // Loupe    -> the temporary magnifier (Task 9)
    signal openImage()                          // Image    -> the compact image panel (Task 7)
    signal openLayout()                         // Layout   -> the compact anchored layout menu (Task 8)
    signal toggleOrder()                        // Order    -> Manga RTL <-> Comic LTR, no surface
    signal toggleBookmark()                     // Bookmark -> this page's bookmark
    // --- the gold rail ---
    signal seekRequested(int page)              // paged scrub -> snapped/exact page
    signal scrubFractionRequested(real fraction) // strip scrub -> scroll fraction
    signal prevRequested()                      // rail arrow: the PREVIOUS entry (crossing)
    signal nextRequested()                      // rail arrow: the NEXT entry (crossing)
    // --- page-turn intents from the edge side bars. DISTINCT from prev/nextRequested, which CROSS
    //     entries — these turn one page/unit WITHIN the entry (shell to pageNext/pagePrev). ---
    signal advancePageRequested()               // forward in reading order
    signal retreatPageRequested()               // backward in reading order
    // --- the title strip ---
    signal backRequested()
    signal minimizeRequested()
    signal fullscreenRequested()
    signal closeRequested()

    Theme { id: theme }

    // chrome micro-values. The two top bars carry the approved mock's solid dark weight (it is a
    // BAR in the mock, not a wash) and the rail carries flat gold; both still FLOAT over the comic,
    // which never resizes to make room for them — that is the "never shifts" rule, and it is also
    // why they can vanish whole after 2.5s and hand the screen back.
    readonly property color cTitleBar:    Qt.rgba(5 / 255, 5 / 255, 6 / 255, 0.92)
    readonly property color cCommandBar:  Qt.rgba(8 / 255, 8 / 255, 9 / 255, 0.90)
    readonly property color cHairline:    Qt.rgba(1, 1, 1, 0.07)
    readonly property color cGlassDeep:   Qt.rgba(9 / 255, 10 / 255, 13 / 255, 0.94)
    readonly property color cRailGold:    Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.94)
    readonly property color cRailInk:     Qt.rgba(23 / 255, 19 / 255, 10 / 255, 1.0)
    readonly property color cRailTrack:   Qt.rgba(23 / 255, 19 / 255, 10 / 255, 0.42)
    readonly property color cRailTick:    Qt.rgba(23 / 255, 19 / 255, 10 / 255, 0.55)
    readonly property color cSideTrack:   Qt.rgba(1, 1, 1, 0.12)
    readonly property color cSideThumb:   Qt.rgba(1, 1, 1, 0.38)

    // ================= pure position math =================
    function _clamp01(v) { return Math.max(0, Math.min(1, v)) }
    function ratioForIndex(idx0, count) { return (count > 1) ? _clamp01(idx0 / (count - 1)) : 0 }
    // Is this a PAGED layout (Single Page or Paired Pages) rather than the continuous column? Every
    // rail decision below turns on THIS, not on `mode === "double_page"`. Asking the old question
    // was a real, visible bug: Single Page fell to the strip branch, so `stripFraction` (always 0
    // in a paged layout) drove the rail and it sat dead at zero for the whole book.
    readonly property bool paged: mode !== "long_strip"
    // the scrub fill/knob position: PAGED maps (page-1)/(max-1); STRIP is the raw scroll fraction.
    function fillRatio() {
        return paged ? ratioForIndex(currentPage - 1, max) : _clamp01(stripFraction)
    }
    // a bookmark tick sits at pageIndex/(max-1)
    function tickRatio(pageIndex0) { return ratioForIndex(pageIndex0, max) }

    // scrub drag -> page: PAIRED snaps to the containing canonical unit anchor; SINGLE and STRIP are
    // idx+1 (Single Page snaps to no unit — that is the whole point of the layout).
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

    // pair-aware position readout: a real pair -> "lo-hi"; a single/spread -> "page".
    function counterCurrentText() {
        if (mode === "double_page" && reader && reader.core && reader.core.unitForPage) {
            var u = reader.core.unitForPage(currentPage - 1)
            if (u && u.leftIndex >= 0 && u.rightIndex >= 0 && !u.spread) {
                var lo = Math.min(u.rightIndex, u.leftIndex) + 1
                var hi = Math.max(u.rightIndex, u.leftIndex) + 1
                return lo + "–" + hi                 // en dash, matching the approved mock
            }
        }
        return String(Math.max(1, currentPage))
    }
    function counterTotalText() { return String(Math.max(1, max)) }
    // the same readout as ONE string, for anything that wants it whole (toasts, accessibility)
    function counterText() { return counterCurrentText() + " / " + counterTotalText() }

    // ================= chrome visibility + auto-hide =================
    function toggleChrome() { if (reader) reader.chromeVisible = !reader.chromeVisible }
    function reveal() {
        if (reader) reader.chromeVisible = true
        autoHideTimer.restart()
    }
    function notifyActivity() { if (chromeVisible) autoHideTimer.restart() }
    // chrome is HELD while a temporary surface is genuinely UP, or the pointer rests ON the chrome
    // — reaching for a command and pausing to aim must not fade it out from under your cursor.
    //
    // "UP" MEANS MOUNTED, NEVER MERELY INTENDED. This used to read `activeOverlay.length > 0` as
    // well, and that is a real bug with a real victim: `activeOverlay` is the shell's INTENT (which
    // command took ownership), while `modalOpen` is the aggregate of the surfaces that actually
    // mounted. Raise a command whose surface is not there — which is exactly what the Loupe command
    // was between Task 5, where it went live, and Task 9, where its surface landed — and the intent
    // is set, nothing appears, and the chrome is pinned open for the rest of the session. The cursor
    // goes with it (the shell blanks it only while the chrome is away), so the reader gets a HUD and
    // an arrow that never sleep and no visible surface to dismiss. Measured in the real app against a
    // 176-page volume: `openOverlay("<name with no surface>")` -> chromeVisible true forever.
    //
    // `modalOpen` cannot lie the same way: each overlay's `open` is a binding on this same
    // `activeOverlay`, so a surface that exists reports itself in the same beat, and one that does
    // not exist reports nothing — which is the honest answer. Every surface the chrome can raise is
    // mounted today, so this changes no behaviour on master; it removes the way the hold can be
    // pinned by a command the reader cannot see. The COMMAND still goes gold off `activeOverlay` —
    // that is presentation, and it is not what decides whether the chrome may sleep.
    readonly property bool _holdChrome: modalOpen || chromeHover.hovered
    function _autoHide() {
        if (_holdChrome) { autoHideTimer.restart(); return }
        if (reader) reader.chromeVisible = false
    }

    Timer {
        id: autoHideTimer
        interval: hud.autoHideMs
        repeat: false
        onTriggered: hud._autoHide()
    }
    onChromeVisibleChanged: { if (chromeVisible) autoHideTimer.restart(); else autoHideTimer.stop() }
    Component.onCompleted: if (chromeVisible) autoHideTimer.restart()

    // ================= crossing intents (boundary-gated) =================
    function pressPrev() { if (hasPrev) prevRequested() }
    function pressNext() { if (hasNext) nextRequested() }

    // ================= edge side bars (page-turn affordance) =================
    // Visible in every PAGED layout — Single Page turns pages exactly like a pair, so gating these
    // on double-page left Single Page with no on-screen page-turn affordance at all. Direction-
    // aware, matching the click zones: LEFT bar advances in RTL / retreats in LTR; RIGHT mirrors it.
    // They live in the chrome layer, so they auto-hide with it.
    readonly property bool navBarsVisible: paged && max > 0
    function navBarTap(isLeft) {
        if (isLeft) { if (rtl) advancePageRequested(); else retreatPageRequested() }
        else        { if (rtl) retreatPageRequested(); else advancePageRequested() }
    }

    // enumerable glyph inventory — every HUD glyph is a ComicReaderIcon (semantic-icon-audit oracle).
    // icBack is EXEMPT: it's the shared BackAction component (back-navigation unification law),
    // which owns its own vector chevron and carries no glyphKind.
    readonly property var iconKinds: [
        icPrev.glyphKind, icNext.glyphKind,
        icMin.glyphKind, icFull.glyphKind, icClose.glyphKind
    ].concat(commandBar.iconKinds)

    // ================= the per-command ANCHOR seam (Task 8) =================
    // A temporary popover hangs under the command that RAISED it — Cover's shape, and the thing
    // Task 7 deferred because two of the six commands are live readouts whose label widths move
    // with the reader's layout and order. The command bar publishes each command's centre; this
    // maps it into the HUD's coordinates, which are the shell's (both fill the reader).
    //
    // Exposed as a plain map as well as a function so a caller's BINDING has something reactive to
    // depend on: mapToItem is a one-shot read, so a binding that only called the function below
    // would evaluate once and never again. Read `commandAnchors` in the same expression (the shell
    // does) and the binding re-runs whenever the row relayouts.
    readonly property var commandAnchors: commandBar.commandAnchors
    function commandAnchorX(command) {
        var local = commandBar.anchorFor(command)
        if (local < 0) return -1
        return commandBar.mapToItem(hud, local, 0).x
    }
    // Force a republish. The offscreen gates drive layout by hand (forceLayout / an explicit
    // relayout) rather than waiting on polish, so they need the same door.
    function refreshCommandAnchors() { commandBar.refreshAnchors() }

    // ============================================================================================
    // inline chrome vocabulary
    // ============================================================================================
    // an immersive window verb (minimize / fullscreen / close) — Colosseum's player-chrome pattern
    // (PlayerPage RoundButton): a TRANSPARENT circular button with a BRIGHT ink glyph, a faint white
    // hover chip + a subtle scale. No persistent glass box, no dim ink.
    component VerbButton: Item {
        id: vb
        property string glyphKindProp: ""
        property alias glyphKind: vbGlyph.kind
        property string verbName: ""
        property int side: 30
        signal tapped()
        implicitWidth: side
        implicitHeight: side
        // The press/hover scale lives on this INNER item, never on `vb` itself — `vb` is what
        // `vbMa` fills, and Qt Quick's hit-testing maps a click through an item's own transform,
        // so animating the scale of the MouseArea's own item would animate its clickable region
        // too. Hardening, not the reported bug's root cause (the dominant cause was NavBar
        // overlapping this whole row — see the NavBar fix above); kept because animating the
        // geometry that hit-tests is a footgun worth closing regardless.
        Item {
            id: visual
            anchors.fill: parent
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
                accessibleName: vb.verbName
                width: Math.round(vb.side * 0.5); height: width
                ink: theme.ink
            }
        }
        MouseArea {
            id: vbMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: vb.tapped()
        }
    }

    // a rail arrow (crossing to the previous / next entry) — dark ink on the gold rail, no box.
    component RailArrow: Item {
        id: ra
        property string glyphKindProp: ""
        property alias glyphKind: raGlyph.kind
        property string verbName: ""
        property bool enabledArrow: true
        signal tapped()
        implicitWidth: 30
        implicitHeight: 30
        opacity: ra.enabledArrow ? (raMa.containsMouse ? 1.0 : 0.82) : 0.28
        Behavior on opacity { NumberAnimation { duration: 90 } }
        ComicReaderIcon {
            id: raGlyph
            anchors.centerIn: parent
            kind: ra.glyphKindProp
            accessibleName: ra.verbName
            width: 19; height: 19
            ink: hud.cRailInk
        }
        MouseArea {
            id: raMa
            anchors.fill: parent
            hoverEnabled: true
            enabled: ra.enabledArrow
            cursorShape: Qt.PointingHandCursor
            onClicked: ra.tapped()
        }
    }

    // ============================================================================================
    // the chrome layer (fades whole on auto-hide — toolbar, title toast and rail sleep together)
    // ============================================================================================
    Item {
        id: chromeLayer
        anchors.fill: parent
        opacity: hud.chromeVisible ? 1.0 : 0.0
        visible: opacity > 0.001
        Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        // a pointer resting anywhere on the chrome holds auto-hide — see hud._holdChrome.
        HoverHandler { id: chromeHover }

        // ---- the thin title strip: Back, the book title, the window verbs ----
        Rectangle {
            id: titleBar
            objectName: "readerTitleBar"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 38
            color: hud.cTitleBar

            // click-swallower: an empty-strip click must not fall through and turn a page
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; onClicked: {} }

            // Shared BackAction component (back-navigation unification law). This is the ONLY
            // reader-to-library exit — Escape never leaves the book.
            BackAction {
                id: icBack
                objectName: "hudBackAction"
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                variant: "plain"
                label: "Library"
                labelSize: 13
                idleColor: theme.inkDim
                hoverColor: theme.gold
                onTriggered: hud.backRequested()
            }

            Text {
                id: bookTitle
                objectName: "readerBookTitle"
                anchors.left: icBack.right
                anchors.leftMargin: 22
                anchors.right: verbs.left
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                // "ONE PIECE · VOLUME 70" — series then the entry, the approved mock's title line.
                text: hud.curLabel.length ? (hud.seriesTitle + "  ·  " + hud.curLabel) : hud.seriesTitle
                color: theme.inkDim
                font.family: theme.hud
                font.pixelSize: 12
                font.letterSpacing: 0.6
                elide: Text.ElideRight
            }

            Row {
                id: verbs
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                VerbButton { id: icMin;   objectName: "hudMinimizeButton";   glyphKindProp: "minimize";   verbName: "Minimize";   onTapped: hud.minimizeRequested() }
                VerbButton { id: icFull;  objectName: "hudFullscreenButton"; glyphKindProp: "fullscreen"; verbName: "Fullscreen"; onTapped: hud.fullscreenRequested() }
                VerbButton { id: icClose; objectName: "hudCloseButton";      glyphKindProp: "close";      verbName: "Close";      onTapped: hud.closeRequested() }
            }
        }

        // ---- ONE flat command bar. No sidebar, no drawer, no pills. ----
        Rectangle {
            id: commandStrip
            objectName: "readerCommandStrip"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleBar.bottom
            height: 46
            color: hud.cCommandBar

            Rectangle {                                  // hairline under the bar
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 1
                color: hud.cHairline
            }

            MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; onClicked: {} }

            ComicReaderCommandBar {
                id: commandBar
                anchors.fill: parent
                layout: hud.layout
                order: hud.order
                activeOverlay: hud.activeOverlay
                bookmarked: hud.bookmarkedHere
                // the chrome RAISES intents; the shell's coordinator decides what opens
                onCommandTriggered: function (command) {
                    switch (command) {
                    case "bookmark": hud.toggleBookmark(); break
                    case "pages":    hud.openPages();      break
                    case "loupe":    hud.openLoupe();      break
                    case "image":    hud.openImage();      break
                    case "layout":   hud.openLayout();     break
                    case "order":    hud.toggleOrder();    break
                    }
                }
            }
        }

        // ---- a short fade under the command bar so it does not cut hard against bright page art ----
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: commandStrip.bottom
            height: 26
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.42) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
            }
        }

        // ---- thin side scroller (right edge) — the vertical position thumb, most useful in the
        //      continuous column. Not a sidebar: it draws no panel and holds no commands. ----
        Rectangle {
            id: sideScroller
            width: 3
            radius: 2
            color: hud.cSideTrack
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.top: commandStrip.bottom
            anchors.topMargin: 30
            anchors.bottom: progressRail.top
            anchors.bottomMargin: 24
            Rectangle {
                id: sideThumb
                width: 8
                height: 52
                radius: 5
                color: hud.cSideThumb
                x: (parent.width - width) / 2
                readonly property real _span: Math.max(0, parent.height - height)
                // A plain `y:` binding here was DESTROYED by the first drag: drag.target writes y
                // imperatively, which breaks the binding permanently, so after one drag the thumb
                // stopped tracking the reading position for the rest of the session. A Binding with
                // RestoreBindingOrValue re-arms the tracking as soon as the drag ends. (E6)
                Binding on y {
                    when: !sideThumbMa.drag.active
                    value: hud.fillRatio() * sideThumb._span
                    restoreMode: Binding.RestoreBindingOrValue
                }
                MouseArea {
                    id: sideThumbMa
                    anchors.fill: parent
                    anchors.margins: -6
                    // NO cursorShape. A resize cursor here says "drag to resize", which is not what
                    // this does, and Tankoban 2 explicitly reverted the same SizeVerCursor. (E6)
                    drag.target: sideThumb
                    drag.axis: Drag.YAxis
                    drag.minimumY: 0
                    drag.maximumY: sideThumb._span
                    // same auto-hide pause/reset as the rail scrub — never fade the chrome mid-drag
                    onPressed: autoHideTimer.stop()
                    onReleased: hud.notifyActivity()
                    onPositionChanged: {
                        if (!pressed) return
                        var r = sideThumb._span > 0 ? hud._clamp01(sideThumb.y / sideThumb._span) : 0
                        hud._emitScrub(r)
                    }
                }
            }
        }

        // ---- edge side bars: a visible, direction-aware page-turn affordance (paged layouts) ----
        // Chevron is PHYSICAL (left bar = a left chevron, right bar = a right); the ACTION is
        // direction-aware via navBarTap(). Glyphs are ComicReaderIcon (semantic-icon-audit law).
        component NavBar: Item {
            property bool isLeft: true
            width: 60
            visible: hud.navBarsVisible
            // Tankoban 2's SideNavArrow scheme: a big chevron drawn TWICE — a black drop shadow, then
            // a white foreground — so it reads on ANY page (a plain white glyph vanishes on a white
            // manga page; gold was wrong too). No bg fill. Brighter on hover.
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
        // Bounded BELOW the title bar + command bar and ABOVE the gold rail — geometry, not
        // z-order, is what keeps this strip off the chrome. It used to span the full chrome
        // height (anchors.top/bottom: parent.top/bottom), which put its 60px-wide MouseArea
        // on top of Back (title bar, left edge), Close/Fullscreen (title bar, right edge —
        // Close sat ENTIRELY inside it), and the rightmost command-bar entries (Comic/Manga
        // order) — all because they're declared before this component and Qt Quick stacks
        // equal-z siblings in declaration order. Real, measured overlap, not a suspicion —
        // proven red/green by tests/qml/tst_comicreader_title_controls.qml.
        NavBar {
            objectName: "hudNavLeft";  isLeft: true
            anchors.left: parent.left
            anchors.top: commandStrip.bottom; anchors.bottom: progressRail.top
        }
        NavBar {
            objectName: "hudNavRight"; isLeft: false
            anchors.right: parent.right
            anchors.top: commandStrip.bottom; anchors.bottom: progressRail.top
        }

        // ---- the title toast: the BOOK, stated big and plainly, sleeping with the rest of the
        //      chrome. Sits just above the rail, exactly as the approved mock places it — and it
        //      carries the SHORT form (the series alone) while the strip above carries the long one
        //      (series + entry). Two sizes of the same fact is redundancy, not hierarchy. ----
        Rectangle {
            id: titleToast
            objectName: "readerTitleToast"
            visible: hud.seriesTitle.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: progressRail.top
            anchors.bottomMargin: 26
            width: Math.min(parent.width - 120, titleToastText.implicitWidth + 44)
            height: 42
            color: Qt.rgba(5 / 255, 5 / 255, 6 / 255, 0.82)
            Text {
                id: titleToastText
                objectName: "readerTitleToastText"
                anchors.centerIn: parent
                width: parent.width - 44
                horizontalAlignment: Text.AlignHCenter
                text: hud.seriesTitle
                color: theme.ink
                font.family: theme.hud
                font.pixelSize: 19
                elide: Text.ElideRight
            }
        }

        // ---- a short fade above the rail, mirroring the one under the command bar ----
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: progressRail.top
            height: 40
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.46) }
            }
        }

        // ---- ONE gold bottom rail: where you are, how long the book is, and the way to move ----
        Rectangle {
            id: progressRail
            objectName: "readerProgressRail"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 54
            color: hud.cRailGold

            // click-swallower (floating-panel/click-swallower house law): a click on the rail's
            // empty ground must NOT fall through to the page input beneath. Declared FIRST so the
            // interactive children below sit above it and still receive their clicks.
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; onClicked: {} }

            RailArrow {
                id: icPrev
                objectName: "railPrevEntry"
                glyphKindProp: "prev"
                verbName: "Previous chapter"
                enabledArrow: hud.prevEnabled
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                onTapped: hud.pressPrev()
            }
            Text {
                id: railCurrent
                objectName: "railCurrent"
                anchors.left: icPrev.right
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: hud.counterCurrentText()
                color: hud.cRailInk
                font.family: theme.hud
                font.pixelSize: 17
                font.bold: true
            }
            RailArrow {
                id: icNext
                objectName: "railNextEntry"
                glyphKindProp: "next"
                verbName: "Next chapter"
                enabledArrow: hud.nextEnabled
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                onTapped: hud.pressNext()
            }
            Text {
                id: railTotal
                objectName: "railTotal"
                anchors.right: icNext.left
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: hud.counterTotalText()
                color: hud.cRailInk
                font.family: theme.hud
                font.pixelSize: 17
            }

            // ------- the scrub affordance -------
            // hidden for a one-page entry (there is nowhere to scrub to); the two numbers stay, so
            // the rail still answers "where am I".
            Item {
                id: scrub
                objectName: "hudScrub"
                visible: hud.max > 1
                anchors.left: railCurrent.right
                anchors.right: railTotal.left
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                height: 14

                Rectangle {
                    id: scrubTrack
                    objectName: "hudScrubTrack"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 3
                    radius: 2
                    color: hud.cRailTrack

                    // the knob/bubble DISPLAY position follows the POINTER while hovering OR
                    // dragging; otherwise it shows the real read position.
                    readonly property bool pointerActive: hud._scrubbing || scrubHover.hovered
                    readonly property real knobRatio: pointerActive ? hud._scrubRatio : hud.fillRatio()

                    // fill to the current position
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * scrubTrack.knobRatio
                        radius: 2
                        color: hud.cRailInk
                    }

                    // bookmark ticks
                    Repeater {
                        model: hud.bookmarkPages
                        delegate: Rectangle {
                            required property var modelData
                            width: 2; height: 11; radius: 1
                            color: hud.cRailTick
                            y: -4
                            x: scrubTrack.width * hud.tickRatio(modelData) - width / 2
                        }
                    }

                    // the knob GROWS while hovering/dragging (TB2's 4->5px-radius grow, scaled to
                    // the approved rest size)
                    Rectangle {
                        id: knob
                        objectName: "hudKnob"
                        width: scrubTrack.pointerActive ? 20 : 16
                        height: width
                        radius: width / 2
                        Behavior on width { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }
                        color: hud.cRailGold
                        border.width: 3
                        border.color: hud.cRailInk
                        x: scrubTrack.width * scrubTrack.knobRatio - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    // page bubble above the knob (on hover / drag)
                    Rectangle {
                        id: bubble
                        visible: scrubTrack.pointerActive
                        height: 22
                        width: bubbleText.implicitWidth + 20
                        radius: 6
                        color: hud.cGlassDeep
                        border.width: 1
                        border.color: theme.edge
                        y: -36
                        x: Math.max(0, Math.min(scrubTrack.width - width,
                                    scrubTrack.width * scrubTrack.knobRatio - width / 2))
                        Text {
                            id: bubbleText
                            objectName: "hudBubbleText"
                            anchors.centerIn: parent
                            // consults the shell's geometry-honest resolver instead of recomputing
                            // its own estimate — degrades to the pure math if the seam is absent.
                            text: (hud.reader && hud.reader.pageAtFraction)
                                  ? hud.reader.pageAtFraction(scrubTrack.knobRatio)
                                  : hud.pageForRatio(scrubTrack.knobRatio)
                            color: theme.gold
                            font.family: theme.hud
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                }

                HoverHandler { id: scrubHover }
                MouseArea {
                    anchors.fill: parent
                    anchors.topMargin: -10
                    anchors.bottomMargin: -10
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    function _ratioAt(mx) {
                        return scrubTrack.width > 0 ? hud._clamp01(mx / scrubTrack.width) : 0
                    }
                    // PAUSE auto-hide for the whole drag, then reset on release — else scrubbing a
                    // long book for >2.5s fires _autoHide -> chromeVisible=false -> this MouseArea
                    // (inside the opacity-gated chromeLayer) vanishes under the cursor mid-drag.
                    onPressed: function (m) { autoHideTimer.stop(); hud._scrubbing = true; hud._scrubRatio = _ratioAt(m.x); hud._emitScrub(hud._scrubRatio) }
                    // a plain hover move only moves the bubble; only a real drag EMITS navigation.
                    onPositionChanged: function (m) {
                        hud._scrubRatio = _ratioAt(m.x)
                        if (hud._scrubbing) hud._emitScrub(hud._scrubRatio)
                    }
                    onReleased: { hud._scrubbing = false; hud.notifyActivity() }
                }
            }
        }
    }

    // ================= scrub interaction state =================
    property bool _scrubbing: false
    property real _scrubRatio: 0
    // PAGED layouts seek a PAGE; only the continuous column takes a scroll fraction. Asking
    // `mode === "double_page"` here sent the strip command to Single Page, whose surface has no
    // scrolling column at all — so dragging the rail in Single Page did nothing whatsoever.
    function _emitScrub(ratio) {
        if (paged) seekRequested(pageForRatio(ratio))
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
