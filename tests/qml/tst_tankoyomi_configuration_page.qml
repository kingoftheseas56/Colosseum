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
        property bool androidHost: false
    }

    Component {
        id: fakeMangaComponent
        QtObject {
            signal chapterConfigurationChanged()
            property string chapterDefaultLanguage: "es"
            property var languageRows: [
                { code: "en", label: "English", countryCode: "GB", providerCount: 1, enabledProviderCount: 1, default: false },
                { code: "es", label: "Español", countryCode: "ES", providerCount: 2, enabledProviderCount: 2, default: true },
                { code: "pt", label: "Português (Brasil)", countryCode: "BR", providerCount: 1, enabledProviderCount: 1, default: false },
                { code: "fr", label: "Français", countryCode: "FR", providerCount: 1, enabledProviderCount: 1, default: false }
            ]
            property var providerRows: ({
                en: [ { id: "en-one", name: "WeebCentral", allowedHosts: ["weebcentral.com"], enabled: true, rank: 0 } ],
                es: [
                    { id: "es-one", name: "ZonaTMO", allowedHosts: ["zonatmo.org"], enabled: true, rank: 0 },
                    { id: "es-two", name: "NiAdd", allowedHosts: ["es.niadd.com"], enabled: false, rank: 1 }
                ],
                pt: [ { id: "pt-one", name: "Taiyō", allowedHosts: ["taiyo.moe"], enabled: true, rank: 0 } ],
                fr: [ { id: "fr-one", name: "Sushiscan.fr", allowedHosts: ["sushiscan.fr"], enabled: true, rank: 0 } ]
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
        testWindow.width = 1280
        testWindow.height = 900
        testWindow.androidHost = false
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

    function test_phone_portrait_layout_data() {
        return [{tag: "phone320", width: 320}, {tag: "phone354", width: 354}]
    }

    function test_phone_portrait_layout(data) {
        testWindow.androidHost = true
        testWindow.width = data.width
        testWindow.height = 792
        page.width = data.width
        page.height = 792
        page.selectLanguage("en")
        wait(30)
        verify(page.phonePortrait)
        compare(byName(page, "tankoyomiConfigurationScroll").width, data.width - 36)
        const row = byName(page, "tankoyomiProvider_en-one")
        const info = byName(page, "tankoyomiProviderInfo_en-one")
        const name = byName(page, "tankoyomiProviderName_en-one")
        verify(info.width >= 120, "Provider details have a readable text column")
        verify(!name.truncated, "Provider: " + JSON.stringify({text: name.text,
            width: name.width, height: name.height, implicitHeight: name.implicitHeight,
            lines: name.lineCount, truncated: name.truncated}))
        for (const suffix of ["Toggle", "Up", "Down"]) {
            const action = byName(page, "tankoyomiProvider" + suffix + "_en-one")
            compare(action.width, 48)
            compare(action.height, 48)
            const p = action.mapToItem(row, 0, 0)
            verify(p.x >= 0 && p.x + action.width <= row.width)
            verify(p.y >= info.y + info.height && p.y + action.height <= row.height)
        }
        compare(byName(page, "tankoyomiResetOrder").height, 48)
        compare(byName(page, "tankoyomiSearchAction").width, 48)
        for (const verb of ["Minimize", "Fullscreen", "Close"])
            verify(!byName(page, "tankoyomi" + verb).visible)
        const autoPick = byName(page, "tankoyomiBehavior_Auto-pick")
        const fallback = byName(page, "tankoyomiBehavior_Fallback")
        const manual = byName(page, "tankoyomiBehavior_Manual choice")
        compare(autoPick.width, autoPick.parent.width)
        verify(fallback.y > autoPick.y && manual.y > fallback.y,
               "Behavior explanations stack instead of squeezing into three columns")
    }

    function test_phone_provider_control_keeps_its_intent() {
        testWindow.androidHost = true
        testWindow.width = 354
        testWindow.height = 792
        page.width = 354
        page.height = 792
        wait(30)
        const control = byName(page, "tankoyomiProviderToggle_es-two")
        const scroll = byName(page, "tankoyomiConfigurationScroll")
        scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height,
            control.mapToItem(scroll.contentItem, 0, 0).y - 80))
        wait(30)
        mouseClick(control, 24, 24)
        verify(manga.calls.indexOf("enabled:es:es-two:true") >= 0)
    }
    function test_tablet_default_language_uses_shared_sheet_and_native_intent() {
        testWindow.androidHost=true;testWindow.width=800;testWindow.height=1280
        page.width=800;page.height=1280
        page.languageMenuOpen=true
        tryVerify(function(){return page.activeChoiceSheet!==null && page.activeChoiceSheet.opened})
        wait(40)
        const sheet=page.activeChoiceSheet
        compare(sheet.background.color.a,1)
        const english=byName(sheet.contentItem,"en")
        verify(english!==null && english.height>=48)
        mouseClick(english,english.width/2,english.height/2)
        verify(manga.calls.indexOf("default:en")>=0)
        compare(page.languageMenuOpen,false)
    }

    function test_phone_gate_uses_window_orientation() {
        testWindow.androidHost = true
        testWindow.width = 792
        testWindow.height = 354
        page.width = 792
        page.height = 2000
        wait(0)
        verify(!page.phonePortrait)
        verify(byName(page, "tankoyomiBehavior_Auto-pick").width
               < byName(page, "tankoyomiBehavior_Auto-pick").parent.width)
    }

    function test_panel_contents_respect_insets_data() {
        return [{tag: "desktop", width: 1280}, {tag: "stacked", width: 1024},
                {tag: "narrow", width: 600}]
    }

    function test_panel_contents_respect_insets(data) {
        page.width = data.width
        wait(0)
        var languages = byName(page, "tankoyomiLanguagePanel")
        var providers = byName(page, "tankoyomiProviderPanel")
        var checks = [
            {item: byName(page, "tankoyomiLanguage_en"), panel: languages},
            {item: byName(page, "tankoyomiDefaultLanguageSelector"), panel: languages},
            {item: byName(page, "tankoyomiResetOrder"), panel: providers},
            {item: byName(page, "tankoyomiProvider_es-one"), panel: providers}
        ]
        for (var i = 0; i < checks.length; ++i) {
            var item = checks[i].item
            var panel = checks[i].panel
            var pos = item.mapToItem(panel, 0, 0)
            verify(pos.x >= 19, item.objectName + " left inset")
            verify(pos.x + item.width <= panel.width - 19,
                   item.objectName + " right inset")
        }
    }

    function test_manifest_flags_render_for_every_supported_language() {
        compare(byName(page, "tankoyomiLanguageFlag_en").countryCode, "GB")
        compare(byName(page, "tankoyomiLanguageFlag_es").countryCode, "ES")
        compare(byName(page, "tankoyomiLanguageFlag_pt").countryCode, "BR")
        compare(byName(page, "tankoyomiLanguageFlag_fr").countryCode, "FR")
        compare(byName(page, "tankoyomiDetailFlag").countryCode, "ES")
    }

    function test_provider_rows_render_manifest_name_and_primary_host() {
        var providerName = byName(page, "tankoyomiProviderName_es-one")
        var providerHost = byName(page, "tankoyomiProviderHost_es-one")
        compare(providerName.text, "ZonaTMO")
        compare(providerHost.text, "zonatmo.org")
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

    function test_desktop_provider_name_does_not_elide_short_label() {
        wait(0)
        var providerName = byName(page, "tankoyomiProviderName_es-one")
        verify(providerName !== null)
        compare(providerName.text, "ZonaTMO")
        verify(!providerName.truncated)
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
        tryVerify(function() {
            var titlePos = title.mapToItem(header, 0, 0)
            return titlePos.x >= -1 && titlePos.x + title.width <= header.width + 1
        }, 1000, "The polished title must fit inside the compact header")
        var masterPos = master.mapToItem(scroll, 0, 0)
        verify(masterPos.x >= -1 && masterPos.x + master.width <= scroll.width + 1)
    }
}
