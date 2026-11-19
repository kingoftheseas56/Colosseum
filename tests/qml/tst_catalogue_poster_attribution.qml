import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "CataloguePosterAttribution"
    when: windowShown
    width: 900
    height: 700

    Window {
        id: testWindow
        width: 900
        height: 700
        visible: true
        color: "#0c0f18"
    }

    readonly property var audiobookItem: ({
        id: "book-1",
        title: "Audiobook One",
        raw: { workId: "work-1", recordingId: "recording-1" }
    })
    readonly property var movieItem: ({
        id: "tt1234567",
        title: "Movie One",
        releaseInfo: "2026",
        imdbRating: "8.1"
    })

    Component {
        id: railComponent
        Colosseum.PosterRail {
            width: 700
            visualProfile: "gallery"
            items: [testCase.audiobookItem, testCase.movieItem]
        }
    }

    Component {
        id: gridComponent
        Colosseum.CataloguePosterGrid {
            width: 800
            height: 600
            visualProfile: "gallery"
            items: [testCase.audiobookItem, testCase.movieItem]
        }
    }

    function findCard(root, title) {
        if (!root)
            return null
        if (root.item && root.item.title === title && root.hoverSourceText !== undefined)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findCard(children[i], title)
            if (found)
                return found
        }
        return null
    }

    function test_posterRail_hidesAudiobookImdbAndKeepsMovieImdb() {
        const rail = createTemporaryObject(railComponent, testWindow.contentItem)
        verify(rail !== null)

        tryVerify(() => findCard(rail, "Audiobook One") !== null)
        const audiobook = findCard(rail, "Audiobook One")
        const movie = findCard(rail, "Movie One")
        verify(movie !== null)

        compare(audiobook.hoverSourceText, "")
        audiobook.testHovered = true
        compare(audiobook.sourceVisible, false)

        compare(movie.hoverSourceText, "IMDb")
        movie.testHovered = true
        compare(movie.sourceVisible, true)
    }

    function test_posterGrid_hidesAudiobookImdbAndKeepsMovieImdb() {
        const grid = createTemporaryObject(gridComponent, testWindow.contentItem)
        verify(grid !== null)

        tryVerify(() => findCard(grid, "Audiobook One") !== null)
        const audiobook = findCard(grid, "Audiobook One")
        const movie = findCard(grid, "Movie One")
        verify(movie !== null)

        compare(audiobook.hoverSourceText, "")
        audiobook.testHovered = true
        compare(audiobook.sourceVisible, false)

        compare(movie.hoverSourceText, "IMDb")
        movie.testHovered = true
        compare(movie.sourceVisible, true)
    }
}
