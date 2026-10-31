import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneBookRows"
    when: windowShown

    Window {
        id: testWindow
        width: 354
        height: 792
        visible: true
        color: "#080a10"

        App.BiblioBook {
            id: bookPage
            anchors.fill: parent
        }
    }

    readonly property string longSize: "SIZE-FIXTURE-" + "1234567890".repeat(12)
    readonly property string longSource: "LONG-SOURCE-FIXTURE-" + "SOURCE0123456789".repeat(10)
    readonly property string longLanguage: "LANGUAGE-FIXTURE-" + "LANG0123456789".repeat(10)
    readonly property string longPosted: "POSTED-FIXTURE-" + "DATE0123456789".repeat(10)

    function init() {
        testWindow.width = 354
        testWindow.height = 792
        bookPage.androidHost = true
        bookPage.booksRef = null
        bookPage.bookTorrentsRef = null
        bookPage.collectionRef = null
        bookPage.progressRef = null
        bookPage.book = ({})
        bookPage.edLoading = false
        bookPage.torLoading = false
        bookPage.abLoading = false
        bookPage.editions = [{
            md5: "phone-edition-fixture",
            format: "epub",
            size: longSize,
            source: longSource,
            year: "2038-" + "YEAR0123456789".repeat(8),
            language: longLanguage,
            best: false
        }]
        bookPage.abRows = [{
            slug: "phone-audiobook-fixture",
            format: "audiobook",
            size: longSize,
            language: longLanguage,
            posted: longPosted
        }]
        wait(100)
    }

    function findChild(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findChild(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function rectIn(item, ancestor) {
        const point = item.mapToItem(ancestor, 0, 0)
        return { "x": point.x, "y": point.y, "width": item.width, "height": item.height }
    }

    function checkPhoneMetadataRow(metaName, indicatorName, label, expectedFormat) {
        const meta = findChild(bookPage, metaName)
        const indicator = findChild(bookPage, indicatorName)
        verify(meta !== null, label + " metadata Text should be instantiated")
        verify(indicator !== null, label + " download indicator should be instantiated")
        if (!(meta && indicator))
            return

        const row = indicator.parent
        const metaRect = rectIn(meta, row)
        const indicatorRect = rectIn(indicator, row)
        console.log("PHONE_BOOK_ROW " + label + " " + testWindow.width + "x"
                    + testWindow.height + " textFormat=" + meta.textFormat
                    + " width=" + meta.width + " painted=" + meta.paintedWidth
                    + " implicit=" + meta.implicitWidth + " truncated=" + meta.truncated
                    + " meta=" + JSON.stringify(metaRect)
                    + " indicator=" + JSON.stringify(indicatorRect))
        compare(meta.textFormat, expectedFormat,
                label + " phone metadata should use its expected safe text format")
        compare(meta.elide, Text.ElideRight,
                label + " phone metadata should elide at the reserved indicator gap")
        verify(meta.width > 0, label + " metadata should retain a positive layout width")
        verify(meta.paintedWidth <= meta.width + 0.5,
               label + " rendered metadata should fit within its bounded Text width")
        verify(meta.truncated,
               label + " fixture should exercise actual single-line metadata elision")
        verify(metaRect.x + meta.paintedWidth <= indicatorRect.x - 8,
               label + " rendered metadata should leave at least 8dp before the indicator; meta="
               + JSON.stringify(metaRect) + " paintedWidth=" + meta.paintedWidth
               + " indicator=" + JSON.stringify(indicatorRect))
    }

    function checkPhoneSize(width, height) {
        testWindow.width = width
        testWindow.height = height
        bookPage.androidHost = true
        wait(140)
        checkPhoneMetadataRow("biblioEditionMeta_0", "biblioEditionIndicator_0", "edition",
                              Text.PlainText)
        checkPhoneMetadataRow("biblioAudiobookMeta_0", "biblioAudiobookIndicator_0", "audiobook",
                              Text.AutoText)
    }

    function test_edition_and_audiobook_metadata_elide_inside_phone_rows() {
        checkPhoneSize(354, 792)
        checkPhoneSize(792, 354)
    }

    function test_desktop_edition_metadata_keeps_rich_formatting() {
        testWindow.width = 1280
        testWindow.height = 800
        bookPage.androidHost = false
        wait(160)
        const meta = findChild(bookPage, "biblioEditionMeta_0")
        verify(meta !== null, "desktop edition metadata Text should be instantiated")
        if (meta) {
            compare(meta.textFormat, Text.RichText,
                    "desktop edition metadata should retain rich-text formatting")
            compare(meta.elide, Text.ElideNone,
                    "desktop edition metadata should retain its non-elided presentation")
            verify(String(meta.text).indexOf("<b>") >= 0,
                   "desktop edition size should remain bold rich text")
        }
    }
}
