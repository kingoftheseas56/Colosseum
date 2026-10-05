import QtQuick
Item {
    FontLoader { id: regular; source: "../../assets/fonts/Inter-Regular.otf" }
    FontLoader { id: medium; source: "../../assets/fonts/Inter-Medium.otf" }
    FontLoader { id: semibold; source: "../../assets/fonts/Inter-SemiBold.otf" }
    FontLoader { id: bold; source: "../../assets/fonts/Inter-Bold.otf" }
    FontLoader { id: display; source: "../../assets/fonts/Fraunces-Regular.ttf" }
    readonly property bool ready: regular.status === FontLoader.Ready && medium.status === FontLoader.Ready
        && semibold.status === FontLoader.Ready && bold.status === FontLoader.Ready && display.status === FontLoader.Ready
}
