import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ComboBox {
    id:control
    property string sheetTitle:qsTr("Choose an option")
    readonly property var activePhoneSheet:metrics.touch ? phonePopup : null
    AdaptiveLayout { id:metrics;page:control }
    implicitWidth:Math.max(metrics.touchTargetSize,implicitBackgroundWidth+leftInset+rightInset,implicitContentWidth+leftPadding+rightPadding)
    implicitHeight:Math.max(metrics.touchTargetSize,implicitBackgroundHeight+topInset+bottomInset,implicitContentHeight+topPadding+bottomPadding,implicitIndicatorHeight+topPadding+bottomPadding)
    Binding on popup { when:metrics.touch;value:phonePopup;restoreMode:Binding.RestoreBindingOrValue }
    PhoneModalSheet {
        id:phonePopup
        title:control.sheetTitle
        preferredFocusReturn:control
        Repeater {
            model:control.count
            PhoneListItem {
                required property int index
                objectName:"touchComboChoice_"+index
                width:parent.width
                text:control.textAt(index)
                trailingText:index===control.currentIndex ? "✓" : ""
                onClicked: { control.currentIndex=index;control.activated(index);phonePopup.close() }
            }
        }
    }
}
