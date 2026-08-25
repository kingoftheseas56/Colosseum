import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

// Vault ux uplift S8 — the detail sheet tells the truth it already knows. Drives the PRODUCTION
// VaultDetailSheet with seeded browseDetail()-shaped maps (the same seeded-stub contract as
// tst_vault_browse_page.qml's own Slice-7 block): the runtime line (present only when the
// engine knows the duration), a rejected copy's human reason under the COPIES row, the
// "Identify again" signal (the handler is VaultPage.qml's — a different owner; this proves the
// sheet emits it with the group key), and the typographic-title poster fallback that replaced
// the literal placeholder word "artwork".
TestCase {
    name: "VaultDetailSheet"
    when: windowShown

    Window { id: testWindow; width: 900; height: 760; visible: true }
    Component { id: sheetComp; Colosseum.VaultDetailSheet {} }
    property var sheet: null
    SignalSpy { id: identifyAgainSpy; signalName: "identifyAgainRequested" }

    function makeSheet(detail, rowState) {
        if (sheet) sheet.destroy()
        sheet = sheetComp.createObject(testWindow, {
            detail: detail,
            identityStateOfRow: rowState || ""
        })
        verify(sheet !== null)
        identifyAgainSpy.target = sheet
        identifyAgainSpy.clear()
        wait(40)
        return sheet
    }

    function cleanup() {
        identifyAgainSpy.target = null
        if (sheet) sheet.destroy()
        sheet = null
    }

    function baseDetail(overrides) {
        var d = {
            found: true, key: "/root/Film (2021)", displayTitle: "Spider-Man: No Way Home",
            year: 2021, identityState: "identified", identityLabel: "identity certain",
            copiesHeld: 1, bestQualityLine: "1080p WEBRip",
            copies: [ { path: "/root/Film (2021)/film.mkv", rootPath: "/root",
                        quality: "1080p WEBRip", sizeBytes: 2254857830, sizeText: "2.1 GB",
                        where: "hemanth's folder / Film (2021)", away: false,
                        admissionVerdict: "", statusDetail: "" } ],
            companions: [], extras: [], evidence: "", ignoredCount: 0,
            coverRef: "", playPath: "/root/Film (2021)/film.mkv"
        }
        for (var k in overrides) d[k] = overrides[k]
        return d
    }

    // ── 1. runtime: rendered when the engine knows it, entirely absent when it does not ─────
    function test_runtime_line_present_only_when_known() {
        makeSheet(baseDetail({ runtimeText: "1h 47m" }))
        var runtime = findChild(sheet, "vaultBrowseSheetRuntime")
        verify(runtime !== null)
        compare(runtime.visible, true)
        compare(runtime.text, "1h 47m")

        // NEGATIVE CONTROL (same map minus the runtime fact): the line must not render at all —
        // no "-1", no "0m", no stub. The engine omits the key while unknown (S8 rule).
        makeSheet(baseDetail({}))
        runtime = findChild(sheet, "vaultBrowseSheetRuntime")
        verify(runtime !== null)
        compare(runtime.visible, false)
        verify(findText(sheet, "1h 47m") === null)
        verify(findText(sheet, "0m") === null)
    }

    // ── 2. honest failure: a rejected copy states its reason; a healthy copy states nothing ─
    function test_rejected_copy_states_its_reason_healthy_copy_stays_quiet() {
        makeSheet(baseDetail({
            copies: [
                { path: "/root/Film (2021)/film.mkv", rootPath: "/root", quality: "1080p WEBRip",
                  sizeBytes: 2254857830, sizeText: "2.1 GB",
                  where: "hemanth's folder / Film (2021)", away: false,
                  admissionVerdict: "Admitted", statusDetail: "" },
                { path: "/e/Film (2021)/film.mkv", rootPath: "/e", quality: "1080p WEBRip",
                  sizeBytes: 2254857830, sizeText: "2.1 GB",
                  where: "archive / Film (2021)", away: false,
                  admissionVerdict: "RejectedNoVideo", statusDetail: "no video track" }
            ]
        }))
        var rejectedStatus = findChild(sheet, "vaultBrowseSheetCopyStatus_1")
        verify(rejectedStatus !== null)
        compare(rejectedStatus.visible, true)
        compare(rejectedStatus.text, "no video track")
        var healthyStatus = findChild(sheet, "vaultBrowseSheetCopyStatus_0")
        verify(healthyStatus !== null)
        compare(healthyStatus.visible, false)   // quiet for a healthy copy — no verdict noise
    }

    // ── 3. "Identify again": emits the group key for an uncertain row; absent once certain ──
    function test_identify_again_emits_group_key_and_hides_once_identified() {
        makeSheet(baseDetail(), "uncertain")
        var again = findChild(sheet, "vaultBrowseSheetIdentifyAgain")
        verify(again !== null)
        compare(again.visible, true)
        mouseClick(again)
        wait(20)
        compare(identifyAgainSpy.count, 1)
        compare(identifyAgainSpy.signalArguments[0][0], "/root/Film (2021)")

        // NEGATIVE CONTROL: an identified row offers no retry — the conservative gate has
        // nothing to re-run and the sheet must not promise one.
        makeSheet(baseDetail(), "identified")
        again = findChild(sheet, "vaultBrowseSheetIdentifyAgain")
        verify(again !== null)
        compare(again.visible, false)
        compare(identifyAgainSpy.count, 0)
    }

    // ── 4. empty poster slot: the title itself, never the placeholder word "artwork" ────────
    function test_empty_poster_slot_shows_title_never_placeholder_word() {
        makeSheet(baseDetail({ coverRef: "" }))
        var posterTitle = findChild(sheet, "vaultBrowseSheetPosterTitle")
        verify(posterTitle !== null)
        compare(posterTitle.visible, true)
        verify(("" + posterTitle.text).indexOf("Spider-Man") === 0)
        verify(findText(sheet, "artwork") === null)   // the retired placeholder must be gone

        // With art (S5's file:// poster refs land in coverRef), the title floor steps aside.
        makeSheet(baseDetail({ coverRef: "file:///D:/root/Film (2021)/poster.jpg" }))
        posterTitle = findChild(sheet, "vaultBrowseSheetPosterTitle")
        verify(posterTitle !== null)
        compare(posterTitle.visible, false)
    }

    function findText(root, wanted) {
        if (!root) return null
        if (root.text === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findText(kids[i], wanted)
            if (found) return found
        }
        return null
    }

    function findChild(root, wanted) {
        if (!root) return null
        if (root.objectName === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], wanted)
            if (found) return found
        }
        return null
    }
}
