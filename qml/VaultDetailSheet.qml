// VaultDetailSheet — the Vault Browse face's detail sheet (execution plan Slice 7), built to the
// locked design's decision #11 and the approved mock's plate 4: opening a film answers "what do
// I physically hold" — every copy with its drive, its companions, its extras, and why Vault
// believes the identity it does. Deliberately never cast, synopsis, or related titles; the locked
// design gives that away to Theatre in three separate places.
//
// A SAME-WINDOW surface (a plain Item overlay inside VaultPage, the VaultConfirmCard
// convention), never a Window/Popup that would own its own platform window — the
// Lanista bridge structurally cannot see a secondary window (ledger law). Seedable, like its
// siblings: it takes `detail` (VaultLibrary.browseDetail()'s returned map) and emits
// backRequested / playRequested(path) / revealRequested(path) / identifyRequested(key) /
// unidentifyRequested(key) / hideRequested(key) / identifyAgainRequested(key) /
// markWatchedRequested(vaultId, watched) — VaultPage wires those to VaultLibrary and the
// existing openMediaRequested/Progress paths. So a Qt Quick Test drives it with a seeded map,
// no app.
import QtQuick
import QtQuick.Controls

Item {
    id: sheet
    property Item backdrop: null
    objectName: "vaultBrowseSheet"
    anchors.fill: parent

    // ── inputs ──
    // Shape: VaultLibrary.browseDetail()'s QVariantMap — { found, key, displayTitle, year,
    // identityState, identityLabel, runtimeText (ux uplift S8 — PRESENT ONLY when the clicked
    // copy's duration is known: "1h 47m" / "48m", never "-1"/"0m"), ratingText (G1 — the
    // adopted identity's "IMDb 8.1", provenance-badged; OMITTED while the identity carries no
    // rating), genresLine (G1 — the " · "-joined genre list; OMITTED when empty), copiesHeld,
    // coverRef, bestQualityLine, copies:[{path, rootPath, quality, sizeBytes, sizeText, where,
    // away, admissionVerdict, statusDetail (S8 — a rejected/errored copy's human reason; empty
    // when healthy)}], companions:[string], extras:[{title, path}], evidence, ignoredCount,
    // playPath }.
    property var detail: ({})
    property string identityStateOfRow: "" // the grid row's own state, for the Identify/Un-identify choice
    property string mediaKind: ""
    // S7 watched-verb inputs (see the signal's own block below for the full contract).
    property string rowVaultId: ""           // the opened row's vault id ("vault:"-prefixed; "" otherwise)
    property bool rowIsWatched: false        // the live ProgressStore.watchedMark === 1 state

    // ── outputs ──
    signal backRequested()
    signal playRequested(string path)
    signal revealRequested(string path)
    signal identifyRequested(string key)
    signal unidentifyRequested(string key)
    signal hideRequested(string key)
    // "Identify again" (vault ux uplift S8) — a one-shot re-run of the conservative auto gate
    // for THIS group (VaultLibrary::identifyGroup(groupKey): adopt when exactly one match,
    // durably record the ambiguity and stay honest when several). Deliberately NOT wired here:
    // VaultPage.qml owns the handler (a different slice's file). The intended wiring is
    //   onIdentifyAgainRequested: (key) => {
    //       if (typeof VaultLibrary !== "undefined" && key) VaultLibrary.identifyGroup(key)
    //   }
    // — the same guard shape its sibling handlers below on this sheet already use.
    signal identifyAgainRequested(string groupKey)

    // Vault ux uplift S7 — watched/unwatched verbs. The sheet stays seedable like every
    // sibling: it never calls Progress itself. VaultPage supplies the row's vault id (its
    // S6 join key, "vault:"-prefixed — see rowVaultId/rowIsWatched at the inputs above) and
    // the live mark state (ProgressStore.watchedMark === 1 read on the Progress revision
    // clock), and a click EMBEDS the two facts in one emission — the same owner discipline
    // identifyAgainRequested follows. A catalogue/container row's empty id hides both verbs;
    // "Mark unwatched" is the CLEAR verb (never a pinned -1).
    signal markWatchedRequested(string vaultId, bool watched)

    readonly property bool found: !!(detail && detail.found)
    readonly property var copies: (detail && detail.copies) ? detail.copies : []
    readonly property var extrasList: (detail && detail.extras) ? detail.extras : []
    readonly property var companionsList: (detail && detail.companions) ? detail.companions : []

    // ── the Lanista/Quick-Test vocabulary (the VaultConfirmCard.sliceCount convention) — plain
    //    scalars so a scenario/test asserts without walking nested arrays by dot-path. ──
    readonly property int copiesHeld: sheet.copies.length
    readonly property int companionsCount: sheet.companionsList.length
    readonly property int extrasCount: sheet.extrasList.length
    readonly property string evidenceText: (detail && detail.evidence) ? detail.evidence : ""
    readonly property string identityLabel: (detail && detail.identityLabel) ? detail.identityLabel : ""
    readonly property string playPath: (detail && detail.playPath) ? detail.playPath : ""

    VaultTheme { id: theme }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            sheet.backRequested()
            event.accepted = true
        }
    }
    // Vault ux uplift S15 — keyboard accept: Return plays the primary path (the cancel key,
    // Escape/Backspace, is above). Never fires while the sheet has no play target.
    Keys.onReturnPressed: (event) => {
        if (sheet.playPath) {
            sheet.playRequested(sheet.playPath)
            event.accepted = true
        }
    }
    Keys.onEnterPressed: (event) => {
        if (sheet.playPath) {
            sheet.playRequested(sheet.playPath)
            event.accepted = true
        }
    }
    onVisibleChanged: if (visible) Qt.callLater(function() {
        if (playKey.enabled) playKey.keyboardAction.forceActiveFocus(Qt.TabFocusReason)
        else hideKey.keyboardAction.forceActiveFocus(Qt.TabFocusReason)
    })

    function copiesHeldLabel(n) { return n + (n === 1 ? " copy held" : " copies held") }

    Rectangle {
        anchors.fill: parent; color: "#d107090c"
        MouseArea { anchors.fill: parent; onClicked: sheet.backRequested() }
    }
    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(880, parent.width - 40)
        height: Math.min(body.implicitHeight + 84, parent.height - 64)
        radius: 20; color: "transparent"
        Glass {
            anchors.fill: parent
            backdrop: sheet.backdrop; radius: panel.radius; scrim: 0.68
        }
        clip: true
        MouseArea { anchors.fill: parent }
        Image {
            width: parent.width; height: 210
            source: sheet.detail.coverRef || ""; fillMode: Image.PreserveAspectCrop
            asynchronous: true; opacity: 0.22
        }
        Rectangle {
            width: parent.width; height: 212
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: theme.panel }
            }
        }
        Flickable {
            id: flick
            anchors.fill: parent; anchors.margins: 30; anchors.topMargin: 54
            clip: true; contentWidth: width; contentHeight: body.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            Keys.onPressed: (event) => flickKeys.handle(event)
            Keys.onReleased: (event) => flickKeys.handleRelease(event)
            KeyboardScrollController { id: flickKeys; flick: flick }
            ScrollBar.vertical: HouseScrollBar { flick: flick }
            Column {
                id: body; width: flick.width; spacing: 22; bottomPadding: 8
                Row {
                    width: parent.width; spacing: 22
                    Rectangle {
                        width: panel.width < 600 ? 90 : 120; height: width * 1.5; radius: 12
                        color: theme.panel; clip: true
                        Text {
                            objectName: "vaultBrowseSheetPosterTitle"
                            anchors.fill: parent; anchors.margins: 12
                            text: sheet.detail.displayTitle || ""
                            visible: !sheet.detail.coverRef
                            verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap
                        }
                        Image { anchors.fill: parent; source: sheet.detail.coverRef || ""; fillMode: Image.PreserveAspectCrop; asynchronous: true }
                    }
                    Column {
                        width: parent.width - (panel.width < 600 ? 90 : 120) - 22; spacing: 12
                        topPadding: 12
                        Text {
                            objectName: "vaultBrowseSheetIdentityLabel"
                            width: parent.width; text: sheet.detail.identityLabel || ""
                            color: theme.gold; font.family: theme.ui; font.pixelSize: 11; wrapMode: Text.WordWrap
                        }
                        Text {
                            width: parent.width; text: sheet.detail.displayTitle || ""
                            color: theme.ink; font.family: theme.display; font.pixelSize: panel.width < 600 ? 25 : 33
                            font.weight: Font.DemiBold; wrapMode: Text.WordWrap
                        }
                        Flow {
                            width: parent.width; spacing: 12
                            Text { text: sheet.detail.year > 0 ? String(sheet.detail.year) : ""; visible: text.length > 0; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                            Text { objectName: "vaultBrowseSheetRuntime"; text: sheet.detail.runtimeText || ""; visible: text.length > 0; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                            Text { objectName: "vaultBrowseSheetCopiesHeld"; text: sheet.copiesHeldLabel(sheet.copies.length); color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                            Text { objectName: "vaultBrowseSheetRating"; text: sheet.detail.ratingText || ""; visible: text.length > 0; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                        }
                        Text { objectName: "vaultBrowseSheetGenres"; width: parent.width; text: sheet.detail.genresLine || ""; visible: text.length > 0; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    }
                }
                Flow {
                    width: parent.width; spacing: 10
                    VaultAction {
                        id: playKey; objectName: "vaultBrowseSheetPlay"
                        text: sheet.mediaKind === "book" || sheet.mediaKind === "comic" ? "Read" : "▶  Play"
                        primary: true; enabled: !!sheet.playPath
                        previousFocus: closeAction.keyboardAction
                        onTriggered: sheet.playRequested(sheet.playPath)
                    }
                    VaultAction {
                        objectName: "vaultBrowseSheetReveal"; text: "Reveal in Explorer"
                        visible: !!sheet.playPath; onTriggered: sheet.revealRequested(sheet.playPath)
                    }
                    VaultAction {
                        objectName: "vaultBrowseSheetMarkWatched"; text: "Mark watched"
                        visible: sheet.rowVaultId.length > 0 && !sheet.rowIsWatched
                        onTriggered: sheet.markWatchedRequested(sheet.rowVaultId, true)
                    }
                    VaultAction {
                        objectName: "vaultBrowseSheetMarkUnwatched"; text: "Mark unwatched"
                        visible: sheet.rowVaultId.length > 0 && sheet.rowIsWatched
                        onTriggered: sheet.markWatchedRequested(sheet.rowVaultId, false)
                    }
                }
                Rectangle { width: parent.width; height: 1; color: theme.edge }
                Column {
                    width: parent.width; spacing: 10; visible: sheet.copies.length > 0
                    Text { text: "Copies you hold"; color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Repeater {
                        model: sheet.copies
                        delegate: Rectangle {
                            id: copyDelegate
                            required property var modelData
                            required property int index
                            objectName: "vaultBrowseSheetCopy_" + index
                            readonly property string quality: modelData.quality || ""
                            readonly property string whereText: modelData.where || ""
                            readonly property string sizeText: modelData.sizeText || ""
                            readonly property bool away: !!modelData.away
                            width: parent.width; height: copyInfo.implicitHeight + 28; radius: 6
                            color: theme.glassTint; border.width: 1; border.color: theme.edge
                            Column {
                                id: copyInfo
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter; anchors.margins: 14
                                spacing: 7
                                Flow {
                                    width: parent.width; spacing: 16
                                    Text { text: copyDelegate.quality || "Quality unknown"; color: theme.ink; font.family: theme.ui; font.pixelSize: 13 }
                                    Text { text: copyDelegate.sizeText; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                                    Text { text: copyDelegate.away ? "Drive not connected" : "Available"; color: copyDelegate.away ? theme.gold : theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                                }
                                Text { width: parent.width; text: copyDelegate.modelData.path || copyDelegate.whereText; wrapMode: Text.WrapAnywhere; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11 }
                                Text {
                                    objectName: "vaultBrowseSheetCopyStatus_" + copyDelegate.index
                                    width: parent.width; text: copyDelegate.modelData.statusDetail || ""
                                    visible: text.length > 0; wrapMode: Text.WordWrap
                                    color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
                Column {
                    width: parent.width; spacing: 10; visible: sheet.companionsList.length > 0
                    Text { text: "Companions"; color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Repeater {
                        model: sheet.companionsList
                        Text { required property var modelData; width: parent.width; text: modelData; wrapMode: Text.WrapAnywhere; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12 }
                    }
                }
                Column {
                    width: parent.width; spacing: 10; visible: sheet.extrasList.length > 0
                    Text { text: "Extras"; color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Repeater {
                        model: sheet.extrasList
                        VaultAction { required property var modelData; width: Math.min(parent.width, implicitWidth); text: "▶  " + (modelData.title || "Play extra"); enabled: !!modelData.path; onTriggered: sheet.playRequested(modelData.path) }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: theme.edge; visible: sheet.evidenceText.length > 0 }
                Column {
                    width: parent.width; spacing: 10; visible: sheet.evidenceText.length > 0
                    Text { text: "Why Vault believes this"; color: theme.ink; font.family: theme.ui; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Text { objectName: "vaultBrowseSheetEvidence"; width: parent.width; text: sheet.evidenceText; wrapMode: Text.WordWrap; color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 12; lineHeight: 1.5 }
                }
                Flow {
                    width: parent.width; spacing: 10
                    VaultAction {
                        objectName: "vaultBrowseSheetIdentify"; text: "Identify…"
                        visible: sheet.identityStateOfRow === "uncertain" || sheet.identityStateOfRow === "resolving"
                        onTriggered: sheet.identifyRequested(sheet.detail.key || "")
                    }
                    VaultAction { objectName: "vaultBrowseSheetIdentifyAgain"; text: "Identify again"; quiet: true; visible: sheet.identityStateOfRow === "uncertain" || sheet.identityStateOfRow === "resolving"; onTriggered: sheet.identifyAgainRequested(sheet.detail.key || "") }
                    VaultAction {
                        objectName: "vaultBrowseSheetUnidentify"; text: "Un-identify"; quiet: true
                        visible: sheet.identityStateOfRow === "identified"
                        onTriggered: sheet.unidentifyRequested(sheet.detail.key || "")
                    }
                    VaultAction { id: hideKey; objectName: "vaultBrowseSheetHide"; text: "Hide"; quiet: true; nextFocus: closeAction.keyboardAction; onTriggered: sheet.hideRequested(sheet.detail.key || "") }
                }
            }
        }
        VaultAction {
            id: closeAction
            objectName: "vaultBrowseSheetClose"
            anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 12
            width: 32; height: 32; text: "×"; accessibleName: "Close Vault details"; quiet: true
            nextFocus: playKey.enabled ? playKey.keyboardAction : hideKey.keyboardAction
            previousFocus: hideKey.keyboardAction
            onTriggered: sheet.backRequested()
        }
    }
}
