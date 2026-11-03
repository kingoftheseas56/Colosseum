import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App
import "../../qml/account" as Account

// WT-04: Theme consumes the real PlatformRuntime fontScale notification (Android kind);
// shared Back, widget headers and account form/button/header typography grow with it and
// their hosts grow with the text; the desktop runtime and scale 1 keep exact desktop sizes.
// The density/visual-zoom factor is a separate whole-Qt scale and is not applied here.
TestCase {
    id: testCase
    name: "AndroidTextScaling"
    when: windowShown

    Window { id: testWindow; width: 1185; height: 533; visible: true }

    Component { id: backComponent; App.BackAction { label: "Back to Theatre" } }
    Component { id: headerComponent; App.WidgetHeader { width: 520; title: "Continue watching for a very long translated title"; moreLabel: "See all" } }
    Component { id: fieldComponent; Account.AccountField { width: 400; label: "Username"; hint: "Letters and numbers"; placeholderText: "Your username" } }
    Component { id: buttonComponent; Account.AccountButton { text: "Continue" } }
    Component { id: panelComponent; Account.AccountPanelHeader { width: 320; kicker: "ACCOUNT"; title: "Welcome back to Colosseum"; copy: "Sign in to sync your library." } }
    Component { id: themeComponent; App.Theme {} }

    function init() {
        FontTestFixture.useDesktopRuntime(false)
        FontTestFixture.setScale(1)
    }
    function cleanupTestCase() { FontTestFixture.setScale(1) }

    function texts(root, out) {
        const kids = root.children || []
        for (let i = 0; i < kids.length; ++i) {
            if (kids[i].font !== undefined && kids[i].text !== undefined && kids[i].contentHeight !== undefined)
                out.push(kids[i])
            texts(kids[i], out)
        }
        if (root.contentItem && root.contentItem.font !== undefined) out.push(root.contentItem)
        return out
    }

    function test_theme_follows_native_notification_dynamically() {
        const theme = themeComponent.createObject(testCase)
        compare(theme.fontScale, 1)
        compare(theme.fontSize(13), 13)
        FontTestFixture.setScale(1.3)
        compare(theme.fontScale, 1.3)
        compare(theme.fontSize(13), 17)
        FontTestFixture.setScale(2)
        compare(theme.fontSize(13), 26)
        // Desktop runtime: the preference never applies.
        FontTestFixture.useDesktopRuntime(true)
        FontTestFixture.setDesktopScale(2)
        const desktopTheme = themeComponent.createObject(testCase)
        compare(desktopTheme.fontScale, 1)
        theme.destroy(); desktopTheme.destroy()
    }

    function test_common_controls_scale_and_contain_text_data() {
        return [{ tag: "1.0", scale: 1.0 }, { tag: "1.3", scale: 1.3 }, { tag: "2.0", scale: 2.0 }]
    }
    function test_common_controls_scale_and_contain_text(data) {
        const comps = [backComponent, headerComponent, fieldComponent, buttonComponent, panelComponent]
        const base = []
        for (let c = 0; c < comps.length; ++c) {
            const item = createTemporaryObject(comps[c], testWindow.contentItem)
            verify(item !== null)
            base.push({ item: item, sizes: texts(item, []).map(t => t.font.pixelSize), height: item.height })
        }
        FontTestFixture.setScale(data.scale)
        wait(20)
        for (let c = 0; c < base.length; ++c) {
            const item = base[c].item
            const now = texts(item, [])
            for (let i = 0; i < now.length; ++i) {
                compare(now[i].font.pixelSize, Math.round(base[c].sizes[i] * data.scale),
                        "component " + c + " text " + i)
                if (!now[i].visible || now[i].text.length === 0) continue
                // Text stays inside its host vertically (no clipping under fixed heights).
                const p = now[i].mapToItem(item, 0, 0)
                verify(p.y >= -0.5 && p.y + Math.min(now[i].height, now[i].contentHeight) <= item.height + 0.5,
                       "component " + c + " text " + i + " inside host at " + data.scale
                       + " y=" + p.y + " h=" + now[i].contentHeight + " host=" + item.height)
            }
            if (data.scale > 1) verify(item.height >= base[c].height)
        }
    }

    function test_widget_header_title_elides_before_more_action() {
        FontTestFixture.setScale(2)
        const header = createTemporaryObject(headerComponent, testWindow.contentItem)
        wait(20)
        let title = null, more = null
        const all = texts(header, [])
        for (let i = 0; i < all.length; ++i) {
            if (all[i].text === header.title) title = all[i]
            if (all[i].text === "See all") more = all[i]
        }
        verify(title !== null && more !== null)
        const t = title.mapToItem(header, 0, 0), m = more.mapToItem(header, 0, 0)
        verify(t.x + title.width <= m.x, "title ends before the more action")
        verify(title.truncated)
    }

    function test_scale_one_is_desktop_identical() {
        FontTestFixture.useDesktopRuntime(true)
        const back = createTemporaryObject(backComponent, testWindow.contentItem)
        compare(back.height, 34)
        const field = createTemporaryObject(fieldComponent, testWindow.contentItem)
        const button = createTemporaryObject(buttonComponent, testWindow.contentItem)
        compare(button.implicitHeight, 46)
        const header = createTemporaryObject(headerComponent, testWindow.contentItem)
        compare(header.implicitHeight, 30)
    }
}
