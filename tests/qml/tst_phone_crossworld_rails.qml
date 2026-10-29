import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneCrossworldRails"
    when: windowShown

    Window {
        id: host
        width: 318
        height: 720
        visible: true
        color: "#080a10"
    }

    Component {
        id: trendingComponent
        App.TrendingTop10 {
            width: host.width
            namePrefix: "crossworldTrend_"
            items: testCase.makeTrendingItems()
        }
    }

    Component {
        id: bookRailComponent
        App.BiblioBookRail {
            width: host.width
            title: "Local books"
            showSeeAll: false
            items: testCase.makeBookItems()
        }
    }

    SignalSpy { id: trendSpy; signalName: "itemClicked" }
    SignalSpy { id: bookSpy; signalName: "itemActivated" }

    function makeTrendingItems() {
        return [
            { caption: "Local title one", cover: "", c1: "#554", c2: "#221" },
            { caption: "Local title two", cover: "", c1: "#554", c2: "#221" },
            { caption: "Local title three", cover: "", c1: "#554", c2: "#221" },
            { caption: "The Local Book With An Exceptionally Long Title That Must Remain Readable",
              cover: "", c1: "#554", c2: "#221" },
            { caption: "Local title five", cover: "", c1: "#554", c2: "#221" },
            { caption: "Local title six", cover: "", c1: "#554", c2: "#221" }
        ]
    }

    function makeBookItems() {
        return [
            { id: "local-1", title: "Local Book One", author: "Local Author One",
              cover: "", rating: "4.7", source: "Local catalogue" },
            { id: "local-2", title: "Local Book Two", author: "Local Author Two",
              cover: "", rating: "4.2", source: "Local catalogue" },
            { id: "local-3", title: "Local Book Three", author: "Local Author Three",
              cover: "", rating: "4.9", source: "Local catalogue" },
            { id: "local-4",
              title: "The Local Book With An Exceptionally Long Title That Must Remain Readable",
              author: "An Exceptionally Long Local Author Name That Must Stay At Rest",
              cover: "", rating: "4.5", source: "Local catalogue" },
            { id: "local-5", title: "Local Book Five", author: "Local Author Five",
              cover: "", rating: "4.1", source: "Local catalogue" },
            { id: "local-6", title: "Local Book Six", author: "Local Author Six",
              cover: "", rating: "4.3", source: "Local catalogue" }
        ]
    }

    function childWithName(root, objectName) {
        if (!root)
            return null
        if (root.objectName === objectName)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = childWithName(children[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function enableCompact(item) {
        if (!item || !("compactLayout" in item))
            return false
        item.compactLayout = true
        return true
    }

    function touch(item) {
        const point = item.mapToItem(host.contentItem, item.width / 2, item.height / 2)
        verify(point.x >= 0 && point.x <= host.width
               && point.y >= 0 && point.y <= host.height,
               "touch target is inside the visible local host")
        touchEvent(host.contentItem).press(0, host.contentItem, point.x, point.y).commit()
        wait(20)
        touchEvent(host.contentItem).release(0, host.contentItem, point.x, point.y).commit()
        wait(20)
    }

    function test_compact_sizing_seams_exist() {
        const trend = createTemporaryObject(trendingComponent, host.contentItem)
        const books = createTemporaryObject(bookRailComponent, host.contentItem)
        verify(trend !== null)
        verify(books !== null)
        verify("compactLayout" in trend,
               "TrendingTop10 must expose an opt-in compactLayout property")
        verify("posterWidthOverride" in trend,
               "TrendingTop10 must expose a posterWidthOverride property")
        verify("compactLayout" in books,
               "BiblioBookRail must expose an opt-in compactLayout property")
        verify("posterWidthOverride" in books,
               "BiblioBookRail must expose a posterWidthOverride property")
        verify("effectivePosterWidth" in trend && "effectivePosterHeight" in trend
               && "effectiveRailHeight" in trend,
               "TrendingTop10 must expose effective poster and rail sizing")
        verify("effectivePosterWidth" in books && "effectivePosterHeight" in books
               && "effectiveRailHeight" in books,
               "BiblioBookRail must expose effective poster and rail sizing")
    }

    function test_default_geometry_is_preserved() {
        const trend = createTemporaryObject(trendingComponent, host.contentItem)
        const books = createTemporaryObject(bookRailComponent, host.contentItem)
        verify(trend !== null)
        verify(books !== null)
        if (!("compactLayout" in trend) || !("compactLayout" in books))
            return
        wait(0)
        compare(trend.effectivePosterWidth, 132)
        compare(trend.effectivePosterHeight, 196)
        compare(trend.effectiveStripHeight, 212)
        const trendRow = childWithName(trend, "trendingTop10Row_crossworldTrend_")
        verify(trendRow !== null, "default Top 10 row is discoverable")
        const trendPoster = childWithName(trend, "crossworldTrend_poster_0")
        verify(trendPoster !== null, "default Top 10 poster is discoverable")
        if (trendPoster) {
            compare(trendPoster.width, 132)
            compare(trendPoster.height, 196)
        }
        if (trendRow) {
            compare(trendRow.spacing, 30)
            compare(trendRow.leftPadding, 6)
        }
        compare(books.effectivePosterWidth, 130)
        compare(books.effectivePosterHeight, 182)
        compare(books.effectiveShelfHeight, 218)
        const bookRow = childWithName(books, "biblioBookRailCardRow")
        verify(bookRow !== null, "default book row is discoverable")
        if (bookRow) {
            compare(bookRow.spacing, 16)
            compare(bookRow.leftPadding, 6)
            compare(bookRow.rightPadding, 20)
        }
        const bookCover = childWithName(books, "biblioBookRailCover_0")
        verify(bookCover !== null, "default book cover is discoverable")
        if (bookCover) {
            compare(bookCover.width, 130)
            compare(bookCover.height, 182)
        }
        books.ranked = true
        compare(books.effectiveShelfHeight, 236)
        if (bookRow) {
            compare(bookRow.spacing, 26)
            compare(bookRow.leftPadding, 30)
        }
        const defaultRank = childWithName(books, "biblioBookRailLargeRank_0")
        verify(defaultRank !== null && defaultRank.visible,
               "legacy ranked mode retains its large rank numeral")
        compare(trend.effectiveRailHeight, trend.implicitHeight)
        compare(books.effectiveRailHeight, books.implicitHeight)
    }

    function test_compact_trending_fits_three_cards_and_two_line_title() {
        const trend = createTemporaryObject(trendingComponent, host.contentItem)
        verify(trend !== null)
        if (!enableCompact(trend))
            return
        trend.posterWidthOverride = 98
        wait(0)
        compare(trend.effectivePosterWidth, 98)
        compare(trend.effectivePosterHeight, 147)
        const row = childWithName(trend, "trendingTop10Row_crossworldTrend_")
        verify(row !== null)
        if (row) {
            compare(row.spacing, 12)
            compare(row.leftPadding, 0)
            const first = childWithName(row, "crossworldTrend_0")
            const second = childWithName(row, "crossworldTrend_1")
            const third = childWithName(row, "crossworldTrend_2")
            verify(first !== null && second !== null && third !== null,
                   "three local posters are instantiated")
            if (first && second && third) {
                compare(first.width, 98)
                compare(second.width, 98)
                compare(third.width, 98)
                compare(first.x, 0)
                compare(second.x, 110)
                compare(third.x + third.width, 318)
            }
        }
        const caption = childWithName(trend, "crossworldTrend_compactCaption_3")
        const rank = childWithName(trend, "crossworldTrend_compactRank_3")
        const cover = childWithName(trend, "crossworldTrend_poster_3")
        const card = childWithName(trend, "crossworldTrend_3")
        verify(caption !== null && caption.visible,
               "the long compact title remains visible")
        verify(rank !== null && rank.visible,
               "compact rank is overlaid on poster art")
        verify(cover !== null && card !== null)
        if (caption && rank && cover && card) {
            compare(caption.font.pixelSize, 13)
            compare(caption.maximumLineCount, 2)
            compare(cover.y, 0)
            compare(cover.height, 147)
            compare(caption.width, 98)
            compare(caption.lineCount, 2, "Long captions use both available lines")
            verify(caption.height >= caption.implicitHeight,
                   "The title box fits the platform font's two lines")
            compare(trend.effectiveStripHeight, cover.height + 8 + caption.height)
            compare(caption.y, cover.y + cover.height + 8)
            verify(caption.y + caption.height <= card.height
                   && card.height === trend.effectiveStripHeight,
                   "caption bottom=" + (caption.y + caption.height)
                   + ", card height=" + card.height
                   + ", strip height=" + trend.effectiveStripHeight)
            verify(rank.x >= cover.x && rank.y >= cover.y
                   && rank.x + rank.width <= cover.x + cover.width
                   && rank.y + rank.height <= cover.y + cover.height,
                   "the rank badge remains over the cover art")
        }
        verify(trend.effectiveRailHeight > trend.effectiveStripHeight,
               "the effective rail height includes the header and spacing")
    }

    function test_compact_book_cards_keep_author_reveal_and_rank_semantics() {
        const books = createTemporaryObject(bookRailComponent, host.contentItem)
        verify(books !== null)
        if (!enableCompact(books))
            return
        books.posterWidthOverride = 98
        wait(0)
        compare(books.effectivePosterWidth, 98)
        compare(books.effectivePosterHeight, 147)
        const cover = childWithName(books, "biblioBookRailCover_3")
        const title = childWithName(books, "biblioBookRailTitle_3")
        const author = childWithName(books, "biblioBookRailAuthor_3")
        const row = childWithName(books, "biblioBookRailCardRow")
        verify(cover !== null && title !== null && author !== null)
        verify(row !== null)
        if (cover && title && author) {
            compare(cover.width, 98)
            compare(cover.height, 147)
            verify(title.visible && author.visible,
                   "long title and author are shown at rest")
            compare(title.font.pixelSize, 13)
            compare(title.lineCount, 2, "Long book titles use both available lines")
            verify(title.height >= title.implicitHeight,
                   "The title box fits the platform font's two lines")
            verify(author.height >= author.contentHeight,
                   "The author box fits the platform font")
            compare(title.maximumLineCount, 2)
            compare(title.text,
                    "The Local Book With An Exceptionally Long Title That Must Remain Readable")
            compare(author.text, "An Exceptionally Long Local Author Name That Must Stay At Rest")
            compare(author.y, title.y + title.height + 2)
            compare(author.y + author.height, books.effectiveShelfHeight)
        }
        if (row) {
            const firstCover = childWithName(books, "biblioBookRailCover_0")
            const secondCover = childWithName(books, "biblioBookRailCover_1")
            const thirdCover = childWithName(books, "biblioBookRailCover_2")
            verify(firstCover !== null && secondCover !== null && thirdCover !== null)
            if (firstCover && secondCover && thirdCover) {
                compare(firstCover.mapToItem(row, 0, 0).x, 0)
                compare(secondCover.mapToItem(row, 0, 0).x, 110)
                compare(thirdCover.mapToItem(row, 0, 0).x + thirdCover.width, 318)
            }
        }
        verify(!books.ratingVisibleAt(3))
        books.testHoveredIndex = 3
        verify(books.revealedAt(3))
        verify(books.ratingVisibleAt(3), "unranked ratings reveal on hover")
        verify(books.sourceVisibleAt(3), "source reveal remains available")
        books.ranked = true
        verify(!books.ratingVisibleAt(3), "ranked mode suppresses the rating")
        const rank = childWithName(books, "biblioBookRailCompactRank_3")
        const largeRank = childWithName(books, "biblioBookRailLargeRank_3")
        verify(rank !== null && rank.visible,
               "ranked compact mode shows its rank over the cover")
        verify(largeRank !== null && !largeRank.visible,
               "ranked compact mode removes the legacy oversized numeral")
        compare(books.effectiveShelfHeight, author.y + author.height)
    }

    function test_compact_book_skeleton_matches_cover_geometry() {
        const books = createTemporaryObject(bookRailComponent, host.contentItem)
        verify(books !== null)
        if (!enableCompact(books))
            return
        books.items = []
        books.loading = true
        books.ranked = true
        wait(0)
        compare(books.effectivePosterWidth, 98)
        compare(books.effectivePosterHeight, 147)
        verify(books.effectiveShelfHeight >= 202)
        const first = childWithName(books, "biblioBookRailSkeleton_0")
        const second = childWithName(books, "biblioBookRailSkeleton_1")
        const third = childWithName(books, "biblioBookRailSkeleton_2")
        verify(first !== null && second !== null && third !== null,
               "six local loading skeletons reserve shelf space")
        if (first && second && third) {
            compare(first.x, 0)
            compare(first.width, books.effectivePosterWidth)
            compare(first.height, books.effectivePosterHeight)
            compare(second.x - first.x, 110)
            compare(third.x + third.width, 318)
        }
        verify(books.effectiveRailHeight > books.effectiveShelfHeight,
               "loading rail reservation includes header and spacing")
    }

    function test_compact_caption_is_part_of_card_touch_target() {
        const trend = createTemporaryObject(trendingComponent, host.contentItem)
        verify(trend !== null && enableCompact(trend))
        wait(30)
        trendSpy.target = trend
        trendSpy.clear()
        const caption = childWithName(trend, "crossworldTrend_compactCaption_0")
        verify(caption !== null)
        touch(caption)
        compare(trendSpy.count, 1, "The title below the cover opens its card")
        compare(trendSpy.signalArguments[0][0], 0)
        trendSpy.clear()
        touch(childWithName(trend, "crossworldTrend_poster_0"))
        compare(trendSpy.count, 1, "A cover tap opens exactly once")
        trendSpy.target = null
    }

    function test_compact_touch_activates_the_selected_indices() {
        const trend = createTemporaryObject(trendingComponent, host.contentItem)
        const books = createTemporaryObject(bookRailComponent, host.contentItem)
        verify(trend !== null && books !== null)
        if (books)
            books.y = 250
        if (!enableCompact(trend) || !enableCompact(books))
            return
        trend.posterWidthOverride = 98
        books.posterWidthOverride = 98
        wait(0)
        const trendPoster = childWithName(trend, "crossworldTrend_poster_2")
        const bookCover = childWithName(books, "biblioBookRailCover_2")
        verify(trendPoster !== null && bookCover !== null)
        if (!trendPoster || !bookCover)
            return
        trendSpy.target = trend
        bookSpy.target = books
        trendSpy.clear()
        bookSpy.clear()
        touch(trendPoster)
        touch(bookCover)
        compare(trendSpy.count, 1)
        compare(trendSpy.signalArguments[0][0], 2)
        compare(bookSpy.count, 1)
        compare(bookSpy.signalArguments[0][0].id, "local-3")
        trendSpy.target = null
        bookSpy.target = null
    }
}
