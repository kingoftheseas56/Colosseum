import QtQuick 2.15
import QtQuick.Window 2.15
import "../../../qml" as Colosseum
import "../../../qml/account" as Account

Window {
    id: window
    objectName: "keyboardKeyEventsWindow"
    width: 760
    height: 520
    visible: true

    Account.AccountPageFrame {
        id: accountPage
        objectName: "accountPageFrame"
        anchors.fill: parent
        visible: true
        focus: true
        headline: "Account"
        panelContent: Item {
            width: 520
            height: 700
            Colosseum.KeyboardAction {
                objectName: "accountActionA"
                x: 12
                y: 20
                width: 420
                height: 40
                accessibleName: "Account A"
            }
            Colosseum.KeyboardAction {
                objectName: "accountActionB"
                x: 12
                y: 440
                width: 420
                height: 40
                accessibleName: "Account B"
            }
            Colosseum.KeyboardAction {
                objectName: "accountActionC"
                x: 12
                y: 500
                width: 420
                height: 40
                accessibleName: "Account C"
            }
        }
    }

    Item {
        id: deferredOwner
        objectName: "deferredKeyboardOwner"
        anchors.fill: parent
        visible: false
        focus: true
        property string landedId: ""
        property int movementCount: 0
        property int aLandingCount: 0
        property int bLandingCount: 0
        property int pressGeneration: 0
        property int repeatGeneration: 0
        property int releaseCount: 0

        Colosseum.KeyboardAction {
            id: deferredA
            objectName: "deferredA"
            x: 20
            y: 20
            width: 100
            height: 44
            pointerEnabled: false
        }
        Colosseum.KeyboardAction {
            id: deferredB
            objectName: "deferredB"
            x: 20
            y: 100
            width: 100
            height: 44
            pointerEnabled: false
        }
        Colosseum.KeyboardSpatialNavigator {
            id: deferredNavigator
            objectName: "deferredNavigator"
            root: deferredOwner
        }

        Connections {
            target: deferredA
            function onActiveFocusChanged() {
                if (deferredA.activeFocus) {
                    deferredOwner.landedId = "A"
                    deferredOwner.movementCount += 1
                    deferredOwner.aLandingCount += 1
                }
            }
        }
        Connections {
            target: deferredB
            function onActiveFocusChanged() {
                if (deferredB.activeFocus) {
                    deferredOwner.landedId = "B"
                    deferredOwner.movementCount += 1
                    deferredOwner.bLandingCount += 1
                }
            }
        }

        function queueLanding(item, isRepeat) {
            var generation = deferredNavigator.beginNavigation(Qt.Key_Down)
            if (isRepeat)
                deferredOwner.repeatGeneration = generation
            else
                deferredOwner.pressGeneration = generation
            deferredNavigator.deferLanding(item, Qt.Key_Down,
                                           Qt.TabFocusReason, generation)
            Qt.callLater(deferredNavigator.settlePendingLanding)
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Down) {
                queueLanding(event.isAutoRepeat ? deferredB : deferredA,
                             event.isAutoRepeat)
                event.accepted = true
            }
        }
        Keys.onReleased: function(event) {
            if (event.key === Qt.Key_Down) {
                deferredOwner.releaseCount += 1
                deferredNavigator.handleRelease(event)
                event.accepted = true
            }
        }
    }
}
