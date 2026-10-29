import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App
import "../../qml/Universes.js" as UDB

TestCase {
    id: testCase
    name: "PhoneCosmereLayout"
    when: windowShown

    readonly property url localCover: Qt.resolvedUrl("../../assets/wallpaper/sagas/dune.jpg")
    readonly property var sourceCosmereGuide: UDB.configFor("Cosmere")

    Window {
        id: host
        width: 354
        height: 792
        visible: true
    }

    Component {
        id: pageComponent
        App.CosmereUniversePage {
            anchors.fill: parent
            universeName: ""
            androidHost: true
        }
    }

    property var cosmere: null
    SignalSpy { id: bookRequestedSpy; target: testCase.cosmere; signalName: "bookRequested" }

    function localBook(id, title) {
        return { id: id, title: title, cover: localCover }
    }

    function localGuide() {
        return {
            banner: "",
            blurb: "Local fixture copy for the Cosmere portrait layout test.",
            cosmereStarters: sourceCosmereGuide.cosmereStarters.map(function(starter) {
                return { label: starter.label, short: starter.short, note: starter.note,
                         query: starter.query }
            }),
            cosmereWorlds: sourceCosmereGuide.cosmereWorlds.map(function(world) {
                return { name: world.name, epithet: world.epithet, accent: world.accent }
            })
        }
    }

    function localAtlas(guide) {
        return {
            name: "Cosmere",
            blurb: "A local Cosmere atlas fixture with no remote metadata.",
            banner: "",
            metaline: "",
            starters: guide.cosmereStarters.map(function(starter, index) {
                return { query: starter.query,
                         book: localBook("starter-" + index, starter.label) }
            }),
            worlds: guide.cosmereWorlds.map(function(world, index) {
                return {
                    name: world.name,
                    epithet: world.epithet,
                    accent: world.accent,
                    books: [
                        { label: "LOCAL VOLUME", book: localBook("world-" + index,
                                                                  world.name + " local volume") },
                        { label: "LOCAL COMPANION", book: localBook("companion-" + index,
                                                                      world.name + " companion") }
                    ]
                }
            }),
            series: [{
                name: "Mistborn — Era One",
                epithet: "LOCAL SERIES FIXTURE",
                accent: "#b8734a",
                books: [
                    { label: "01", book: localBook("series-1", "The Final Empire") },
                    { label: "02", book: localBook("series-2", "The Well of Ascension") }
                ]
            }]
        }
    }

    function init() {
        host.width = 354
        host.height = 792
        cosmere = pageComponent.createObject(host.contentItem)
        verify(cosmere !== null, "the actual Cosmere universe page instantiates")
        wait(30)
        compare(cosmere.universeName, "", "empty universe name prevents remote atlas loading")

        const guide = localGuide()
        cosmere.guide = guide
        cosmere.uni = localAtlas(guide)
        wait(500)
        verify(cosmere.uni.worlds.length === 6, "six local world fixtures are rendered")
        verify(cosmere.guide.cosmereStarters.length === 3, "the three curated starters remain present")
    }

    function cleanup() {
        if (cosmere)
            cosmere.destroy()
        cosmere = null
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

    function findDescendants(parent, predicate, result) {
        const matches = result || []
        if (!parent)
            return matches
        if (predicate(parent))
            matches.push(parent)
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i)
            findDescendants(children[i], predicate, matches)
        return matches
    }

    function findAncestor(item, predicate) {
        for (let current = item ? item.parent : null; current; current = current.parent) {
            if (predicate(current))
                return current
        }
        return null
    }

    function rectIn(item, target) {
        const point = item.mapToItem(target, 0, 0)
        return { x: point.x, y: point.y, width: item.width, height: item.height }
    }

    function paintedRectIn(item, target) {
        const point = item.mapToItem(target, 0, 0)
        return { x: point.x, y: point.y,
                 width: Math.max(item.width, item.paintedWidth),
                 height: Math.max(item.height, item.paintedHeight) }
    }

    function overlaps(a, b) {
        return a.x < b.x + b.width && b.x < a.x + a.width
            && a.y < b.y + b.height && b.y < a.y + a.height
    }

    function visibleTexts(parent) {
        return findDescendants(parent, function(item) {
            return item && item.visible && typeof item.text === "string"
                && typeof item.lineCount === "number"
                && typeof item.paintedWidth === "number"
        })
    }

    function assertTextPaintFits(parent, description) {
        const texts = visibleTexts(parent)
        verify(texts.length > 0, description + " has visible text to inspect")
        for (let i = 0; i < texts.length; ++i) {
            const rect = paintedRectIn(texts[i], parent)
            verify(rect.x >= -1 && rect.y >= -1
                   && rect.x + rect.width <= parent.width + 1
                   && rect.y + rect.height <= parent.height + 1,
                   description + " text stays within its painted bounds (text=" + texts[i].text + ")")
        }
    }

    function assertTextFitsContainer(text, container, description) {
        const rect = paintedRectIn(text, container)
        verify(rect.x >= -1 && rect.y >= -1
               && rect.x + rect.width <= container.width + 1
               && rect.y + rect.height <= container.height + 1,
               description + " stays within its container")
    }

    function assertElementFitsContainer(item, container, description) {
        const rect = rectIn(item, container)
        verify(rect.x >= -1 && rect.y >= -1
               && rect.x + rect.width <= container.width + 1
               && rect.y + rect.height <= container.height + 1,
               description + " stays within its container")
    }

    function setViewport(width, height, androidHost) {
        host.width = width
        host.height = height
        cosmere.androidHost = androidHost
        wait(40)
        compare(cosmere.width, width)
        compare(cosmere.height, height)
        compare(cosmere.androidHost, androidHost)
    }

    function assertMapBoundsAndText(width, height) {
        setViewport(width, height, true)
        compare(cosmere.phoneLayout, true)
        const mapSection = findChild(cosmere, "cosmereMapSection")
        const mapHeading = findChild(cosmere, "cosmereMapHeading")
        const map = findChild(cosmere, "cosmereSystemMap")
        verify(mapSection && mapHeading && map, "the live map section, heading, and map are present")
        assertTextPaintFits(mapHeading, "map heading")
        assertElementFitsContainer(mapHeading, mapSection, "map heading")
        assertElementFitsContainer(map, mapSection, "system map")
        verify(!overlaps(rectIn(mapHeading, mapSection), rectIn(map, mapSection)),
               "map heading and map do not overlap")

        const rects = []
        for (let i = 0; i < 6; ++i) {
            const node = findChild(cosmere, "cosmereWorldNode_" + i)
            verify(node !== null, "world map node " + i + " is present")
            const rect = rectIn(node, map)
            verify(rect.x >= -1 && rect.y >= -1
                   && rect.x + rect.width <= map.width + 1
                   && rect.y + rect.height <= map.height + 1,
                   "world map node " + i + " stays inside the map viewport")
            verify(node.width >= 48 && node.height >= 48,
                   "world map node " + i + " remains a usable touch target")
            assertTextPaintFits(node, "world map node " + i)
            for (let j = 0; j < rects.length; ++j)
                verify(!overlaps(rect, rects[j]),
                       "world map nodes " + i + " and " + j + " do not overlap")
            rects.push(rect)
        }
        return { mapSection: mapSection, map: map }
    }

    function touchItem(item) {
        const root = host.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(30)
    }

    function revealItemInPageViewport(item) {
        const scroll = findChild(cosmere, "cosmerePageScroll")
        const itemContentRect = rectIn(item, scroll.contentItem)
        const desiredY = itemContentRect.y - (scroll.height - item.height) / 2
        scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height, desiredY))
        wait(30)
        const itemRect = rectIn(item, host.contentItem)
        const scrollRect = rectIn(scroll, host.contentItem)
        verify(itemRect.y >= scrollRect.y - 1
               && itemRect.y + itemRect.height <= scrollRect.y + scroll.height + 1,
               "ordinary page scrolling reveals the requested item")
    }

    function assertWorldTarget(index) {
        const back = findDescendants(cosmere, function(item) {
            return item && item.variant === "plain" && item.label === "Back"
        })[0]
        const section = findChild(cosmere, "cosmereWorldSection_" + index)
        verify(back && section, "fixed Back and target world section are present")
        const backRect = rectIn(back, host.contentItem)
        const sectionRect = rectIn(section, host.contentItem)
        verify(sectionRect.y >= backRect.y + backRect.height + 8,
               "the selected world section starts below fixed Back")
        verify(sectionRect.y <= 88,
               "the selected world section is revealed near 80dp below fixed Back")
        const world = cosmere.uni.worlds[index]
        const epithet = findDescendants(section, function(item) {
            return item && item.text === world.epithet && typeof item.paintedWidth === "number"
        })[0]
        const title = findDescendants(section, function(item) {
            return item && item.text === world.name && typeof item.paintedWidth === "number"
        })[0]
        const countText = world.books.length + (world.books.length === 1 ? " portal" : " portals")
        const count = findDescendants(section, function(item) {
            return item && item.text === countText && typeof item.paintedWidth === "number"
        })[0]
        verify(title && epithet && count,
               "the selected section title, epithet, and portal count remain present")
        for (const headingText of [title, epithet, count])
            assertTextFitsContainer(headingText, section, "selected world section text")
        const headingRects = [paintedRectIn(title, section),
                              paintedRectIn(epithet, section),
                              paintedRectIn(count, section)]
        for (let i = 0; i < headingRects.length; ++i) {
            for (let j = i + 1; j < headingRects.length; ++j)
                verify(!overlaps(headingRects[i], headingRects[j]),
                       "selected world heading and count do not overlap")
        }
        const titleRect = paintedRectIn(title, host.contentItem)
        const epithetRect = paintedRectIn(epithet, host.contentItem)
        const countRect = paintedRectIn(count, host.contentItem)
        verify(titleRect.y >= backRect.y + backRect.height
               && epithetRect.y >= backRect.y + backRect.height
               && countRect.y >= backRect.y + backRect.height
               && titleRect.y + titleRect.height <= host.height + 1
               && epithetRect.y + epithetRect.height <= host.height + 1
               && countRect.y + countRect.height <= host.height + 1,
               "the selected world heading, epithet, and count are visible in the phone viewport")
        const scroll = findChild(cosmere, "cosmerePageScroll")
        const sectionContentRect = rectIn(section, scroll.contentItem)
        verify(sectionContentRect.y + section.height <= scroll.contentHeight + 1
               && scroll.contentHeight - scroll.height >= sectionContentRect.y + section.height - scroll.height,
               "the full selected section remains reachable through the page scroll range")
    }

    function test_phone_portrait_map_nodes_are_bounded_and_disjoint() {
        assertMapBoundsAndText(354, 792)
    }

    function test_phone_landscape_map_nodes_and_headings_are_bounded() {
        assertMapBoundsAndText(792, 354)
        const gatesHeading = findChild(cosmere, "cosmereWorldGatesHeading")
        const shelvesHeading = findChild(cosmere, "cosmereCompleteShelvesHeading")
        verify(gatesHeading && shelvesHeading, "the world and complete-shelf headings are present")
        assertTextPaintFits(gatesHeading, "world gates heading")
        assertTextPaintFits(shelvesHeading, "complete shelves heading")
    }

    function test_phone_world_sections_and_book_gate_actions_fit() {
        setViewport(354, 792, true)
        const scroll = findChild(cosmere, "cosmerePageScroll")
        const lastSection = findChild(cosmere, "cosmereWorldSection_5")
        verify(scroll && lastSection, "world content and the final section are present")
        const lastContentRect = rectIn(lastSection, scroll.contentItem)
        verify(lastContentRect.y + lastSection.height <= scroll.contentHeight + 1,
               "content height includes the final world section")
        verify(scroll.contentHeight - scroll.height >= lastContentRect.y - 80,
               "the phone scroll range can reveal the final world below fixed Back")

        for (let i = 0; i < 6; ++i) {
            const section = findChild(cosmere, "cosmereWorldSection_" + i)
            verify(section !== null, "world section " + i + " is present")
            const world = cosmere.uni.worlds[i]
            const title = findDescendants(section, function(item) {
                return item && item.text === world.name && typeof item.paintedWidth === "number"
            })[0]
            const epithet = findDescendants(section, function(item) {
                return item && item.text === world.epithet && typeof item.paintedWidth === "number"
            })[0]
            const countText = world.books.length + (world.books.length === 1 ? " portal" : " portals")
            const count = findDescendants(section, function(item) {
                return item && item.text === countText && typeof item.paintedWidth === "number"
            })[0]
            verify(title && epithet && count, "world title, epithet, and portal count exist for section " + i)
            for (const headingText of [title, epithet, count])
                assertTextFitsContainer(headingText, section, "world section text " + i)
            const headingRects = [paintedRectIn(title, section),
                                  paintedRectIn(epithet, section),
                                  paintedRectIn(count, section)]
            for (let j = 0; j < headingRects.length; ++j) {
                for (let k = j + 1; k < headingRects.length; ++k)
                    verify(!overlaps(headingRects[j], headingRects[k]),
                           "world heading text does not overlap in section " + i)
            }

            const gates = findDescendants(section, function(item) {
                return item && item.portal && item.portal.book && typeof item.activated === "function"
            })
            compare(gates.length, world.books.length,
                    "each local world book has a real BookGate action for section " + i)
            for (let j = 0; j < gates.length; ++j) {
                const gate = gates[j]
                const action = findDescendants(gate, function(item) {
                    return item && item.pressed !== undefined && item.containsMouse !== undefined
                })[0]
                verify(gate.width >= 48 && gate.height >= 48,
                       "BookGate remains a touch target for section " + i)
                verify(action !== undefined, "BookGate has an actual MouseArea action")
                if (action) {
                    const actionRect = rectIn(action, gate)
                    verify(actionRect.x >= -1 && actionRect.y >= -1
                           && actionRect.x + actionRect.width <= gate.width + 1
                           && actionRect.y + actionRect.height <= gate.height + 1,
                           "BookGate action stays within its card")
                }
                assertTextPaintFits(gate, "BookGate text for section " + i)
            }
        }

        setViewport(792, 354, true)
        const landscapeScroll = findChild(cosmere, "cosmerePageScroll")
        const seriesSection = findChild(cosmere, "cosmereSeriesSection_0")
        verify(seriesSection !== null, "the local complete-series section is present in landscape")
        const seriesRect = rectIn(seriesSection, landscapeScroll.contentItem)
        verify(seriesRect.y + seriesSection.height <= landscapeScroll.contentHeight + 1,
               "landscape content height includes the complete-series section")
        const seriesGates = findDescendants(seriesSection, function(item) {
            return item && item.portal && item.portal.book && typeof item.activated === "function"
        })
        compare(seriesGates.length, 2, "the local series exposes two real BookGate actions")
        for (let i = 0; i < seriesGates.length; ++i) {
            const gate = seriesGates[i]
            verify(gate.width >= 48 && gate.height >= 48,
                   "series BookGate remains a touch target in landscape")
            assertTextPaintFits(gate, "series BookGate text")
            const action = findDescendants(gate, function(item) {
                return item && item.pressed !== undefined && item.containsMouse !== undefined
            })[0]
            verify(action !== undefined, "series BookGate has an actual MouseArea action")
            if (action)
                assertElementFitsContainer(action, gate, "series BookGate action")
        }
    }

    function test_phone_touching_first_and_last_map_nodes_reveals_matching_world() {
        setViewport(354, 792, true)
        bookRequestedSpy.clear()
        const first = findChild(cosmere, "cosmereWorldNode_0")
        const last = findChild(cosmere, "cosmereWorldNode_5")
        verify(first && last, "the first and last map destinations are real interactive nodes")
        for (const node of [first, last]) {
            revealItemInPageViewport(node)
            const rect = rectIn(node, host.contentItem)
            verify(rect.y >= 0 && rect.y + rect.height <= host.height,
                   "each portrait map node is visible before touch")
            touchItem(node)
            const index = Number(node.objectName.slice("cosmereWorldNode_".length))
            assertWorldTarget(index)
            compare(bookRequestedSpy.count, 0, "world navigation never activates a book")
        }
    }

    function test_phone_landscape_touches_reveal_matching_world_headers() {
        setViewport(792, 354, true)
        bookRequestedSpy.clear()
        const first = findChild(cosmere, "cosmereWorldNode_0")
        const last = findChild(cosmere, "cosmereWorldNode_5")
        verify(first && last, "landscape retains real first and last world destinations")
        for (const node of [first, last]) {
            revealItemInPageViewport(node)
            const rect = rectIn(node, host.contentItem)
            verify(rect.y >= 0 && rect.y + rect.height <= host.height,
                   "each landscape map node is visible before touch")
            touchItem(node)
            const index = Number(node.objectName.slice("cosmereWorldNode_".length))
            assertWorldTarget(index)
            compare(bookRequestedSpy.count, 0, "landscape world navigation does not activate a book")
        }
    }

    function test_desktop_keeps_the_existing_three_by_two_map() {
        setViewport(1280, 800, false)
        compare(cosmere.phoneLayout, false)
        const mapSection = findChild(cosmere, "cosmereMapSection")
        const map = findChild(cosmere, "cosmereSystemMap")
        compare(mapSection.height, 470)
        const rects = []
        for (let i = 0; i < 6; ++i) {
            const node = findChild(cosmere, "cosmereWorldNode_" + i)
            verify(node !== null)
            compare(node.width, 230)
            compare(node.height, 74)
            const rect = rectIn(node, map)
            verify(rect.x >= -1 && rect.x + rect.width <= map.width + 1
                   && rect.y >= -1 && rect.y + rect.height <= map.height + 1,
                   "desktop map node stays within its original map")
            if (i > 0 && i % 3 !== 0)
                compare(rect.y, rects[i - 1].y, "three desktop nodes share each row")
            if (i > 0 && i % 3 === 0)
                verify(rect.y > rects[i - 1].y, "the second desktop row remains below the first")
            for (let j = 0; j < rects.length; ++j)
                verify(!overlaps(rect, rects[j]), "desktop nodes remain separate")
            rects.push(rect)
        }
    }
}
