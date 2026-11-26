pragma ComponentBehavior: Bound
import QtQuick

// Vault rows use the same hero renderer and navigation as the other worlds.
FocusScope {
    id: hero
    property var rows: []
    property alias currentIndex: carousel.index
    readonly property var row: rows.length ? rows[Math.max(0, Math.min(currentIndex, rows.length - 1))] : ({})
    signal activated(var row)
    signal detailsRequested(var row)
    signal moveDown()
    height: 336

    function genreText(genres) {
        return typeof genres === "string" ? genres
            : (genres ? Array.prototype.slice.call(genres, 0, 2).join(", ") : "")
    }

    onRowsChanged: currentIndex = 0
    onActiveFocusChanged: if (activeFocus) carousel.focusSlides(Qt.OtherFocusReason)

    FeaturedCarousel {
        id: carousel
        objectName: "vaultFeaturedCarousel"
        anchors.fill: parent
        automationId: "vaultFeaturedCarouselView"
        kicker: Number(hero.row.progressFraction || 0) > 0 ? qsTr("Continue your story") : qsTr("In your library")
        slides: hero.rows.map(row => ({
            raw: row,
            title: String(row.title || qsTr("Your library")),
            logo: row.logo || "",
            art: row.backdrop || row.art || row.cover || row.poster || "",
            blurb: String(row.blurb || row.description || row.subtitle || ""),
            metadataText: [row.year || "", row.runtimeText || "",
                hero.genreText(row.genres),
                row.imdbRating ? "IMDb " + row.imdbRating : ""].filter(v => String(v).length).join("  ·  "),
            progressFraction: Number(row.progressFraction || 0),
            progressText: String(row.progressText || qsTr("In progress")),
            cta: Number(row.progressFraction || 0) > 0 ? qsTr("Resume")
                : (row.kind === "book" || row.kind === "comic" ? qsTr("Read") : qsTr("Watch")),
            secondaryLabel: qsTr("Details")
        }))
        onPrimaryClicked: index => { if (index >= 0 && index < hero.rows.length) hero.activated(hero.rows[index]) }
        onSecondaryClicked: index => { if (index >= 0 && index < hero.rows.length) hero.detailsRequested(hero.rows[index]) }
    }
    Timer {
        objectName: "vaultBillboardAdvance"
        interval: 9000
        repeat: true
        running: hero.visible && hero.rows.length > 1 && !hero.activeFocus && !hover.hovered
        onTriggered: hero.currentIndex = (hero.currentIndex + 1) % hero.rows.length
    }
    HoverHandler { id: hover }
    Keys.onDownPressed: hero.moveDown()
}
