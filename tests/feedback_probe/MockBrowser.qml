import QtQuick
Item {
    property url pageUrl: ""
    property bool supportPage: false
    property bool loading: false
    property string errorText: ""
    signal closeRequested()
}
