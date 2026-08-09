import QtQuick 2.15
import QtQuick.Controls.Basic 2.15 as Basic

Rectangle {
    id: root
    objectName: "guideIndex"
    color: "#111111"
    border.color: "#303030"
    border.width: 1
    width: drawerMode ? 264 : 224

    property string currentSection: "start"
    property bool drawerMode: parent ? parent.width < 980 : false
    property bool drawerOpen: false
    readonly property var sections: [
        { id: "home", label: "Guide Home" },
        { id: "start", label: "Start here" },
        { id: "tankoban", label: "Tankoban" },
        { id: "biblio", label: "Biblio" },
        { id: "theatre", label: "Theatre" },
        { id: "house", label: "Vault and local media" },
        { id: "downloads", label: "Downloads and sources" },
        { id: "personalization", label: "Personalization" },
        { id: "fix", label: "Fix a problem" }
    ]
    signal sectionRequested(string section)

    visible: !drawerMode || drawerOpen
    z: drawerMode ? 10 : 1

    function selectSection(section) {
        if (section === "home" || section === "start" || section === "tankoban" || section === "biblio"
                || section === "theatre" || section === "house" || section === "downloads"
                || section === "personalization" || section === "fix") {
            currentSection = section
            sectionRequested(section)
            if (drawerMode) drawerOpen = false
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 3

        Text {
            text: "GUIDE INDEX"
            color: "#8f8f8f"
            font.pixelSize: 11
            font.letterSpacing: 1.2
            bottomPadding: 8
        }

        Repeater {
            model: root.sections
            delegate: Basic.Button {
                width: parent.width
                height: 38
                text: modelData.label
                activeFocusOnTab: true
                Accessible.name: text
                contentItem: Text {
                    text: parent.text
                    color: parent.hovered || parent.activeFocus || root.currentSection === modelData.id ? "#ffffff" : "#c5c5c5"
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 14
                    leftPadding: 10
                }
                background: Rectangle {
                    color: root.currentSection === modelData.id ? "#2b2b2b" : (parent.hovered ? "#202020" : "transparent")
                    border.color: parent.activeFocus ? "#efefef" : "transparent"
                    border.width: parent.activeFocus ? 1 : 0
                    radius: 2
                }
                onClicked: root.selectSection(modelData.id)
            }
        }
    }
}
