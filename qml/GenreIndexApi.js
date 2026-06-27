// GenreIndexApi.js — data for the Colosseum genre INDEX ("Explore" / full genre list, manga lane).
//
// Recreates MyAnimeList's `manga.php` genre index: the four grouped sections — Genres · Explicit Genres ·
// Themes · Demographics — each a grid of genre tiles (name + count), rendered as the cover mosaic. Live
// COUNTS come from Jikan `/genres/manga` (one keyless call); COVERS are BAKED below (download-once model:
// one representative, de-duplicated cover per genre, harvested from Jikan top-by-readers — see
// mocks/_gen_genre_index.js). Baking covers means every tile has art instantly, with zero per-tile
// network burst on open. Tiles route into GenrePage by name.
.pragma library

var JIKAN = "https://api.jikan.moe/v4";

// MAL's `manga.php` section membership (the only structural bit hardcoded; everything else → Themes).
var GENRES = ["Action","Adventure","Avant Garde","Award Winning","Boys Love","Comedy","Drama","Fantasy",
              "Girls Love","Gourmet","Horror","Mystery","Romance","Sci-Fi","Slice of Life","Sports",
              "Supernatural","Suspense"];
var EXPLICIT = ["Ecchi","Erotica","Hentai"];
var DEMOGRAPHICS = ["Shounen","Shoujo","Seinen","Josei","Kids"];

// baked representative cover per genre (de-duplicated so each genre shows its OWN art).
var COVERS = {
    "Action": "https://cdn.myanimelist.net/images/manga/1/157897l.jpg",
    "Adventure": "https://cdn.myanimelist.net/images/manga/2/253146l.jpg",
    "Avant Garde": "https://cdn.myanimelist.net/images/manga/3/161311l.jpg",
    "Award Winning": "https://cdn.myanimelist.net/images/manga/3/216464l.jpg",
    "Boys Love": "https://cdn.myanimelist.net/images/manga/1/220586l.jpg",
    "Comedy": "https://cdn.myanimelist.net/images/manga/3/80661l.jpg",
    "Drama": "https://cdn.myanimelist.net/images/manga/2/37846l.jpg",
    "Fantasy": "https://cdn.myanimelist.net/images/manga/3/222295l.jpg",
    "Girls Love": "https://cdn.myanimelist.net/images/manga/1/232311l.jpg",
    "Gourmet": "https://cdn.myanimelist.net/images/manga/1/115803l.jpg",
    "Horror": "https://cdn.myanimelist.net/images/manga/3/114037l.jpg",
    "Mystery": "https://cdn.myanimelist.net/images/manga/3/186922l.jpg",
    "Romance": "https://cdn.myanimelist.net/images/manga/2/245008l.jpg",
    "Sci-Fi": "https://cdn.myanimelist.net/images/manga/5/260006l.jpg",
    "Slice of Life": "https://cdn.myanimelist.net/images/manga/3/266834l.jpg",
    "Sports": "https://cdn.myanimelist.net/images/manga/2/258225l.jpg",
    "Supernatural": "https://cdn.myanimelist.net/images/manga/3/210341l.jpg",
    "Suspense": "https://cdn.myanimelist.net/images/manga/1/258245l.jpg",
    "Ecchi": "https://cdn.myanimelist.net/images/manga/2/172982l.jpg",
    "Erotica": "https://cdn.myanimelist.net/images/manga/2/178011l.jpg",
    "Adult Cast": "https://cdn.myanimelist.net/images/manga/3/258224l.jpg",
    "Anthropomorphic": "https://cdn.myanimelist.net/images/manga/1/115443l.jpg",
    "CGDCT": "https://cdn.myanimelist.net/images/manga/2/259651l.jpg",
    "Childcare": "https://cdn.myanimelist.net/images/manga/3/219741l.jpg",
    "Combat Sports": "https://cdn.myanimelist.net/images/manga/2/250313l.jpg",
    "Crossdressing": "https://cdn.myanimelist.net/images/manga/3/267782l.jpg",
    "Delinquents": "https://cdn.myanimelist.net/images/manga/3/196272l.jpg",
    "Detective": "https://cdn.myanimelist.net/images/manga/1/264496l.jpg",
    "Educational": "https://cdn.myanimelist.net/images/manga/3/200497l.jpg",
    "Gag Humor": "https://cdn.myanimelist.net/images/manga/2/166124l.jpg",
    "Gore": "https://cdn.myanimelist.net/images/manga/3/145997l.jpg",
    "Harem": "https://cdn.myanimelist.net/images/manga/1/181212l.jpg",
    "High Stakes Game": "https://cdn.myanimelist.net/images/manga/1/278020l.jpg",
    "Historical": "https://cdn.myanimelist.net/images/manga/3/179023l.jpg",
    "Idols (Female)": "https://cdn.myanimelist.net/images/manga/3/295874l.jpg",
    "Idols (Male)": "https://cdn.myanimelist.net/images/manga/2/256420l.jpg",
    "Isekai": "https://cdn.myanimelist.net/images/manga/3/167639l.jpg",
    "Iyashikei": "https://cdn.myanimelist.net/images/manga/5/259524l.jpg",
    "Love Polygon": "https://cdn.myanimelist.net/images/manga/1/262324l.jpg",
    "Love Status Quo": "https://cdn.myanimelist.net/images/manga/1/268387l.jpg",
    "Magical Sex Shift": "https://cdn.myanimelist.net/images/manga/1/188806l.jpg",
    "Mahou Shoujo": "https://cdn.myanimelist.net/images/manga/3/259289l.jpg",
    "Martial Arts": "https://cdn.myanimelist.net/images/manga/3/249658l.jpg",
    "Mecha": "https://cdn.myanimelist.net/images/manga/1/145061l.jpg",
    "Medical": "https://cdn.myanimelist.net/images/manga/3/201154l.jpg",
    "Memoir": "https://cdn.myanimelist.net/images/manga/2/180846l.jpg",
    "Military": "https://cdn.myanimelist.net/images/manga/3/243675l.jpg",
    "Music": "https://cdn.myanimelist.net/images/manga/3/66993l.jpg",
    "Mythology": "https://cdn.myanimelist.net/images/manga/3/161911l.jpg",
    "Organized Crime": "https://cdn.myanimelist.net/images/manga/3/210716l.jpg",
    "Otaku Culture": "https://cdn.myanimelist.net/images/manga/2/215054l.jpg",
    "Parody": "https://cdn.myanimelist.net/images/manga/1/230705l.jpg",
    "Performing Arts": "https://cdn.myanimelist.net/images/manga/2/209753l.jpg",
    "Pets": "https://cdn.myanimelist.net/images/manga/1/182059l.jpg",
    "Psychological": "https://cdn.myanimelist.net/images/manga/2/188918l.jpg",
    "Racing": "https://cdn.myanimelist.net/images/manga/2/186990l.jpg",
    "Reincarnation": "https://cdn.myanimelist.net/images/manga/3/233991l.jpg",
    "Reverse Harem": "https://cdn.myanimelist.net/images/manga/1/206834l.jpg",
    "Samurai": "https://cdn.myanimelist.net/images/manga/1/259070l.jpg",
    "School": "https://cdn.myanimelist.net/images/manga/1/209370l.jpg",
    "Showbiz": "https://cdn.myanimelist.net/images/manga/2/255389l.jpg",
    "Space": "https://cdn.myanimelist.net/images/manga/3/170572l.jpg",
    "Strategy Game": "https://cdn.myanimelist.net/images/manga/2/149018l.jpg",
    "Super Power": "https://cdn.myanimelist.net/images/manga/2/204842l.jpg",
    "Survival": "https://cdn.myanimelist.net/images/manga/1/197883l.jpg",
    "Team Sports": "https://cdn.myanimelist.net/images/manga/5/213340l.jpg",
    "Time Travel": "https://cdn.myanimelist.net/images/manga/2/153742l.jpg",
    "Urban Fantasy": "https://cdn.myanimelist.net/images/manga/3/252929l.jpg",
    "Vampire": "https://cdn.myanimelist.net/images/manga/2/269907l.jpg",
    "Video Game": "https://cdn.myanimelist.net/images/manga/2/228144l.jpg",
    "Villainess": "https://cdn.myanimelist.net/images/manga/5/303096l.jpg",
    "Visual Arts": "https://cdn.myanimelist.net/images/manga/2/204827l.jpg",
    "Workplace": "https://cdn.myanimelist.net/images/manga/3/155740l.jpg",
    "Josei": "https://cdn.myanimelist.net/images/manga/1/262323l.jpg",
    "Kids": "https://cdn.myanimelist.net/images/manga/2/265951l.jpg",
    "Seinen": "https://cdn.myanimelist.net/images/manga/2/188925l.jpg",
    "Shoujo": "https://cdn.myanimelist.net/images/manga/2/256907l.jpg",
    "Shounen": "https://cdn.myanimelist.net/images/manga/3/180031l.jpg"
};

// stable per-genre gradient fallback (only used if a name has no baked cover — e.g. Hentai under sfw).
var SWATCH = [ ["#c9683f","#5a2816"], ["#4a6478","#1a2832"], ["#c93f8a","#5a1640"], ["#7a9a3f","#2e3a16"],
               ["#9a5a4f","#36201c"], ["#3fa0b0","#163e46"], ["#8a5ac9","#36205a"], ["#3f5640","#111b12"],
               ["#5a3a3f","#160d0b"], ["#3c4a63","#0e121b"], ["#b08a3f","#3a2c12"], ["#5b3a64","#170d1b"] ];
function swatch(name) {
    var h = 0;
    for (var i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) & 0xffff;
    return SWATCH[h % SWATCH.length];
}

function classOf(name) {
    if (EXPLICIT.indexOf(name) >= 0) return "explicit";
    if (DEMOGRAPHICS.indexOf(name) >= 0) return "demographics";
    if (GENRES.indexOf(name) >= 0) return "genres";
    return "themes";
}

function requestJson(url, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState !== XMLHttpRequest.DONE) return;
        if (xhr.status < 200 || xhr.status >= 300) { done(null); return; }
        try { done(JSON.parse(xhr.responseText)); } catch (e) { done(null); }
    };
    xhr.open("GET", url);
    xhr.send();
}

function toTile(g) {
    var s = swatch(g.name);
    return { name: g.name, count: g.count || 0, id: g.mal_id,
             cover: COVERS[g.name] || "", c1: s[0], c2: s[1] };
}

// load the grouped index. includeExplicit gates the Explicit Genres section.
// done([{ group, genres:[tile] }]) in MAL's section order; Genres + Themes alpha-sorted, others as-is.
function loadMangaGroups(includeExplicit, done) {
    requestJson(JIKAN + "/genres/manga", function(j) {
        if (!j || !j.data) { done([]); return; }
        var buckets = { genres: [], explicit: [], themes: [], demographics: [] };
        var seen = {};
        j.data.forEach(function(g) {
            if (seen[g.name]) return;            // the flat list can repeat a name across internal filters
            seen[g.name] = true;
            buckets[classOf(g.name)].push(toTile(g));
        });
        var byName = function(a, b) { return a.name < b.name ? -1 : 1; };
        buckets.genres.sort(byName);
        buckets.themes.sort(byName);
        var groups = [{ group: "Genres", genres: buckets.genres }];
        if (includeExplicit && buckets.explicit.length)
            groups.push({ group: "Explicit Genres", genres: buckets.explicit });
        groups.push({ group: "Themes", genres: buckets.themes });
        groups.push({ group: "Demographics", genres: buckets.demographics });
        done(groups);
    });
}

// one-line subtitle per group (editorial nav aid — matches the approved mock).
function groupSub(name) {
    return ({ "Genres": "the broad strokes", "Explicit Genres": "mature content",
              "Themes": "threads that cut across genres", "Demographics": "who they're written for" })[name] || "";
}
