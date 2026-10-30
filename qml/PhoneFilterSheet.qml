import QtQuick

PhoneModalSheet {
    id:sheet
    property var choices:[]
    property string selectedKey:""
    property string automationPrefix:""
    signal picked(string key)
    Repeater {
        model:sheet.choices
        delegate:PhoneListItem {
            required property var modelData
            objectName:sheet.automationPrefix+String(modelData.key || "header")
            width:parent ? parent.width : 0
            text:String(modelData.header || modelData.text || modelData.label || "")
            description:String(modelData.sub || modelData.note || "")
            trailingText:modelData.header===undefined && String(modelData.key)===sheet.selectedKey ? "✓" : ""
            enabled:modelData.header===undefined
            onClicked: {
                const key=String(modelData.key)
                sheet.close()
                sheet.picked(key)
            }
        }
    }
}
