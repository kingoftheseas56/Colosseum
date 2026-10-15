import QtQuick
import QtQuick.Controls
import QtTest

TestCase {
    name: "VinylArtistScrollbars"
    when: windowShown
    property var host

    function init() {
        var component = Qt.createComponent("../../qml/vinyl/ArtistPreview.qml");
        verify(component.status === Component.Ready, component.errorString());
        host = component.createObject(null);
        verify(host !== null);
        host.pageData.frozen = true;
        tryCompare(host, "ready", true);
        host.artistData.open("Eminem");
    }

    function cleanup() {
        if (host) {
            host.close();
            host.destroy();
            host = null;
        }
    }

    function assertNoVisibleScrollbars(node) {
        if (!node || !node.visible) return;
        if (node instanceof ScrollBar)
            verify(false, "Visible scrollbar on artist destination: " + node);
        var children = node.children || [];
        for (var i = 0; i < children.length; ++i)
            assertNoVisibleScrollbars(children[i]);
    }

    function test_scrollbarsHiddenAndKeyboardScrolls() {
        var page = host.artistScroll;
        wait(50);
        verify(page.contentHeight > page.height);
        assertNoVisibleScrollbars(page);
        page.contentY = 0;
        page.forceActiveFocus();
        keyClick(Qt.Key_PageDown);
        verify(page.contentY > 0, "Keyboard scrolling must remain enabled");
    }

    function test_tabAndPopupScrollbarsRemainHidden() {
        var tabs = ["albums", "singles", "videos", "live"];
        for (var i = 0; i < tabs.length; ++i) {
            findChild(host.contentItem, "vinylArtistTab_" + tabs[i]).activate();
            wait(0);
            assertNoVisibleScrollbars(host.contentItem);
        }
        findChild(host.contentItem, "vinylArtistTab_albums").activate();
        findChild(host.contentItem, "vinylArtistReleaseMenuButton").activate();
        wait(0);
        var menu = findChild(host, "vinylArtistReleaseMenu");
        verify(menu.visible);
        assertNoVisibleScrollbars(menu.contentItem);
        host.artistScroll.closeReleaseMenu(false);
    }
}
