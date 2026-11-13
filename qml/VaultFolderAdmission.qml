// Keep native folder confirmation and identity decisions reachable after retiring the browser page.
import QtQuick
Item {
    id: root
    property var library: null
    property bool ceremonyDismissed: false
    readonly property bool cardVisible: !!(library && library.cardVisible)
    readonly property var ceremonies: library ? library.identityCeremonies || [] : []
    readonly property bool modalOpen: cardVisible || ceremony.opened
    function initialize() {
        if (!library) return
        if (typeof library.offerUnconfirmedRoots === "function") library.offerUnconfirmedRoots()
        if (typeof library.rescanDegradedRoots === "function") library.rescanDegradedRoots()
    }
    function handleBack() {
        if (ceremony.opened) { ceremonyDismissed = true; ceremony.close(); return true }
        if (cardVisible) { library.dismissCard(); return true }
        return false
    }
    VaultConfirmCard {
        objectName: "vaultCard"; anchors.fill: parent; visible: root.cardVisible
        model: root.library ? root.library.candidate || [] : []
        rootPath: root.library ? root.library.candidateRoot || "" : ""
        onShelveRequested: overrides => root.library.confirmRoot(rootPath, overrides)
        onDismissRequested: root.library.dismissCard()
    }
    VaultIdentityCeremonyDialog {
        id: ceremony; anchors.centerIn: parent
        visible: root.ceremonies.length > 0 && !root.ceremonyDismissed
        ceremony: root.ceremonies.length ? root.ceremonies[0] : ({})
        onChoiceMade: (relationship, choice) => {
            if (root.library && root.library.decideIdentityCeremony(relationship, choice)) {
                root.ceremonyDismissed = true
                close()
            }
        }
        onCancelRequested: { root.ceremonyDismissed = true; close() }
    }
    Connections { target: root.library; ignoreUnknownSignals: true; function onIdentityCeremoniesChanged() { root.ceremonyDismissed = false } }
}
