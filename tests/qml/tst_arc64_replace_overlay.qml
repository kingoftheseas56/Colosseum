import QtQuick
import QtQuick.Window
import QtTest
TestCase {
    name: "Arc64ReplaceOverlay"
    when: windowShown
    Window { id: stage; width:1000; height:720; visible:true; color:"#101013" }
    Arc64Fonts { id: fonts }
    property var subject: null
    function init() {
        tryCompare(fonts,"ready",true)
        // Exercise the production overlay subtree and completion function; the app's
        // native stores/whole detail page are outside this isolated component test.
        var xhr=new XMLHttpRequest()
        xhr.open("GET",Qt.resolvedUrl("../../qml/BiblioBook.qml"),false);xhr.send()
        var source=xhr.responseText
        if (!source.length) skip("Set QML_XHR_ALLOW_FILE_READ=1 to exercise the production overlay subtree")
        var start=source.indexOf("    // Same foreground shade/glass ownership")
        var end=source.indexOf("    ScrollGlide",start)
        verify(start>=0 && end>start)
        var fnStart=source.indexOf("    function finishAudiobookReplace(")
        var fnEnd=source.indexOf("    function startAudiobookRow(",fnStart)
        var imports='import QtQuick; import "'+Qt.resolvedUrl("../../qml")+'"; '
        var shell='Item { id: detail; width:1000; height:720; property Item backdrop: null; '
            +'property bool abReplaceOpen:false; property int abReplaceIndex:0; property var abReplaceRow:null; '
            +'property int starts:0; Theme {id:theme} Item {id:audiobookList} '
            +'function startAudiobookRow(row,replace) { if(replace) starts++ } '
        subject=Qt.createQmlObject(imports+shell+source.slice(fnStart,fnEnd)+source.slice(start,end)+'}',stage.contentItem)
        verify(subject!==null);stage.requestActivate();wait(50)
    }
    function cleanup() { if(subject)subject.destroy();subject=null;wait(0) }
    function openConfirm() {
        subject.abReplaceRow={slug:"fixture"};subject.abReplaceIndex=0;subject.abReplaceOpen=true
        findChild(subject,"biblioReplaceAudiobookFocus").forceActiveFocus(Qt.PopupFocusReason)
        tryCompare(findChild(subject,"biblioReplaceAudiobookConfirm"),"visible",true)
        tryCompare(findChild(subject,"biblioReplaceAudiobookFocus"),"activeFocus",true)
    }
    function test_visualEvidence() {
        if (typeof arc64EvidenceOutputDirectory === "undefined" || !arc64EvidenceOutputDirectory)
            skip("Capture output not requested")
        openConfirm();waitForRendering(subject)
        grabImage(subject).save(arc64EvidenceOutputDirectory + "/replace-overlay.png")
    }
    function test_default_no_and_escape() {
        openConfirm();keyClick(Qt.Key_Return);compare(subject.abReplaceOpen,false);compare(subject.abReplaceRow,null)
        openConfirm();keyClick(Qt.Key_Right);compare(subject.abReplaceIndex,1)
        keyClick(Qt.Key_Escape);compare(subject.abReplaceOpen,false);compare(subject.abReplaceRow,null)
    }
    function test_tab_containment_and_yes_once() {
        openConfirm();keyClick(Qt.Key_Tab);compare(subject.abReplaceIndex,1)
        keyClick(Qt.Key_Backtab);compare(subject.abReplaceIndex,0)
        keyClick(Qt.Key_Right);keyClick(Qt.Key_Return)
        compare(subject.abReplaceOpen,false);compare(subject.starts,1)
        subject.finishAudiobookReplace(true);compare(subject.starts,1)
    }
}
