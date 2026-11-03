import QtQuick
Item {
    property url pageUrl: ""
    property bool loading: false
    property string errorText: ""
    signal closeRequested()
}
