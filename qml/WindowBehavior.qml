// WindowBehavior — chrome-free desktop interaction for the secret F11 windowed mode.
// No titlebar is added. Instead: a backmost drag/double-click region is reparented BEHIND
// the existing TopBar's interactive children (so pills/icons keep first claim on pointer
// input while unused TopBar space starts a native system move), plus eight invisible
// edge/corner zones that start native system resizes. All of it self-disables outside
// normal windowed mode, so fullscreen stays pixel-for-pixel unchanged.
import QtQuick
import QtQuick.Window

Item {
    id: root
    required property Window shell
    required property Item dragSurface
    required property var controller
    readonly property bool shellInteractive: controller
                                            && controller.shellWindowed
                                            && !controller.pipMode
                                            && (shell.visibility === Window.Windowed
                                                || shell.visibility === Window.Maximized)
    readonly property bool resizable: shellInteractive
                                      && shell.visibility === Window.Windowed
    anchors.fill: parent
    z: 100000

    // Move/double-click surface, reparented behind TopBar's controls (z: -1). TopBar's
    // pills/system icons sit above it, so unused space drags while controls still click.
    Item {
        parent: root.dragSurface
        anchors.fill: parent
        z: -1
        enabled: root.shellInteractive

        DragHandler {
            target: null
            enabled: root.shell.visibility === Window.Windowed
            acceptedButtons: Qt.LeftButton
            onActiveChanged: if (active)
                root.controller.startSystemMove(root.shell)
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.DragThreshold
            onDoubleTapped: root.controller.toggleMaximized(root.shell)
        }
    }

    component ResizeZone: MouseArea {
        required property int edges
        property int cursor: Qt.ArrowCursor
        enabled: root.resizable
        hoverEnabled: true
        cursorShape: cursor
        onPressed: mouse => {
            root.controller.startSystemResize(root.shell, edges)
            mouse.accepted = true
        }
    }

    ResizeZone { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 6; edges: Qt.LeftEdge; cursor: Qt.SizeHorCursor }
    ResizeZone { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 6; edges: Qt.RightEdge; cursor: Qt.SizeHorCursor }
    ResizeZone { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 6; edges: Qt.TopEdge; cursor: Qt.SizeVerCursor }
    ResizeZone { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 6; edges: Qt.BottomEdge; cursor: Qt.SizeVerCursor }
    ResizeZone { anchors.left: parent.left; anchors.top: parent.top; width: 10; height: 10; edges: Qt.TopEdge | Qt.LeftEdge; cursor: Qt.SizeFDiagCursor }
    ResizeZone { anchors.right: parent.right; anchors.top: parent.top; width: 10; height: 10; edges: Qt.TopEdge | Qt.RightEdge; cursor: Qt.SizeBDiagCursor }
    ResizeZone { anchors.left: parent.left; anchors.bottom: parent.bottom; width: 10; height: 10; edges: Qt.BottomEdge | Qt.LeftEdge; cursor: Qt.SizeBDiagCursor }
    ResizeZone { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 10; height: 10; edges: Qt.BottomEdge | Qt.RightEdge; cursor: Qt.SizeFDiagCursor }
}
