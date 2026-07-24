// Comic Reader — OVERLAYS oracle (Task 12). Instantiates the overlay surfaces offscreen with an
// INJECTED FAKE reader seam (the ComicReaderShell-facing API the overlays read/write) and asserts
// their behaviour. The pixel look is Hemanth's eyes-on; this pins BEHAVIOUR.
//
// SLICE 1 — ComicReaderSettingsSheet, DISPLAY section (mode + direction on existing persisted seams):
//   * closed by default; open() shows it and reports `opened`; a fake reader's modalOpen tracks it.
//   * DISPLAY: Mode chips reflect reader.mode (the active chip is `on`); tapping the inactive chip
//     writes reader.persistedMode (NOT reader.mode — a crossing's load() owns mode). Direction chips
//     reflect reader.rtl and write reader.persistedDirection. gold marks ONLY the active choice.
//   * dismiss: the X, a scrim tap, and close() all fire dismissed() and clear `opened`.
//   * click-swallower: a tap on the sheet BODY does not fall through to the scrim (no dismiss).
//
// HOUSE HARNESS PATTERN (mirrors comicreader_surfaces_harness.qml): a thrown error hangs qml.exe
// offscreen, so `ck` never throws — it collects failures; prints exactly one COMICREADER_OVERLAYS_OK
// when clean, else one COMICREADER_OVERLAYS_FAIL:<msg> per failure and Qt.exit(1).

import QtQuick

Item {
    id: harness
    width: 1000; height: 700
    visible: true

    property var failures: []
    function ck(cond, msg) { if (!cond) failures.push(msg) }

    // ---- fake shell/reader seam: the ComicReaderShell-facing API the settings sheet reads/writes ----
    component FakeReader: QtObject {
        // the single user-facing identity (Hemanth 2026-07-25): manga | comic | strip.
        property string readingMode: "manga"
        property string persistedMode: ""         // setReadingMode writes the internal layout seam
        property string persistedDirection: ""    // setReadingMode writes the internal direction seam
        property string nightVeil: "off"          // "off" | "low" | "high" — the sheet writes HERE (live setting)
        property real   gutterStrength: 0.35      // double-page gutter shadow (0/.22/.35/.55) — the sheet writes HERE
        property int    zoomPercent: 100          // double-page zoom (readout only this pass)
        property bool   modalOpen: false
        // mirror the real shell: translate the identity to the internal layout+direction seams
        function setReadingMode(rm) {
            readingMode = rm
            persistedMode = (rm === "strip") ? "long_strip" : "double_page"
            persistedDirection = (rm === "manga") ? "rtl" : "ltr"
        }
    }

    FakeReader { id: fakeReader }

    property var sheetComp: null
    property var sheet: null

    // find a descendant by a marker objectName (the sheet tags its interactive bits)
    function byName(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var f = byName(kids[i], name)
            if (f) return f
        }
        return null
    }
    function clickCenter(item) {
        if (!item) return false
        // synthesize a click by invoking the item's tapped() if present, else via a MouseArea child.
        if (typeof item.tapped === "function") { item.tapped(); return true }
        return false
    }

    function report() {
        if (failures.length === 0) { console.log("COMICREADER_OVERLAYS_OK"); Qt.exit(0) }
        else {
            for (var i = 0; i < failures.length; i++)
                console.log("COMICREADER_OVERLAYS_FAIL: " + failures[i])
            Qt.exit(1)
        }
    }

    function runSettings() {
        sheet = sheetComp.createObject(harness, { "anchors.fill": harness, "reader": fakeReader })
        if (!sheet) { failures.push("settings: createObject returned null"); return }

        // --- closed by default ---
        ck(sheet.opened === false, "settings: must start CLOSED")
        ck(fakeReader.modalOpen === false, "settings: modalOpen must be false while closed")

        // --- open ---
        sheet.open()
        ck(sheet.opened === true, "settings: open() must set opened=true")

        // --- DISPLAY: ONE Mode row — Manga / Comic / Strip (direction baked in). Chips reflect
        //     reader.readingMode; a tap calls reader.setReadingMode(rm) which writes the internal
        //     layout + direction seams (no separate RTL/LTR toggle). ---
        var modeManga = byName(sheet, "settingsModeManga")
        var modeComic = byName(sheet, "settingsModeComic")
        var modeStrip = byName(sheet, "settingsModeStrip")
        ck(modeManga !== null && modeComic !== null && modeStrip !== null, "settings: Mode chips (Manga/Comic/Strip) must exist")
        ck(modeManga && modeManga.active === true, "settings: Manga chip ACTIVE when readingMode=manga")
        ck(modeComic && modeComic.active === false && modeStrip && modeStrip.active === false, "settings: only Manga active at default")
        clickCenter(modeComic)
        ck(fakeReader.readingMode === "comic", "settings: tapping Comic must set readingMode=comic, got '" + fakeReader.readingMode + "'")
        ck(fakeReader.persistedMode === "double_page" && fakeReader.persistedDirection === "ltr",
           "settings: Comic must write double_page + LTR seams, got '" + fakeReader.persistedMode + "'/'" + fakeReader.persistedDirection + "'")
        clickCenter(modeStrip)
        ck(fakeReader.readingMode === "strip", "settings: tapping Strip must set readingMode=strip")
        ck(fakeReader.persistedMode === "long_strip", "settings: Strip must write long_strip layout, got '" + fakeReader.persistedMode + "'")
        // reflect a mode change coming back from the shell
        fakeReader.readingMode = "manga"
        ck(modeManga.active === true && modeComic.active === false && modeStrip.active === false,
           "settings: Mode chips must re-reflect when readingMode changes back to manga")

        // --- DISPLAY: Night veil chips reflect reader.nightVeil, tap writes it live (Off default) ---
        var veilOff  = byName(sheet, "settingsVeilOff")
        var veilLow  = byName(sheet, "settingsVeilLow")
        var veilHigh = byName(sheet, "settingsVeilHigh")
        ck(veilOff !== null && veilLow !== null && veilHigh !== null, "settings: Night veil chips (Off/Low/High) must exist")
        ck(veilOff && veilOff.active === true, "settings: Off chip must be ACTIVE when reader.nightVeil=off (default)")
        ck(veilLow && veilLow.active === false && veilHigh && veilHigh.active === false, "settings: only Off active by default")
        clickCenter(veilLow)
        ck(fakeReader.nightVeil === "low", "settings: tapping Low must write reader.nightVeil=low, got '" + fakeReader.nightVeil + "'")
        // reflect a level change coming back from the shell
        fakeReader.nightVeil = "high"
        ck(veilHigh.active === true && veilOff.active === false && veilLow.active === false,
           "settings: Night veil chips must re-reflect when reader.nightVeil changes (High active)")
        fakeReader.nightVeil = "off"   // restore for later assertions

        // --- DOUBLE PAGE section: mode-aware, gutter presets (0/.22/.35/.55), zoom readout ---
        fakeReader.readingMode = "manga"   // the double section shows in Manga/Comic (double-page), not Strip
        var dpSection = byName(sheet, "settingsDoubleSection")
        ck(dpSection !== null, "settings: DOUBLE PAGE section must exist")
        ck(dpSection && dpSection.visible === true, "settings: DOUBLE PAGE section must be VISIBLE in double_page mode")
        // gutter shadow chips reflect reader.gutterStrength (Medium=0.35 default) and write the presets
        var gOff = byName(sheet, "settingsGutterOff"),    gSub = byName(sheet, "settingsGutterSubtle")
        var gMed = byName(sheet, "settingsGutterMedium"), gStr = byName(sheet, "settingsGutterStrong")
        ck(gOff && gSub && gMed && gStr, "settings: Gutter chips (Off/Subtle/Medium/Strong) must exist")
        ck(gMed && gMed.active === true, "settings: Medium gutter chip must be ACTIVE at the 0.35 default")
        clickCenter(gStr)
        ck(Math.abs(fakeReader.gutterStrength - 0.55) < 1e-9, "settings: tapping Strong must write gutterStrength=0.55, got " + fakeReader.gutterStrength)
        clickCenter(gOff)
        ck(fakeReader.gutterStrength === 0, "settings: tapping Off must write gutterStrength=0, got " + fakeReader.gutterStrength)
        fakeReader.gutterStrength = 0.22   // reflect a change coming back
        ck(gSub.active === true && gOff.active === false, "settings: gutter chips must re-reflect when gutterStrength changes (Subtle active)")
        // zoom readout reflects reader.zoomPercent (display only this pass)
        var zoomVal = byName(sheet, "settingsZoomValue")
        ck(zoomVal !== null, "settings: Zoom readout must exist")
        ck(zoomVal && String(zoomVal.text).indexOf("100") >= 0, "settings: Zoom readout must show 100(%), got '" + (zoomVal ? zoomVal.text : "") + "'")
        fakeReader.zoomPercent = 180
        ck(zoomVal && String(zoomVal.text).indexOf("180") >= 0, "settings: Zoom readout must reflect a zoomPercent change to 180, got '" + (zoomVal ? zoomVal.text : "") + "'")
        // mode-aware: in Strip the whole double section yields (hidden)
        fakeReader.readingMode = "strip"
        ck(dpSection.visible === false, "settings: DOUBLE PAGE section must be HIDDEN in Strip mode")
        fakeReader.readingMode = "manga"   // restore

        // --- dismiss: X ---
        harness.dismissCount = 0
        sheet.dismissed.connect(function () { harness.dismissCount += 1 })
        var xBtn = byName(sheet, "settingsCloseX")
        ck(xBtn !== null, "settings: close X must exist")
        clickCenter(xBtn)
        ck(sheet.opened === false, "settings: tapping X must close the sheet")
        ck(harness.dismissCount >= 1, "settings: tapping X must emit dismissed()")

        // --- dismiss: scrim tap (reopen first) ---
        sheet.open()
        ck(sheet.opened === true, "settings: reopen for scrim test")
        var scrim = byName(sheet, "settingsScrim")
        ck(scrim !== null, "settings: scrim must exist")
        clickCenter(scrim)
        ck(sheet.opened === false, "settings: scrim tap must close the sheet")

        // --- click-swallower: a body tap must NOT close ---
        sheet.open()
        var body = byName(sheet, "settingsBody")
        ck(body !== null, "settings: sheet body must exist")
        clickCenter(body)   // body swallows: no dismiss
        ck(sheet.opened === true, "settings: a tap on the sheet BODY must NOT dismiss (click-swallower law)")

        // --- close() API ---
        sheet.close()
        ck(sheet.opened === false, "settings: close() must close the sheet")
    }
    property int dismissCount: 0

    function runChecks() {
        try { runSettings() }
        catch (e) { failures.push("exception: " + e.message) }
        report()
    }

    Component.onCompleted: {
        try {
            sheetComp = Qt.createComponent("../qml/comicreader/ComicReaderSettingsSheet.qml")
            if (sheetComp.status === Component.Error) throw new Error("settings component: " + sheetComp.errorString())
            Qt.callLater(runChecks)
        } catch (e) {
            console.log("COMICREADER_OVERLAYS_FAIL: setup: " + e.message); Qt.exit(1)
        }
    }

    Timer { interval: 8000; running: true; onTriggered: { console.log("COMICREADER_OVERLAYS_FAIL: timeout"); Qt.exit(1) } }
}
