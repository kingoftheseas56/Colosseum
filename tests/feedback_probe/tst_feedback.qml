import QtQuick
import QtTest
import "../../qml" as App
import "../../qml/+android" as AndroidUi

Item {
    width: 1200; height: 1280
    Rectangle { id: feedbackBackdrop; anchors.fill: parent; color: "#08090d" }

    Component { id: panelComponent; App.FeedbackPanel { browserSource: Qt.resolvedUrl("MockBrowser.qml") } }
    Component {
        id: coveredShellComponent
        Item {
            property int underlyingClicks: 0
            property int underlyingWheels: 0
            MouseArea {
                objectName: "underlyingInput"
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.AllButtons
                onClicked: parent.underlyingClicks++
                onWheel: parent.underlyingWheels++
            }
            App.FeedbackPanel {
                objectName: "coveredFeedbackPanel"
                anchors.fill: parent
                browserSource: Qt.resolvedUrl("MockBrowser.qml")
            }
        }
    }
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

        function test_panel_blocks_underlying_pointer_input() {
            const shell = createTemporaryObject(coveredShellComponent, parent, {width:1200, height:800});
            verify(shell);
            const panel = findChild(shell, "coveredFeedbackPanel");
            const loader = findChild(panel, "feedbackBrowserLoader");
            tryCompare(loader, "status", Loader.Ready);
            const label = findTextNode(panel, "Feedback");
            verify(label);
            const header = label.mapToItem(panel, label.width / 2, label.height / 2);
            for (const button of [Qt.LeftButton, Qt.RightButton, Qt.MiddleButton]) {
                mouseClick(panel, header.x, header.y, button);
                compare(shell.underlyingClicks, 0, "Unused feedback header must block the underlying world buttons");
                mouseClick(panel, panel.width / 2, 8, button);
                mouseClick(panel, panel.width / 2, panel.height - 8, button);
                compare(shell.underlyingClicks, 0, "Empty panel space must block underlying input");
            }
            mouseMove(panel, header.x, header.y);
            compare(findChild(shell, "underlyingInput").containsMouse, false);
            mouseWheel(panel, header.x, header.y, 0, 120);
            compare(shell.underlyingWheels, 0, "Wheel input must not scroll the covered world");

            closeSpy.target = panel; closeSpy.clear();
            mouseClick(findChild(panel, "feedbackBack"));
            compare(closeSpy.count, 1, "The foreground Back control remains clickable");
            const firstBrowser = loader.item;
            mouseClick(findChild(panel, "feedbackRefresh"));
            tryVerify(function() { return loader.status === Loader.Ready && loader.item !== firstBrowser }, 1000,
                      "The foreground Refresh control still reloads the mock browser");
            compare(shell.underlyingClicks, 0);
        }

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

        function test_inline_taskbar_utilities_align_inside_wide_dock_data() {
            return [{tag:"desktop-wide",component:desktopTaskbarHost,width:1920},
                    {tag:"android-wide",component:androidTaskbarHost,width:1200}]
        }
        function test_inline_taskbar_utilities_align_inside_wide_dock(data) {
            const shell = createTemporaryObject(data.component, parent, {width:data.width, height:800});
            verify(shell);
            const bar = findChild(shell, "colosseumTaskbar");
            const dock = findChild(bar, "taskbarDock");
            const scroll = findChild(bar, "taskbarDockScroll");
            const row = findChild(bar, "taskbarDockRow");
            const utilities = findChild(bar, "taskbarUtilities");
            verify(dock && scroll && row && utilities);
            bar.open = true; wait(300);
            tryCompare(utilities, "visible", true);
            compare(utilities.parent, row, "Utilities are direct children of the dock row");
            verify(isDescendantOf(row, scroll), "The dock row lives inside the horizontal viewport");
            compare(scroll.parent, dock, "The viewport is clipped by the expanded dock");

            const anchor = data.tag === "desktop-wide"
                ? findChild(bar, "taskbarHistoryStats")
                : findChild(bar, "colosseumTaskbarHomeButton");
            verify(anchor && anchor.visible, "Use a real neighboring dock icon as the alignment anchor");
            const anchorRect = anchor.mapToItem(row, 0, 0);
            const actionKeys = ["trackers", "updates", "wallpaper", "sharing"];
            let actionCount = 0;
            for (let i = 0; i < actionKeys.length; ++i) {
                const action = findChild(bar, "taskbarUtility_" + actionKeys[i]);
                if (!action) continue;
                ++actionCount;
                const cell = action.parent;
                compare(cell.width, 46); compare(cell.height, 46);
                compare(action.width, 46); compare(action.height, 46);
                const iconRect = action.mapToItem(row, 0, 0);
                verify(Math.abs((iconRect.y + action.height / 2)
                                - (anchorRect.y + anchor.height / 2)) <= 1,
                       actionKeys[i] + " must share the dock icon centerline");
                const viewportRect = action.mapToItem(scroll, 0, 0);
                verify(viewportRect.x >= 0 && viewportRect.x + action.width <= scroll.width + 1
                       && viewportRect.y >= 0 && viewportRect.y + action.height <= scroll.height + 1,
                       actionKeys[i] + " must be visible inside the wide dock viewport");
                compare(countTextDescendants(cell), 0,
                        actionKeys[i] + " has no visible text label; its name is available through Tooltip/accessibility");
            }
            verify(actionCount >= 3 && actionCount <= 4,
                   "The inline dock contains Trackers, Updates, Wallpaper, and optional Wi-Fi sharing");
            verify(utilities.x >= anchor.x + anchor.width,
                   "The utilities row follows the existing dock actions");
            compare(scroll.interactive, false, "A wide dock does not need horizontal scrolling");
        }

        function test_narrow_desktop_dock_flick_keyboard_and_collapse() {
            const shell = createTemporaryObject(desktopTaskbarHost, parent, {width:900, height:800});
            verify(shell);
            const bar = findChild(shell, "colosseumTaskbar");
            const dock = findChild(bar, "taskbarDock");
            const scroll = findChild(bar, "taskbarDockScroll");
            const row = findChild(bar, "taskbarDockRow");
            bar.groups = makeTaskbarGroups(24);
            bar.open = true; wait(300);
            verify(scroll.contentWidth > scroll.width, "A 900px dock with open sessions overflows its viewport");
            compare(scroll.interactive, true);

            scroll.contentX = 0;
            const dragY = scroll.height / 2;
            dragFlickableLeft(scroll, scroll.width * 0.8, dragY, scroll.width * 0.2, dragY, 8);
            tryVerify(function() { return scroll.contentX > 0 }, 1000,
                      "A horizontal flick exposes content beyond the left edge");
            scroll.cancelFlick();

            const trackers = findChild(bar, "taskbarUtility_trackers");
            verify(trackers && trackers.visible);
            scroll.contentX = Math.max(0, scroll.contentWidth - scroll.width);
            wait(0);
            utilitySpy.target = bar; utilitySpy.clear();
            bar.focusUtilityAction("trackers");
            tryCompare(trackers, "activeFocus", true);
            tryVerify(function() {
                const position = trackers.mapToItem(scroll, 0, 0);
                return position.x >= 0 && position.x + trackers.width <= scroll.width + 1;
            }, 1000, "Keyboard focus scrolls the utility action into view");
            keyClick(Qt.Key_Return);
            compare(utilitySpy.count, 1);
            compare(utilitySpy.signalArguments[0][0], "trackers");
            tryCompare(bar, "open", false);
            tryCompare(scroll, "contentX", 0);
            tryCompare(dock, "width", bar.closedWidth, 1000);
            compare(scroll.interactive, false, "Closing the dock disables horizontal scrolling");

            const closedNames = ["colosseumTaskbarHomeButton", "taskbarVaultDoor", "taskbarCollections"];
            for (let i = 0; i < closedNames.length; ++i) {
                const control = findChild(bar, closedNames[i]);
                verify(control && control.visible, closedNames[i] + " stays visible in the closed dock");
                const position = control.mapToItem(scroll, 0, 0);
                verify(position.x >= 0 && position.x + control.width <= scroll.width + 1,
                       closedNames[i] + " remains unclipped when the dock collapses");
            }
            compare(row.objectName, "taskbarDockRow");
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
        function isDescendantOf(item, ancestor) {
            for (let current = item; current; current = current.parent) {
                if (current === ancestor) return true;
            }
            return false;
        }
        function countTextDescendants(root) {
            if (!root) return 0;
            let count = root.text !== undefined ? 1 : 0;
            const kids = root.children || [];
            for (let i = 0; i < kids.length; ++i) count += countTextDescendants(kids[i]);
            return count;
        }
        function dragFlickableLeft(target, x0, y0, x1, y1, steps) {
            mousePress(target, x0, y0);
            for (let i = 1; i <= steps; ++i) {
                mouseMove(target, x0 + (x1 - x0) * i / steps,
                          y0 + (y1 - y0) * i / steps, -1);
            }
            mouseRelease(target, x1, y1);
        }
        function makeTaskbarGroups(count) {
            const groups = [];
            for (let i = 0; i < count; ++i) {
                groups.push({appType:"tankoban", sessions:[{id:"inline-" + i, title:"Session " + i}]});
            }
            return groups;
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
