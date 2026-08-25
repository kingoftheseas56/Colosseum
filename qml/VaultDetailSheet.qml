// VaultDetailSheet — the Vault Browse face's detail sheet (execution plan Slice 7), built to the
// locked design's decision #11 and the approved mock's plate 4: opening a film answers "what do
// I physically hold" — every copy with its drive, its companions, its extras, and why Vault
// believes the identity it does. Deliberately never cast, synopsis, or related titles; the locked
// design gives that away to Theatre in three separate places.
//
// A SAME-WINDOW surface (a plain Item overlay inside VaultPage, the VaultConfirmCard/
// VaultFolderView convention), never a Window/Popup that would own its own platform window — the
// Lanista bridge structurally cannot see a secondary window (ledger law). Seedable, like its
// siblings: it takes `detail` (VaultLibrary.browseDetail()'s returned map) and emits
// backRequested / playRequested(path) / revealRequested(path) / identifyRequested(key) /
// unidentifyRequested(key) / hideRequested(key) / identifyAgainRequested(key) — VaultPage wires
// those to VaultLibrary + the existing openMediaRequested path. So a Qt Quick Test drives it
// with a seeded map, no app.
import QtQuick
import QtQuick.Controls

Item {
    id: sheet
    objectName: "vaultBrowseSheet"
    anchors.fill: parent

    // ── inputs ──
    // Shape: VaultLibrary.browseDetail()'s QVariantMap — { found, key, displayTitle, year,
    // identityState, identityLabel, runtimeText (ux uplift S8 — PRESENT ONLY when the clicked
    // copy's duration is known: "1h 47m" / "48m", never "-1"/"0m"), copiesHeld, coverRef,
    // bestQualityLine, copies:[{path, rootPath, quality, sizeBytes, sizeText, where, away,
    // admissionVerdict, statusDetail (S8 — a rejected/errored copy's human reason; empty when
    // healthy)}], companions:[string], extras:[{title, path}], evidence, ignoredCount, playPath }.
    property var detail: ({})
    property string identityStateOfRow: "" // the grid row's own state, for the Identify/Un-identify choice

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

    Theme { id: theme }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace) {
            sheet.backRequested()
            event.accepted = true
        }
    }
    onVisibleChanged: if (visible) sheet.forceActiveFocus()

    function copiesHeldLabel(n) { return n + (n === 1 ? " copy held" : " copies held") }

    // ── dimmed Vault behind + veil (modal over the browse face, the VaultConfirmCard convention) ──
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.02, 0.024, 0.035, 0.62)
        MouseArea { anchors.fill: parent; onClicked: sheet.backRequested() }
    }

    Rectangle {
        id: panel
        width: Math.min(760, parent.width - 80)
        height: Math.min(bodyCol.implicitHeight + 64, parent.height - 100)
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(40, parent.height * 0.08)
        radius: 20
        color: Qt.rgba(0.071, 0.082, 0.110, 0.99)
        border.width: 1
        border.color: theme.edge
        clip: true
        MouseArea { anchors.fill: parent } // swallow so the veil below never sees a click-through

        Flickable {
            id: flick
            anchors.fill: parent
            anchors.margins: 30
            contentWidth: width
            contentHeight: bodyCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HouseScrollBar { flick: flick }

            Row {
                id: bodyCol
                width: flick.width
                spacing: 26

                // ── poster / artwork ──
                Rectangle {
                    id: posterBox
                    width: 172; height: Math.round(width * 3 / 2)
                    radius: 5
                    color: Qt.rgba(1, 1, 1, 0.06)
                    clip: true
                    Image {
                        anchors.fill: parent
                        visible: !!(sheet.detail && sheet.detail.coverRef)
                        source: (sheet.detail && sheet.detail.coverRef) ? sheet.detail.coverRef : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true; cache: true
                    }
                    Text {
                        // Typographic-title fallback (ux uplift S8) — the cards' own permanent
                        // floor (VaultPosterCard's settled layer: real title, centered, wrapped,
                        // elided) instead of the literal placeholder word "artwork". Never an
                        // empty frame; real art (S5's file:// poster refs) paints on top.
                        objectName: "vaultBrowseSheetPosterTitle"
                        anchors.centerIn: parent
                        visible: !(sheet.detail && sheet.detail.coverRef)
                        width: parent.width - 24
                        text: (sheet.detail && sheet.detail.displayTitle) ? sheet.detail.displayTitle : ""
                        color: theme.inkDim
                        font.family: theme.ui; font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight
                        maximumLineCount: 4
                    }
                }

                Column {
                    width: bodyCol.width - posterBox.width - bodyCol.spacing
                    spacing: 0

                    Text {
                        id: titleText
                        width: parent.width
                        text: (sheet.detail && sheet.detail.displayTitle) ? sheet.detail.displayTitle : ""
                        color: theme.ink
                        font.family: theme.display; font.pixelSize: 33; font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        topPadding: 10
                        spacing: 9
                        Text {
                            visible: !!(sheet.detail && sheet.detail.year > 0)
                            text: (sheet.detail && sheet.detail.year > 0) ? String(sheet.detail.year) : ""
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                        Text {
                            // Runtime (ux uplift S8) — durationSec as the engine formatted it
                            // ("1h 47m"); the engine OMITS the key while unknown, so no "-1"/"0m"
                            // can ever reach this line.
                            objectName: "vaultBrowseSheetRuntime"
                            visible: !!(sheet.detail && sheet.detail.runtimeText)
                            text: (sheet.detail && sheet.detail.runtimeText) ? sheet.detail.runtimeText : ""
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                        Text {
                            objectName: "vaultBrowseSheetCopiesHeld"
                            text: sheet.copiesHeldLabel(sheet.copies.length)
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                        Text {
                            objectName: "vaultBrowseSheetIdentityLabel"
                            text: (sheet.detail && sheet.detail.identityLabel) ? sheet.detail.identityLabel : ""
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                        }
                    }

                    // ── Copies you hold ──
                    Column {
                        width: parent.width
                        topPadding: 24
                        spacing: 10
                        visible: sheet.copies.length > 0
                        Text {
                            text: "COPIES YOU HOLD"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.5
                        }
                        Repeater {
                            model: sheet.copies
                            delegate: Rectangle {
                                id: copyDelegate
                                required property var modelData
                                required property int index
                                objectName: "vaultBrowseSheetCopy_" + index
                                width: parent.width
                                height: copyCol.implicitHeight + 24
                                radius: 10
                                color: Qt.rgba(1, 1, 1, 0.042)
                                border.width: 1; border.color: theme.edge
                                opacity: modelData.away ? 0.55 : 1.0

                                readonly property string quality: modelData.quality || ""
                                readonly property string whereText: modelData.where || ""
                                readonly property string sizeText: modelData.sizeText || ""
                                readonly property bool away: !!modelData.away

                                Column {
                                    id: copyCol
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 14; anchors.rightMargin: 14
                                    spacing: 5

                                    Row {
                                        id: copyRow
                                        width: parent.width
                                        spacing: 14
                                        Text {
                                            text: (modelData.quality && modelData.quality.length) ? modelData.quality
                                                  : modelData.away ? "Drive not connected" : "Quality unknown"
                                            color: theme.ink; font.family: theme.ui; font.pixelSize: 13
                                            width: 130; elide: Text.ElideRight
                                        }
                                        Text {
                                            text: modelData.where || ""
                                            color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 13
                                            width: parent.width - 130 - 90 - 28
                                            elide: Text.ElideMiddle
                                        }
                                        Text {
                                            text: modelData.sizeText || ""
                                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                            width: 90; horizontalAlignment: Text.AlignRight
                                        }
                                    }

                                    // Honest failure (ux uplift S8): a rejected/errored copy's
                                    // own recorded reason, quiet and factual — never a bare
                                    // verdict code. Empty for a healthy copy; the Column drops
                                    // the invisible line, so nothing shifts.
                                    Text {
                                        objectName: "vaultBrowseSheetCopyStatus_" + copyDelegate.index
                                        width: parent.width
                                        visible: !!(copyDelegate.modelData.statusDetail
                                                    && copyDelegate.modelData.statusDetail.length)
                                        text: visible ? copyDelegate.modelData.statusDetail : ""
                                        color: theme.inkDimmer; font.family: theme.ui; font.pixelSize: 11
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }

                    // ── Companions ──
                    Column {
                        width: parent.width
                        topPadding: 22
                        spacing: 10
                        visible: sheet.companionsList.length > 0
                        Text {
                            text: "COMPANIONS"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.5
                        }
                        Flow {
                            width: parent.width
                            spacing: 8
                            Repeater {
                                model: sheet.companionsList
                                delegate: Rectangle {
                                    required property var modelData
                                    width: chipText.implicitWidth + 22; height: 30; radius: 8
                                    color: Qt.rgba(1, 1, 1, 0.042)
                                    border.width: 1; border.color: theme.edge
                                    Text {
                                        id: chipText
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: theme.inkDim; font.family: theme.ui; font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }

                    // ── Extras (folded out of the grid, listed here, playable) ──
                    Column {
                        width: parent.width
                        topPadding: 22
                        spacing: 8
                        visible: sheet.extrasList.length > 0
                        Text {
                            text: "EXTRAS"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.5
                        }
                        Repeater {
                            model: sheet.extrasList
                            delegate: Row {
                                required property var modelData
                                spacing: 8
                                Text {
                                    text: modelData.title || ""
                                    color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                                }
                                MouseArea {
                                    width: 60; height: playLabel.implicitHeight
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (modelData.path) sheet.playRequested(modelData.path)
                                    Text {
                                        id: playLabel
                                        text: "Play"
                                        color: theme.gold; font.family: theme.ui; font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }

                    // ── Why Vault believes this ──
                    Column {
                        width: parent.width
                        topPadding: 22
                        spacing: 8
                        visible: !!(sheet.detail && sheet.detail.evidence)
                        Text {
                            text: "WHY VAULT BELIEVES THIS"
                            color: theme.inkDimmer
                            font.family: theme.ui; font.pixelSize: 11; font.letterSpacing: 1.5
                        }
                        Text {
                            objectName: "vaultBrowseSheetEvidence"
                            width: parent.width
                            text: (sheet.detail && sheet.detail.evidence) ? sheet.detail.evidence : ""
                            color: theme.inkDim; font.family: theme.ui; font.pixelSize: 13
                            lineHeight: 1.5; wrapMode: Text.WordWrap
                        }
                    }

                    // ── actions: Play (primary) + the reachable capabilities the design requires ──
                    Row {
                        topPadding: 22
                        spacing: 18

                        Rectangle {
                            objectName: "vaultBrowseSheetPlay"
                            width: playRowLabel.implicitWidth + 42; height: 42; radius: 10
                            color: playMa.containsMouse ? Qt.rgba(0.94, 0.77, 0.29, 0.92) : theme.gold
                            Row {
                                id: playRowLabel
                                anchors.centerIn: parent
                                spacing: 9
                                Text { text: "▶"; color: "#17120a"; font.pixelSize: 13 }
                                Text { text: "Play"; color: "#17120a"; font.family: theme.ui
                                       font.pixelSize: 14; font.weight: Font.DemiBold }
                            }
                            MouseArea {
                                id: playMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                enabled: !!(sheet.detail && sheet.detail.playPath)
                                onClicked: sheet.playRequested(sheet.detail.playPath)
                            }
                        }

                        Text {
                            objectName: "vaultBrowseSheetReveal"
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Reveal in Explorer"
                            visible: !!(sheet.detail && sheet.detail.playPath)
                            color: revealMa.containsMouse ? theme.ink : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 13
                            MouseArea { id: revealMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: sheet.revealRequested(sheet.detail.playPath) }
                        }

                        Text {
                            objectName: "vaultBrowseSheetIdentify"
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Identify…"
                            visible: sheet.identityStateOfRow === "uncertain" || sheet.identityStateOfRow === "resolving"
                            color: identifyMa.containsMouse ? theme.ink : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 13
                            MouseArea { id: identifyMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: sheet.identifyRequested(sheet.detail.key || "") }
                        }

                        Text {
                            // "Identify again" (ux uplift S8) — the one-shot conservative retry
                            // beside the manual picker: VaultLibrary::identifyGroup() re-runs the
                            // certainty gate for this one group (auto-adopt on a single match,
                            // durably record ambiguity and stay honest on several). The handler
                            // is VaultPage.qml's, not this sheet's — see the signal's own comment.
                            objectName: "vaultBrowseSheetIdentifyAgain"
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Identify again"
                            visible: sheet.identityStateOfRow === "uncertain" || sheet.identityStateOfRow === "resolving"
                            color: identifyAgainMa.containsMouse ? theme.ink : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 13
                            MouseArea { id: identifyAgainMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: sheet.identifyAgainRequested(sheet.detail.key || "") }
                        }

                        Text {
                            objectName: "vaultBrowseSheetUnidentify"
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Un-identify"
                            visible: sheet.identityStateOfRow === "identified"
                            color: unidentifyMa.containsMouse ? theme.ink : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 13
                            MouseArea { id: unidentifyMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: sheet.unidentifyRequested(sheet.detail.key || "") }
                        }

                        Text {
                            objectName: "vaultBrowseSheetHide"
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Hide"
                            color: hideMa.containsMouse ? theme.ink : theme.inkDim
                            font.family: theme.ui; font.pixelSize: 13
                            MouseArea { id: hideMa; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: sheet.hideRequested(sheet.detail.key || "") }
                        }
                    }
                }
            }
        }
    }

    BackAction {
        objectName: "vaultBrowseSheetBack"
        variant: "capsule"; tip: "Back"
        anchors.top: parent.top; anchors.left: parent.left
        anchors.topMargin: 21; anchors.leftMargin: theme.margin - 10
        onTriggered: sheet.backRequested()
    }
}
