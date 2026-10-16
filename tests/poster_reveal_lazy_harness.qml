// Actual Qt Quick object-tree and timeline contract. Run offscreen/software with qml.exe.
// The same file must fail against the frozen eager card and pass against the lazy card.
import QtQuick
import "../qml" as UI

Item {
    id: root
    width: 900; height: 600
    property var failures: []
    property int phase: 0
    property var sample: ({title:"Manga One", year:"2017", rating:"8.7", cover:"", author:"T. Writer"})
    property var changed: ({title:"Manga Two", year:"2026", rating:"9.4", cover:"", author:"New Writer"})
    function check(value, what) { if (!value) failures.push("phase " + phase + ": " + what) }
    function search(n, name) {
        if (!n) return null
        if (n.objectName === name) return n
        var c = n.children || []
        for (var i=0; i<c.length; i++) { var found=search(c[i], name); if (found) return found }
        return null
    }
    function textCount(n, text) {
        if (!n) return 0
        var count = n.text !== undefined && String(n.text) === text ? 1 : 0
        var c = n.children || []
        for (var i=0; i<c.length; i++) count += textCount(c[i], text)
        return count
    }
    function textItem(n, value) {
        if (!n) return null
        if (n.text !== undefined && String(n.text) === value) return n
        var c = n.children || []
        for (var i=0; i<c.length; i++) { var found=textItem(c[i], value); if (found) return found }
        return null
    }
    function loader(c) { return search(c, c.objectName + "_reveal_loader") }
    function wrapper(c) { return search(c, c.objectName + "_reveal_wrapper") }
    function named(c, name) { return search(c, name) }
    function next(p, delay) { phase=p; clock.interval=delay; clock.restart() }
    function finish() {
        if (failures.length) console.log("POSTER_LAZY_RED " + failures.length + "\n" + failures.join("\n"))
        else console.log("POSTER_LAZY_GREEN idle-loader-retention-pointer-focus-skeleton-bindings-fades")
        Qt.exit(failures.length ? 2 : 0)
    }
    UI.CataloguePosterCard {
        id: classic
        objectName: "classicProof"
        x: 30; y: 30; width: 140; height: 250
        item: root.sample
    }
    UI.CataloguePosterCard {
        id: gallery
        objectName: "galleryProof"
        x: 210; y: 30; width: 148; height: 260
        visualProfile: "gallery"; hoverSourceText: "IMDb"; item: root.sample
    }
    UI.CataloguePosterCard {
        id: book
        objectName: "bookProof"
        x: 400; y: 30; width: 148; height: 260
        visualProfile: "gallery"; item: root.sample
        showAuthorAtRest: true; revealOnFocus: true
        hoverSourceText: "Book Data"
    }
    UI.CataloguePosterCard {
        id: skeletonCard
        objectName: "skeletonProof"
        x: 580; y: 30; width: 148; height: 260
        visualProfile: "gallery"; skeleton: true
        hoverSourceText: "Pending"
    }
    Connections {
        target: classic
        function onActivated(item) { root.activationCount++; root.lastActivated = item }
    }
    property int activationCount: 0
    property var lastActivated: null
    Timer {
        id: clock; interval: 80; running: true; repeat: false
        onTriggered: {
            if (phase === 0) {
                // Actual descendants, NOT just public ratingVisible. Original eager QML is RED here.
                check(textCount(classic, "2017") === 0, "classic idle year Text must not exist")
                check(textCount(classic, "★ 8.7") === 0, "classic idle rating Text must not exist")
                check(textCount(classic, "▶") === 0, "classic idle play glyph must not exist")
                check(textCount(gallery, "IMDb") === 0, "gallery idle source Text must not exist")
                check(textCount(gallery, "8.7") === 0, "gallery idle rating Text must not exist")
                check(textCount(skeletonCard, "Pending") === 0, "skeleton idle source must not exist")
                check(!!loader(classic) && !loader(classic).active && loader(classic).item === null,
                      "classic inactive Loader owns no object")
                check(!!loader(gallery) && !loader(gallery).active && loader(gallery).item === null,
                      "gallery inactive Loader owns no object")
                check(!!loader(skeletonCard) && !loader(skeletonCard).active,
                      "skeleton has no active Loader")
                check(textCount(book, "T. Writer") === 1, "Biblio author is present outside reveal at rest")
                check(classic.posterWidthToken === 132 && classic.posterRadiusToken === 8
                      && gallery.posterWidthToken === 148 && gallery.posterRadiusToken === 12,
                      "both immutable visual profiles")
                check(classic.ratingVisible === false && gallery.sourceVisible === false,
                      "metadata is visibly absent at rest")
                if (failures.length) { finish(); return }
                // Focus without opt-in must NEVER instantiate the subtree.
                classic.keyboardFocused = true; gallery.keyboardFocused = true
                check(!loader(classic).active && !loader(gallery).active,
                      "nonopt-in keyboard focus leaves Loader inactive")
                check(textCount(classic, "2017") === 0, "focus does not allocate classic rating row")
                classic.keyboardFocused = false; gallery.keyboardFocused = false
                // Opted-in focus does instantiate on demand without pointer hover.
                book.keyboardFocused = true
                check(!!loader(book).item && textCount(book, "Book Data") === 1,
                      "opted-in keyboard focus constructs source Text")
                check(textCount(book, "T. Writer") === 1, "always-on author is not duplicated")
                book.keyboardFocused = false
                // Skeleton hover is suppressed even if its test pointer flag is true.
                skeletonCard.testHovered = true
                check(!loader(skeletonCard).active && textCount(skeletonCard, "Pending") === 0,
                      "hovered skeleton never constructs reveal")
                skeletonCard.testHovered = false
                classic.testHovered = true; gallery.testHovered = true
                check(!!loader(classic).item && !!loader(gallery).item,
                      "first pointer hover immediately loads real subtree")
                check(textCount(classic, "2017") === 1 && textCount(classic, "★ 8.7") === 1,
                      "classic hover constructs actual year/rating Text")
                check(textCount(classic, "▶") === 1 && textItem(classic, "▶").parent.visible
                      && textItem(gallery, "▶") && !textItem(gallery, "▶").parent.visible,
                      "classic actual play ring visible; gallery ring parent hidden")
                check(textCount(gallery, "IMDb") === 1 && textCount(gallery, "8.7") === 1,
                      "gallery hover loads real source/rating Text")
                check(wrapper(classic).width === 140 && wrapper(classic).height === 210
                      && wrapper(gallery).width === 148 && wrapper(gallery).height === 222,
                      "reveal wrapper retains unchanged poster geometry")
                check(loader(classic).item.width === 140 && loader(gallery).item.height === 222,
                      "Loader's actual item fills its poster plane")
                check(wrapper(classic).opacity < 0.4 && wrapper(gallery).opacity < 0.4,
                      "first reveal begins near zero opacity, not immediately opaque")
                root.firstClassicItem=loader(classic).item
                root.firstGalleryItem=loader(gallery).item
                next(1, 70); return
            }
            if (phase === 1) {
                check(wrapper(classic).opacity > 0.05 && wrapper(classic).opacity < 0.95,
                      "first classic fade is in progress")
                check(wrapper(gallery).opacity > 0.04 && wrapper(gallery).opacity < 0.95,
                      "first gallery fade is in progress")
                next(2, 250); return
            }
            if (phase === 2) {
                check(wrapper(classic).opacity > 0.98 && wrapper(gallery).opacity > 0.98,
                      "first fade reaches full opacity")
                classic.testHovered = false; gallery.testHovered = false
                check(loader(classic).item === root.firstClassicItem
                      && loader(gallery).item === root.firstGalleryItem,
                      "hover exit retains the exact instantiated object")
                next(3, 65); return
            }
            if (phase === 3) {
                check(wrapper(classic).opacity < 0.95 && wrapper(classic).opacity > 0.02,
                      "subsequent fade-out is animated")
                check(wrapper(gallery).opacity < 0.95 && wrapper(gallery).opacity > 0.02,
                      "gallery fade-out is animated")
                next(4, 260); return
            }
            if (phase === 4) {
                check(wrapper(classic).opacity < 0.02 && wrapper(gallery).opacity < 0.02,
                      "hide reaches zero opacity")
                check(loader(classic).item === root.firstClassicItem
                      && loader(gallery).item === root.firstGalleryItem,
                      "loaded objects stay retained while invisible")
                classic.testHovered=true; gallery.testHovered=true
                check(wrapper(classic).opacity < 0.4 && wrapper(gallery).opacity < 0.4,
                      "second reveal also begins transparent")
                next(5, 65); return
            }
            if (phase === 5) {
                check(wrapper(classic).opacity > 0.05 && wrapper(classic).opacity < 0.95,
                      "second classic reveal animates")
                check(wrapper(gallery).opacity > 0.04 && wrapper(gallery).opacity < 0.95,
                      "second gallery reveal animates")
                // Update while cached: Text must rebind, including missing rating and source.
                classic.item=root.changed; gallery.item=root.changed
                gallery.hoverSourceText="Catalogue"
                check(textCount(classic, "2026") === 1 && textCount(classic, "2017") === 0,
                      "retained classic year updates")
                check(textCount(classic, "★ 9.4") === 1 && textCount(gallery, "9.4") === 1,
                      "retained rating updates")
                check(textCount(gallery, "Catalogue") === 1 && textCount(gallery, "IMDb") === 0,
                      "retained source updates")
                gallery.item=({title:"No rating",year:"2025",rating:"",cover:""})
                check(gallery.ratingVisible === false && textCount(gallery, "9.4") === 0,
                      "missing rating absent in real retained metadata")
                gallery.hoverSourceText=""
                check(gallery.sourceVisible === false && textCount(gallery, "Catalogue") === 0,
                      "missing source absent in real retained metadata")
                skeletonCard.item=root.sample; skeletonCard.skeleton=false
                check(!loader(skeletonCard).active && !loader(skeletonCard).item,
                      "skeleton-to-real does not eagerly load reveal")
                skeletonCard.testHovered=true
                check(!!loader(skeletonCard).item, "real post-skeleton hover loads reveal")
                skeletonCard.skeleton=true
                check(!loader(skeletonCard).active && !loader(skeletonCard).item,
                      "reset to skeleton unloads all reveal objects")
                skeletonCard.item=null; skeletonCard.testHovered=false; skeletonCard.skeleton=false
                check(!loader(skeletonCard).active && !loader(skeletonCard).item,
                      "reset-to-real without hover stays unloaded")
                classic.testHovered=false; classic.item=root.sample; gallery.testHovered=false
                next(6, 310); return
            }
            if (phase === 6) {
                check(wrapper(classic).opacity < 0.02 && wrapper(gallery).opacity < 0.02,
                      "later exits fade to fully hidden")
                check(loader(classic).item === root.firstClassicItem
                      && loader(gallery).item === root.firstGalleryItem,
                      "last data/visibility transition keeps cached Loader items")
                check(book.showAuthorAtRest && textCount(book, "T. Writer") === 1,
                      "Biblio author remains always on after focus exit")
                finish()
            }
        }
    }
    property var firstClassicItem: null
    property var firstGalleryItem: null
    Timer { interval: 1800; repeat: false; running: true; onTriggered: {
        failures.push("watchdog: 1800ms test ran too long"); finish() } }
}