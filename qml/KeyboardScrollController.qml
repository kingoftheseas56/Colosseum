// KeyboardScrollController â€” keyboard face for an existing Flickable/ScrollGlide pair.
// It adds no wheel physics. Unhandled collection keys may bubble here for page scrolling.
import QtQuick
import "KeyboardViewport.js" as Viewport

Item {
    id: nav

    required property Flickable flick
    property var glide: null
    property real lineStep: 72
    property real pageFraction: 0.85
    property bool arrowScrolling: true
    property bool homeEndEnabled: true

    visible: false

    property var registeredFlick: null

    function rebindController() {
        if (nav.registeredFlick === nav.flick)
            return
        if (nav.registeredFlick)
            Viewport.unregisterController(nav.registeredFlick, nav)
        nav.registeredFlick = nav.flick
        if (nav.registeredFlick)
            Viewport.registerController(nav.registeredFlick, nav)
    }

    onFlickChanged: rebindController()
    Component.onCompleted: rebindController()
    Component.onDestruction: {
        if (nav.registeredFlick)
            Viewport.unregisterController(nav.registeredFlick, nav)
        nav.registeredFlick = null
    }

    KeyboardSpatialNavigator {
        id: spatial
        root: nav.flick
        scrollStep: nav.lineStep
    }

    function maxY() {
        return nav.flick ? Viewport.maximum(nav.flick, false) : 0
    }

    function scrollBy(px) {
        if (!nav.flick || px === 0)
            return false
        const before = nav.flick.contentY
        if (nav.glide && nav.glide.smoothScrollBy)
            nav.glide.smoothScrollBy(px)
        else
            Viewport.setPosition(nav.flick, false, before + px)
        return px < 0 ? before > Viewport.minimum(nav.flick, false) : before < nav.maxY()
    }
    function scrollTo(y) {
        if (!nav.flick)
            return false
        const target = Viewport.bounded(nav.flick, false, y)
        if (Math.abs(target - nav.flick.contentY) < 0.5)
            return false
        if (nav.glide && nav.glide._animateTo)
            nav.glide._animateTo(target)
        else
            nav.flick.contentY = target
        return true
    }

    function handle(event) {
        if (!event || !nav.flick)
            return false
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return false

        if (nav.arrowScrolling && (event.key === Qt.Key_Up || event.key === Qt.Key_Down))
            return spatial.handle(event)

        let handled = false
        if (event.key === Qt.Key_PageUp) {
            if (nav.glide && nav.glide.pageUp) {
                handled = nav.flick.contentY > Viewport.minimum(nav.flick, false)
                nav.glide.pageUp()
            } else handled = nav.scrollBy(-nav.flick.height * nav.pageFraction)
        } else if (event.key === Qt.Key_PageDown) {
            if (nav.glide && nav.glide.pageDown) {
                handled = nav.flick.contentY < Viewport.maximum(nav.flick, false)
                nav.glide.pageDown()
            } else handled = nav.scrollBy(nav.flick.height * nav.pageFraction)
        } else if (nav.homeEndEnabled && event.key === Qt.Key_Home) {
            handled = nav.flick.contentY > Viewport.minimum(nav.flick, false)
            if (handled && nav.glide && nav.glide.toTop) nav.glide.toTop()
            else if (handled) nav.scrollTo(Viewport.minimum(nav.flick, false))
        } else if (nav.homeEndEnabled && event.key === Qt.Key_End) {
            handled = nav.flick.contentY < nav.maxY()
            if (handled && nav.glide && nav.glide.toBottom) nav.glide.toBottom()
            else if (handled) nav.scrollTo(nav.maxY())
        }

        if (handled)
            event.accepted = true
        return handled
    }

    function handleRelease(event) {
        return spatial.handleRelease(event)
    }
}

