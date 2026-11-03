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
            App.Taskbar { anchors.fill: parent; backdrop: shellBackdrop }
        }
    }
    Component {
        id: androidTaskbarHost
        Item {
            Rectangle { id: shellBackdrop; anchors.fill: parent; color: "#08090d" }
            AndroidUi.Taskbar { anchors.fill: parent; backdrop: shellBackdrop }
        }
    }

    SignalSpy { id: closeSpy; signalName: "closeRequested" }
    SignalSpy { id: minimizeSpy; signalName: "minimizeRequested" }
    SignalSpy { id: fullscreenSpy; signalName: "fullscreenRequested" }
    SignalSpy { id: quitSpy; signalName: "quitRequested" }
    SignalSpy { id: feedbackSpy; signalName: "feedbackClicked" }
    SignalSpy { id: utilitySpy; signalName: "utilityTriggered" }

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

        function test_feedback_is_topbar_icon_only_at_all_widths_and_worlds_data() {
            return [{tag:"desktop",component:desktopTopBarHost},
                    {tag:"android",component:androidTopBarHost}]
        }
        function test_feedback_is_topbar_icon_only_at_all_widths_and_worlds(data) {
            const shell = createTemporaryObject(data.component, parent, {width:328, height:96});
            verify(shell);
            const bar = findChild(shell, "testTopBar");
            verify(bar);
            const widths = [328, 360, 800, 1200, 1700];
            const worlds = ["", "Tankoban", "Biblio", "Theatre", "Universes", "Vault", "Vinyl"];
            for (let wi = 0; wi < widths.length; ++wi) {
                shell.width = widths[wi];
                for (let mi = 0; mi < worlds.length; ++mi) {
                    bar.activeMedium = worlds[mi];
                    wait(0);
                    const action = findChild(bar, "feedbackEntry");
                    verify(action && action.visible,
                           "Feedback must stay visible at width " + widths[wi] + " in " + (worlds[mi] || "Home"));
                    compare(action.accessibleName, "Send feedback");
                    compare(action.parent.labelled, false, "Feedback must remain icon-only");
                    compare(action.parent.width, 44, "The Feedback entry must not expand into a text button");
                    compare(action.width, 44, "The Feedback hit target stays 44px wide");
                    compare(action.height, 44, "The Feedback hit target stays 44px tall");
                    const icon = findSourceNode(action.parent, "feedback.svg");
                    verify(icon && icon.visible, "The icon remains visible on the icon-only entry");
                    const label = findTextNode(action.parent, "Feedback");
                    if (label) compare(label.visible, false, "The text label must stay hidden at every width");
                    const position = action.mapToItem(bar, 0, 0);
                    verify(position.x >= 0 && position.x + action.width <= bar.width + 1
                           && position.y >= 0 && position.y + action.height <= bar.height + 1,
                           "Feedback must remain inside the top bar");
                    verify(findChild(bar, "topBarStremioButton") === null,
                           "Stremio must not return as a separate top bar button");
                }
            }
        }

        function test_feedback_click_emits_topbar_signal() {
            const shell = createTemporaryObject(desktopTopBarHost, parent, {width:1200, height:96});
            verify(shell);
            const bar = findChild(shell, "testTopBar");
            const action = findChild(bar, "feedbackEntry");
            verify(action && action.visible);
            feedbackSpy.target = bar; feedbackSpy.clear();
            mouseClick(action, action.width / 2, action.height / 2);
            compare(feedbackSpy.count, 1);
        }

        function test_vault_header_feedback_remains_icon_only_data() {
            return [{tag:"phone",width:360}, {tag:"tablet",width:800},
                    {tag:"desktop",width:1200}, {tag:"wide",width:1700}]
        }
        function test_vault_header_feedback_remains_icon_only(data) {
            const vault = createTemporaryObject(vaultComponent, parent, {width:data.width, height:800});
            verify(vault);
            const action = findChild(vault, "vaultFeedbackEntry");
            verify(action && action.visible, "Vault keeps Feedback in its persistent header");
            compare(action.accessibleName, "Send feedback");
            compare(action.parent.labelled, false);
            compare(action.parent.width, 44);
            compare(action.width, 44); compare(action.height, 44);
            const icon = findSourceNode(action.parent, "feedback.svg");
            verify(icon && icon.visible);
            const label = findTextNode(action.parent, "Feedback");
            if (label) compare(label.visible, false);
            const position = action.mapToItem(vault, 0, 0);
            verify(position.x >= 0 && position.x + action.width <= vault.width + 1
                   && position.y >= 0 && position.y + action.height <= vault.height + 1,
                   "Vault Feedback must stay within the header at every width");
            if (data.width <= 1200) {
                feedbackSpy.target = vault; feedbackSpy.clear();
                mouseClick(action, action.width / 2, action.height / 2);
                compare(feedbackSpy.count, 1);
            }
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

        function test_taskbar_has_no_feedback_and_keeps_tracker_utility_data() {
            return [{tag:"desktop",component:desktopTaskbarHost},
                    {tag:"android",component:androidTaskbarHost}]
        }
        function test_taskbar_has_no_feedback_and_keeps_tracker_utility(data) {
            const shell = createTemporaryObject(data.component, parent, {width:1200, height:800});
            verify(shell);
            const bar = findChild(shell, "colosseumTaskbar");
            verify(bar);
            verify(findChild(bar, "feedbackEntry") === null,
                   "Feedback belongs to the top bar, not the taskbar");
            bar.open = true; wait(0);
            const trackers = findChild(bar, "taskbarUtility_trackers");
            verify(trackers && trackers.visible);
            utilitySpy.target = bar; utilitySpy.clear();
            trackers.forceActiveFocus(Qt.OtherFocusReason);
            tryCompare(trackers, "activeFocus", true);
            keyClick(Qt.Key_Return);
            compare(utilitySpy.count, 1);
            compare(utilitySpy.signalArguments[0][0], "trackers");
        }

        function test_utilities_stay_inline_and_focus_scrolls_data() {
            return [{tag:"desktop-phone",component:desktopTaskbarHost,width:360},
                    {tag:"desktop-tablet",component:desktopTaskbarHost,width:800},
                    {tag:"desktop-wide",component:desktopTaskbarHost,width:1200},
                    {tag:"android-phone",component:androidTaskbarHost,width:360},
                    {tag:"android-tablet",component:androidTaskbarHost,width:800},
                    {tag:"android-wide",component:androidTaskbarHost,width:1200}]
        }
        function test_utilities_stay_inline_and_focus_scrolls(data) {
            const shell = createTemporaryObject(data.component, parent, {width:data.width,height:800});
            verify(shell);
            const bar = findChild(shell, "colosseumTaskbar");
            bar.open = true; wait(300);
            const dock = findChild(bar, "taskbarDock");
            const scroll = findChild(bar, "taskbarDockScroll");
            const row = findChild(bar, "taskbarDockRow");
            const utilities = findChild(bar, "taskbarUtilities");
            verify(dock && scroll && row && utilities);
            compare(utilities.parent, row, "Utilities use the existing dock row");
            for (const key of ["trackers", "updates", "wallpaper"]) {
                const action = findChild(utilities, "taskbarUtility_" + key);
                verify(action && action.visible);
                const point = action.mapToItem(dock, 0, 0);
                verify(point.y >= 0 && point.y + action.height <= dock.height,
                       "Utility stays inside dock height");
                action.forceActiveFocus(Qt.TabFocusReason);
                tryCompare(action, "activeFocus", true);
                tryVerify(function() {
                    const visiblePoint = action.mapToItem(scroll, 0, 0);
                    return visiblePoint.x >= -1 && visiblePoint.x + action.width <= scroll.width + 1;
                }, 1000, "Focused utility is reachable inside dock viewport");
            }
            bar.open = false; wait(300);
            compare(scroll.contentX, 0);
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
