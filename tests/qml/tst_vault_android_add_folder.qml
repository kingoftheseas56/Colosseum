import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

// Android Vault step 2: the access card and Colosseum's own folder chooser. Drives the
// production components with seeded storage places and a real temporary folder tree.
TestCase {
    name: "VaultAndroidAddFolder"
    when: windowShown

    Window { id: testWindow; width: 900; height: 700; visible: true }
    Item { id: wall; parent: testWindow.contentItem; anchors.fill: parent }

    Component { id: cardComp; Colosseum.VaultAccessCard { anchors.fill: parent; backdrop: wall } }
    Component { id: chooserComp; Colosseum.VaultFolderChooser { width: 800; height: 560; backdrop: wall } }
    SignalSpy { id: settingsSpy; signalName: "openSettingsRequested" }
    SignalSpy { id: cancelSpy; signalName: "cancelRequested" }
    SignalSpy { id: addSpy; signalName: "addRequested" }

    function test_access_card_says_it_plainly_and_emits() {
        const card = createTemporaryObject(cardComp, testWindow.contentItem)
        settingsSpy.target = card; cancelSpy.target = card
        compare(findChild(card, "vaultAccessCardText").text,
                "Colosseum needs All files access to read your folders. It only reads the folders you add.")
        mouseClick(findChild(card, "vaultAccessCardOpenSettings"))
        compare(settingsSpy.count, 1)
        mouseClick(findChild(card, "vaultAccessCardCancel"))
        compare(cancelSpy.count, 1)
        settingsSpy.clear(); cancelSpy.clear()
    }

    function test_chooser_lists_places_then_folders_and_adds_current() {
        const volumes = [{ label: "Internal storage", path: "/storage/emulated/0", removable: false },
                         { label: "SD card", path: "/storage/ABCD-1234", removable: true }]
        const chooser = createTemporaryObject(chooserComp, testWindow.contentItem, { volumes: volumes })
        addSpy.target = chooser
        verify(chooser.atPlaces)
        compare(findChild(chooser, "vaultFolderChooserPlaces").count, 2)
        compare(findChild(chooser, "vaultAddThisFolder").enabled, false)
        chooser.open("/storage/emulated/0/Download")
        verify(!chooser.atPlaces)
        compare(chooser.volumeLabel, "Internal storage")
        compare(chooser.relativeLocation, "Download")
        compare(findChild(chooser, "vaultAddThisFolder").enabled, true)
        mouseClick(findChild(chooser, "vaultAddThisFolder"))
        compare(addSpy.count, 1)
        compare(addSpy.signalArguments[0][0], "/storage/emulated/0/Download")
        addSpy.clear()
        // the top of a place is addable, and Up from it returns to the places
        chooser.open("/storage/emulated/0")
        compare(chooser.relativeLocation, "")
        verify(chooser.handleBack())
        verify(chooser.atPlaces)
        verify(!chooser.handleBack())   // Back at the places level lets the page close the chooser
    }

    function test_one_place_and_hidden_folders() {
        const chooser = createTemporaryObject(chooserComp, testWindow.contentItem,
            { volumes: [{ label: "Internal storage", path: "/storage/emulated/0", removable: false }] })
        compare(chooser.currentPath, "/storage/emulated/0")
        verify(chooser.hiddenFolder("/storage/emulated/0/Android/data"))
        verify(chooser.hiddenFolder("/storage/emulated/0/Android/obb"))
        verify(!chooser.hiddenFolder("/storage/emulated/0/Android/media"))
        verify(!chooser.hiddenFolder("/storage/emulated/0/Download"))
    }
}
