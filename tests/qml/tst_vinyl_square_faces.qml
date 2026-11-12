import QtQuick
import QtTest
import "../../qml/vinyl" as Vinyl

TestCase {
    id: test
    name: "VinylSquareFaces"
    when: windowShown
    visible: true
    width: 800
    height: 700

    Component { id: songFactory; Vinyl.VinylSongWidget {} }
    Component { id: artistFactory; Vinyl.VinylArtistWidget {} }

    function sizes() { return [{tag:"house130", size:130}, {tag:"default196", size:196}]; }
    function test_songAllocatedSquare_data() { return sizes(); }
    function test_songAllocatedSquare(data) {
        var song = createTemporaryObject(songFactory, test,
            {width:data.size, title:"Fixture song", artist:"Fixture artist"});
        verify(song !== null);
        compare(song.height, data.size, "Allocated song face must stay square");
        compare(song.implicitWidth, 196);
        verify(song.includesTitle && song.includesSubtitle);
        var artwork = findChild(song, "vinylSongArtwork");
        var paper = findChild(song, "vinylSongPaper");
        verify(artwork !== null && paper !== null);
        fuzzyCompare(artwork.x, data.size * 9 / 220, 0.01);
        fuzzyCompare(artwork.width, data.size * 202 / 220, 0.01);
        compare(artwork.height, artwork.width);
        verify(paper.y > artwork.y && paper.y < artwork.y + artwork.height);
        fuzzyCompare(paper.y + paper.height, artwork.y + artwork.height, 0.01);
        fuzzyCompare(paper.height, data.size * 66 / 220, 0.01);
        var title = findChild(song, "vinylSongTitle");
        var artist = findChild(song, "vinylSongArtist");
        compare(title.text, "Fixture song");
        compare(artist.text, "FIXTURE ARTIST");
        compare(title.textFormat, Text.PlainText);
        verify(title.mapToItem(song, 0, 0).y >= paper.y);
        verify(artist.mapToItem(song, 0, artist.height).y <= paper.y + paper.height);
    }
    function test_artistAllocatedSquare_data() { return sizes(); }
    function test_artistAllocatedSquare(data) {
        var artist = createTemporaryObject(artistFactory, test,
            {width:data.size, title:"Fixture artist"});
        verify(artist !== null);
        compare(artist.height, data.size, "Allocated artist face must stay square");
        compare(artist.implicitWidth, 196);
        compare(artist.includesTitle, false);
        compare(artist.children.length, 1, "Artist has only its record, without a frame or caption");
        compare(artist.initials, "FA");
        var material = findChild(artist, "vinylRecordMaterial");
        verify(material !== null);
        var record = material.parent;
        compare(record.x, 0);
        compare(record.y, 0);
        compare(record.width, data.size);
        compare(record.height, data.size);
        var photo = findChild(artist, "vinylArtistArtwork");
        verify(photo !== null);
        fuzzyCompare(photo.width, data.size * 0.67, 0.01);
        compare(photo.height, photo.width);
        var initials = findChild(artist, "vinylArtistInitials");
        verify(initials !== null && initials.visible);
        compare(initials.text, "FA");
        compare(findChild(artist, "vinylArtistNameCaption"), null);
    }
}