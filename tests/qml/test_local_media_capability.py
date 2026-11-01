"""Exercise the real dock and extracted Main entry points with/without their native owner."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / "qml/Main.qml").read_text(encoding="utf-8")

    def block(marker, end):
        start = source.index(marker)
        return source[start:source.index(end, start)]

    capability = block("    readonly property bool localMediaAvailable:", "\n")
    expression = capability.split(":", 1)[1]
    capability = "readonly property bool localMediaAvailable: { const LocalLaunch = win.localLaunchOwner; return " + expression + "; }"
    command = block("    KeyboardCommand {\n        id: openMediaCommand", "    KeyboardCommand {\n        id: openVaultCommand")
    drop = block("        onDropped: (drop) => {", "\n    }\n\n    Shortcut {")
    drop = drop.replace("        onDropped: (drop) => {", "    function dropMedia(drop) {")
    accepted = block("        onAccepted: {\n            if (!win.localMediaAvailable)", "\n    }\n\n    // Vault Add-folder")
    accepted = accepted.replace("        onAccepted: {", "    function acceptMedia() {")
    opened = block("    function openLocalMedia(paths)", "    function openNextToOpen")
    opened = opened.replace("function openLocalMedia(paths) {", "function openLocalMedia(paths) { const LocalLaunch = win.localLaunchOwner;")
    drop = drop.replace("function dropMedia(drop) {", "function dropMedia(drop) { const LocalLaunch = win.localLaunchOwner;")
    root = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "qml"
    qml = '''import QtQuick
import QtTest
import "QML_URI" as UI
Item {
    id: win
    width:1280; height:720
    property var localLaunchOwner: undefined
    property var selectedFiles: ["file:///example.mp4"]
    CAPABILITY
    QtObject { id: nativeOwner; property int opens:0; property int directories:0
        function openAsync(paths) { nativeOwner.opens++ }
        function isDir(path) { nativeOwner.directories++; return false }
    }
    QtObject { id: openMediaDialog; property int opens:0; function open() { openMediaDialog.opens++ } }
    function showFolderDropExplain() { fail("Unexpected folder route") }
    OPENED
    DROP
    ACCEPTED
    COMMAND
    Item { id: wall }
    UI.Taskbar { id:dock; localMediaAvailable:win.localMediaAvailable; open:true }
    SignalSpy { id: opens; target:dock; signalName:"openMediaClicked" }
    SignalSpy { id: recents; target:dock; signalName:"openRecentRequested" }
    TestCase {
        name:"LocalMediaCapability"
        when:windowShown
        function init() { win.localLaunchOwner=undefined; dock.open=true; opens.clear(); recents.clear(); nativeOwner.opens=0; nativeOwner.directories=0; openMediaDialog.opens=0; wait(250) }
        function test_absent_owner_blocks_all_dock_activation() {
            compare(win.localMediaAvailable,false)
            for (const name of ["taskbarOpenMediaAction","taskbarOpenRecentAction"]) {
                const action=findChild(dock,name)
                verify(action !== null); compare(action.enabled,false)
                verify(action.accessibleDescription.length>0)
                mouseClick(action,5,5)
                action.forceActiveFocus(); keyClick(Qt.Key_Return)
                compare(action.activate(Qt.OtherFocusReason),false)
            }
            compare(opens.count,0); compare(recents.count,0)
        }
        function test_present_owner_retains_dock_actions() {
            win.localLaunchOwner=nativeOwner
            compare(win.localMediaAvailable,true)
            const open=findChild(dock,"taskbarOpenMediaAction")
            const recent=findChild(dock,"taskbarOpenRecentAction")
            compare(open.enabled,true); compare(recent.enabled,true)
            mouseClick(open,10,10); compare(opens.count,1)
            recent.forceActiveFocus(); keyClick(Qt.Key_Return); compare(recents.count,1)
            compare(open.activate(Qt.OtherFocusReason),true); compare(opens.count,2)
        }
        function test_absent_owner_rejects_main_entry_points() {
            compare(openMediaCommand.invoke("shortcut"),false)
            compare(openMediaDialog.opens,0)
            win.acceptMedia(); win.openLocalMedia(win.selectedFiles)
            const drop={hasUrls:true, urls:win.selectedFiles, accepted:true}
            win.dropMedia(drop); compare(drop.accepted,false)
            compare(nativeOwner.opens,0); compare(nativeOwner.directories,0)
        }
        function test_present_owner_keeps_main_entry_points() {
            win.localLaunchOwner=nativeOwner
            compare(openMediaCommand.invoke("shortcut"),true); compare(openMediaDialog.opens,1)
            win.acceptMedia(); compare(nativeOwner.opens,1)
            const drop={hasUrls:true,urls:win.selectedFiles,accepted:false}
            win.dropMedia(drop); compare(drop.accepted,true)
            compare(nativeOwner.opens,2); compare(nativeOwner.directories,1)
        }
    }
}
'''
    for key,value in {"QML_URI":root.resolve().as_uri(),"CAPABILITY":capability,"COMMAND":command.replace("    KeyboardCommand", "    UI.KeyboardCommand"),"OPENED":opened,"DROP":drop,"ACCEPTED":accepted}.items():
        qml=qml.replace(key,value)
    with tempfile.TemporaryDirectory(prefix="local-media-capability-") as directory:
        path=Path(directory)
        (path/"tst_local_media.qml").write_text(qml,encoding="utf-8")
        report=path/"results.txt"
        result=subprocess.run([sys.argv[1],"-platform","offscreen","-input",directory,"-o",str(report)+",txt"],env=dict(os.environ,QT_QUICK_BACKEND="software"),timeout=30)
        print(report.read_text(encoding="utf-8") if report.exists() else "No Qt test report")
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
