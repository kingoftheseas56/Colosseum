import QtQuick
import QtQuick.Window
import QtTest

TestCase {
    id: testCase
    name: "VinylSourceAttribution"
    when: windowShown
    visible: true
    width: 520
    height: 420

    property var rail: null

    TextInput {
        id: outsideInput
        x: 380
        y: 380
        width: 110
        height: 30
    }

    SignalSpy {
        id: sourceSpy
        target: testCase.rail
        signalName: "sourceRequested"
    }

    function createRail(properties) {
        var component = Qt.createComponent("../../qml/vinyl/VinylSourceAttribution.qml");
        verify(component.status === Component.Ready, component.errorString());
        var initialProperties = { width: 160, height: 32 };
        if (properties) {
            for (var key in properties)
                initialProperties[key] = properties[key];
        }
        rail = component.createObject(testCase, initialProperties);
        verify(rail !== null);
        return rail;
    }

    function item(name) {
        if (name === "vinylSourceAttributionSourcesPopup")
            return rail.sourcesPopupControl;
        if (name === "vinylSourceAttributionContributorsPopup")
            return rail.contributorsPopupControl;
        function walk(object, depth) {
            if (!object || depth > 24)
                return null;
            if (object.objectName === name)
                return object;
            var content = object.contentItem;
            if (content && content !== object) {
                var contentMatch = walk(content, depth + 1);
                if (contentMatch)
                    return contentMatch;
            }
            var children = object.children || [];
            for (var i = 0; i < children.length; ++i) {
                var childMatch = walk(children[i], depth + 1);
                if (childMatch)
                    return childMatch;
            }
            return null;
        }
        var window = testCase.Window.window;
        if (window && window.contentItem) {
            var windowMatch = walk(window.contentItem, 0);
            if (windowMatch)
                return windowMatch;
        }
        var popupMatch = walk(rail.sourcesPopupControl, 0)
            || walk(rail.contributorsPopupControl, 0);
        if (popupMatch)
            return popupMatch;
        var railMatch = walk(rail, 0);
        return railMatch || walk(testCase, 0);
    }

    function cleanup() {
        if (rail) {
            rail.destroy();
            rail = null;
        }
        sourceSpy.clear();
    }

    function test_validSourcesExposeProviderAndSourcesActions() {
        createRail({
            lyricsSourceUrl: "https://lrclib.net/api/get?artist=Example&track=Song",
            annotationSourceUrl: "https://genius.com/example-song-lyrics",
            contributors: [{ name: "Writer", url: "https://www.genius.com/writer" }]
        });
        verify(item("vinylSourceAttributionLrclibAction").visible);
        verify(item("vinylSourceAttributionGeniusAction").visible);
        verify(item("vinylSourceAttributionSourcesAction").visible);
        verify(item("vinylSourceAttributionContributorsAction").visible);
        compare(rail.validatedLyricsSourceUrl, rail.lyricsSourceUrl);
        compare(rail.validatedAnnotationSourceUrl, rail.annotationSourceUrl);
        compare(rail.validatedAnnotationSources.length, 1);
    }

    function test_absentSourcesHideProviderAndContributorActions() {
        createRail({ lyricsSourceUrl: "", annotationSourceUrl: "", contributors: [] });
        verify(!item("vinylSourceAttributionLrclibAction").visible);
        verify(!item("vinylSourceAttributionGeniusAction").visible);
        verify(!item("vinylSourceAttributionSourcesAction").visible);
        verify(!item("vinylSourceAttributionContributorsAction").visible);
        compare(rail.sourceEntries.length, 0);
        compare(rail.displayedContributors.length, 0);
    }

    function test_invalidSourcesCannotBecomeVisibleOrLaunchable() {
        createRail({ contributors: [] });
        var invalidLyrics = [
            "http://lrclib.net/api/get",
            "https://lrclib.net.evil.example/api/get",
            "https://lrclib.net@evil.example/api/get",
            "https://lrclib.net:443/api/get",
            "https://lrclib.net/api/with space",
            "https://lrclib.net/api/%0aheader",
            "javascript:alert(1)"
        ];
        for (var i = 0; i < invalidLyrics.length; ++i) {
            rail.lyricsSourceUrl = invalidLyrics[i];
            verify(!item("vinylSourceAttributionLrclibAction").visible, invalidLyrics[i]);
            verify(!item("vinylSourceAttributionSourcesAction").visible, invalidLyrics[i]);
            verify(!rail.requestLyricsSource(invalidLyrics[i]), invalidLyrics[i]);
        }
        var invalidGenius = [
            "http://genius.com/example",
            "https://genius.com.evil.example/example",
            "https://user@genius.com/example",
            "https://www.genius.com:443/example",
            "https://genius.com/example\tprofile",
            "file:///genius.com/example"
        ];
        for (var j = 0; j < invalidGenius.length; ++j) {
            rail.annotationSourceUrl = invalidGenius[j];
            verify(!item("vinylSourceAttributionGeniusAction").visible, invalidGenius[j]);
            verify(!rail.requestAnnotationSource(invalidGenius[j]), invalidGenius[j]);
        }
        compare(sourceSpy.count, 0);
    }

    function test_encodedNonLatinGeniusProfilesRemainValid() {
        var profile = "https://genius.com/artists/%E6%9D%B1%E4%BA%AC";
        createRail({
            annotationSourceUrl: "https://genius.com/songs/%E3%81%82-lyrics",
            contributors: [{ name: "東京", url: profile }]
        });
        compare(rail.validatedAnnotationSourceUrl, rail.annotationSourceUrl);
        compare(rail.displayedContributors[0].profileUrl, profile);
        verify(item("vinylSourceAttributionGeniusAction").visible);
        verify(item("vinylSourceAttributionContributorsAction").visible);
    }

    function test_contributorNamesStayPlainTextAndScrollByKeyboard() {
        var contributors = [{ name: "Writer <Actual Name> & Friends", url: "" }];
        for (var i = 1; i < 34; ++i)
            contributors.push({ name: "Contributor " + i, id: "author-" + i });
        createRail({ contributors: contributors, contextKey: "song-a" });

        var action = item("vinylSourceAttributionContributorsAction");
        verify(action.visible);
        action.forceActiveFocus();
        keyClick(Qt.Key_Return);

        var popup = item("vinylSourceAttributionContributorsPopup");
        tryCompare(popup, "opened", true);
        var scroll = item("vinylSourceAttributionContributorsScroll");
        verify(scroll !== null);
        var firstName = item("vinylSourceAttributionContributor_0PlainName");
        verify(firstName !== null);
        compare(firstName.text, "Writer <Actual Name> & Friends");
        compare(firstName.textFormat, Text.PlainText);
        tryVerify(function() { return scroll.contentHeight > scroll.height; });
        scroll.forceActiveFocus();
        keyClick(Qt.Key_PageDown);
        tryVerify(function() { return scroll.contentY > 0; });
        keyClick(Qt.Key_End);
        tryVerify(function() { return scroll.atYEnd; });
        keyClick(Qt.Key_Home);
        compare(scroll.contentY, 0);
        keyClick(Qt.Key_Escape);
        tryCompare(popup, "opened", false);
        tryVerify(function() { return action.activeFocus; });
    }

    function test_contextSwitchClosesContributorPopupAndReplacesNames() {
        createRail({
            contextKey: "song-old",
            contributors: [{ name: "Old contributor", url: "https://genius.com/old-profile" }]
        });
        var action = item("vinylSourceAttributionContributorsAction");
        mouseClick(action);
        var popup = item("vinylSourceAttributionContributorsPopup");
        tryCompare(popup, "opened", true);
        compare(item("vinylSourceAttributionContributor_0").modelData.name, "Old contributor");

        rail.contextKey = "song-new";
        rail.contributors = [{ name: "New contributor", url: "" }];
        tryCompare(popup, "opened", false);
        mouseClick(action);
        tryCompare(popup, "opened", true);
        compare(item("vinylSourceAttributionContributor_0").modelData.name, "New contributor");
    }

    function test_sourcesPopupKeepsEveryValidatedDistinctAnnotationUrl() {
        var first = "https://genius.com/example-first-lyrics";
        var second = "https://www.genius.com/example-second-lyrics";
        createRail({
            lyricsSourceUrl: "https://lrclib.net/api/get?track=Example",
            annotationSourceUrl: first,
            annotationSources: [first, second, first, "https://genius.com.evil.example/fake"]
        });
        compare(rail.validatedAnnotationSourceUrl, first);
        compare(rail.validatedAnnotationSources.length, 2);
        compare(rail.sourceEntries.length, 3);
        compare(rail.sourceEntries[1].label, "Annotation 1");
        compare(rail.sourceEntries[1].url, first);
        compare(rail.sourceEntries[2].label, "Annotation 2");
        compare(rail.sourceEntries[2].url, second);

        mouseClick(item("vinylSourceAttributionSourcesAction"));
        var popup = item("vinylSourceAttributionSourcesPopup");
        tryCompare(popup, "opened", true);
        verify(item("vinylSourceAttributionSourceLink_0").visible);
        verify(item("vinylSourceAttributionSourceLink_1").visible);
        verify(item("vinylSourceAttributionSourceLink_2").visible);
    }

    function test_outsideClickKeepsFocusOnTheClickedControl() {
        createRail({contributors: [{name: "Original contributor"}]});
        mouseClick(item("vinylSourceAttributionContributorsAction"));
        var popup = item("vinylSourceAttributionContributorsPopup");
        tryCompare(popup, "opened", true);
        mouseClick(outsideInput, 10, 10);
        tryCompare(popup, "opened", false);
        wait(0);
        verify(outsideInput.activeFocus, "Dismissing credits must preserve the control clicked outside");
    }

    function test_openPopupStaysInsideAResizedWindow() {
        var window = testCase.Window.window;
        var oldWidth = window.width, oldHeight = window.height;
        createRail({x: oldWidth - 160, y: oldHeight - 40,
            contributors: [{name: "Original contributor"}]});
        mouseClick(item("vinylSourceAttributionContributorsAction"));
        var popup = item("vinylSourceAttributionContributorsPopup");
        tryCompare(popup, "opened", true);
        try {
            window.width = 320;
            window.height = 240;
            tryVerify(function() { return popup.x >= 0 && popup.x + popup.width <= window.width; },
                2000, "Popup x=" + popup.x + " width=" + popup.width + " viewport=" + window.width);
            tryVerify(function() { return popup.y >= 0 && popup.y + popup.height <= window.height; },
                2000, "Popup y=" + popup.y + " height=" + popup.height + " viewport=" + window.height);
        } finally {
            window.width = oldWidth;
            window.height = oldHeight;
        }
    }
}
