import QtQuick
import ".."

AccountPageFrame {
    id: root
    objectName: "accountTheatreSync"

    required property var nuvioAccount
    required property var stremioActions
    required property bool stremioAvailable
    property string provider: ""
    signal completed()
    signal cancelRequested()

    eyebrow: qsTr("COLOSSEUM · WELCOME")
    headline: qsTr("Sync your Theatre.")
    detail: qsTr("Bring your library, watched items, playback progress and addons into Colosseum. You'll review a preview before anything is imported.")

    function clearSecrets() {
        if (providerLoader.status === Loader.Ready && typeof providerLoader.item.clearSecrets === "function")
            providerLoader.item.clearSecrets()
    }

    Column {
        width: parent.width
        visible: root.provider === ""
        spacing: 10

        AccountPanelHeader {
            kicker: qsTr("THEATRE SYNC")
            title: qsTr("Which service do you use?")
            copy: qsTr("This service will become your primary Theatre sync connection.")
        }

        Item { width: 1; height: 12 }

        AccountChoice {
            objectName: "accountTheatreChooseStremio"
            width: parent.width
            visible: root.stremioAvailable
            iconSource: "../../assets/icons/stremio-official.svg"
            title: qsTr("Stremio")
            detail: qsTr("Sign in through your browser.")
            onChosen: root.provider = "stremio"
        }

        AccountChoice {
            objectName: "accountTheatreChooseNuvio"
            width: parent.width
            visible: root.nuvioAccount && root.nuvioAccount.available
            iconSource: "../../assets/icons/nuvio-official.png"
            title: qsTr("Nuvio")
            detail: qsTr("Sign in and choose the Nuvio profile to import.")
            onChosen: root.provider = "nuvio"
        }

        AccountChoice {
            objectName: "accountTheatrePickerCancel"
            width: parent.width
            title: qsTr("Cancel")
            detail: qsTr("Return to the welcome screen.")
            onChosen: root.cancelRequested()
        }
    }

    Loader {
        id: providerLoader
        width: parent.width
        active: root.provider !== ""
        visible: active
        sourceComponent: root.provider === "nuvio" ? nuvioComponent : stremioComponent
        onLoaded: item.takeKeyboardFocus()
    }

    Component {
        id: stremioComponent
        StremioSyncPanel {
            shown: true
            firstRun: true
            actions: root.stremioActions
            onCompleted: root.completed()
            onCloseRequested: root.cancelRequested()
        }
    }

    Component {
        id: nuvioComponent
        NuvioAccountFlow {
            account: root.nuvioAccount
            firstRun: true
            onCompleted: root.completed()
            onCancelRequested: root.cancelRequested()
        }
    }
}
