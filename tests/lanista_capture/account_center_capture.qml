import QtQuick
import QtQuick.Window
import "../../qml/account" as Account

Window {
    id: win
    objectName: "accountCaptureWindow"
    width: 1280
    height: 720
    visible: true
    title: "Colosseum Account Centre Capture"
    color: "#0d0c09"

    Account.AccountCenter {
        id: center
        anchors.fill: parent
        initial: "H"
        yourColosseumMonthName: "August"
        yourColosseumMonthYear: "2026"
        yourColosseumWatchTimeText: ""
        yourColosseumPagesReadText: ""
        yourColosseumCompletedText: ""
        yourColosseumActiveDaysText: ""
        Component.onCompleted: open("colosseum")
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_1) center.open("colosseum")
            else if (event.key === Qt.Key_2) center.open("privacy")
        }
    }
}
