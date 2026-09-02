// KeyboardScrollController â€” keyboard face for an existing Flickable/ScrollGlide pair.
// It adds no wheel physics. Unhandled collection keys may bubble here for page scrolling.
import QtQuick

Item {
    id: nav

    required property Flickable flick
    property var glide: null
    property real lineStep: 72
    property real pageFraction: 0.85
    property bool arrowScrolling: true
    property bool homeEndEnabled: true

    visible: false

    function maxY() {
        return nav.flick ? Math.max(0, nav.flick.contentHeight - nav.flick.height) : 0
    }

    function scrollBy(px) {
        if (!nav.flick || px === 0)
            return false
        const before = nav.flick.contentY
        if (nav.glide && nav.glide.smoothScrollBy)
            nav.glide.smoothScrollBy(px)
        else
            nav.flick.contentY = Math.max(0, Math.min(nav.maxY(), before + px))
        return px < 0 ? before > 0 : before < nav.maxY()
    }
    function scrollTo(y) {
        if (!nav.flick)
            return false
        const target = Math.max(0, Math.min(nav.maxY(), y))
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

        let handled = false
        if (event.key === Qt.Key_PageUp) {
            if (nav.glide && nav.glide.pageUp) {
                handled = nav.flick.contentY > 0
                nav.glide.pageUp()
            } else handled = nav.scrollBy(-nav.flick.height * nav.pageFraction)
        } else if (event.key === Qt.Key_PageDown) {
            if (nav.glide && nav.glide.pageDown) {
                handled = nav.flick.contentY < nav.maxY()
                nav.glide.pageDown()
            } else handled = nav.scrollBy(nav.flick.height * nav.pageFraction)
        } else if (nav.arrowScrolling && event.key === Qt.Key_Up)
            handled = nav.scrollBy(-nav.lineStep)
        else if (nav.arrowScrolling && event.key === Qt.Key_Down)
            handled = nav.scrollBy(nav.lineStep)
        else if (nav.homeEndEnabled && event.key === Qt.Key_Home) {
            handled = nav.flick.contentY > 0
            if (handled && nav.glide && nav.glide.toTop) nav.glide.toTop()
            else if (handled) nav.scrollTo(0)
        } else if (nav.homeEndEnabled && event.key === Qt.Key_End) {
            handled = nav.flick.contentY < nav.maxY()
            if (handled && nav.glide && nav.glide.toBottom) nav.glide.toBottom()
            else if (handled) nav.scrollTo(nav.maxY())
        }

        if (handled)
            event.accepted = true
        return handled
    }
}

