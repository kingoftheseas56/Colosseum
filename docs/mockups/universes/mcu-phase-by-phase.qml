// MCU phase-by-phase mock. Uses the exact production UniverseExtensionPage used by One Piece/DCAU.
// No MCU-specific renderer exists here. Comic post arrays stay empty in this mock until exact
// GetComics numeric post IDs are pinned; never promote empty posts into a production payload.
import QtQuick
import "../../../qml" as Colosseum

Colosseum.UniverseExtensionPage {
    id: root
    width: 1440
    height: 900
    universeName: "Marvel Cinematic Universe"

    readonly property var phaseData: [{"r":"I","m":[["tt0371746","Iron Man"],["tt0800080","The Incredible Hulk"],["tt1228705","Iron Man 2"],["tt0800369","Thor"],["tt0458339","Captain America: The First Avenger"],["tt0848228","The Avengers"]],"tv":[],"sp":[],"adj":[],"c":["Iron Man 2: Public Identity","Iron Man 2: Agents of S.H.I.E.L.D.","Captain America: First Vengeance","The Avengers Prelude: Fury's Big Week","The Avengers: Black Widow Strikes","Iron Man: I Am Iron Man!","Iron Man 2 Adaptation","Thor Adaptation","Captain America: The First Avenger Adaptation","The Avengers Adaptation"]},{"r":"II","m":[["tt1300854","Iron Man 3"],["tt1981115","Thor: The Dark World"],["tt1843866","Captain America: The Winter Soldier"],["tt2015381","Guardians of the Galaxy"],["tt2395427","Avengers: Age of Ultron"],["tt0478970","Ant-Man"]],"tv":[],"sp":[],"adj":[],"c":["Iron Man 3 Prelude","Thor: The Dark World Prelude","Captain America: The Winter Soldier Infinite Comic","Guardians of the Galaxy Prelude","Guardians of the Galaxy Prequel Infinite Comic","Avengers: Age of Ultron Prelude: This Sceptre'd Isle","Ant-Man Prelude","Ant-Man: Scott Lang, Small Time"]},{"r":"III","m":[["tt3498820","Captain America: Civil War"],["tt1211837","Doctor Strange"],["tt3896198","Guardians of the Galaxy Vol. 2"],["tt2250912","Spider-Man: Homecoming"],["tt3501632","Thor: Ragnarok"],["tt1825683","Black Panther"],["tt4154756","Avengers: Infinity War"],["tt5095030","Ant-Man and the Wasp"],["tt4154664","Captain Marvel"],["tt4154796","Avengers: Endgame"],["tt6320628","Spider-Man: Far From Home"]],"tv":[],"sp":[],"adj":[],"c":["Captain America: Civil War Prelude Infinite Comic","Doctor Strange Prelude","Doctor Strange Prelude Infinite Comic: The Zealot","Black Panther Prelude","Avengers: Infinity War Prelude","Captain Marvel Prelude","Captain America: Civil War Prelude #1-4","Guardians of the Galaxy Vol. 2 Prelude","Spider-Man: Homecoming Prelude","Thor: Ragnarok Prelude","Ant-Man and the Wasp Prelude","Avengers: Endgame Prelude","Spider-Man: Far From Home Prelude"]},{"r":"IV","m":[["tt3480822","Black Widow"],["tt9376612","Shang-Chi and the Legend of the Ten Rings"],["tt9032400","Eternals"],["tt10872600","Spider-Man: No Way Home"],["tt9419884","Doctor Strange in the Multiverse of Madness"],["tt10648342","Thor: Love and Thunder"],["tt9114286","Black Panther: Wakanda Forever"]],"tv":[["tt9140560","WandaVision"],["tt9208876","The Falcon and the Winter Soldier"],["tt9140554","Loki"],["tt10168312","What If...?"],["tt10160804","Hawkeye"],["tt10234724","Moon Knight"],["tt10857164","Ms. Marvel"],["tt13623148","I Am Groot"],["tt10857160","She-Hulk: Attorney at Law"]],"sp":[["tt15318872","Werewolf by Night"],["tt13623136","The Guardians of the Galaxy Holiday Special"]],"adj":[],"c":["Marvel's Black Widow Prelude","Eternals: The 500 Year War Infinity Comic"]},{"r":"V","m":[["tt10954600","Ant-Man and the Wasp: Quantumania"],["tt6791350","Guardians of the Galaxy Vol. 3"],["tt10676048","The Marvels"],["tt6263850","Deadpool & Wolverine"],["tt14513804","Captain America: Brave New World"],["tt20969586","Thunderbolts*"]],"tv":[["tt13157618","Secret Invasion"],["tt13623148","I Am Groot"],["tt9140554","Loki"],["tt10168312","What If...?"],["tt13966962","Echo"],["tt15571732","Agatha All Along"],["tt18923754","Daredevil: Born Again"],["tt13623126","Ironheart"]],"sp":[],"adj":[["tt16026746","X-Men '97"],["tt16027074","Your Friendly Neighborhood Spider-Man"]],"c":["Thunderbolts* Dossier","Your Friendly Neighborhood Spider-Man","Doodlepool","Kahhori: Reshaper of Worlds"]},{"r":"VI","m":[["tt10676052","The Fantastic Four: First Steps"],["tt22084616","Spider-Man: Brand New Day"],["tt21357150","Avengers: Doomsday"],["tt21361444","Avengers: Secret Wars"]],"tv":[["tt21066182","Wonder Man"],["tt18923754","Daredevil: Born Again"],["tt23112594","VisionQuest"]],"sp":[],"adj":[["tt13968252","Eyes of Wakanda"],["tt16027014","Marvel Zombies"]],"c":["Fantastic Four: First Steps #1","Fantastic Four: First Foes #1","Fantastic Four: First Foes: Shalla-Bal #1","Fantastic Four: First Foes: Dragon Man #1","Daredevil: Born Again"]}]

    function videoEntries(items, type) {
        var out = []
        for (var i = 0; i < items.length; ++i)
            out.push({ id: items[i][0], type: type, title: items[i][1] })
        return out
    }

    function comicEntries(items) {
        var out = []
        for (var i = 0; i < items.length; ++i)
            out.push({ provider: "getcomics", title: items[i], posts: [] })
        return out
    }

    function buildPayload() {
        var sections = []
        function add(id, title, kind, entries) {
            if (entries.length)
                sections.push({ id: id, title: title, kind: kind, entries: entries })
        }
        for (var i = 0; i < root.phaseData.length; ++i) {
            var p = root.phaseData[i]
            var slug = p.r.toLowerCase()
            add("phase-" + slug + "-movies", "Phase " + p.r + " · Movies",
                "video", root.videoEntries(p.m, "movie"))
            add("phase-" + slug + "-tv", "Phase " + p.r + " · TV Shows",
                "video", root.videoEntries(p.tv, "series"))
            add("phase-" + slug + "-specials", "Phase " + p.r + " · Specials",
                "video", root.videoEntries(p.sp, "movie"))
            add("phase-" + slug + "-adjacent", "Phase " + p.r + " Era · Adjacent Releases",
                "video", root.videoEntries(p.adj, "series"))
            add("phase-" + slug + "-comics", "Phase " + p.r + " · Comics",
                "comic", root.comicEntries(p.c))
        }
        return {
            id: "mcu",
            title: "Marvel Cinematic Universe",
            background: "https://images.metahub.space/background/medium/tt0848228/img",
            sections: sections
        }
    }

    Component.onCompleted: root.payload = root.buildPayload()
}
