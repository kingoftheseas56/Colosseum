import QtQuick
import QtTest
import "../../qml" as App

Item {
    width: 1280; height: 1000
    Component { id: viewComponent; App.FeedbackWebView {} }
    TestCase {
        name: "FeedbackLiveWebEngine"
        when: windowShown
        function test_owned_feedback_page_renders_without_submitting() {
            const view=createTemporaryObject(viewComponent,parent,{width:1280,height:1000,
                pageUrl:"https://getcolosseum.world/feedback/?version=qualification&platform=" + Qt.platform.os});
            verify(view);
            const web=findChild(view,"feedbackWebDocument");verify(web);
            tryVerify(function() { return web.loadProgress === 100 && !view.loading },30000);
            compare(view.errorText,"");
            let snapshot="";
            web.runJavaScript("JSON.stringify({form:!!document.querySelector('#feedback-form'),options:document.querySelectorAll('#area option').length,version:document.querySelector('#environment')?.textContent,sendEnabled:!!document.querySelector('#send:not(:disabled)')})",
                function(value) { snapshot=value || ""; });
            tryVerify(function() { return snapshot.length>0 },10000);
            const state=JSON.parse(snapshot);
            const platformNames = {windows:"Windows",linux:"Linux",osx:"macOS",android:"Android"};
            verify(state.form);compare(state.options,6);verify(state.version.indexOf(platformNames[Qt.platform.os] || "Web")>=0);
            // Config may finish after the initial document; sending itself is never invoked.
            let ready=false;
            tryVerify(function() {
                if (!ready) web.runJavaScript("!!document.querySelector('#send:not(:disabled)')",function(value) { ready=value; });
                return ready;
            },10000);
        }
    }
}
