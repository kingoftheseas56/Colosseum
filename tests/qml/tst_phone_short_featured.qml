import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneShortFeatured"
    when: windowShown

    property var activeScene: null
    property int primaryCount: 0
    property int secondaryCount: 0
    property int lastPrimaryIndex: -1
    property int lastSecondaryIndex: -1

    readonly property string longTitle:
        "The unreasonably long featured title keeps its words within the visible two-line phone headline without pushing actions off-screen"
    readonly property string longBlurb:
        "A long featured description explains the whole story, its characters, the setting, the season, the latest release, and the reason a reader may want to open the details page before choosing whether to start reading. It must remain a short visible preview so the next browse section can still be discovered on a short phone screen."
    readonly property var slides: [
        { id: "short-feature-one", title: longTitle, blurb: longBlurb,
          c1: "#34251a", c2: "#171016" },
        { id: "short-feature-two", title: "Second featured item", blurb: "A second slide keeps swipe behavior testable.",
          c1: "#25213a", c2: "#120b1a" },
        { id: "short-feature-three", title: "Third featured item", blurb: "The selected page must survive resizing.",
          c1: "#1d3430", c2: "#101818" }
    ]

    Window {
        id: host
        width: 354
        height: 792
        visible: true
        property bool androidHost: true
    }

    Component {
        id: sceneComponent
        Item {
            id: scene
            width: host.width
            height: host.height
            property var slideData: []
            property alias carousel: featured
            property alias nextContent: nextWorldContent

            App.FeaturedCarousel {
                id: featured
                objectName: "shortFeaturedCarousel"
                width: scene.width
                height: implicitHeight
                slides: scene.slideData
                primaryLabel: "Open"
                secondaryLabel: "Details"
            }

            Rectangle {
                id: nextWorldContent
                objectName: "nextWorldContent"
                x: 0
                y: featured.height
                width: scene.width
                height: 64
                color: "#263126"
                Text { anchors.centerIn: parent; text: "Next browse section" }
            }
        }
    }

    function init() {
        host.androidHost = true
        host.width = 354
        host.height = 792
        primaryCount = 0
        secondaryCount = 0
        lastPrimaryIndex = -1
        lastSecondaryIndex = -1
    }

    function cleanup() {
        if (activeScene) {
            activeScene.destroy()
            activeScene = null
        }
    }

    function newScene(width, height) {
        host.width = width
        host.height = height
        wait(0)
        activeScene = createTemporaryObject(sceneComponent, host.contentItem,
                                            { slideData: slides })
        verify(activeScene !== null, "the real FeaturedCarousel scene is created")
        wait(50)
        activeScene.carousel.primaryClicked.connect(function(index) {
            testCase.primaryCount += 1
            testCase.lastPrimaryIndex = index
        })
        activeScene.carousel.secondaryClicked.connect(function(index) {
            testCase.secondaryCount += 1
            testCase.lastSecondaryIndex = index
        })
        return activeScene
    }

    function findText(node, wanted) {
        if (!node)
            return null
        if (node.text === wanted)
            return node
        const children = node.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findText(children[i], wanted)
            if (found)
                return found
        }
        return null
    }

    function carouselView(carousel) {
        return carousel ? findChild(carousel, "featuredCarouselView") : null
    }

    function carouselSlide(carousel, index) {
        const view = carouselView(carousel)
        return view ? view.itemAt(index) : null
    }

    function carouselIdentity(carousel, index) {
        const view = carouselView(carousel)
        return view ? view.keyboardIdentityForIndex(index) : ""
    }

    function itemBox(item, parent) {
        const point = item.mapToItem(parent, 0, 0)
        return { left: point.x, top: point.y,
                 right: point.x + item.width, bottom: point.y + item.height }
    }

    function boxesOverlap(a, b) {
        return a.left < b.right && b.left < a.right
                && a.top < b.bottom && b.top < a.bottom
    }

    function assertPhoneGeometry(width, height) {
        const scene = newScene(width, height)
        const carousel = scene.carousel
        const shortLandscape = width === 792 && height === 354
        compare(carousel.height, carousel.implicitHeight)
        if (shortLandscape) {
            verify(carousel.height >= 180 && carousel.height <= 190,
                   "short Android landscape hero is " + carousel.height + "dp, expected 180-190dp")
        } else {
            compare(carousel.height, 260)
        }

        const slide = carouselSlide(carousel, 0)
        verify(slide !== null, "the real first CarouselSlide is loaded by SwipeView")
        tryVerify(function() {
            const title = findText(slide, longTitle)
            return title !== null && title.lineCount > 0
        }, 2000)

        const title = findText(slide, longTitle)
        const blurb = findText(slide, longBlurb)
        verify(title !== null && blurb !== null, "long title and description are rendered by CarouselSlide")
        compare(title.maximumLineCount, 2)
        verify(title.lineCount <= 2, "long title remains capped at two lines")
        verify(blurb.maximumLineCount <= 2, "long phone description remains a short preview")
        verify(blurb.lineCount <= 2, "long phone description does not expand the card")

        const titleBox = itemBox(title, slide)
        const blurbBox = itemBox(blurb, slide)
        verify(titleBox.left >= 0 && titleBox.top >= 0
               && titleBox.right <= slide.width + 1 && titleBox.bottom <= slide.height + 1,
               "painted title item stays inside the " + slide.width + "x" + slide.height + " slide")
        verify(blurbBox.left >= 0 && blurbBox.top >= 0
               && blurbBox.right <= slide.width + 1 && blurbBox.bottom <= slide.height + 1,
               "description item stays inside the slide")

        const primary = findChild(slide, "featuredPrimaryAction_0")
        const secondary = findChild(slide, "featuredSecondaryAction_0")
        verify(primary !== null && secondary !== null,
               "primary and secondary semantic actions remain available")
        verify(primary.visible && primary.enabled && secondary.visible && secondary.enabled,
               "both action targets stay visible and enabled")
        compare(primary.accessibleName, "Open")
        compare(secondary.accessibleName, "Details")
        verify(!primary.pointerEnabled && !secondary.pointerEnabled,
               "KeyboardAction does not duplicate the custom pointer delivery")
        verify(primary.width >= 48 && primary.height >= 48,
               "primary hit target is at least 48dp, got " + primary.width + "x" + primary.height)
        verify(secondary.width >= 48 && secondary.height >= 48,
               "secondary hit target is at least 48dp, got " + secondary.width + "x" + secondary.height)

        const primaryBox = itemBox(primary, slide)
        const secondaryBox = itemBox(secondary, slide)
        verify(primaryBox.left >= 0 && primaryBox.top >= 0
               && primaryBox.right <= slide.width + 1 && primaryBox.bottom <= slide.height + 1,
               "primary target is fully contained by the slide")
        verify(secondaryBox.left >= 0 && secondaryBox.top >= 0
               && secondaryBox.right <= slide.width + 1 && secondaryBox.bottom <= slide.height + 1,
               "secondary target is fully contained by the slide")
        verify(!boxesOverlap(primaryBox, secondaryBox), "primary and secondary targets do not overlap")
        verify(!boxesOverlap(blurbBox, primaryBox) && !boxesOverlap(blurbBox, secondaryBox),
               "copy and actions do not overlap")

        const nextContent = findChild(scene, "nextWorldContent")
        verify(nextContent !== null && nextContent.visible, "next browse content remains in the scene")
        verify(nextContent.y < host.height,
               "next browse content starts at " + nextContent.y + "dp within a " + host.height + "dp viewport")
        verify(host.height - nextContent.y >= 48,
               "at least 48dp of following content is discoverable")
        return { scene: scene, carousel: carousel, slide: slide,
                 primary: primary, secondary: secondary }
    }

    function test_long_copy_and_actions_fit_portrait_and_short_landscape() {
        assertPhoneGeometry(354, 792)
        cleanup()
        assertPhoneGeometry(792, 354)
    }

    function test_phone_primary_and_secondary_pointer_actions_route_once_with_feedback() {
        const layout = assertPhoneGeometry(792, 354)
        const primaryButton = layout.primary.parent
        const secondaryButton = layout.secondary.parent
        const primaryPointer = findChild(layout.slide, "featuredPrimaryPointer_0")
        const secondaryPointer = findChild(layout.slide, "featuredSecondaryPointer_0")
        verify(primaryPointer !== null && secondaryPointer !== null,
               "the real pointer handlers remain attached beside semantic actions")
        const primaryRest = String(primaryButton.color)
        const primaryPoint = layout.primary.mapToItem(host.contentItem,
                                                       layout.primary.width / 2,
                                                       layout.primary.height / 2)
        touchEvent(host.contentItem).press(0, host.contentItem,
                                           primaryPoint.x, primaryPoint.y).commit()
        wait(20)
        verify(primaryPointer.pressed,
               "primary pointer handler enters pressed state")
        verify(String(primaryButton.color) !== primaryRest,
               "primary target provides visible pressed feedback (rest="
               + String(primaryRest) + ", pressed=" + String(primaryButton.color) + ")")
        touchEvent(host.contentItem).release(0, host.contentItem,
                                             primaryPoint.x, primaryPoint.y).commit()
        wait(20)
        compare(primaryCount, 1)
        compare(lastPrimaryIndex, 0)
        compare(secondaryCount, 0)

        const secondaryRest = String(secondaryButton.color)
        const secondaryPoint = layout.secondary.mapToItem(host.contentItem,
                                                           layout.secondary.width / 2,
                                                           layout.secondary.height / 2)
        touchEvent(host.contentItem).press(0, host.contentItem,
                                           secondaryPoint.x, secondaryPoint.y).commit()
        wait(20)
        verify(secondaryPointer.pressed,
               "secondary pointer handler enters pressed state")
        verify(String(secondaryButton.color) !== secondaryRest,
               "secondary target provides visible pressed feedback (rest="
               + String(secondaryRest) + ", pressed=" + String(secondaryButton.color) + ")")
        touchEvent(host.contentItem).release(0, host.contentItem,
                                             secondaryPoint.x, secondaryPoint.y).commit()
        wait(20)
        compare(secondaryCount, 1)
        compare(lastSecondaryIndex, 0)
        compare(primaryCount, 1)
    }

    function test_horizontal_drag_changes_slide_without_activating_actions() {
        const layout = newScene(354, 792)
        compare(layout.carousel.index, 0)
        mouseDrag(layout.carousel, 275, 70, -190, 0,
                  Qt.LeftButton, Qt.NoModifier, 40)
        tryCompare(layout.carousel, "index", 1, 2000)
        compare(primaryCount, 0)
        compare(secondaryCount, 0)
    }

    function test_selected_slide_survives_short_height_resize() {
        const scene = newScene(354, 792)
        const carousel = scene.carousel
        carousel.index = 2
        tryCompare(carousel, "index", 2)

        host.width = 792
        host.height = 354
        tryCompare(carousel, "implicitHeight", 186, 2000)
        tryCompare(carousel, "index", 2)
        compare(carouselIdentity(carousel, carousel.index), "short-feature-three")

        host.width = 354
        host.height = 792
        tryCompare(carousel, "implicitHeight", 260, 2000)
        tryCompare(carousel, "index", 2)
        compare(carouselIdentity(carousel, carousel.index), "short-feature-three")
    }

    function test_desktop_and_android_tablet_keep_existing_carousel_geometry() {
        host.androidHost = false
        const desktop = newScene(792, 354)
        compare(desktop.carousel.implicitHeight, 300)
        const desktopSlide = carouselSlide(desktop.carousel, 0)
        tryVerify(function() { return desktopSlide !== null })
        compare(desktopSlide.phoneLayout, false)
        const desktopAction = findChild(desktopSlide, "featuredPrimaryAction_0")
        const desktopPointer = findChild(desktopSlide, "featuredPrimaryPointer_0")
        compare(desktopAction.height, 42)
        verify(desktopPointer !== null)
        const desktopButton = desktopAction.parent
        const desktopRestColor = String(desktopButton.color)
        const desktopPoint = desktopAction.mapToItem(host.contentItem,
                                                      desktopAction.width / 2,
                                                      desktopAction.height / 2)
        mousePress(host.contentItem, desktopPoint.x, desktopPoint.y)
        wait(0)
        verify(desktopPointer.pressed, "desktop pointer remains on its existing MouseArea path")
        compare(String(desktopButton.color), desktopRestColor,
                "desktop button keeps its existing visual color while pressed")
        mouseRelease(host.contentItem, desktopPoint.x, desktopPoint.y)
        cleanup()

        host.androidHost = true
        const tablet = newScene(900, 600)
        compare(tablet.carousel.implicitHeight, 340)
        const tabletSlide = carouselSlide(tablet.carousel, 0)
        tryVerify(function() { return tabletSlide !== null })
        compare(tabletSlide.phoneLayout, false)
        compare(findChild(tabletSlide, "featuredPrimaryAction_0").height, 48)
    }
}
