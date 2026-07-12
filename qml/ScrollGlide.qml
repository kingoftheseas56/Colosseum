// ScrollGlide - the manga reader's eased wheel-scroll, extracted as a drop-in. Attach with
// one line inside a page: `ScrollGlide { flick: theFlickable }`. Wheel notches feed an
// accumulating target; an eased animation glides contentY toward it. Touch/drag
// flicks pass straight through; a user drag re-bases the target.
//
// 2026-07-12 "extra smooth" retune (Hemanth: wheel felt rough on the hand): 240ms OutCubic
// → 420ms OutQuint. Each notch still re-anchors FROM the live position (no input lag);
// the longer, softer tail is what melts the notchy steps together.
import QtQuick

Item {
    id: glide

    property Flickable flick: null
    property real speed: 1.4
    property real _target: 0

    NumberAnimation {
        id: anim
        target: glide.flick
        property: "contentY"
        duration: 420
        easing.type: Easing.OutQuint
    }

    Connections {
        target: glide.flick

        function onMovingChanged() {
            if (glide.flick && glide.flick.moving) {
                anim.stop()
                glide._target = glide.flick.contentY
            }
        }
    }

    WheelHandler {
        target: glide.flick
        acceptedModifiers: Qt.NoModifier

        onWheel: function(e) {
            if (!glide.flick)
                return

            var hmax = Math.max(0, glide.flick.contentHeight - glide.flick.height)
            if (hmax <= 0)
                return

            var base = anim.running ? glide._target : glide.flick.contentY
            glide._target = Math.max(0, Math.min(hmax, base - e.angleDelta.y * glide.speed))
            anim.stop()
            anim.from = glide.flick.contentY
            anim.to = glide._target
            anim.start()
        }
    }
}
