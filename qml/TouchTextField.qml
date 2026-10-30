import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.TextField {
    id:control
    AdaptiveLayout { id:metrics; page:control }
    FontMetrics { id:textMetrics; font:control.font }
    Binding on implicitHeight {
        when:metrics.touch
        value:Math.max(metrics.touchTargetSize,control.implicitBackgroundHeight+control.topInset+control.bottomInset,
            Math.max(control.contentHeight,textMetrics.height)+control.topPadding+control.bottomPadding)
        restoreMode:Binding.RestoreBindingOrValue
    }
}
