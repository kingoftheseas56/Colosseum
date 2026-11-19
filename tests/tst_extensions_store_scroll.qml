import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../qml"

Item {
    id: harness
    width: 900
    height: 600
    property var rail: null
    property var world: null
    property var installed: null

    Item { id: surface; anchors.fill: parent }

    Component {
        id: railComponent
        ExtensionsStoreRail {
            width: 600
            title: "Essentials"
            automationId: "testEssentials"
            canSeeAll: false
            items: [
                { slug: "one", name: "One", description: "Source one" },
                { slug: "two", name: "Two", description: "Source two" },
                { slug: "three", name: "Three", description: "Source three" },
                { slug: "four", name: "Four", description: "Source four" }
            ]
        }
    }

    Component {
        id: worldComponent
        WorldPage {
            width: 900
            height: 600
            medium: "Extensions"
            glassScrollBar: true
            Item { width: parent.width; height: 1000 }
        }
    }

    Component {
        id: installedComponent
        ExtensionsInstalledSection {
            width: 600
            entries: [
                { id: "core", installedAt: 10, core: true, manifest: { name: "Built-in" } },
                { id: "imported", installedAt: 20, manifest: { name: "Imported add-on" } }
            ]
            capabilitiesFor: () => "Streams"
            instanceLabelFor: () => ""
        }
    }

    Component {
        id: managementComponent
        ExtensionsInstalledPage {
            width: 600
            height: 500
            visible: false
            entries: [
                { id: "torrentio", transportUrl: "first", manifest: { name: "Torrentio" } },
                { id: "torrentio", transportUrl: "second", manifest: { name: "Torrentio" } }
            ]
            capabilitiesFor: () => "Streams"
            instanceLabelFor: index => "Instance " + (index + 1)
            configurableFor: () => true
        }
    }

    SignalSpy { id: manageSpy; target: harness.installed; signalName: "manageRequested" }

    TestCase {
        name: "ExtensionsStoreScroll"
        when: windowShown

        function init() {
            harness.rail = railComponent.createObject(surface)
            verify(harness.rail !== null)
        }
        function cleanup() {
            if (harness.rail) harness.rail.destroy()
            if (harness.world) harness.world.destroy()
            if (harness.installed) harness.installed.destroy()
            harness.rail = null
            harness.world = null
            harness.installed = null
            manageSpy.clear()
        }
        function test_uninstall_confirmation_works_from_the_store() {
            const management = createTemporaryObject(managementComponent, surface)
            verify(management)
            var removedUrl = ""
            management.removeRequested.connect(entry => { removedUrl = entry.transportUrl })
            management.requestRemoval([0, 1])
            const popup = findChild(management, "extensionsRemoveDialog")
            verify(popup)
            tryCompare(popup, "opened", true)
            const confirm = findChild(popup.contentItem, "extensionsConfirmRemoval")
            verify(confirm && !confirm.enabled)
            management.selectRemoval(1)
            verify(confirm.enabled)
            confirm.clicked()
            compare(removedUrl, "second")
        }

        function test_essentials_exposes_separate_card_actions() {
            const rail = harness.rail
            rail.essentialsLayout = true
            rail.items = [
                { slug: "torrentio", name: "Torrentio", configurable: true },
                { slug: "cinemeta", name: "Cinemeta", id: "com.linvo.cinemeta", configurable: false }
            ]
            wait(100)  // allow the horizontal row to position its new delegates
            const install = findChild(rail, "essentialsAction_torrentio")
            const gear = findChild(rail, "essentialsConfigure_torrentio")
            const builtIn = findChild(rail, "essentialsAction_cinemeta")
            verify(install && gear && builtIn)
            var installed = false
            rail.installedFn = () => installed
            var actions = []
            rail.addonActivated.connect(addon => actions.push("install " + addon.slug))
            rail.addonConfigured.connect(addon => actions.push("configure " + addon.slug))
            rail.addonRemoved.connect(addon => actions.push("uninstall " + addon.slug))

            compare(install.accessibleName, "Install Torrentio")
            verify(gear.visible && gear.height >= 44)
            verify(!builtIn.enabled)
            mouseClick(gear)
            mouseClick(install)
            installed = true
            rail.revision++
            compare(install.accessibleName, "Uninstall Torrentio")
            mouseClick(install)
            compare(actions.join(", "), "configure torrentio, install torrentio, uninstall torrentio")
        }

        function test_compactCardsAndGrabbableHorizontalBar() {
            var card = findChild(harness.rail, "extensionsCard_one")
            var flick = findChild(harness.rail, "testEssentials")
            var bar = findChild(harness.rail, "testEssentialsScrollBar")
            verify(card !== null && flick !== null && bar !== null)
            verify(card.width < 240, "The desktop shelf should fit more add-ons across")
            compare(bar.policy, ScrollBar.AlwaysOn)
            verify(bar.visible && bar.glassStyle && bar.height > bar.contentItem.height)
            verify(flick.contentWidth > flick.width)
            mouseClick(bar, bar.width * 0.85, bar.height / 2, Qt.LeftButton)
            tryVerify(function() { return flick.contentX > 0 }, 1000)
        }
        function test_shelf_buttons_page_in_both_directions() {
            var flick = findChild(harness.rail, "testEssentials")
            var previous = findChild(harness.rail, "testEssentialsPrevious")
            var next = findChild(harness.rail, "testEssentialsNext")
            verify(flick && previous && next)
            verify(!previous.enabled && next.enabled)
            mouseClick(next)
            tryVerify(function() { return flick.contentX > 0 })
            verify(previous.enabled && !next.enabled)
            previous.forceActiveFocus()
            keyClick(Qt.Key_Return)
            tryCompare(flick, "contentX", 0)
            verify(!previous.enabled && next.enabled)
        }
        function test_verticalBarIsVisibleWhenStoreOverflows() {
            harness.world = worldComponent.createObject(surface)
            verify(harness.world !== null)
            var bar = findChild(harness.world, "extensionsWorldScrollBar")
            verify(bar !== null)
            compare(bar.policy, ScrollBar.AlwaysOn)
            verify(bar.visible && bar.glassStyle && bar.width > bar.contentItem.width)
            mouseClick(bar, bar.width / 2, bar.height * 0.85, Qt.LeftButton)
            tryVerify(function() { return bar.flick.contentY > 0 }, 1000)
        }
        function test_recent_addon_is_first_and_opens_its_management_entry() {
            harness.rail.visible = false
            harness.installed = installedComponent.createObject(surface)
            verify(harness.installed !== null)
            var card = findChild(harness.installed, "extensionsInstalledCard_0")
            verify(card !== null)
            compare(card.addonName, "Imported add-on")
            mouseClick(card)
            tryCompare(manageSpy, "count", 1)
            compare(manageSpy.signalArguments[0][0], 1)
        }
    }
}
