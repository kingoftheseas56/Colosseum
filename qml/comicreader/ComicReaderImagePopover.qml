// ComicReaderImagePopover — the compact Image panel (Task 7, plan 2026-07-28).
//
// Hemanth took Cover's real reader for a ride while this was being designed. Its
// "Image settings" opened a small drop directly under the command's own label —
// not a side drawer, not a sheet — holding three things and nothing else. His
// ruling: Cover is "aggressively selective; it does not expose every technically
// possible adjustment", and he wanted YACReader's DEPTH without losing Cover's
// calm. The approved shape, in his words:
//
//   "Image opens a compact anchored panel and does not move the comic."
//   "Contrast, gamma, rotation, and auto-crop behind one Advanced image tools
//    row. Panel floats over the comic without shifting it."
//
// So: THREE controls you see instantly — Quality, Brightness, Night filter — and
// everything else one disclosure deeper, in the SAME anchored panel. Not a second
// surface, not a sheet, and never a sidebar (standing law: no reader sidebar).
//
// PRESENTATION + INTENTS ONLY, like the rest of the reader chrome. This component
// owns no reading state and never touches the backend: the live profile is pushed
// in as `profile` and every control raises ONE signal,
// profileChangeRequested(map), carrying a COMPLETE profile map. That completeness
// is deliberate — ComicReaderCore::setRenderProfile REPLACES rather than merges,
// so a partial map would silently reset the fields it omitted. Building the map
// here, from the live one, in a single function, is what makes that impossible to
// get wrong at a call site.
//
// `open` is a RULE-level property, deliberately not `visible`: QQuickItem.visible
// is EFFECTIVE visibility, so a test asserting on a child's `visible` reads its
// ancestors' state too. `open` and `advancedOpen` say what this surface believes,
// whatever its parents are doing.
//
// EVERY control is harness-callable as a plain function (the ComicReaderInput
// house pattern), and the pointer paths call those same functions — so the tested
// logic is the shipped logic rather than a parallel description of it.

import QtQuick
import "../"   // Theme (lives in qml/, the parent of qml/comicreader/)

Item {
    id: root
    objectName: "readerImagePopover"

    // Fill the shell explicitly. anchors.fill resolves to 0x0 for this overlay
    // mount (the same QML quirk ComicReaderSettingsSheet and the Pages filmstrip
    // both document); an explicit size binding tracks the parent reliably and
    // follows a resize / fullscreen flip.
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    // ================= injected facts (never written back) =================
    // The LIVE, already-normalised profile — ComicReaderCore::renderProfile()'s
    // map, handed down by the shell. Read through the guarded accessors below so
    // a partial or absent map degrades to the defaults instead of NaN.
    property var profile: ({})

    // ---- open state: the RULE, not the pixels (see the header note) ----
    property bool open: false
    visible: open

    // The disclosure. Closed on every fresh open: the panel's promise is that its
    // primary surface is three controls, and a panel that remembered itself
    // expanded would quietly break that promise for the rest of the session.
    property bool advancedOpen: false
    onOpenChanged: if (!open) advancedOpen = false

    // ================= intents =================
    signal profileChangeRequested(var profile)   // a COMPLETE profile map
    signal dismissRequested()                    // close me; nothing else changes

    // ================= the chrome this surface has to live between =================
    // Mirrors ComicReaderHud's own numbers: the title strip (38) + the command bar
    // (46) at the top, the gold rail (54) at the bottom. Properties rather than
    // literals so a shell that ever changes the chrome says so in one place.
    property int chromeTopInset: 84
    property int railHeight: 54
    // WHERE THE PANEL HANGS. `anchorX` is the CENTRE of the command that raised
    // it, in the shell's coordinates, published by ComicReaderCommandBar through
    // ComicReaderHud — Task 8's anchor seam, built once for this panel and the
    // Layout menu together.
    //
    // This is the seam this file asked for. Task 7 shipped hanging off the command
    // bar's right edge and said why: the row is right-aligned and two of its six
    // commands are live READOUTS whose labels change width with the reader's
    // layout and order, so a per-command anchor cannot be a constant and needed a
    // way out of the chrome. Hemanth's reference — Cover's reader — drops the
    // panel under its own label, which is what this now does.
    //
    // -1 means "no seam" (a bare harness mount, or a chrome that never published
    // an anchor). Then, and only then, the old right-edge placement stands, so the
    // panel degrades to exactly what it did before rather than to x=0.
    property real anchorX: -1
    property int panelRightMargin: 26
    property int panelEdgeMargin: 12       // never let the panel touch the window edge

    Theme { id: theme }

    readonly property color cGlassDeep:  Qt.rgba(9 / 255, 10 / 255, 13 / 255, 0.96)
    readonly property color cEdge:       Qt.rgba(1, 1, 1, 0.18)
    readonly property color cEdgeSoft:   Qt.rgba(1, 1, 1, 0.09)
    readonly property color cRowLine:    Qt.rgba(1, 1, 1, 0.06)
    readonly property color cGoldChipBg: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.08)
    readonly property color cGoldBorder: Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.55)
    readonly property color cTrack:      Qt.rgba(1, 1, 1, 0.10)

    // ================= the live profile, read defensively =================
    // A missing key is the DEFAULT, never 0 — gamma 0 would be a black page, and
    // an undefined slider value is a NaN handle halfway off the track.
    function _int(value, fallback) {
        var n = Number(value)
        return (value === undefined || value === null || isNaN(n)) ? fallback : Math.round(n)
    }
    readonly property int    brightness:  _int(profile ? profile.brightness : undefined, 0)
    readonly property int    contrast:    _int(profile ? profile.contrast : undefined, 0)
    readonly property int    gamma:       _int(profile ? profile.gamma : undefined, 100)
    // NOT `rotation` — QQuickItem already owns that name, and shadowing it makes the panel
    // itself turn. (Qt says so out loud: "Member rotation ... overrides a member of the base
    // object".) The PROFILE KEY is still "rotation"; only this readback is renamed.
    readonly property int    rotationDegrees: _int(profile ? profile.rotation : undefined, 0)
    readonly property bool   autoCrop:    !!(profile && profile.autoCrop)
    readonly property bool   nightFilter: !!(profile && profile.nightFilter)
    readonly property string quality:
        (profile && (profile.quality === "fast" || profile.quality === "best"))
            ? profile.quality : "balanced"

    // The approved vocabulary, fixed: three qualities and four quarter turns. Not
    // configurable — which controls exist is a design decision, not a runtime one.
    readonly property var qualities: ["fast", "balanced", "best"]
    readonly property var rotations: [0, 90, 180, 270]
    function qualityLabel(q) {
        return q === "fast" ? "Fast" : q === "best" ? "Best" : "Balanced"
    }

    // ================= THE ONE DOOR =================
    // Every control routes through here, and it always emits a COMPLETE map built
    // from the live one. setRenderProfile replaces rather than merges, so this is
    // the difference between "set brightness" and "set brightness and silently
    // un-rotate the book".
    function _apply(field, value) {
        var next = {
            "brightness":  root.brightness,
            "contrast":    root.contrast,
            "gamma":       root.gamma,
            "rotation":    root.rotationDegrees,
            "autoCrop":    root.autoCrop,
            "nightFilter": root.nightFilter,
            "quality":     root.quality
        }
        next[field] = value
        profileChangeRequested(next)
    }

    // ---- the named verbs. The pointer paths call THESE; so does the gate. ----
    function setQuality(q) {
        if (root.qualities.indexOf(String(q)) < 0) return   // inert, never a fallthrough
        _apply("quality", String(q))
    }
    function setBrightness(v) { _apply("brightness", Math.round(Math.max(-100, Math.min(100, v)))) }
    function setContrast(v)   { _apply("contrast",   Math.round(Math.max(-100, Math.min(100, v)))) }
    function setGamma(v)      { _apply("gamma",      Math.round(Math.max(10,  Math.min(300, v)))) }
    function setNightFilter(on) { _apply("nightFilter", on === true) }
    function setAutoCrop(on)    { _apply("autoCrop",    on === true) }
    function setRotation(deg) {
        var d = ((Math.round(Number(deg) / 90) % 4) + 4) % 4 * 90
        _apply("rotation", d)
    }
    function toggleAdvanced() { advancedOpen = !advancedOpen }
    // Close, and ONLY close. Nothing here can change the picture.
    function dismiss() { dismissRequested() }

    // ================= the canvas catcher =================
    // "Panel floats over the comic without shifting it" — and a click on the comic
    // puts it away. It covers the comic only: the chrome bands keep working, so
    // Back, the commands and the rail are all still reachable with the panel up.
    // It emits dismissRequested and nothing else.
    MouseArea {
        objectName: "imageDismissCatcher"
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

    // a pill switch — gold only when ON, same law as the chips (ported from the
    // settings sheet's `.sw`, so the two read as one family while both exist).
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
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sw.tapped() }
    }

    // A continuous control — the one thing the chip vocabulary cannot express.
    //
    // `value` is a PLAIN property the owner assigns, never a binding, and the
    // reason is the scar this codebase already carries twice (the HUD's thumb
    // `y:`, the cursor shape the shell refuses to assign): a bound value that the
    // drag then writes to would destroy its own binding on the first press and
    // never track the profile again. Instead `externalValue` is the bound one and
    // `value` follows it only while the handle is not held — so the profile drives
    // the handle, except during the drag that is driving the profile.
    component ReaderSlider: Item {
        id: slider
        property string label: ""
        property real from: -100
        property real to: 100
        property real externalValue: 0
        property real value: 0
        property string readout: String(Math.round(slider.value))
        property bool held: false
        signal moved(real value)

        implicitHeight: 44
        width: parent ? parent.width : 0

        onExternalValueChanged: if (!held) value = externalValue
        Component.onCompleted: value = externalValue

        // PURE, and harness-callable: which value a press at `x` means, and where
        // the handle for `value` sits. Testing the mapping needs no synthesized
        // mouse events, and the pointer path below cannot drift from it.
        function valueAt(x) {
            if (track.width <= 0) return slider.from
            var t = Math.max(0, Math.min(1, x / track.width))
            return slider.from + t * (slider.to - slider.from)
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
        objectName: "imagePanel"
        width: 322
        // Content-sized: the panel is exactly as tall as what it holds, which is
        // what makes the Advanced disclosure GROW the same panel rather than
        // opening a second surface.
        height: column.implicitHeight + 28
        // Centred under the command that raised it, clamped inside the reader so a command near an
        // edge can never push the panel off screen. See `anchorX` for the -1 fallback.
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
        // Fades IN only, and `visible` stays a plain rule: an offscreen harness
        // never ticks animations, so gating visibility on the animation would make
        // the panel permanently invisible under test (the Pages filmstrip carries
        // the same note for the same reason).
        Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

        // click-swallower (floating-panel house law): the panel's own ground must
        // not fall through to the catcher below and dismiss the thing you are
        // adjusting. Declared FIRST so every control sits above it.
        MouseArea {
            id: panelSwallow
            objectName: "imagePanelSwallow"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            function tap() { /* swallowed on purpose */ }
            onClicked: tap()
        }

        Column {
            id: column
            objectName: "imagePanelColumn"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 14
            spacing: 0

            // ---- PRIMARY: Quality ----
            Item {
                width: parent.width
                height: 30
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Quality"
                    color: theme.inkDim
                    font.family: theme.hud
                    font.pixelSize: 12
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5
                    Repeater {
                        model: root.qualities
                        delegate: Chip {
                            required property string modelData
                            objectName: "imageQuality_" + modelData
                            label: root.qualityLabel(modelData)
                            active: root.quality === modelData
                            onTapped: root.setQuality(modelData)
                        }
                    }
                }
            }

            // ---- PRIMARY: Brightness ----
            ReaderSlider {
                id: brightnessSlider
                objectName: "imageBrightness"
                label: "Brightness"
                from: -100; to: 100
                externalValue: root.brightness
                onMoved: function (v) { root.setBrightness(v) }
            }

            // ---- PRIMARY: Night filter ----
            Item {
                width: parent.width
                height: 30
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Night filter"
                    color: theme.inkDim
                    font.family: theme.hud
                    font.pixelSize: 12
                }
                Switch {
                    objectName: "imageNightFilter"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: root.nightFilter
                    onTapped: root.setNightFilter(!root.nightFilter)
                }
            }

            Item { width: 1; height: 6 }
            Rectangle { width: parent.width; height: 1; color: root.cRowLine }

            // ---- the ONE disclosure. Flat: a label and a mark, no pill, no box. ----
            Item {
                objectName: "imageAdvancedDisclosure"
                width: parent.width
                height: 34
                function tap() { root.toggleAdvanced() }
                Text {
                    id: discloseLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Advanced image tools"
                    color: discloseMa.containsMouse || root.advancedOpen ? theme.ink : theme.inkDim
                    font.family: theme.hud
                    font.pixelSize: 12
                }
                // A REAL glyph, never a text arrow (semantic-icon-audit law). The
                // chevron the reader already owns, turned a quarter to point down
                // when the section is open — there is no separate down-chevron in
                // the vocabulary and inventing one for a rotation is a new asset
                // for no new meaning.
                ComicReaderIcon {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 15; height: 15
                    kind: "next"
                    accessibleName: "Advanced image tools"
                    ink: discloseLabel.color
                    rotation: root.advancedOpen ? 90 : 0
                }
                MouseArea {
                    id: discloseMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: parent.tap()
                }
            }

            // ---- ADVANCED, inside the SAME panel ----
            Column {
                id: advanced
                objectName: "imageAdvancedSection"
                width: parent.width
                spacing: 0
                visible: root.advancedOpen
                height: root.advancedOpen ? implicitHeight : 0
                clip: true

                ReaderSlider {
                    objectName: "imageContrast"
                    label: "Contrast"
                    from: -100; to: 100
                    externalValue: root.contrast
                    onMoved: function (v) { root.setContrast(v) }
                }

                ReaderSlider {
                    objectName: "imageGamma"
                    label: "Gamma"
                    from: 10; to: 300
                    externalValue: root.gamma
                    // Hundredths on the wire, a familiar 1.00 on the face.
                    readout: (Math.round(value) / 100).toFixed(2)
                    onMoved: function (v) { root.setGamma(v) }
                }

                Item {
                    width: parent.width
                    height: 32
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Rotate"
                        color: theme.inkDim
                        font.family: theme.hud
                        font.pixelSize: 12
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5
                        Repeater {
                            model: root.rotations
                            delegate: Chip {
                                required property int modelData
                                objectName: "imageRotate_" + modelData
                                label: modelData + "°"
                                active: root.rotationDegrees === modelData
                                onTapped: root.setRotation(modelData)
                            }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: 32
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Auto-crop"
                        color: theme.inkDim
                        font.family: theme.hud
                        font.pixelSize: 12
                    }
                    Switch {
                        objectName: "imageAutoCrop"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        checked: root.autoCrop
                        onTapped: root.setAutoCrop(!root.autoCrop)
                    }
                }
            }
        }
    }
}
