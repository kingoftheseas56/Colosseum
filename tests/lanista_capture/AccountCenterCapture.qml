import QtQuick
import "../../qml/account" as Account

Window {
    width: 1440
    height: 900
    visible: true
    color: "#0d0c09"
    title: "Colosseum Account Centre Capture"

    QtObject {
        id: fakeController
        property string username: "Colosseum"
    }

    Account.AccountCenter {
        id: center
        anchors.fill: parent
        controller: fakeController
        initial: "C"
        yourColosseumMonthName: "August"
        yourColosseumMonthYear: "2026"
        yourColosseumWatchTimeText: "18h 42m"
        yourColosseumPagesReadText: "1,284"
        yourColosseumCompletedText: "12"
        yourColosseumActiveDaysText: "17"
        Component.onCompleted: center.open("colosseum")
    }
}
