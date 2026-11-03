import QtQuick
import QtTest
import "../../qml" as App
import "../../qml/+android" as AndroidUi

Item {
    width: 1200; height: 1280
    Rectangle { id: feedbackBackdrop; anchors.fill: parent; color: "#08090d" }

    Component { id: panelComponent; App.FeedbackPanel { browserSource: Qt.resolvedUrl("MockBrowser.qml") } }
    Component { id: vaultComponent; App.VaultPage { backdrop: feedbackBackdrop } }
    Component {
        id: desktopTopBarHost
        Item {
            Rectangle { id: shellBackdrop; anchors.fill: parent; color: "#08090d" }
            App.TopBar { objectName: "testTopBar"; width: parent.width; height: 90; backdrop: shellBackdrop }
        }
    }
    Component {
        id: androidTopBarHost
        Item {
            Rectangle { id: shellBackdrop; anchors.fill: parent; color: "#08090d" }
            AndroidUi.TopBar {
                objectName: "testTopBar"
                width: parent.width; height: 90
                backdrop: shellBackdrop
                androidHost: true
            }
        }
    }
    Component {
        id: desktopTaskbarHost
        Item {
            Rectangle { id: shellBackdrop; anchors.fill: parent; color: "#08090d" }
            App.Taskbar { anchors.fill: parent }
        }
    }
    Component {
        id: androidTaskbarHost
        Item {
            Rectangle { id: shellBackdrop; anchors.fill: parent; color: "#08090d" }
            AndroidUi.Taskbar { anchors.fill: parent }
        }
    }

    SignalSpy { id: closeSpy; signalName: "closeRequested" }
    SignalSpy { id: minimizeSpy; signalName: "minimizeRequested" }
    SignalSpy { id: fullscreenSpy; signalName: "fullscreenRequested" }
    SignalSpy { id: quitSpy; signalName: "quitRequested" }
    SignalSpy { id: feedbackSpy; signalName: "feedbackClicked" }
    SignalSpy { id: trackerSpy; signalName: "trackersClicked" }

    TestCase {
        name: "FeedbackShell"
        when: windowShown

        function test_panel_data() {
            return [{tag:"phone",w:360,h:740}, {tag:"tablet",w:800,h:1280}, {tag:"desktop",w:1200,h:800}]
        }
        function test_panel(data) {
            const panel = createTemporaryObject(panelComponent, parent, {width:data.w, height:data.h});
            verify(panel);
            const loader = findChild(panel, "feedbackBrowserLoader");
            tryCompare(loader, "status", Loader.Ready);
            verify(loader.item.pageUrl.toString().indexOf("https://getcolosseum.world/feedback/") === 0);
            const back = findChild(panel, "feedbackBack");
            verify(back.width >= 48); verify(back.height >= 48);
            closeSpy.target = panel; closeSpy.clear();
            back.focusControl(); tryCompare(back, "automationActiveFocus", true);
            keyClick(Qt.Key_Escape);
            compare(closeSpy.count, 1);
            loader.item.errorText = "offline"; wait(0); verify(panel.visible);

            if (data.tag === "desktop") {
                const minimize = findChild(panel, "feedbackWindowMinimize");
                const fullscreen = findChild(panel, "feedbackWindowFullscreen");
                const quit = findChild(panel, "feedbackWindowQuit");
                verify(minimize && fullscreen && quit,
                       "Feedback overlay exposes the shared minimize, fullscreen, and quit controls");
                minimizeSpy.target = panel; minimizeSpy.clear();
                mouseClick(minimize);
                compare(minimizeSpy.count, 1, "Minimize forwards to the panel root");
                fullscreenSpy.target = panel; fullscreenSpy.clear();
                mouseClick(fullscreen);
                compare(fullscreenSpy.count, 1, "Fullscreen forwards to the panel root");
                quitSpy.target = panel; quitSpy.clear();
                mouseClick(quit);
                compare(quitSpy.count, 1, "Quit forwards to the panel root");
            }
        }

        function test_topbar_restores_utility_actions_data() {
            return [{tag:"desktop",component:desktopTopBarHost},
                    {tag:"android",component:androidTopBarHost}]
        }
        function test_topbar_restores_utility_actions(data) {
            const shell = createTemporaryObject(data.component, parent, {width:1700, height:96});
            verify(shell);
            const bar = findChild(shell, "testTopBar");
            bar.activeMedium = "Theatre";
            bar.trackersEnabled = true;
            wait(0);
            const trackers = findChild(bar, "topBarTrackersButton_Theatre");
            const wallpaper = findChild(bar, "topBarWallpaperButton");
            verify(trackers && trackers.visible, "Trackers is available from the world top bar");
            verify(wallpaper && wallpaper.visible, "Wallpaper is back in the top bar");
            verify(findChild(bar, "feedbackEntry") === null, "Feedback is reached through the taskbar");
            trackerSpy.target = bar; trackerSpy.clear();
            bar.focusTrackersButton();
            keyClick(Qt.Key_Return);
            compare(trackerSpy.count, 1, "The restored Trackers control still opens its owner");
            bar.activeMedium = "";
            wait(0);
            verify(findChild(bar, "colosseumTopbarUpdateButton").visible, "Updates is back on Home");
        }

        function test_vault_header_uses_shared_taskbar_feedback() {
            const vault = createTemporaryObject(vaultComponent, parent, {width:1200, height:800});
            verify(vault);
            verify(findChild(vault, "vaultFeedbackEntry") === null, "Vault uses the same taskbar feedback entry");
        }

        function test_android_topbar_keeps_search_but_hides_desktop_window_controls() {
            const shell = createTemporaryObject(androidTopBarHost, parent, {width:1200, height:96});
            verify(shell);
            const bar = findChild(shell, "testTopBar");
            bar.activeMedium = "Theatre"; wait(0);
            const search = findChild(bar, "topBarSearch");
            verify(search && search.visible, "Search stays in the top bar on Android");
            const minimize = findAccessibleName(bar, "Minimize");
            const fullscreen = findAccessibleNameEither(bar, "Enter fullscreen", "Exit fullscreen");
            const quit = findAccessibleName(bar, "Quit Colosseum");
            verify(minimize && fullscreen && quit);
            compare(minimize.visible, false);
            compare(fullscreen.visible, false);
            compare(quit.visible, false);
        }

        function test_taskbar_feedback_is_icon_only_and_activates_data() {
            return [{tag:"desktop",component:desktopTaskbarHost,width:1200},
                    {tag:"desktop-compact",component:desktopTaskbarHost,width:904},
                    {tag:"android-tablet",component:androidTaskbarHost,width:800},
                    {tag:"android-phone",component:androidTaskbarHost,width:360}]
        }
        function test_taskbar_feedback_is_icon_only_and_activates(data) {
            const shell = createTemporaryObject(data.component, parent, {width:data.width, height:800});
            verify(shell);
            const bar = findChild(shell, "colosseumTaskbar");
            verify(bar);
            bar.open = true;
            wait(300);
            const action = findChild(bar, "feedbackEntry");
            verify(action && action.visible, "Feedback is available in the expanded taskbar");
            compare(action.accessibleName, "Send feedback");
            compare(action.parent.labelled, false, "Feedback stays icon-only");
            const icon = findSourceNode(action.parent, "feedback.svg");
            verify(icon && icon.visible);
            const label = findTextNode(action.parent, "Feedback");
            if (label) compare(label.visible, false);
            const position = action.mapToItem(bar, 0, 0);
            verify(position.x >= 0 && position.x + action.width <= bar.width + 1
                   && position.y >= 0 && position.y + action.height <= bar.height + 1,
                   "Feedback stays inside the taskbar at the supported width");
            feedbackSpy.target = bar; feedbackSpy.clear();
            mouseClick(action);
            compare(feedbackSpy.count, 1, "Clicking feedback opens it exactly once");
            compare(bar.open, false, "Opening feedback collapses the dock");
            bar.open = true;
            wait(300);
            action.forceActiveFocus(Qt.TabFocusReason);
            keyClick(Qt.Key_Return);
            compare(feedbackSpy.count, 2, "Feedback remains keyboard accessible");
            compare(bar.open, false);
            verify(findChild(bar, "taskbarUtilities") === null, "Top bar utilities do not remain duplicated in the dock");
        }

        function findChild(root, objectName) {
            if (!root) return null;
            if (root.objectName === objectName) return root;
            const kids = root.children || [];
            for (let i = 0; i < kids.length; ++i) {
                const found = findChild(kids[i], objectName);
                if (found) return found;
            }
            return null;
        }
        function findTextNode(root, text) {
            if (!root) return null;
            if (root.text !== undefined && root.text === text) return root;
            const kids = root.children || [];
            for (let i = 0; i < kids.length; ++i) {
                const found = findTextNode(kids[i], text);
                if (found) return found;
            }
            return null;
        }
        function findSourceNode(root, sourcePart) {
            if (!root) return null;
            if (root.source !== undefined && String(root.source).indexOf(sourcePart) >= 0) return root;
            const kids = root.children || [];
            for (let i = 0; i < kids.length; ++i) {
                const found = findSourceNode(kids[i], sourcePart);
                if (found) return found;
            }
            return null;
        }
        function findAccessibleName(root, name) {
            if (!root) return null;
            if (root.accessibleName === name) return root;
            const kids = root.children || [];
            for (let i = 0; i < kids.length; ++i) {
                const found = findAccessibleName(kids[i], name);
                if (found) return found;
            }
            return null;
        }
        function findAccessibleNameEither(root, first, second) {
            return findAccessibleName(root, first) || findAccessibleName(root, second);
        }
    }
}
