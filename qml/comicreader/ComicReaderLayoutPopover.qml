// ComicReaderLayoutPopover — the compact Layout menu (Task 8, plan 2026-07-28).
//
// The approved shape, verbatim from the design ledger:
//
//   "Long Strip owns its contextual controls in the active Layout menu: portrait width, page
//    spacing, Auto-scroll start/pause and speed."
//   "Layout and motion remain separate. Long Strip creates the vertical page flow; Auto-scroll only
//    supplies motion at the already chosen width. Starting or resuming Auto-scroll must never
//    resize the page."
//   "range: 40-100% of viewport width, default 78%, persisted per series, landscape spreads remain
//    100% width, width changes reflow in place while preserving the visible page/fraction."
//   "Page spacing offers at least Seamless and Breathing Room. Seamless remains the default."
//
// Hemanth called the portrait width out by name while this was being designed — "one of the most
// important features is the potrait width in autoscroll. I hope you're not forgetting about that"
// — and then confirmed 78% as the default. So this panel treats 78 and never-resize as the two
// things it exists to get right, and the ONE law it enforces structurally is that no Auto-scroll
// control has any path to a width change: they raise different signals, and the width verbs are
// the only ones that carry a width at all.
//
// CONTEXTUAL, not conditional-looking. The three layouts are always here — Layout is the command,
// and picking one is what it is for. The Long Strip block appears only when Long Strip is the live
// layout, because portrait width and page spacing mean nothing to a paged surface and Auto-scroll
// has nothing to move.
//
// PRESENTATION + INTENTS ONLY, like the rest of the reader chrome (ComicReaderImagePopover is this
// panel's sibling and they are deliberately one family — same glass, same chip vocabulary, same
// anchored drop). It owns no reading state and never touches the backend. Every control raises one
// signal and the shell decides.
//
// `open` and `longStripControlsVisible` are RULE-level properties, deliberately not `visible`:
// QQuickItem.visible is EFFECTIVE visibility, so a test asserting on a child's `visible` reads its
// ancestors' state too. These say what this surface BELIEVES, whatever its parents are doing.
//
// EVERY control is harness-callable as a plain function (the ComicReaderInput house pattern), and
// the pointer paths call those same functions — so the tested logic is the shipped logic.

import QtQuick
import "../"   // Theme (lives in qml/, the parent of qml/comicreader/)

Item {
    id: root
    objectName: "readerLayoutPopover"

    // Fill the shell explicitly. anchors.fill resolves to 0x0 for this overlay mount (the same QML
    // quirk ComicReaderSettingsSheet, the Pages filmstrip and the Image panel all document); an
    // explicit size binding tracks the parent reliably and follows a resize / fullscreen flip.
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    // ================= injected facts (never written back) =================
    property string layout: "long_strip"      // single_page | paired_pages | long_strip
    property int    stripWidthPct: 78         // 40..100 — the approved contract, default 78
    property int    stripGap: 0               // 0..80 px between pages; 0 is Seamless (the default)
    property bool   autoScrollRunning: false  // SESSION state, owned by the shell; never persisted
    property real   autoScrollSpeed: 1.0      // 0.25..3.0, default 1.0

    // ---- open state: the RULE, not the pixels (see the header note) ----
    property bool open: false
    visible: open

    // THE contextual rule, as a readable property rather than a repeated predicate. The gates
    // assert on THIS: an offscreen harness roots its tree invisible, so every child's `visible`
    // reads false there whatever the rule says.
    readonly property bool longStripControlsVisible: layout === "long_strip"

    // ================= intents =================
    signal layoutRequested(string layout)               // pick a layout
    signal stripLayoutRequested(int widthPct, int gap)  // ONE door, BOTH values (the shell's setter shape)
    signal autoScrollStartRequested()
    signal autoScrollPauseRequested()
    signal autoScrollSpeedRequested(real speed)
    signal dismissRequested()                           // close me; nothing else changes

    // ================= the chrome this surface has to live between =================
    // Mirrors ComicReaderHud's own numbers: the title strip (38) + the command bar (46) at the top,
    // the gold rail (54) at the bottom. Properties rather than literals so a shell that ever changes
    // the chrome says so in one place.
    property int chromeTopInset: 84
    property int railHeight: 54
    // Where the panel hangs. `anchorX` is the CENTRE of the command that raised this panel, in the
    // shell's coordinates, published by ComicReaderCommandBar through ComicReaderHud (Task 8's
    // anchor seam). It is dynamic on purpose: the Layout command is a live READOUT, so its label —
    // and therefore its centre — moves when the layout or the order changes.
    //
    // -1 means "no seam" (a bare harness mount, or a chrome that never published). Then, and only
    // then, the panel falls back to the bar's right margin, which is where Task 7's Image panel
    // hung before this seam existed.
    property real anchorX: -1
    property int panelRightMargin: 26
    property int panelEdgeMargin: 12          // never let the panel touch the window edge

    Theme { id: theme }

    readonly property color cGlassDeep:  Qt.rgba(9 / 255, 10 / 255, 13 / 255, 0.96)
    readonly property color cEdge:       Qt.rgba(1, 1, 1, 0.18)
    readonly property color cEdgeSoft:   Qt.rgba(1, 1, 1, 0.09)
    readonly property color cRowLine:    Qt.rgba(1, 1, 1, 0.06)
    readonly property color cGoldChipBg: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.08)
    readonly property color cGoldBorder: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
    readonly property color cTrack:      Qt.rgba(1, 1, 1, 0.10)

    // ================= the approved vocabulary, fixed =================
    // Which layouts exist and what they are called is a design decision, not a runtime one.
    readonly property var layouts: ["single_page", "paired_pages", "long_strip"]
    function layoutLabel(v) {
        return v === "single_page" ? "Single page"
             : v === "paired_pages" ? "Paired pages"
             : v === "long_strip" ? "Long strip" : ""
    }
    function layoutGlyph(v) {
        return v === "single_page" ? "layoutSingle"
             : v === "paired_pages" ? "layoutPaired" : "layoutStrip"
    }

    // Page spacing: the two the design names, and only those. Seamless is the default and stays it.
    // (The retired settings sheet also carried an 8px middle step; it was never a named design
    // value, and a series that has one keeps it — nothing here overwrites a gap it does not offer,
    // because a gap is only ever written by a tap on one of these two chips.)
    readonly property var spacings: [0, 20]
    function spacingLabel(px) { return px === 0 ? "Seamless" : "Breathing room" }

    // The portrait-width contract, in one place: 40..100, default 78.
    readonly property int widthMin: 40
    readonly property int widthMax: 100
    readonly property int widthDefault: 78
    // ...and the speed contract: 0.25..3.0, default 1.0.
    readonly property real speedMin: 0.25
    readonly property real speedMax: 3.0
    readonly property real speedDefault: 1.0

    // ================= the named verbs (the pointer paths call THESE; so do the gates) =================

    // Pick a layout. An unknown value is INERT rather than falling through onto a neighbour, and
    // re-picking the live layout raises nothing: the shell's setLayout already refuses a no-op, and
    // a menu that emitted anyway would make "did the reader change something" unanswerable.
    function setLayout(value) {
        var v = String(value)
        if (layouts.indexOf(v) < 0) return
        if (v === layout) return
        layoutRequested(v)
    }

    // Portrait width. Clamped at this panel's own door to the approved range — the backend clamps
    // too, but a control that can emit 400 and be silently corrected is a control whose readout
    // lies about what it just asked for.
    //
    // It carries the CURRENT gap, unchanged, because the shell's setter takes both: sending a
    // partial pair would silently reset the spacing every time the width moved.
    function setPortraitWidth(pct) {
        var w = Math.round(Math.max(widthMin, Math.min(widthMax, Number(pct))))
        if (!isFinite(w)) return
        stripLayoutRequested(w, root.stripGap)
    }

    // Page spacing. Same door, same completeness rule in the other direction: it carries the
    // current WIDTH through untouched, so choosing Breathing room can never resize the page.
    function setSpacing(gap) {
        var g = Math.round(Math.max(0, Math.min(80, Number(gap))))
        if (!isFinite(g)) return
        stripLayoutRequested(root.stripWidthPct, g)
    }

    // ---- Auto-scroll. NOTHING here carries a width, and that is structural, not a promise:
    //      these three signals have no width argument to pass. ----
    function startAutoScroll() { autoScrollStartRequested() }
    function pauseAutoScroll() { autoScrollPauseRequested() }
    function toggleAutoScroll() {
        if (autoScrollRunning) autoScrollPauseRequested()
        else autoScrollStartRequested()
    }
    function setSpeed(v) {
        var s = Math.max(speedMin, Math.min(speedMax, Number(v)))
        if (!isFinite(s)) return
        // Quarter steps on the wire. A continuous handle producing 1.0374892x is a number nobody
        // asked for and a readout nobody can read back.
        autoScrollSpeedRequested(Math.round(s * 4) / 4)
    }
    // Close, and ONLY close. Nothing here can change the layout, the width or the motion.
    function dismiss() { dismissRequested() }

    // The speed as it reads on the face: "1x", "1.5x", "0.25x".
    function speedText(v) {
        var s = Math.round(Number(v) * 4) / 4
        if (!isFinite(s)) s = root.speedDefault
        return (s === Math.round(s) ? String(s) : String(s)) + "×"
    }

    // ================= the canvas catcher =================
    // "Panel floats over the comic without shifting it" — and a click on the comic puts it away. It
    // covers the comic only: the chrome bands keep working, so Back, the commands and the rail are
    // all still reachable with the panel up. It emits dismissRequested and nothing else.
    MouseArea {
        objectName: "layoutDismissCatcher"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: root.chromeTopInset
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.railHeight
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.ArrowCursor
        function tap() { root.dismiss() }
        onClicked: tap()
    }

    // ================= vocabulary =================
    // Ported from ComicReaderImagePopover so the two anchored panels read as ONE family while both
    // exist — the same thing that panel did with the settings sheet's switch, and for the same
    // reason. A shared file would be a third surface to keep in step for two callers.

    // a single choice chip — gold ONLY when active (the active-choice-only-gold law)
    component Chip: Rectangle {
        id: chip
        property bool active: false
        property string label: ""
        signal tapped()
        implicitHeight: 22
        implicitWidth: chipText.implicitWidth + 18
        radius: 7
        color: chip.active ? root.cGoldChipBg : "transparent"
        border.width: 1
        border.color: chip.active ? root.cGoldBorder : root.cEdgeSoft
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.active ? theme.gold : theme.inkDimmer
            font.family: theme.hud
            font.pixelSize: 12
            font.bold: chip.active
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: chip.tapped() }
    }

    // A continuous control. `value` is a PLAIN property the owner assigns, never a binding: a bound
    // value that the drag then writes to would destroy its own binding on the first press and never
    // track the real state again (the scar this codebase carries three times over — the HUD's thumb
    // `y:`, the cursor shape the shell refuses to assign, the Image panel's sliders). So
    // `externalValue` is the bound one and `value` follows it only while the handle is not held.
    component ReaderSlider: Item {
        id: slider
        property string label: ""
        property real from: 0
        property real to: 100
        property real externalValue: 0
        property real value: 0
        property real step: 0            // 0 = continuous; >0 snaps the emitted value
        property string readout: String(Math.round(slider.value))
        property bool held: false
        signal moved(real value)

        implicitHeight: 44
        width: parent ? parent.width : 0

        onExternalValueChanged: if (!held) value = externalValue
        Component.onCompleted: value = externalValue

        // PURE, and harness-callable: which value a press at `x` means, and where the handle for
        // `value` sits. Testing the mapping needs no synthesized mouse events, and the pointer path
        // below cannot drift from it.
        function valueAt(x) {
            if (track.width <= 0) return slider.from
            var t = Math.max(0, Math.min(1, x / track.width))
            var v = slider.from + t * (slider.to - slider.from)
            if (slider.step > 0) v = Math.round(v / slider.step) * slider.step
            return Math.max(slider.from, Math.min(slider.to, v))
        }
        function fractionOf(v) {
            if (slider.to === slider.from) return 0
            return Math.max(0, Math.min(1, (v - slider.from) / (slider.to - slider.from)))
        }
        // THE one door a drag or a harness goes through.
        function moveTo(v) {
            var clamped = Math.max(slider.from, Math.min(slider.to, v))
            slider.value = clamped
            slider.moved(clamped)
        }

        Text {
            id: sliderLabel
            anchors.left: parent.left
            anchors.top: parent.top
            text: slider.label
            color: theme.inkDim
            font.family: theme.hud
            font.pixelSize: 12
        }
        Text {
            objectName: slider.objectName.length ? (slider.objectName + "Readout") : ""
            anchors.right: parent.right
            anchors.top: parent.top
            text: slider.readout
            color: theme.inkDimmer
            font.family: theme.hud
            font.pixelSize: 11
        }

        Rectangle {
            id: track
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: sliderLabel.bottom
            anchors.topMargin: 11
            height: 3
            radius: 1.5
            color: root.cTrack

            Rectangle {
                height: parent.height
                radius: parent.radius
                width: Math.round(track.width * slider.fractionOf(slider.value))
                color: theme.gold
                opacity: 0.85
            }
            Rectangle {
                width: 11; height: 11
                radius: 5.5
                y: -4
                x: Math.round(track.width * slider.fractionOf(slider.value)) - 5.5
                color: theme.gold
            }

            MouseArea {
                anchors.fill: parent
                anchors.topMargin: -11
                anchors.bottomMargin: -11
                cursorShape: Qt.PointingHandCursor
                onPressed: function (mouse) { slider.held = true; slider.moveTo(slider.valueAt(mouse.x)) }
                onPositionChanged: function (mouse) { if (slider.held) slider.moveTo(slider.valueAt(mouse.x)) }
                onReleased: slider.held = false
                onCanceled: slider.held = false
            }
        }
    }

    // ================= the panel =================
    Rectangle {
        id: panel
        objectName: "layoutPanel"
        // The Image panel's exact width. These two are the reader's only anchored drops and they
        // are meant to read as one family — two compact panels of different widths hanging off the
        // same row would read as two systems.
        width: 322
        // Content-sized: the panel is exactly as tall as what it holds, which is what makes the
        // Long Strip block GROW the same panel rather than opening a second surface.
        height: column.implicitHeight + 28
        // Centred under the command that raised it, and clamped inside the reader so a command near
        // an edge can never push the panel off screen.
        x: {
            if (root.anchorX < 0)
                return root.width - width - root.panelRightMargin
            return Math.max(root.panelEdgeMargin,
                            Math.min(root.width - width - root.panelEdgeMargin,
                                     root.anchorX - width / 2))
        }
        y: root.chromeTopInset + 8
        radius: 12
        color: root.cGlassDeep
        border.width: 1
        border.color: root.cEdge
        opacity: root.open ? 1.0 : 0.0
        // Fades IN only, and `visible` stays a plain rule: an offscreen harness never ticks
        // animations, so gating visibility on the animation would make the panel permanently
        // invisible under test (the Image panel and the Pages filmstrip both carry this note).
        Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

        // click-swallower (floating-panel house law): the panel's own ground must not fall through
        // to the catcher below and dismiss the thing you are adjusting. Declared FIRST so every
        // control sits above it.
        MouseArea {
            id: panelSwallow
            objectName: "layoutPanelSwallow"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            function tap() { /* swallowed on purpose */ }
            onClicked: tap()
        }

        Column {
            id: column
            objectName: "layoutPanelColumn"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 14
            spacing: 0

            // ---- ALWAYS: the three layouts. This is what the command is FOR. ----
            Repeater {
                model: root.layouts
                delegate: Item {
                    id: choice
                    required property string modelData
                    objectName: "layoutChoice_" + modelData
                    readonly property bool active: root.layout === modelData
                    width: parent.width
                    height: 32
                    function tap() { root.setLayout(modelData) }

                    // A REAL glyph, never a text mark (semantic-icon-audit law) — the same three
                    // the command bar wears for the same three layouts, so the menu and the bar
                    // are visibly one idea.
                    ComicReaderIcon {
                        id: choiceIcon
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16; height: 16
                        kind: root.layoutGlyph(choice.modelData)
                        accessibleName: root.layoutLabel(choice.modelData)
                        ink: choice.active ? theme.gold : (choiceMa.containsMouse ? theme.ink : theme.inkDim)
                    }
                    Text {
                        anchors.left: choiceIcon.right
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.layoutLabel(choice.modelData)
                        color: choice.active ? theme.gold : (choiceMa.containsMouse ? theme.ink : theme.inkDim)
                        font.family: theme.hud
                        font.pixelSize: 13
                        font.bold: choice.active
                    }
                    MouseArea {
                        id: choiceMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: choice.tap()
                    }
                }
            }

            // ---- LONG STRIP's contextual controls, inside the SAME panel ----
            // Portrait width and page spacing describe a vertical column of pages; Auto-scroll has
            // nothing to move without one. In a paged layout they are not dimmed, they are absent.
            Column {
                id: longStrip
                objectName: "layoutLongStripSection"
                width: parent.width
                spacing: 0
                visible: root.longStripControlsVisible
                height: root.longStripControlsVisible ? implicitHeight : 0
                clip: true

                Item { width: 1; height: 8 }
                Rectangle { width: parent.width; height: 1; color: root.cRowLine }
                Item { width: 1; height: 6 }

                // ---- Portrait width. THE first-class control (Hemanth named it himself). ----
                ReaderSlider {
                    id: widthSlider
                    objectName: "layoutPortraitWidth"
                    label: "Portrait width"
                    from: root.widthMin; to: root.widthMax
                    step: 1
                    externalValue: root.stripWidthPct
                    readout: Math.round(value) + "%"
                    onMoved: function (v) { root.setPortraitWidth(v) }
                }

                // ---- Page spacing. Seamless is the default and stays it. ----
                Item {
                    width: parent.width
                    height: 32
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Page spacing"
                        color: theme.inkDim
                        font.family: theme.hud
                        font.pixelSize: 12
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5
                        Repeater {
                            model: root.spacings
                            delegate: Chip {
                                required property int modelData
                                objectName: "layoutSpacing_" + modelData
                                label: root.spacingLabel(modelData)
                                active: root.stripGap === modelData
                                onTapped: root.setSpacing(modelData)
                            }
                        }
                    }
                }

                Item { width: 1; height: 4 }
                Rectangle { width: parent.width; height: 1; color: root.cRowLine }
                Item { width: 1; height: 4 }

                // ---- Auto-scroll: motion ONLY, at the width already chosen. ----
                // The start/pause control carries no width and cannot reach one — the whole
                // never-resize rule, expressed as an absence rather than a guard.
                Item {
                    objectName: "layoutAutoScrollRow"
                    width: parent.width
                    height: 34
                    function tap() { root.toggleAutoScroll() }
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Auto-scroll"
                        color: theme.inkDim
                        font.family: theme.hud
                        font.pixelSize: 12
                    }
                    Chip {
                        id: autoChip
                        objectName: "layoutAutoScrollToggle"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        // It names what a press DOES, not what the reader is watching: a control
                        // labelled with the current state is the one everybody presses by mistake.
                        label: root.autoScrollRunning ? "Pause" : "Start"
                        active: root.autoScrollRunning
                        onTapped: root.toggleAutoScroll()
                    }
                }

                // ---- ...and its speed. Motion only: this never touches the width either. ----
                ReaderSlider {
                    id: speedSlider
                    objectName: "layoutAutoScrollSpeed"
                    label: "Speed"
                    from: root.speedMin; to: root.speedMax
                    step: 0.25
                    externalValue: root.autoScrollSpeed
                    readout: root.speedText(value)
                    onMoved: function (v) { root.setSpeed(v) }
                }

                Item { width: 1; height: 2 }
            }
        }
    }
}
