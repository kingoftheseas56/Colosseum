import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneGalaxyDestinations"
    when: windowShown

    readonly property var destinationFixtures: [
        { id: "fixture-1", name: "THE HIGH REPUBLIC", orbit: 170, angle: 3.49, size: 27, y: 5, light: "#eee", mid: "#999", dark: "#222" },
        { id: "fixture-2", name: "FALL OF THE JEDI", orbit: 245, angle: 2.36, size: 32, y: -4, light: "#bbb", mid: "#777", dark: "#222" },
        { id: "fixture-3", name: "RISE OF THE EMPIRE", orbit: 320, angle: 1.52, size: 35, y: 2, light: "#ddd", mid: "#888", dark: "#333" },
        { id: "fixture-4", name: "AGE OF REBELLION", orbit: 390, angle: 0.61, size: 31, y: -3, light: "#ccc", mid: "#666", dark: "#222" },
        { id: "fixture-5", name: "NEW REPUBLIC", orbit: 465, angle: 5.64, size: 28, y: 4, light: "#eee", mid: "#777", dark: "#222" },
        { id: "fixture-6", name: "FIRST ORDER", orbit: 540, angle: 4.73, size: 37, y: -2, light: "#ddd", mid: "#888", dark: "#333" },
        { id: "fixture-7", name: "RESISTANCE", orbit: 615, angle: 2.04, size: 26, y: 3, light: "#eee", mid: "#999", dark: "#222" },
        { id: "fixture-8", name: "THE MANDALORIAN ERA", orbit: 690, angle: 0.21, size: 34, y: -5, light: "#ccc", mid: "#777", dark: "#222" },
        { id: "fixture-9", name: "LEGACY OF THE FORCE", orbit: 760, angle: 5.08, size: 30, y: 1, light: "#ddd", mid: "#888", dark: "#333" }
    ]

    Window {
        id: host
        width: 354
        height: 792
        visible: true
        property bool androidHost: true
    }

    Component {
        id: galaxyComponent
        App.StarWarsGalaxySystem {
            anchors.fill: parent
            androidHost: true
            reducedMotion: true
            destinations: testCase.destinationFixtures
        }
    }

    property var galaxy: null

    function init() {
        host.width = 354
        host.height = 792
        host.androidHost = true
        galaxy = galaxyComponent.createObject(host.contentItem)
        verify(galaxy !== null, "the actual Galaxy system instantiates with presentation fixtures only")
        wait(0)
    }

    function cleanup() {
        if (galaxy)
            galaxy.destroy()
        galaxy = null
        wait(0)
    }

    function findChild(parent, objectName) {
        if (!parent)
            return null
        if (parent.objectName === objectName)
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findChild(children[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function findDescendant(parent, predicate) {
        if (!parent)
            return null
        if (predicate(parent))
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findDescendant(children[i], predicate)
            if (found)
                return found
        }
        return null
    }

    function rectIn(item, target) {
        const point = item.mapToItem(target, 0, 0)
        return { x: point.x, y: point.y, width: item.width, height: item.height }
    }

    function visibleThroughParents(item, stopAt) {
        for (let node = item; node && node !== stopAt; node = node.parent) {
            if (!node.visible)
                return false
        }
        return true
    }

    function overlaps(first, second) {
        return first.x < second.x + second.width && second.x < first.x + first.width
            && first.y < second.y + second.height && second.y < first.y + first.height
    }

    function touchItem(item) {
        const root = host.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(30)
    }

    function assertPhoneComposition(width, height) {
        host.width = width
        host.height = height
        host.androidHost = true
        galaxy.androidHost = true
        wait(30)
        compare(galaxy.width, width)
        compare(galaxy.height, height)
        verify(galaxy.phoneLayout,
               "the Android-host phone composition is active at " + width + "x" + height)

        const destinations = findChild(galaxy, "starWarsGalaxyPhoneDestinations")
        const skywalker = findChild(galaxy, "starWarsGalaxyPhoneSkywalker")
        const heading = findDescendant(galaxy, function(item) {
            return item.text === "In a galaxy far far away"
        })
        verify(destinations && skywalker && heading,
               "phone destinations, Skywalker action, and landing title exist")
        verify(destinations.visible && destinations.clip,
               "the destination card list is a visible clipped viewport")
        verify(heading.visible)

        const art = galaxy.phoneArtBounds
        verify(art && art.x !== undefined && art.y !== undefined
               && art.width > 0 && art.height > 0,
               "the bounded phone art rectangle is exposed")
        const artRect = { x: art.x, y: art.y, width: art.width, height: art.height }
        const destinationRect = rectIn(destinations, galaxy)
        const headingRect = rectIn(heading, galaxy)
        verify(artRect.x >= -1 && artRect.y >= -1
               && artRect.x + artRect.width <= galaxy.width + 1
               && artRect.y + artRect.height <= galaxy.height + 1,
               "the orbit art box stays inside the phone viewport")
        verify(destinationRect.x >= -1 && destinationRect.y >= -1
               && destinationRect.x + destinationRect.width <= galaxy.width + 1
               && destinationRect.y + destinationRect.height <= galaxy.height + 1,
               "the destination viewport stays inside the phone viewport")
        verify(!overlaps(artRect, destinationRect),
               "orbit art and destination cards have separate phone regions")
        verify(!overlaps(headingRect, artRect) && !overlaps(headingRect, destinationRect),
               "the title does not overlap phone art or destination cards")
        if (height > width) {
            verify(artRect.y + artRect.height <= destinationRect.y + 1,
                   "portrait places bounded orbit art above the destination cards")
        } else {
            verify(artRect.x + artRect.width <= destinationRect.x + 1,
                   "landscape places bounded orbit art to the left of destination cards")
        }

        verify(skywalker.visible && skywalker.width >= 48 && skywalker.height >= 48,
               "the phone Skywalker action has a visible 48dp touch target")
        const skywalkerRect = rectIn(skywalker, destinations)
        verify(skywalkerRect.x >= -1 && skywalkerRect.y >= -1
               && skywalkerRect.x + skywalkerRect.width <= destinations.width + 1
               && skywalkerRect.y + skywalkerRect.height <= destinations.height + 1,
               "the Skywalker action is reachable in the phone destination viewport")

        for (let i = 0; i < destinationFixtures.length; ++i) {
            const fixture = destinationFixtures[i]
            const card = findChild(galaxy, "starWarsGalaxyPhoneDestination_" + fixture.id)
            const action = findChild(galaxy, "starWarsGalaxyPhoneDestinationAction_" + fixture.id)
            verify(card && action,
                   "the phone destination card and action exist for " + fixture.id)
            verify(card.visible && card.width <= destinations.width + 1
                   && card.height >= 48,
                   fixture.name + " is a bounded card with a 48dp row height")
            verify(action.visible && action.width >= 48 && action.height >= 48,
                   fixture.name + " keeps at least a 48dp action target")
            const title = findDescendant(card, function(item) { return item.text === fixture.name })
            verify(title && title.visible, fixture.name + " label is visible")
            const titleRect = rectIn(title, card)
            verify(titleRect.x >= -1 && titleRect.y >= -1
                   && titleRect.x + titleRect.width <= card.width + 1
                   && titleRect.y + titleRect.height <= card.height + 1,
                   fixture.name + " label stays inside its card")
        }

        verify(destinations.contentHeight >= (destinationFixtures.length + 1) * 48,
               "the scroll content can expose the Skywalker row and all nine 48dp destination rows")
        return { destinations: destinations, skywalker: skywalker }
    }

    function test_phone_portrait_and_landscape_card_geometry_and_touch_routes() {
        let phone = assertPhoneComposition(354, 792)
        const firstCard = findChild(galaxy, "starWarsGalaxyPhoneDestination_fixture-1")
        const firstAction = findChild(galaxy, "starWarsGalaxyPhoneDestinationAction_fixture-1")
        const lastCard = findChild(galaxy, "starWarsGalaxyPhoneDestination_fixture-9")
        const lastAction = findChild(galaxy, "starWarsGalaxyPhoneDestinationAction_fixture-9")
        verify(firstCard && firstAction && lastCard && lastAction)

        const destinationsActivated = []
        let skywalkerActivated = 0
        const onDestination = function(id) { destinationsActivated.push(id) }
        const onSkywalker = function() { ++skywalkerActivated }
        galaxy.destinationActivated.connect(onDestination)
        galaxy.skywalkerActivated.connect(onSkywalker)

        touchItem(phone.skywalker)
        compare(skywalkerActivated, 1, "phone Skywalker touch emits its actual signal")
        touchItem(firstAction)
        compare(destinationsActivated.length, 1)
        compare(destinationsActivated[0], "fixture-1",
                "the first phone destination touch emits its own ID")

        phone = assertPhoneComposition(792, 354)
        compare(phone.destinations.interactive, true,
                "short-landscape destination choices are vertically scrollable")
        phone.destinations.contentY = phone.destinations.contentHeight - phone.destinations.height
        wait(40)
        const scrolledLastRect = rectIn(lastCard, phone.destinations)
        verify(scrolledLastRect.y >= -1
               && scrolledLastRect.y + scrolledLastRect.height <= phone.destinations.height + 1,
               "the last destination card is reachable inside the scrolled viewport")
        touchItem(lastAction)
        compare(destinationsActivated.length, 2)
        compare(destinationsActivated[1], "fixture-9",
                "the last destination touch after scrolling emits its own ID")

        galaxy.destinationActivated.disconnect(onDestination)
        galaxy.skywalkerActivated.disconnect(onSkywalker)
    }

    function test_phone_map_labels_and_large_map_hit_areas_are_disabled() {
        assertPhoneComposition(354, 792)
        for (let i = 0; i < destinationFixtures.length; ++i) {
            const fixture = destinationFixtures[i]
            const gate = findChild(galaxy, "starWarsGalaxyGate_" + fixture.id)
            verify(gate !== null, "the original map target remains available for " + fixture.id)
            const label = findDescendant(gate, function(item) { return item.text === fixture.name })
            verify(label !== null, fixture.name + " retains its map label object")
            verify(!label.visible || !label.parent.visible,
                   fixture.name + " map callout is hidden on phone")
            const mapTarget = findDescendant(gate, function(item) {
                return item.containsMouse !== undefined && item.mouseX !== undefined
                       && item.mouseY !== undefined
            })
            verify(mapTarget !== null, fixture.name + " retains its original map hit target")
            compare(mapTarget.enabled, false,
                    fixture.name + " disables the overlapping map hit area on phone")
        }
    }

    function test_desktop_hides_phone_choices_and_keeps_original_map_targets() {
        host.width = 1280
        host.height = 720
        host.androidHost = false
        galaxy.androidHost = false
        wait(30)
        compare(galaxy.phoneLayout, false)

        const destinations = findChild(galaxy, "starWarsGalaxyPhoneDestinations")
        const skywalkerCard = findChild(galaxy, "starWarsGalaxyPhoneSkywalker")
        verify(destinations && skywalkerCard)
        compare(destinations.visible, false,
                "the phone destination list stays hidden in desktop composition")
        verify(!visibleThroughParents(skywalkerCard, galaxy),
                "the phone Skywalker action stays hidden in desktop composition")

        let destinationId = ""
        let skywalkerActivated = 0
        const onDestination = function(id) { destinationId = id }
        const onSkywalker = function() { ++skywalkerActivated }
        galaxy.destinationActivated.connect(onDestination)
        galaxy.skywalkerActivated.connect(onSkywalker)

        const fixture = destinationFixtures[0]
        const gate = findChild(galaxy, "starWarsGalaxyGate_" + fixture.id)
        verify(gate && gate.visible, "the original desktop map target remains visible")
        const label = findDescendant(gate, function(item) { return item.text === fixture.name })
        verify(label && label.visible, "the original desktop map callout remains visible")
        const mapTarget = findDescendant(gate, function(item) {
            return item.containsMouse !== undefined && item.mouseX !== undefined
                   && item.mouseY !== undefined
        })
        verify(mapTarget && mapTarget.enabled,
               "the original desktop map hit area remains enabled")
        mouseClick(mapTarget)
        compare(destinationId, fixture.id,
                "the original desktop map target still emits destinationActivated")

        const skywalkerNode = findChild(galaxy, "starWarsGalaxySkywalker")
        const skywalkerMapTarget = findDescendant(skywalkerNode, function(item) {
            return item.containsMouse !== undefined && item.mouseX !== undefined
                   && item.mouseY !== undefined
        })
        verify(skywalkerMapTarget && skywalkerMapTarget.enabled,
               "the original desktop Skywalker map hit area remains enabled")
        mouseClick(skywalkerMapTarget)
        compare(skywalkerActivated, 1,
                "the original desktop Skywalker target still emits its signal")

        galaxy.destinationActivated.disconnect(onDestination)
        galaxy.skywalkerActivated.disconnect(onSkywalker)
    }
}
