import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "TankoyomiConfigurationPage"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 900
        visible: true
    }

    Component {
        id: fakeMangaComponent
        QtObject {
            signal chapterConfigurationChanged()
            property string chapterDefaultLanguage: "es"
            property var languageRows: [
                { code: "en", label: "English", providerCount: 1, enabledProviderCount: 1, default: false },
                { code: "es", label: "Español", providerCount: 2, enabledProviderCount: 2, default: true }
            ]
            property var providerRows: ({
                en: [ { id: "en-one", name: "English One", enabled: true, rank: 0 } ],
                es: [
                    { id: "es-one", name: "Español One", enabled: true, rank: 0 },
                    { id: "es-two", name: "Español Two", enabled: false, rank: 1 }
                ]
            })
            property var calls: []
            function chapterLanguages() { return languageRows }
            function chapterProviders(language) { return providerRows[language] || [] }
            function setChapterDefaultLanguage(language) {
                calls.push("default:" + language)
                chapterDefaultLanguage = language
                return true
            }
            function setChapterProviderEnabled(language, providerId, enabled) {
                calls.push("enabled:" + language + ":" + providerId + ":" + enabled)
                return true
            }
            function moveChapterProviderUp(language, providerId) {
                calls.push("up:" + language + ":" + providerId)
                return true
            }
            function moveChapterProviderDown(language, providerId) {
                calls.push("down:" + language + ":" + providerId)
                return true
            }
            function resetChapterProviderOrder(language) {
                calls.push("reset:" + language)
                return true
            }
        }
    }

    Component {
        id: fakeExtensionsComponent
        QtObject {
            property int revision: 1
            signal changed()
            property var rows: [
                { id: "colosseum.well.tankoyomi", enabled: false,
                  manifest: { name: "Tankoyomi", behaviorHints: { configurable: true } } }
            ]
            property var calls: []
            function installed() { return rows }
            function setEnabled(id, value) { calls.push(id + ":" + value); rows[0].enabled = value; revision++; changed() }
        }
    }

    Component {
        id: pageComponent
        App.TankoyomiConfigurationPage {}
    }

    property var manga: null
    property var extensions: null
    property var page: null
    property int backCount: 0

    function byName(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        var children = root.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = byName(children[i], name)
            if (found) return found
        }
        return null
    }

    function init() {
        manga = fakeMangaComponent.createObject(testWindow)
        extensions = fakeExtensionsComponent.createObject(testWindow)
        page = pageComponent.createObject(testWindow.contentItem, {
            width: 1280, height: 900, mangaRef: manga, extensionsRef: extensions
        })
        verify(page !== null)
        backCount = 0
        page.backRequested.connect(function() { backCount++ })
        wait(0)
    }

    function cleanup() {
        if (page) page.destroy()
        if (manga) manga.destroy()
        if (extensions) extensions.destroy()
        page = null
        manga = null
        extensions = null
        backCount = 0
    }

    function test_initial_state_uses_persisted_default_and_truthful_master() {
        compare(page.selectedLanguage, "es")
        compare(page.activeTab, "configuration")
        compare(page.tankoyomiEnabled, false)
        verify(byName(page, "tankoyomiMasterSwitch") !== null)
        verify(byName(page, "tankoyomiLanguageList") !== null)
        verify(byName(page, "tankoyomiProviderLadder") !== null)
    }

    function test_provider_actions_delegate_to_native_policy() {
        verify(page.toggleProvider("es", "es-two", true))
        verify(page.moveProviderUp("es", "es-two"))
        verify(page.moveProviderDown("es", "es-one"))
        verify(page.resetProviderOrder("es"))
        verify(page.setDefaultLanguage("en"))
        compare(manga.calls.join("|"),
                "enabled:es:es-two:true|up:es:es-two|down:es:es-one|reset:es|default:en")
    }

    function test_master_focus_and_back_actions_are_reachable() {
        verify(page.setMasterEnabled(true))
        compare(extensions.calls.join("|"), "colosseum.well.tankoyomi:true")
        compare(page.tankoyomiEnabled, true)

        page.takeKeyboardFocus()
        verify(byName(page, "tankoyomiConfigurationScroll").activeFocusOnTab)

        page.closeFromPage()
        compare(backCount, 1)
    }

    function test_about_tab_is_reachable_without_losing_model() {
        page.activeTab = "about"
        compare(page.activeTab, "about")
        compare(page.selectedLanguage, "es")
        page.activeTab = "configuration"
        compare(page.activeTab, "configuration")
    }

    function test_minimum_supported_width_keeps_layout_inside_scroll() {
        page.width = 1024
        page.height = 640
        wait(0)
        var scroll = byName(page, "tankoyomiConfigurationScroll")
        var languages = byName(page, "tankoyomiLanguagePanel")
        var providers = byName(page, "tankoyomiProviderPanel")
        var logo = byName(page, "tankoyomiLogoImage")
        verify(scroll !== null)
        verify(languages !== null && providers !== null)
        verify(providers.x + providers.width <= scroll.width + 1)
        verify(scroll.contentHeight > scroll.height)
        verify(logo !== null && String(logo.source).indexOf("tankoyomi.png") >= 0)
    }

    function test_pip_width_stacks_panels_without_negative_geometry() {
        page.width = 360
        page.height = 240
        wait(0)
        var scroll = byName(page, "tankoyomiConfigurationScroll")
        var languages = byName(page, "tankoyomiLanguagePanel")
        var providers = byName(page, "tankoyomiProviderPanel")
        var info = byName(page, "tankoyomiProviderInfo_es-one")
        var providerName = byName(page, "tankoyomiProviderName_es-one")
        var header = byName(page, "tankoyomiConfigurationHeader")
        var breadcrumb = byName(page, "tankoyomiBreadcrumb")
        var title = byName(page, "tankoyomiTitle")
        var master = byName(page, "tankoyomiMasterSwitch")
        verify(languages.width === scroll.width)
        verify(providers.width === scroll.width)
        verify(providers.y > languages.y + languages.height)
        verify(info.width >= 0)
        verify(providerName !== null && providerName.width >= 0 && isFinite(providerName.width))
        verify(providers.x + providers.width <= scroll.width + 1)
        verify(header !== null && master !== null)
        verify(breadcrumb !== null && breadcrumb.width <= header.width + 1)
        var breadcrumbPos = breadcrumb.mapToItem(header, 0, 0)
        verify(breadcrumbPos.x >= -1 && breadcrumbPos.x + breadcrumb.width <= header.width + 1)
        verify(title !== null && !title.truncated)
        var titlePos = title.mapToItem(header, 0, 0)
        verify(titlePos.x >= -1 && titlePos.x + title.width <= header.width + 1)
        var masterPos = master.mapToItem(scroll, 0, 0)
        verify(masterPos.x >= -1 && masterPos.x + master.width <= scroll.width + 1)
    }
}
