// ComicReaderSettingsSheet — the reader's glass side sheet (Task 12, mockup surface 02). Slides from
// the right over a dimmed page; sectioned in the player's letter-spaced label voice, gold marking
// ONLY the active choice. "Lineage layout, player soul": glass-deep panel, Segoe UI chrome, sparing
// gold. Opens from the HUD settings pill or right-click (shell wires settingsRequested -> open()).
//
// Mode writes only reader.persistedMode / reader.persistedDirection (never reader.mode / reader.rtl),
// like the HUD, so a crossing's load() still owns the actual toggle.
//
// Sections: DISPLAY (Mode, Night veil) always · DOUBLE PAGE (Coupling, Gutter shadow, Zoom readout)
// in Manga/Comic · LONG STRIP (Page width, Gap) in Strip — those two are mirrors, exactly one is up
// at a time · TOOLS (2x2 launcher grid, Memory saver, danger row) in every mode, because a tool is
// not a display choice.
//
// The tool tiles ask the SHELL to raise each overlay; those overlays land in the following slices,
// exactly as the HUD's own pills already work. The danger actions ARM on the first tap and fire on
// the second — nothing here destroys state on a single touch.
//
// NOT persisted across launches yet: every setting here is live-for-the-session, same as it was
// before this sheet existed. One pass wires the whole sheet to a Settings store; doing it per-row
// would leave the sheet half-remembering, which is worse than not remembering at all.
//
// PRESENTATION + INTENTS ONLY. Reads reading state off the injected `reader` seam; every `reader.`
// use is guarded so a null/partial seam never errors. Dismiss (X / scrim / close()) emits dismissed()
// and clears `opened`; a tap on the sheet BODY is swallowed (floating-panel/click-swallower law).

import QtQuick
import QtQuick.Window   // Window attached property (reliable overlay sizing)
import "../"   // Theme (lives in qml/, the parent of qml/comicreader/)

Item {
    id: root
    // Fill the shell explicitly (anchors.fill resolved to 0×0 for this overlay mount — a QML anchor
    // quirk; an explicit size binding tracks the parent reliably and follows a resize/fullscreen).
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    // ================= the shell seam =================
    property var reader: null

    // ---- open state (source of truth; the shell binds modalOpen off `opened`) ----
    property bool _open: false
    readonly property bool opened: _open
    signal dismissed()
    function open()  { _open = true }
    function close() { _open = false }
    function toggle() { _open = !_open }
    function dismiss() { close(); dismissed() }

    // the sheet only intercepts input while open (a closed overlay is inert over the reader)
    visible: opened || scrim.opacity > 0.001

    // ---- read-only reading state ----
    // ONE user-facing identity (Hemanth 2026-07-25): manga | comic | strip — direction is baked in.
    readonly property string readingMode: reader ? reader.readingMode : "manga"
    readonly property string nightVeil: reader ? reader.nightVeil : "off"
    readonly property real   gutterStrength: reader ? reader.gutterStrength : 0.35
    readonly property int    zoomPercent:    reader ? reader.zoomPercent : 100
    // who owns the double-page phase right now: "auto" (the probe decided) or "manual" (nudged
    // by hand). The Coupling row's active chip IS this answer.
    readonly property string couplingMode:   reader ? reader.couplingMode : "auto"
    // long-strip taste — portrait page width as a % of the viewport, and the gap between pages.
    readonly property int    stripWidthPct:  reader ? reader.stripWidthPct : 78
    readonly property int    stripGap:       reader ? reader.stripGap : 0
    readonly property bool   memorySaver:    reader ? reader.memorySaver === true : false

    // ---- writes ----
    // the single Manga/Comic/Strip identity; the shell translates it into the internal layout +
    // direction seams so a crossing's load() honors the choice (there is no separate RTL/LTR toggle).
    function setReadingMode(rm) { if (reader) reader.setReadingMode(rm) }
    // night veil is a LIVE reader-wide setting; the sheet writes it straight; the shell's veil reads it.
    function setNightVeil(v)    { if (reader) reader.nightVeil = v }
    // gutter shadow is a live double-page setting; the shell feeds it to the double surface.
    function setGutter(v)       { if (reader) reader.gutterStrength = v }
    // Coupling: Nudge pins the phase by hand (every tap flips it again); Auto hands the decision
    // back to the probe. Tapping Auto while ALREADY auto is a deliberate no-op — the reset re-runs
    // a page-decoding probe, and a chip that is already gold shouldn't pay for it.
    function nudgeCoupling()    { if (reader) reader.nudgeCoupling() }
    function resetCoupling()    { if (reader && root.couplingMode !== "auto") reader.resetCoupling() }
    // ONE setter carries both strip numbers, so changing either preserves the other.
    function setStripWidth(pct) { if (reader) reader.setStripLayout(pct, root.stripGap) }
    function setStripGap(px)    { if (reader) reader.setStripLayout(root.stripWidthPct, px) }
    function setMemorySaver(on) { if (reader) reader.setMemorySaver(on) }

    // ---- danger arming ----
    // Both danger actions destroy state a reader can't get back (a resume spot, a book's bookmarks
    // and spread overrides), so neither fires on a single tap: the first tap ARMS, the second
    // commits. Exactly ONE can be armed at a time — two armed hammers is how you hit the wrong one.
    property string armedDanger: ""
    function armDanger(id, fire) {
        if (root.armedDanger === id) { root.armedDanger = ""; fire() }
        else                          root.armedDanger = id
    }
    // A closed sheet must never reopen mid-swing.
    onOpenedChanged: if (!root.opened) root.armedDanger = ""

    Theme { id: theme }

    // mock's exact glass micro-values (surface 02 is the binding contract; finer than Theme's kit)
    readonly property color cGlassDeep:  Qt.rgba(9 / 255, 10 / 255, 13 / 255, 0.94)
    readonly property color cEdge:       Qt.rgba(1, 1, 1, 0.18)
    readonly property color cEdgeSoft:   Qt.rgba(1, 1, 1, 0.09)
    readonly property color cRowLine:    Qt.rgba(1, 1, 1, 0.06)
    readonly property color cGoldChipBg: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.08)
    readonly property color cGoldBorder: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
    readonly property color cDim:        Qt.rgba(0, 0, 0, 0.44)
    readonly property color cDanger:       "#ff8a8a"                              // mock --danger
    readonly property color cDangerBorder: Qt.rgba(1, 138 / 255, 138 / 255, 0.30)  // mock .danger border
    readonly property color cDangerFill:   Qt.rgba(1, 138 / 255, 138 / 255, 0.10)  // armed only

    // ============================================================================================
    // sheet vocabulary
    // ============================================================================================
    // a single choice chip — gold ONLY when active (the active-choice-only-gold law)
    component Chip: Rectangle {
        id: chip
        property bool active: false
        property string label: ""
        signal tapped()
        implicitHeight: 22
        implicitWidth: chipText.implicitWidth + 20
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

    // a settings row: left label, right a set of chips; a hairline under it
    component SettingRow: Item {
        id: srow
        default property alias setContent: setRow.data
        property string label: ""
        implicitHeight: 37
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: srow.label
            color: theme.inkDim
            font.family: theme.hud
            font.pixelSize: 12
        }
        Row {
            id: setRow
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5
        }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: root.cRowLine }
    }

    // a tool tile — icon over label, the grid's one unit. A REAL ComicReaderIcon, never a text
    // glyph (semantic-icon-audit law). Hover lifts the border, matching the chip's restraint:
    // nothing in the tool grid is a "choice", so nothing here goes gold.
    component ToolTile: Rectangle {
        id: tile
        property string kind: ""
        property string label: ""
        property string iconObjectName: ""
        signal tapped()
        implicitHeight: 58
        radius: 9
        color: "transparent"
        border.width: 1
        border.color: tileHover.hovered ? root.cEdge : root.cEdgeSoft
        Column {
            anchors.centerIn: parent
            spacing: 5
            ComicReaderIcon {
                objectName: tile.iconObjectName
                anchors.horizontalCenter: parent.horizontalCenter
                width: 16; height: 16
                kind: tile.kind
                ink: tileHover.hovered ? theme.ink : theme.inkDim
                accessibleName: tile.label
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: tile.label
                color: tileHover.hovered ? theme.ink : theme.inkDim
                font.family: theme.hud
                font.pixelSize: 11
            }
        }
        HoverHandler { id: tileHover }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: tile.tapped() }
    }

    // a pill switch — the mock's `.sw`. Gold only when ON, same law as the chips.
    component Switch: Rectangle {
        id: sw
        property bool checked: false
        signal tapped()
        implicitWidth: 34
        implicitHeight: 18
        radius: height / 2
        color: sw.checked ? root.cGoldChipBg : "transparent"
        border.width: 1
        border.color: sw.checked ? root.cGoldBorder : root.cEdgeSoft
        Rectangle {
            width: 12; height: 12
            radius: 6
            y: 2
            x: sw.checked ? sw.width - width - 3 : 3
            color: sw.checked ? theme.gold : theme.inkDimmer
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sw.tapped() }
    }

    // a danger action — the mock's bordered red pill. Unarmed it just names itself; ARMED it says
    // what the next tap will do and fills faintly. The armed label IS the confirmation; a modal
    // dialog on top of a modal sheet is one layer too many for a two-item row.
    component DangerAction: Rectangle {
        id: danger
        property bool armed: false
        property string label: ""
        signal tapped()
        implicitHeight: 28
        radius: 8
        color: danger.armed ? root.cDangerFill : "transparent"
        border.width: 1
        border.color: danger.armed ? root.cDanger : root.cDangerBorder
        Text {
            anchors.centerIn: parent
            text: danger.armed ? "Tap again to confirm" : danger.label
            color: root.cDanger
            font.family: theme.hud
            font.pixelSize: 12          // mock says 11.5px; pixelSize is an int — 12 matches the chips
            font.bold: danger.armed
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: danger.tapped() }
    }

    component SectionLabel: Text {
        color: theme.inkDimmer
        font.family: theme.hud
        font.pixelSize: 10
        font.letterSpacing: 2
        font.capitalization: Font.AllUppercase
    }

    // ============================================================================================
    // dim scrim — tap to dismiss (fades with open)
    // ============================================================================================
    Rectangle {
        id: scrim
        objectName: "settingsScrim"
        anchors.fill: parent
        color: root.cDim
        opacity: root.opened ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        signal tapped()
        onTapped: root.dismiss()
        MouseArea { anchors.fill: parent; enabled: root.opened; onClicked: scrim.tapped() }
    }

    // ============================================================================================
    // the glass sheet (slides from the right)
    // ============================================================================================
    Rectangle {
        id: body
        objectName: "settingsBody"
        // Size + right-anchor off the SCRIM. The scrim fills the sheet via anchors.fill and paints
        // reliably, so its geometry tracks the real reader area; binding the body's own width/height/
        // anchors to the root did NOT repaint here (a scene-graph quirk with this deeply-nested overlay
        // mount). A right-edge full-height glass column. The whole sheet fades in with `opened`
        // (root.visible gate), so no per-panel slide is needed.
        width: 352
        height: scrim.height
        y: scrim.y
        x: scrim.x + scrim.width - width
        color: root.cGlassDeep
        // left edge line
        Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: root.cEdge }

        // click-swallower: a tap on the sheet body must NOT fall through to the scrim beneath
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; onClicked: {} }

        Column {
            id: content
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            anchors.topMargin: 20
            spacing: 0

            // ---- title row: "Reader settings" + close X ----
            Item {
                width: parent.width
                height: 34
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Reader settings"
                    color: theme.ink
                    font.family: theme.hud
                    font.pixelSize: 13
                    font.bold: true
                    font.letterSpacing: 0.3
                }
                Item {
                    id: closeX
                    objectName: "settingsCloseX"
                    width: 24; height: 24
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    signal tapped()
                    onTapped: root.dismiss()
                    ComicReaderIcon {
                        anchors.centerIn: parent
                        kind: "close"
                        width: 15; height: 15
                        ink: theme.inkDimmer
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: closeX.tapped() }
                }
            }

            // ============ DISPLAY ============
            SectionLabel { text: "Display"; height: 32; verticalAlignment: Text.AlignVCenter }

            // ONE identity row — Manga (RTL double-page, MangaPlus) · Comic (LTR double-page) ·
            // Strip (vertical scroll). Direction is baked into the choice; no RTL/LTR toggle.
            SettingRow {
                width: parent.width
                label: "Mode"
                Chip { objectName: "settingsModeManga"; label: "Manga"; active: root.readingMode === "manga"; onTapped: root.setReadingMode("manga") }
                Chip { objectName: "settingsModeComic"; label: "Comic"; active: root.readingMode === "comic"; onTapped: root.setReadingMode("comic") }
                Chip { objectName: "settingsModeStrip"; label: "Strip"; active: root.readingMode === "strip"; onTapped: root.setReadingMode("strip") }
            }

            SettingRow {
                width: parent.width
                label: "Night veil"
                Chip { objectName: "settingsVeilOff";  label: "Off";  active: root.nightVeil === "off";  onTapped: root.setNightVeil("off") }
                Chip { objectName: "settingsVeilLow";  label: "Low";  active: root.nightVeil === "low";  onTapped: root.setNightVeil("low") }
                Chip { objectName: "settingsVeilHigh"; label: "High"; active: root.nightVeil === "high"; onTapped: root.setNightVeil("high") }
            }

            // ============ DOUBLE PAGE (mode-aware: yields entirely in Strip) ============
            Column {
                objectName: "settingsDoubleSection"
                width: parent.width
                visible: root.readingMode !== "strip"   // shown for Manga + Comic (double-page)
                spacing: 0

                SectionLabel { text: "Double page"; height: 32; verticalAlignment: Text.AlignVCenter }

                // Which pages ride together. Auto = the coupling probe's verdict; Nudge = flip it
                // by hand (and every further tap flips again). The gold chip names who's deciding.
                SettingRow {
                    width: parent.width
                    label: "Coupling"
                    Chip { objectName: "settingsCouplingAuto";  label: "Auto";  active: root.couplingMode === "auto";   onTapped: root.resetCoupling() }
                    Chip { objectName: "settingsCouplingNudge"; label: "Nudge"; active: root.couplingMode !== "auto";   onTapped: root.nudgeCoupling() }
                }

                SettingRow {
                    width: parent.width
                    label: "Gutter shadow"
                    Chip { objectName: "settingsGutterOff";    label: "Off";    active: root.gutterStrength === 0;    onTapped: root.setGutter(0) }
                    Chip { objectName: "settingsGutterSubtle"; label: "Subtle"; active: root.gutterStrength === 0.22; onTapped: root.setGutter(0.22) }
                    Chip { objectName: "settingsGutterMedium"; label: "Medium"; active: root.gutterStrength === 0.35; onTapped: root.setGutter(0.35) }
                    Chip { objectName: "settingsGutterStrong"; label: "Strong"; active: root.gutterStrength === 0.55; onTapped: root.setGutter(0.55) }
                }

                // Zoom is a live READOUT this pass (you zoom with the wheel/keys on the page);
                // the gold value tracks reader.zoomPercent. In-sheet +/- is a later call.
                SettingRow {
                    width: parent.width
                    label: "Zoom"
                    Text {
                        objectName: "settingsZoomValue"
                        text: root.zoomPercent + "%"
                        color: theme.gold
                        font.family: theme.hud
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }

            // ============ LONG STRIP (the mirror: shows ONLY in Strip) ============
            // The mock's words: "in Strip, the double-page section yields to portrait width and
            // gap." Both ride ONE backend setter, so changing either preserves the other.
            Column {
                objectName: "settingsStripSection"
                width: parent.width
                visible: root.readingMode === "strip"
                spacing: 0

                SectionLabel { text: "Long strip"; height: 32; verticalAlignment: Text.AlignVCenter }

                // How wide a portrait page sits in the column. A confirmed spread always spans the
                // full width regardless — this sets the portrait measure only.
                SettingRow {
                    width: parent.width
                    label: "Page width"
                    Chip { objectName: "settingsStripWidthNarrow";  label: "Narrow";  active: root.stripWidthPct === 62;  onTapped: root.setStripWidth(62) }
                    Chip { objectName: "settingsStripWidthComfort"; label: "Comfort"; active: root.stripWidthPct === 78;  onTapped: root.setStripWidth(78) }
                    Chip { objectName: "settingsStripWidthWide";    label: "Wide";    active: root.stripWidthPct === 90;  onTapped: root.setStripWidth(90) }
                    Chip { objectName: "settingsStripWidthFull";    label: "Full";    active: root.stripWidthPct === 100; onTapped: root.setStripWidth(100) }
                }

                // Breathing room between pages. None (0) is the lineage default — TB2's strip ran
                // its pages flush, which is what a scanned volume expects.
                SettingRow {
                    width: parent.width
                    label: "Gap"
                    Chip { objectName: "settingsStripGapNone"; label: "None"; active: root.stripGap === 0;  onTapped: root.setStripGap(0) }
                    Chip { objectName: "settingsStripGapThin"; label: "Thin"; active: root.stripGap === 8;  onTapped: root.setStripGap(8) }
                    Chip { objectName: "settingsStripGapWide"; label: "Wide"; active: root.stripGap === 20; onTapped: root.setStripGap(20) }
                }
            }

            // ============ TOOLS (every mode — these are not display choices) ============
            SectionLabel { text: "Tools"; height: 32; verticalAlignment: Text.AlignVCenter }

            // 2x2 launcher. Each tile asks the SHELL to raise the tool; the overlays themselves
            // land in the following slices, exactly as the HUD's own pills already work.
            Grid {
                width: parent.width
                columns: 2
                columnSpacing: 7
                rowSpacing: 7
                readonly property real cellW: (width - columnSpacing) / 2

                ToolTile {
                    objectName: "settingsToolLoupe"; iconObjectName: "settingsToolLoupeIcon"
                    width: parent.cellW; kind: "loupe"; label: "Loupe"
                    onTapped: if (root.reader) root.reader.loupeRequested()
                }
                ToolTile {
                    objectName: "settingsToolBookmarks"; iconObjectName: "settingsToolBookmarksIcon"
                    width: parent.cellW; kind: "bookmarks"; label: "Bookmarks"
                    onTapped: if (root.reader) root.reader.bookmarksRequested()
                }
                ToolTile {
                    objectName: "settingsToolThumbnails"; iconObjectName: "settingsToolThumbnailsIcon"
                    width: parent.cellW; kind: "thumbnails"; label: "Thumbnails"
                    onTapped: if (root.reader) root.reader.thumbnailsRequested()
                }
                ToolTile {
                    objectName: "settingsToolShortcuts"; iconObjectName: "settingsToolShortcutsIcon"
                    width: parent.cellW; kind: "shortcuts"; label: "Shortcuts"
                    onTapped: if (root.reader) root.reader.shortcutsRequested()
                }
            }

            // Memory saver halves the page cache (512 -> 256 MiB). A switch, not chips: it is a
            // state, not a choice among peers.
            Item {
                width: parent.width
                height: 37
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Memory saver"
                    color: theme.inkDim
                    font.family: theme.hud
                    font.pixelSize: 12
                }
                Switch {
                    objectName: "settingsMemorySaver"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: root.memorySaver
                    onTapped: root.setMemorySaver(!root.memorySaver)
                }
            }

            // The two destructive actions, side by side at the very bottom (mock .dangerrow).
            Row {
                width: parent.width
                height: 40
                spacing: 7
                readonly property real cellW: (width - spacing) / 2

                DangerAction {
                    objectName: "settingsDangerClearResume"
                    width: parent.cellW
                    anchors.verticalCenter: parent.verticalCenter
                    label: "Clear resume"
                    armed: root.armedDanger === "clearResume"
                    onTapped: root.armDanger("clearResume", function () { if (root.reader) root.reader.clearResume() })
                }
                DangerAction {
                    objectName: "settingsDangerResetSeries"
                    width: parent.cellW
                    anchors.verticalCenter: parent.verticalCenter
                    label: "Reset series"
                    armed: root.armedDanger === "resetSeries"
                    onTapped: root.armDanger("resetSeries", function () { if (root.reader) root.reader.resetSeries() })
                }
            }
        }
    }
}
