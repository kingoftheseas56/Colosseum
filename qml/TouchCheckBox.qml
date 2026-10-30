import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.CheckBox {
    id:control
    AdaptiveLayout { id:metrics; page:control }
    implicitWidth:Math.max(metrics.touchTargetSize,implicitBackgroundWidth+leftInset+rightInset,implicitContentWidth+leftPadding+rightPadding)
    implicitHeight:Math.max(metrics.touchTargetSize,implicitBackgroundHeight+topInset+bottomInset,implicitContentHeight+topPadding+bottomPadding,implicitIndicatorHeight+topPadding+bottomPadding)
}
