# Vinyl page reference artwork

Prototype-only assets from the approved Brotherhood packet at commit
74563364e5902da19fa6dab6385093dfb9bc2f1c, under agents/vinyl-pages-prototype-refs/.
Album images are byte-identical to images embedded in album-page.html. Song
images are byte-identical to the packet assets. Copyright remains with the
respective rights holders; no ownership or redistribution licence is asserted.

All Thriller catalogue values are deliberately marked UNVERIFIED fixture data.
The text in lyric lines and annotation notes is original synthetic writing,
not song lyrics or Genius text. Images are supplied to the prototype host via
VinylPageData.artworkRoot. Reusable page components do not refer to this folder.
Do not include these fixtures or VinylPagesPrototype in shipping resources.

Run from a complete Colosseum checkout with Qt 6.11.1:

    qml qml/vinyl/VinylPagesPrototype.qml
    qmltestrunner -input tests/qml/tst_vinyl_page_data.qml
    qmltestrunner -input tests/qml/tst_vinyl_album_page.qml
    qmltestrunner -input tests/qml/tst_vinyl_song_page.qml
    qmltestrunner -input tests/qml/tst_vinyl_note_picker.qml

The four tests are also discovered by the existing colosseum.qml target.
The graphical developer strip changes fixture, route, clock and source state.
Ctrl+D restores the strip after Hide controls. No real media is played.

Integration: supply one pageData-compatible object to both pages and the bar;
resolve its asset URLs in the production owner. Preserve playing edition/disc
identity independently of the shown edition. The host wires route signals and
credits overlay; production can wire those same seams without copying pages.
Windows replay and adoption remain the receiving review's responsibility.
