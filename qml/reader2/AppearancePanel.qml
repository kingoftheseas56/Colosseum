// AppearancePanel.qml — the reader's RIGHT GLASS PANEL (TASK 10): a slide-in column of
// reading controls that LIVE-APPLY to the paper as you touch them — theme swatches,
// typeface cards, a size stepper, line-spacing + margin sliders, a justify segment, and the
// reading-ruler CONTROLS (a toggle + band-height + dim sliders; the ruler's actual visual
// overlay is Task 11). 348px glass over the paper, sliding in from the RIGHT edge with the
// mock's ~.32s cubic. Pixel contract: the chrome mock's `.panel.right`, `.apphead`,
// `.grp`/`.glbl`, `.swatches`/`.swatch`, `.fontrow`/`.fontcard`, `.stepper`, `.sliderrow`,
// `.segment`, `.rulerrow`, `.switch` (agents/colosseum-book-reader-chrome-mock.html).
//
// Like the rest of the reader2 chrome this overlay is BRIDGE-FREE: it takes the CURRENT
// appearance via the `appearance` property and reports every edit up via a single
// changed(key, value) signal. ReaderShell owns the merge + persist (settings.json `reader2`
// sub-object) + the live push to the paper — so this stays instantiable headless (smoke).
//
// [Agent 2 (Claude), biblio]
import QtQuick

Item {
    id: panel

    // ---- inputs (bound by ReaderChrome from ReaderShell's shell.appearance) ----
    property bool open: false
    property var appearance: ({})

    // ---- signals up ----
    signal closeRequested()
    // One edit = one (key, value): ReaderShell does mergeAppearance → persist → live-apply.
    signal changed(string key, var value)

    readonly property int colWidth: 348
    readonly property int topBarPx: 64           // keep the top bar's right icons clickable

    // ---- current values, read from `appearance` with safe fallbacks (defaults from the model) ----
    readonly property string curTheme: (appearance && appearance.theme) ? String(appearance.theme) : "night"
    readonly property string curFont: (appearance && appearance.font) ? String(appearance.font) : "literata"
    readonly property int curSize: (appearance && Number.isFinite(appearance.sizePx)) ? appearance.sizePx : 18
    readonly property real curLine: (appearance && Number.isFinite(appearance.lineHeight)) ? appearance.lineHeight : 1.6
    readonly property int curMargin: (appearance && Number.isFinite(appearance.marginPx)) ? appearance.marginPx : 72
    readonly property bool curJustify: appearance ? (appearance.justify === undefined ? true : !!appearance.justify) : true
    readonly property bool curScrolled: appearance ? appearance.flow === "scrolled" : false
    readonly property bool curRulerOn: appearance ? !!appearance.rulerOn : false
    readonly property int curBand: (appearance && Number.isFinite(appearance.rulerHeightPx)) ? appearance.rulerHeightPx : 92
    readonly property int curDim: (appearance && Number.isFinite(appearance.rulerDimPct)) ? appearance.rulerDimPct : 42
    // band vertical position (0=top .. 100=bottom), clamped so a stored out-of-range value
    // can't drive the overlay off-screen; the overlay's geometry also clamps as a backstop.
    readonly property int curYPct: (appearance && Number.isFinite(appearance.rulerYPct)) ? clampInt(appearance.rulerYPct, 0, 100) : 40

    function clampInt(v, lo, hi) { return Math.max(lo, Math.min(hi, Math.round(v))) }

    // ---------- click-outside-to-dismiss (transparent; paper area LEFT of the column) ----------
    // Only armed while open, and inset below the top bar so the TopBar icons stay live.
    MouseArea {
        anchors.left: parent.left
        anchors.right: column.left
        anchors.top: parent.top
        anchors.topMargin: panel.topBarPx
        anchors.bottom: parent.bottom
        enabled: panel.open
        onClicked: panel.closeRequested()
    }

    // ---------- the glass column ----------
    Rectangle {
        id: column
        width: panel.colWidth
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        color: Theme.panelBg

        // slide in/out from the RIGHT edge (mock: transform .32s cubic-bezier(.2,.8,.2,1)).
        transform: Translate {
            x: panel.open ? 0 : column.width
            Behavior on x {
                NumberAnimation {
                    duration: 320
                    easing.type: Easing.Bezier
                    easing.bezierCurve: [0.2, 0.8, 0.2, 1, 1, 1]
                }
            }
        }

        // left hairline border (mock border-left: 1px var(--bar-border)).
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.barBorder
        }

        // OWN click-swallow (house doctrine): taps/scrolls inside the column never fall
        // through to the paper or the chrome's double-click toggle beneath. Declared FIRST so
        // the real controls (below) sit on top and still receive their clicks.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            onWheel: (w) => { w.accepted = true }
        }

        // ---------- header ----------
        Text {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 22
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            text: "Appearance"
            color: Theme.inkTitle
            font.family: Theme.display
            font.weight: Font.Medium
            font.pixelSize: 16
        }

        // ---------- scrollable body ----------
        Flickable {
            id: bodyFlick
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.topMargin: 8
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            anchors.bottomMargin: 22
            clip: true
            contentWidth: width
            contentHeight: form.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: form
                width: bodyFlick.width
                spacing: 18

                // ===== Theme =====
                Column {
                    width: parent.width
                    spacing: 10
                    GroupLabel { text: "Theme" }
                    Row {
                        width: parent.width
                        spacing: 10
                        readonly property real swW: (width - spacing * 3) / 4
                        Swatch { width: parent.swW; label: "Paper"; bg: "#e9e4d8"; textColor: "#565044"
                            active: panel.curTheme === "paper"; onPicked: panel.changed("theme", "paper") }
                        Swatch { width: parent.swW; label: "Sepia"; bg: "#e5d5b8"; textColor: "#6b5b40"
                            active: panel.curTheme === "sepia"; onPicked: panel.changed("theme", "sepia") }
                        Swatch { width: parent.swW; label: "Slate"; bg: "#232830"; textColor: "#9aa4b4"
                            active: panel.curTheme === "slate"; onPicked: panel.changed("theme", "slate") }
                        Swatch { width: parent.swW; label: "Night"; bg: "#111013"; textColor: Qt.rgba(238/255,233/255,222/255,0.6)
                            active: panel.curTheme === "night"; onPicked: panel.changed("theme", "night") }
                    }
                }

                // ===== Typeface =====
                Column {
                    width: parent.width
                    spacing: 10
                    GroupLabel { text: "Typeface" }
                    Row {
                        width: parent.width
                        spacing: 10
                        readonly property real cardW: (width - spacing * 2) / 3
                        // each card renders its "Aa" in its OWN face.
                        FontCard { width: parent.cardW; face: "Literata"; label: "Literata"
                            active: panel.curFont === "literata"; onPicked: panel.changed("font", "literata") }
                        FontCard { width: parent.cardW; face: Theme.display; label: "Fraunces"
                            active: panel.curFont === "fraunces"; onPicked: panel.changed("font", "fraunces") }
                        FontCard { width: parent.cardW; face: Theme.ui; label: "Inter"
                            active: panel.curFont === "inter"; onPicked: panel.changed("font", "inter") }
                    }
                }

                // ===== Size =====
                Column {
                    width: parent.width
                    spacing: 10
                    GroupLabel { text: "Size" }
                    Row {
                        width: parent.width
                        spacing: 14
                        StepBtn { label: "A−"; onClicked: panel.changed("sizePx", panel.clampInt(panel.curSize - 1, 12, 26)) }
                        Text {
                            width: parent.width - 34 * 2 - 14 * 2
                            anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: Text.AlignHCenter
                            text: panel.curSize + " px"
                            color: Theme.inkDim
                            font.family: Theme.ui
                            font.pixelSize: 13
                        }
                        StepBtn { label: "A+"; onClicked: panel.changed("sizePx", panel.clampInt(panel.curSize + 1, 12, 26)) }
                    }
                }

                // ===== Layout =====
                Column {
                    width: parent.width
                    spacing: 10
                    GroupLabel { text: "Layout" }
                    SliderRow {
                        width: parent.width
                        caption: "Line spacing"; minValue: 1.2; maxValue: 2.2; stepSize: 0.1
                        value: panel.curLine; valueText: panel.curLine.toFixed(1)
                        onMoved: (v) => panel.changed("lineHeight", Math.round(v * 10) / 10)
                    }
                    SliderRow {
                        width: parent.width
                        caption: "Margins"; minValue: 24; maxValue: 160; stepSize: 4
                        value: panel.curMargin; valueText: String(panel.curMargin)
                        onMoved: (v) => panel.changed("marginPx", Math.round(v))
                    }
                    Item {
                        width: parent.width
                        height: 34
                        Text {
                            id: justifyCaption
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 78
                            text: "Justify"
                            color: Theme.inkFaint
                            font.family: Theme.ui
                            font.pixelSize: 12
                        }
                        Segment {
                            anchors.left: justifyCaption.right
                            anchors.right: parent.right
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            leftLabel: "Justified"; rightLabel: "Ragged"
                            leftActive: panel.curJustify
                            onPicked: (left) => panel.changed("justify", left)
                        }
                    }
                    // Reading flow (2026-07-20, Hemanth: "there is no vertical scroll reading") —
                    // Pages = the paginator's column flips; Scroll = one continuous vertical run.
                    Item {
                        width: parent.width
                        height: 34
                        Text {
                            id: flowCaption
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 78
                            text: "Flow"
                            color: Theme.inkFaint
                            font.family: Theme.ui
                            font.pixelSize: 12
                        }
                        Segment {
                            anchors.left: flowCaption.right
                            anchors.right: parent.right
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            leftLabel: "Pages"; rightLabel: "Scroll"
                            leftActive: !panel.curScrolled
                            onPicked: (left) => panel.changed("flow", left ? "paginated" : "scrolled")
                        }
                    }
                }

                // ===== Reading ruler (CONTROLS only — the overlay is Task 11) =====
                Column {
                    width: parent.width
                    spacing: 10
                    GroupLabel { text: "Reading ruler" }
                    Item {
                        width: parent.width
                        height: 22
                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Focus band while reading"
                            color: Theme.inkDim
                            font.family: Theme.ui
                            font.weight: Font.DemiBold
                            font.pixelSize: 13
                        }
                        ToggleSwitch {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            on: panel.curRulerOn
                            onToggled: panel.changed("rulerOn", !panel.curRulerOn)
                        }
                    }
                    SliderRow {
                        width: parent.width
                        caption: "Band height"; minValue: 48; maxValue: 200; stepSize: 4
                        value: panel.curBand; valueText: String(panel.curBand)
                        onMoved: (v) => panel.changed("rulerHeightPx", Math.round(v))
                    }
                    SliderRow {
                        width: parent.width
                        caption: "Dim outside"; minValue: 0; maxValue: 100; stepSize: 2
                        value: panel.curDim; valueText: panel.curDim + "%"
                        onMoved: (v) => panel.changed("rulerDimPct", Math.round(v))
                    }
                    // Band position (0=top .. 100=bottom) — the pointer-transparent overlay
                    // has no on-page grip, so the band is repositioned from here (Task 11).
                    SliderRow {
                        width: parent.width
                        caption: "Band position"; minValue: 0; maxValue: 100; stepSize: 2
                        value: panel.curYPct; valueText: panel.curYPct + "%"
                        onMoved: (v) => panel.changed("rulerYPct", Math.round(v))
                    }
                }
            }
        }
    }

    // ---------- reusable pieces ----------

    // group label — uppercase, tracked, ghost (mock .glbl).
    component GroupLabel: Text {
        color: Theme.inkGhost
        font.family: Theme.ui
        font.pixelSize: 11
        font.weight: Font.Bold
        font.letterSpacing: 1.8
        font.capitalization: Font.AllUppercase
    }

    // a theme swatch — bg fill + label; a gold ring OUTSIDE it when active (mock outline+offset).
    component Swatch: Item {
        id: sw
        property color bg: "#000000"
        property color textColor: "#ffffff"
        property string label: ""
        property bool active: false
        signal picked()
        height: width / 1.35

        Rectangle {
            visible: sw.active
            anchors.fill: parent
            anchors.margins: -3
            radius: 12
            color: "transparent"
            border.color: Theme.gold
            border.width: 2
        }
        Rectangle {
            anchors.fill: parent
            radius: 9
            color: sw.bg
            border.color: Qt.rgba(1, 1, 1, 0.10)
            border.width: 1
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 7
                anchors.bottomMargin: 7
                text: sw.label
                color: sw.textColor
                font.family: Theme.ui
                font.weight: Font.DemiBold
                font.pixelSize: 11
            }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sw.picked() }
    }

    // a typeface card — "Aa" in its own face + the family name; gold border when active.
    component FontCard: Item {
        id: fc
        property string face: ""
        property string label: ""
        property bool active: false
        signal picked()
        height: 62

        Rectangle {
            anchors.fill: parent
            radius: 9
            color: "transparent"
            border.width: 1
            border.color: fc.active ? Qt.rgba(240/255, 194/255, 74/255, 0.6) : Qt.rgba(1, 1, 1, 0.10)
            Column {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                spacing: 3
                Text {
                    text: "Aa"
                    font.family: fc.face
                    font.pixelSize: 19
                    color: fc.active ? Theme.ink : Theme.inkDim
                }
                Text {
                    text: fc.label
                    font.family: Theme.ui
                    font.pixelSize: 11
                    color: Theme.inkGhost
                }
            }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: fc.picked() }
    }

    // a size stepper button (A- / A+) — bordered square, Literata glyph (mock .stepper button).
    component StepBtn: Item {
        id: sb
        property string label: ""
        signal clicked()
        width: 34
        height: 34
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: sbMa.containsMouse ? Theme.rowHover : "transparent"
            border.color: Qt.rgba(1, 1, 1, 0.12)
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: sb.label
                color: Theme.inkDim
                font.family: "Literata"
                font.pixelSize: 15
            }
        }
        MouseArea {
            id: sbMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: sb.clicked()
        }
    }

    // a labeled slider row — caption | track(fill+knob) | value. Emits moved(v) with a
    // step-quantized value, and only when that value actually changes (no drag spam).
    component SliderRow: Item {
        id: sr
        property string caption: ""
        property real minValue: 0
        property real maxValue: 1
        property real value: 0
        property real stepSize: 0
        property string valueText: ""
        signal moved(real v)
        height: 22

        function frac() {
            return sr.maxValue > sr.minValue
                 ? Math.max(0, Math.min(1, (sr.value - sr.minValue) / (sr.maxValue - sr.minValue))) : 0
        }
        function quant(v) {
            var c = Math.max(sr.minValue, Math.min(sr.maxValue, v))
            if (sr.stepSize > 0) c = sr.minValue + Math.round((c - sr.minValue) / sr.stepSize) * sr.stepSize
            return Math.max(sr.minValue, Math.min(sr.maxValue, c))
        }

        Text {
            id: capText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 78
            text: sr.caption
            color: Theme.inkFaint
            font.family: Theme.ui
            font.pixelSize: 12
        }
        Text {
            id: valText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 34
            horizontalAlignment: Text.AlignRight
            text: sr.valueText
            color: Theme.inkGhost
            font.family: Theme.ui
            font.pixelSize: 12
        }
        Item {
            anchors.left: capText.right
            anchors.right: valText.left
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            height: 22

            Rectangle {
                id: slTrack
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 3
                radius: 2
                color: Theme.track

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * sr.frac()
                    radius: 2
                    color: Qt.rgba(1, 1, 1, 0.45)
                }
                Rectangle {
                    width: 11
                    height: 11
                    radius: 5.5
                    color: Theme.ink
                    anchors.verticalCenter: parent.verticalCenter
                    x: parent.width * sr.frac() - width / 2
                }
            }
            MouseArea {
                // fills the 22px inner row — a comfortable target around the 3px track, and
                // deliberately NOT enlarged past it (the rows sit 10px apart, so a taller hit
                // area would overlap the neighbouring slider and grab the wrong one).
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                function emitAt(mx) {
                    var f = slTrack.width > 0 ? Math.max(0, Math.min(1, mx / slTrack.width)) : 0
                    var v = sr.minValue + f * (sr.maxValue - sr.minValue)
                    var qv = sr.quant(v)
                    if (qv !== sr.value) sr.moved(qv)
                }
                onPressed: (m) => emitAt(m.x)
                onPositionChanged: (m) => { if (pressed) emitAt(m.x) }
            }
        }
    }

    // a two-option segment (Justified / Ragged) — the active half is filled (mock .segment).
    component Segment: Item {
        id: seg
        property string leftLabel: ""
        property string rightLabel: ""
        property bool leftActive: true
        signal picked(bool left)
        height: 34

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: Qt.rgba(1, 1, 1, 0.05)
            border.color: Qt.rgba(1, 1, 1, 0.09)
            border.width: 1
            Row {
                id: segRow
                anchors.fill: parent
                anchors.margins: 3
                spacing: 2
                readonly property real segW: (width - spacing) / 2
                SegBtn { width: segRow.segW; label: seg.leftLabel; active: seg.leftActive; onClicked: seg.picked(true) }
                SegBtn { width: segRow.segW; label: seg.rightLabel; active: !seg.leftActive; onClicked: seg.picked(false) }
            }
        }
    }
    component SegBtn: Item {
        id: sgb
        property string label: ""
        property bool active: false
        signal clicked()
        height: parent ? parent.height : 28
        Rectangle {
            anchors.fill: parent
            radius: 6
            color: sgb.active ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
            Text {
                anchors.centerIn: parent
                text: sgb.label
                color: sgb.active ? Theme.ink : Theme.inkFaint
                font.family: Theme.ui
                font.weight: Font.DemiBold
                font.pixelSize: 12
            }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sgb.clicked() }
    }

    // the ruler on/off switch — gold + knob-right when on, gray + knob-left when off (mock .switch).
    component ToggleSwitch: Item {
        id: ts
        property bool on: false
        signal toggled()
        width: 40
        height: 22
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: ts.on ? Theme.gold : Qt.rgba(1, 1, 1, 0.16)
            Rectangle {
                width: 16
                height: 16
                radius: 8
                color: "#141416"
                anchors.verticalCenter: parent.verticalCenter
                x: ts.on ? parent.width - width - 3 : 3
                Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ts.toggled() }
    }
}
