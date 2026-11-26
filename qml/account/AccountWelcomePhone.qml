import QtQuick
import QtQuick.Controls
import ".."

Item {
    id: root
    objectName: "accountWelcomePhone"

    property var controller: null
    property Item backdrop: null

    readonly property bool accountEntryAvailable:
        !root.controller || root.controller.accountFeaturesAvailable !== false

    signal signInRequested()
    signal createAccountRequested()

    Theme { id: theme }
    AdaptiveLayout {
        id: adaptive
        androidHost: Qt.platform.os === "android" || !!(root.Window.window && root.Window.window.androidHost === true)
        viewportWidth: root.visualViewportWidth
        viewportHeight: root.visualViewportHeight
    }

    // The Windows phone-shell harness creates a pixel-sized native window at
    // the desktop DPI. Keep its visual viewport within those physical pixels;
    // Android itself uses ordinary logical-width layout.
    readonly property real visualDensity: Qt.platform.os === "windows"
        ? Math.max(1, root.Screen.devicePixelRatio) : 1
    readonly property real visualViewportWidth: {
        const win = root.Window.window
        const hostWidth = win ? win.width : root.width
        return Math.max(0, Math.min(root.width, hostWidth / root.visualDensity))
    }
    readonly property real visualViewportHeight: {
        const win = root.Window.window
        const hostHeight = win ? win.height : root.height
        return Math.max(0, Math.min(root.height, hostHeight / root.visualDensity))
    }

    Item {
        id: wallpaper
        anchors.fill: parent

        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.backdrop
            live: true
            hideSource: false
            visible: root.backdrop !== null
        }

        Image {
            anchors.fill: parent
            visible: root.backdrop === null
            source: "../../assets/wallpaper/captured-motion.jpg"
            fillMode: Image.PreserveAspectCrop
            cache: true
        }

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0.012, 0.016, 0.031, 0.78)
        }
    }

    KeyboardScrollController {
        id: keyboardScroll
        flick: scroller
    }

    Keys.priority: Keys.AfterItem
    Keys.onPressed: function(event) {
        keyboardScroll.handle(event)
    }
    Keys.onReleased: function(event) {
        keyboardScroll.handleRelease(event)
    }

    Flickable {
        id: scroller
        objectName: "accountWelcomePhoneScrollRegion"
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.visualViewportWidth
        height: root.visualViewportHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: Math.max(height, content.y + content.implicitHeight + (adaptive.compactWelcome ? adaptive.welcomeInset : 24))

        ScrollBar.vertical: ScrollBar {
            policy: scroller.contentHeight > scroller.height
                ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: content
            x: 20
            y: adaptive.compactWelcome ? adaptive.welcomeInset : 28
            width: Math.max(0, scroller.width - 40)
            spacing: 0

            Text {
                width: parent.width
                text: "COLOSSEUM · WELCOME"
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 2.2
                wrapMode: Text.WordWrap
            }

            Item { width: 1; height: adaptive.compactWelcome ? adaptive.welcomeSmallGap : 12 }

            Text {
                width: parent.width
                text: "Welcome to Colosseum."
                color: theme.ink
                font.family: theme.display
                font.pixelSize: adaptive.compactWelcome ? adaptive.welcomeTitleSize : (root.width < 380 ? 31 : 34)
                font.weight: Font.Medium
                font.letterSpacing: -0.8
                lineHeight: 1.05
                wrapMode: Text.WordWrap
            }

            Item { width: 1; height: adaptive.compactWelcome ? adaptive.welcomeGap : 22 }

            Rectangle {
                width: 34
                height: 3
                radius: 2
                color: theme.gold
            }

            Item { width: 1; height: adaptive.compactWelcome ? adaptive.welcomeGap : 20 }

            Text {
                width: parent.width
                text: "Use Colosseum completely locally, or sign in to carry your Collection, progress, reader setup, preferences, and other personal choices between devices."
                color: theme.inkDimmer
                font.family: theme.ui
                font.pixelSize: adaptive.compactWelcome ? adaptive.welcomeBodySize : 14
                lineHeight: adaptive.compactWelcome ? 1.25 : 1.55
                wrapMode: Text.WordWrap
            }

            Item { width: 1; height: adaptive.compactWelcome ? adaptive.welcomeInset : 28 }

            Glass {
                id: panel
                width: parent.width
                height: panelColumn.implicitHeight + (adaptive.compactWelcome ? adaptive.welcomeInset * 2 : 36)
                backdrop: wallpaper
                radius: 18
                tint: 0.09
                scrim: 0.20

                Column {
                    id: panelColumn
                    x: adaptive.compactWelcome ? adaptive.welcomeInset : 18
                    y: adaptive.compactWelcome ? adaptive.welcomeInset : 18
                    width: parent.width - x * 2
                    spacing: 0

                    AccountPanelHeader {
                        compact: adaptive.compactWelcome
                        kicker: "START"
                        title: "How do you want to begin?"
                        copy: "An account is optional. You can change your mind later."
                    }

                    Item { width: 1; height: adaptive.compactWelcome ? adaptive.welcomeGap : 22 }

                    AccountWelcomeChoice {
                        compact: adaptive.compactWelcome
                        objectName: "accountWelcomeContinueLocal"
                        width: parent.width
                        title: "Continue without account"
                        detail: "Keep everything local on this device."
                        onChosen: if (root.controller) root.controller.continueWithoutAccount()
                    }

                    Item { width: 1; height: adaptive.compactWelcome ? adaptive.welcomeSmallGap : 10 }

                    AccountWelcomeChoice {
                        compact: adaptive.compactWelcome
                        objectName: "accountWelcomeSignIn"
                        visible: root.accountEntryAvailable
                        width: parent.width
                        title: "Sign in"
                        detail: "Bring your Colosseum back on this device."
                        onChosen: root.signInRequested()
                    }

                    Item { width: 1; height: adaptive.compactWelcome ? adaptive.welcomeSmallGap : 10 }

                    AccountWelcomeChoice {
                        compact: adaptive.compactWelcome
                        objectName: "accountWelcomeCreateAccount"
                        visible: root.accountEntryAvailable
                        width: parent.width
                        title: "Create account"
                        detail: "Start syncing your personal Colosseum."
                        onChosen: root.createAccountRequested()
                    }

                    Item { width: 1; height: adaptive.compactWelcome ? adaptive.welcomeGap : 20 }

                    Text {
                        width: parent.width
                        text: "Accounts are optional. Sign in later whenever you want sync."
                        color: theme.inkDimmer
                        font.family: theme.ui
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
