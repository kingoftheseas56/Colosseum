import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../qml" as App
import "../../qml/account" as Account
import "../../qml/collections" as Collections
import "../../qml/UiTypography.js" as Typography

TestCase {
    id: suite
    name: "AndroidInterfaceTypography"
    when: windowShown
    visible: true
    width: 1185
    height: 533

    Component { id: themeFactory; App.Theme {} }
    Component { id: fieldFactory; Account.AccountField { width: 320; label: "Account name"; hint: "A longer explanation that wraps on a narrow form"; placeholderText: "Enter your account name" } }
    Component { id: headerFactory; App.WidgetHeader { width: 420; title: "Continue reading a very long title that must yield to the action"; moreLabel: "See all" } }
    Component { id: buttonFactory; Account.AccountButton { text: "Continue with this account" } }
    Component { id: searchFactory; Collections.CollectionSearchField { width: 300; placeholderText: "Search your collections"; text: "Long collection name" } }
    Component { id: cardFactory; App.CataloguePosterCard { visualProfile: "gallery"; item: ({ title: "A long book title that spans more than one line", author: "Long author name" }); showAuthorAtRest: true } }
    Component { id: legacyRoomFactory; App.MangaReadingRoomThumbnailMock { width: 1185; height: 533; seriesTitle: "Legacy manga"; author: "Author evidence"; status: "Ongoing evidence"; year: 2026 } }
    Component {
        id: defaultButtonFactory
        Button {
            id: button
            property var platformDefaultFont: Qt.application.font
            property alias typography: policy
            text: "Native default button"
            App.Theme { id: policy; androidHost: false }
            Binding {
                target: button
                property: policy.defaultFontSizeProperty(button.platformDefaultFont)
                when: policy.androidHost
                value: policy.defaultFontSize(button.platformDefaultFont)
            }
        }
    }

    function objects(root, seen) {
        if (!root || seen.indexOf(root) !== -1) return seen
        seen.push(root)
        const children = root.children || []
        const resources = root.resources || []
        for (let i = 0; i < children.length; ++i) objects(children[i], seen)
        for (let j = 0; j < resources.length; ++j) objects(resources[j], seen)
        if (root.contentItem) objects(root.contentItem, seen)
        return seen
    }
    function useAndroid(root, systemScale) {
        const all = objects(root, [])
        let owners = 0
        for (let i = 0; i < all.length; ++i) {
            if (typeof all[i].fontSize === "function" && all[i].fontScale !== undefined) {
                all[i].androidHost = true
                all[i].fontScale = systemScale === undefined ? 1 : systemScale
                ++owners
            }
        }
        verify(owners > 0, "component exposes its real Theme owner")
    }
    function textObjects(root) {
        return objects(root, []).filter(o => o.font !== undefined && o.text !== undefined
                                       && o.contentHeight !== undefined)
    }

    function test_policy_composes_once_and_keeps_desktop_identity() {
        compare(Typography.fontSize(13, true, 1), 16)
        compare(Typography.fontSize(13, true, 1.3), 20)
        compare(Typography.fontSize(13, true, 2), 31)
        compare(Typography.fontSize(13.25, false, 2), 13.25)
        compare(Typography.fontSize(13, true, NaN), 16)
        compare(Typography.fontSize(13, true, 0), 16)
        const theme = createTemporaryObject(themeFactory, suite, { androidHost: false })
        compare(theme.fontSize(13.25), 13.25)
        theme.androidHost = true
        compare(theme.fontSize(13), 16)
        theme.fontScale = 1.3
        compare(theme.fontSize(13), 20)
    }

    function test_actual_component_typography_data() {
        const components = [
            { tag: "account field", factory: fieldFactory },
            { tag: "widget header", factory: headerFactory },
            { tag: "account button", factory: buttonFactory },
            { tag: "collection input", factory: searchFactory },
            { tag: "shared catalogue card", factory: cardFactory }
        ]
        const rows = []
        for (let i = 0; i < components.length; ++i) {
            for (let j = 0; j < 2; ++j) {
                const scale = j === 0 ? 1 : 1.3
                rows.push({ tag: components[i].tag + " system " + scale,
                            factory: components[i].factory, systemScale: scale })
            }
        }
        return rows
    }
    function test_actual_component_typography(data) {
        const item = createTemporaryObject(data.factory, suite)
        verify(item !== null)
        wait(10)
        const text = textObjects(item)
        const desktopSizes = text.map(t => t.font.pixelSize)
        verify(text.length > 0)
        useAndroid(item, data.systemScale)
        wait(10)
        for (let i = 0; i < text.length; ++i) {
            // Symbol glyphs can retain their own icon size; ordinary labels grow.
            if (!/[A-Za-z]/.test(String(text[i].text))) continue
            compare(text[i].font.pixelSize, Math.round(desktopSizes[i] * 1.20 * data.systemScale), String(text[i].text))
        }
    }

    function test_long_header_yields_and_field_contains_input() {
        const header = createTemporaryObject(headerFactory, suite)
        const field = createTemporaryObject(fieldFactory, suite)
        useAndroid(header, 1.3)
        useAndroid(field, 1.3)
        field.text = "Long account name entered by the user"
        wait(20)
        const labels = textObjects(header)
        const title = labels.find(t => t.text === header.title)
        const more = labels.find(t => t.text === "See all")
        verify(title !== undefined && more !== undefined)
        const titlePosition = title.mapToItem(header, 0, 0)
        const morePosition = more.mapToItem(header, 0, 0)
        verify(titlePosition.x + title.width <= morePosition.x,
               "title right=" + (titlePosition.x + title.width) + " action left=" + morePosition.x)
        verify(title.truncated)
        const input = textObjects(field).find(t => t.text === field.text)
        verify(input !== undefined)
        verify(input.height >= input.contentHeight)
        const p = input.mapToItem(field, 0, 0)
        verify(p.y >= 0 && p.y + input.height <= field.height + 0.5)
    }

    function test_collection_input_retains_padding_with_larger_text() {
        const input = createTemporaryObject(searchFactory, suite)
        useAndroid(input, 1.3)
        wait(20)
        verify(input.contentHeight + input.topPadding + input.bottomPadding <= input.height,
               "input text and padding fit inside control")
    }

    function test_default_font_unit_and_desktop_restoration_data() {
        return [
            { tag: "pixels", baseFont: ({ pixelSize: 13, pointSize: -1 }), property: "font.pixelSize", expected: 16 },
            { tag: "points", baseFont: ({ pixelSize: -1, pointSize: 10 }), property: "font.pointSize", expected: 12 }
        ]
    }
    function test_default_font_unit_and_desktop_restoration(data) {
        const button = createTemporaryObject(defaultButtonFactory, suite)
        const desktopPixels = button.font.pixelSize
        const desktopPoints = button.font.pointSize
        button.platformDefaultFont = data.baseFont
        compare(button.typography.defaultFontSizeProperty(data.baseFont), data.property)
        compare(button.font.pixelSize, desktopPixels, "inactive binding preserves native default")
        button.typography.fontScale = 2
        button.typography.androidHost = true
        // A native default already contains the system preference; apply interface scale once.
        compare(data.property === "font.pixelSize" ? button.font.pixelSize : button.font.pointSize, data.expected)
        button.typography.androidHost = false
        compare(button.font.pixelSize, desktopPixels)
        compare(button.font.pointSize, desktopPoints)
    }

    function test_live_legacy_metadata_uses_its_native_default_once() {
        const room = createTemporaryObject(legacyRoomFactory, suite)
        verify(room !== null)
        const text = textObjects(room)
        const status = text.find(t => t.text === room.status)
        const author = text.find(t => t.text === room.author)
        verify(status !== undefined && author !== undefined)
        const defaultBase = status.font.pixelSize
        const explicitBase = author.font.pixelSize
        useAndroid(room, 1.3)
        wait(20)
        compare(status.font.pixelSize, Math.round(defaultBase * 1.20))
        compare(author.font.pixelSize, Math.round(explicitBase * 1.20 * 1.3))
    }
}
