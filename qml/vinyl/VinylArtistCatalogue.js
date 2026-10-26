.pragma library

// Prototype projection of the frozen catalogue, not an identity store. Only
// spelling variants are folded; the snapshot's broad people aliases are unsafe.
function identity(name) {
    var value = String(name || "").trim();
    var aliases = {
        "Ilayaraja": "Ilaiyaraaja",
        "A.R. Rahman": "A. R. Rahman",
        "A.R.Rahman": "A. R. Rahman",
        "S.P. Balasubrahmanyam": "S. P. Balasubrahmanyam",
        "S.P.Balasubrahmanyam": "S. P. Balasubrahmanyam",
        "S.P.Balasubramaniam": "S. P. Balasubrahmanyam",
        "S.P.Balasubramanyam": "S. P. Balasubrahmanyam"
    };
    return aliases[value] || value;
}

function contains(names, name) {
    return (names || []).some(function(n) { return identity(n) === name; });
}

function artist(catalogue, requestedName) {
    var name = identity(requestedName);
    var releases = [], songs = [], credits = {};
    (catalogue.albums || []).forEach(function(album) {
        var own = contains(album.artists, name);
        var contributed = false;
        (album.tracks || []).forEach(function(track, index) {
            var credited = contains(track.artists, name);
            contributed = contributed || credited;
            if (!own && !credited) return;
            songs.push({album: album, track: track, index: index});
            (track.artists || []).forEach(function(other) {
                var key = identity(other);
                if (key && key !== name) credits[key] = true;
            });
        });
        if (own || contributed)
            releases.push({album: album, credit: own ? "album" : "track"});
    });
    return {name: name, releases: releases, songs: songs,
        collaborators: Object.keys(credits).sort()};
}

function releases(record, filter, newestFirst) {
    var normalized = String(filter || "all");
    return (record.releases || []).filter(function(r) {
        if (normalized === "all" || normalized === "All")
            return true;
        if (normalized === "album" || normalized === "track")
            return r.credit === normalized;
        var cat = String((r.album && r.album.category) || (r.album && r.album.releaseType) || "");
        return cat.toLocaleLowerCase() === normalized.toLocaleLowerCase() || r.credit === normalized;
    }).sort(function(a, b) {
        var delta = Number(a.album.year || 0) - Number(b.album.year || 0);
        return (newestFirst ? -delta : delta) || a.album.title.localeCompare(b.album.title);
    });
}

// A small, deterministic cross-section of years represented in this collection.
// These are release records, not claims about an artist's career milestones.
function releaseHistory(record, singles, limit) {
    var byYear = {};
    var candidates = (record.releases || []).concat((singles || []).map(function(album) {
        return {album:album, credit:"single"};
    }));
    var priority = {album:2, single:1, track:0};
    candidates.forEach(function(release) {
        var year = Number(release.album.year);
        if (!Number.isInteger(year) || year <= 0) return;
        var current = byYear[year];
        if (!current || priority[release.credit] > priority[current.credit]
                || release.credit === current.credit && release.album.title.localeCompare(current.album.title) < 0)
            byYear[year] = release;
    });
    var years = Object.keys(byYear).map(Number).sort(function(a, b) { return a - b; });
    var count = Math.min(Math.max(0, limit === undefined ? 4 : limit), years.length);
    var selected = [];
    for (var i = 0; i < count; ++i) {
        var index = count === 1 ? 0 : Math.round(i * (years.length - 1) / (count - 1));
        selected.push(byYear[years[index]]);
    }
    return selected;
}

// Optional, provider-qualified presentation records. Absence is not a request
// to infer a recommendation, a live recording, or an artist-owned playlist.
// Exact approved HTML preview artwork; see preview asset manifest for provenance.
var approvedEminemVideoArt = {"S9bCLPwzSC0": "artist-media/approved-video-S9bCLPwzSC0.jpg", "uelHwf8o7_U": "artist-media/approved-video-uelHwf8o7_U.jpg", "YVkUvmDQ3HY": "artist-media/approved-video-YVkUvmDQ3HY.jpg", "xFYQQPAOz7Y": "artist-media/approved-video-xFYQQPAOz7Y.jpg", "j5-yKhDd64s": "artist-media/approved-video-j5-yKhDd64s.jpg", "eJO5HU_7_1w": "artist-media/approved-video-eJO5HU_7_1w.jpg"};
function mediaForArtist(catalogue, name) {
    var source = (catalogue || {})[name];
    var result = {portrait:{cover:""}, videos:[], livePerformances:[],
        featuredPlaylists:[], artistPlaylists:[], relatedArtists:[], popularSongs:[], provider:"", sourceUrl:""};
    function sourced(value) {
        return value && typeof value.sourceUrl === "string" && /^https:\/\/[^\s/]+\//.test(value.sourceUrl);
    }
    if (!sourced(source) || typeof source.name !== "string" || identity(source.name) !== name
            || typeof source.provider !== "string" || !source.provider.trim())
        return result;
    result.provider = source.provider;
    result.sourceUrl = source.sourceUrl;
    if (sourced(source.portrait) && typeof source.portrait.cover === "string" && source.portrait.cover)
        result.portrait = {cover:source.portrait.cover, sourceUrl:source.portrait.sourceUrl};
    ["videos", "livePerformances", "featuredPlaylists", "artistPlaylists", "relatedArtists", "popularSongs"].forEach(function(key) {
        var seen = Object.create(null);
        result[key] = (Array.isArray(source[key]) ? source[key] : []).filter(function(item) {
            if (!sourced(item) || typeof item.id !== "string" || !item.id.trim()
                    || typeof item.title !== "string" || !item.title.trim() || seen[item.id]) return false;
            seen[item.id] = true;
            return true;
        }).map(function(item) {
            return {id:item.id, title:item.title, cover:(name === "Eminem" && key === "videos" && approvedEminemVideoArt[item.id])
                    || (typeof item.cover === "string" ? item.cover : ""),
                subtitle:typeof item.subtitle === "string" && item.subtitle ? item.subtitle : source.provider,
                provider:source.provider, albumTitle:typeof item.albumTitle === "string" ? item.albumTitle : "",
                sourceUrl:item.sourceUrl, snapshotAt:source.snapshotAt || "", kind:key};
        });
    });
    return result;
}

var FALLBACK_TINTS = [
    "#4c2e33", "#28384f", "#454039", "#3c313e", "#45392e", "#303c39", "#4d333a"
];

function fallbackTintForIndex(index) {
    var idx = Math.abs(Number(index || 0)) % FALLBACK_TINTS.length;
    return FALLBACK_TINTS[idx];
}

function releaseCards(records) {
    return (records || []).map(function(record, idx) {
        var album = record.album || record;
        var type = record.credit === "track" ? "Appears on" : (album.category || album.releaseType || "Album");
        var year = Number(album.year);
        return {
            id: album.id,
            title: album.title,
            year: Number.isInteger(year) && year > 0 ? year : 0,
            category: type,
            cover: album.cover || "",
            album: album,
            playable: !!(album.tracks && album.tracks.length > 0),
            sourceUrl: album.source || album.url || "",
            fallbackTint: fallbackTintForIndex(idx),
            subtitle: [Number.isInteger(year) && year > 0 ? String(year) : "", type].filter(Boolean).join(" · ")
        };
    });
}

// A provider's ordered artist-song shelf is not a local popularity algorithm.
// Only exact titles that also exist in the artist's credited catalogue are playable.
function sourcedPopularSongs(record, providerSongs) {
    var result = [], seen = Object.create(null);
    (providerSongs || []).forEach(function(item) {
        var title = item.title.trim().toLocaleLowerCase();
        var matches = (record.songs || []).filter(function(song) {
            return song.track && String(song.track.title || "").trim().toLocaleLowerCase() === title;
        });
        var preferred = matches.find(function(song) {
            return item.albumTitle && String(song.album.title).trim().toLocaleLowerCase()
                === item.albumTitle.trim().toLocaleLowerCase();
        }) || matches[0];
        if (!preferred || seen[title]) return;
        seen[title] = true;
        result.push(preferred);
    });
    return result;
}

function libraryReleases(record, singles, profile) {
    var saved = Array.isArray((profile || {}).saved) ? profile.saved : [];
    var records = (record.releases || []).concat((singles || []).map(function(album) { return {album:album}; }));
    return records.filter(function(release, index) {
        return saved.indexOf(release.album.id) >= 0
            && records.findIndex(function(other) { return other.album.id === release.album.id; }) === index;
    });
}

// Approved Eminem HTML design catalogue (27 releases, 43 compact singles rows,
// 3 live performance entries, and 4 rotating reference photographs + studio fallback).
var EMINEM_CURATED_RELEASES = [
    {id:"death", title:"The Death of Slim Shady (Coup de Grâce)", year:2024, category:"Studio", cover:"artist-media/approved-release-death.jpg", url:"https://en.wikipedia.org/wiki/The_Death_of_Slim_Shady_(Coup_de_Gr%C3%A2ce)", matchId:"MPREb_VfwB9V2ZGFR"},
    {id:"mtbmb", title:"Music to Be Murdered By", year:2020, category:"Studio", cover:"artist-media/approved-release-mtbmb.jpg", url:"https://en.wikipedia.org/wiki/Music_to_Be_Murdered_By"},
    {id:"kamikaze", title:"Kamikaze", year:2018, category:"Studio", cover:"artist-media/approved-release-kamikaze.jpg", url:"https://en.wikipedia.org/wiki/Kamikaze_(Eminem_album)"},
    {id:"revival", title:"Revival", year:2017, category:"Studio", cover:"artist-media/approved-release-revival.jpg", url:"https://en.wikipedia.org/wiki/Revival_(Eminem_album)"},
    {id:"mmlp2", title:"The Marshall Mathers LP 2", year:2013, category:"Studio", cover:"artist-media/approved-release-mmlp2.jpg", url:"https://en.wikipedia.org/wiki/The_Marshall_Mathers_LP_2"},
    {id:"recovery", title:"Recovery", year:2010, category:"Studio", cover:"artist-media/approved-release-recovery.jpg", url:"https://en.wikipedia.org/wiki/Recovery_(Eminem_album)", matchId:"MPREb_UUFKiZYr3uO"},
    {id:"relapse", title:"Relapse", year:2009, category:"Studio", cover:"artist-media/approved-release-relapse.jpg", url:"https://en.wikipedia.org/wiki/Relapse_(Eminem_album)"},
    {id:"encore", title:"Encore", year:2004, category:"Studio", cover:"artist-media/approved-release-encore.jpg", url:"https://en.wikipedia.org/wiki/Encore_(Eminem_album)", matchId:"MPREb_xFN2XG22AXC"},
    {id:"eminem-show", title:"The Eminem Show", year:2002, category:"Studio", cover:"artist-media/approved-release-eminem-show.jpg", url:"https://en.wikipedia.org/wiki/The_Eminem_Show", matchTitle:"The Eminem Show"},
    {id:"mmlp", title:"The Marshall Mathers LP", year:2000, category:"Studio", cover:"artist-media/approved-release-mmlp.jpg", url:"https://en.wikipedia.org/wiki/The_Marshall_Mathers_LP", matchId:"MPREb_BEDvMqhixtx"},
    {id:"slim-shady", title:"The Slim Shady LP", year:1999, category:"Studio", cover:"artist-media/approved-release-slim-shady.jpg", url:"https://en.wikipedia.org/wiki/The_Slim_Shady_LP"},
    {id:"infinite", title:"Infinite", year:1996, category:"Studio", cover:"artist-media/approved-release-infinite.jpg", url:"https://en.wikipedia.org/wiki/Infinite_(Eminem_album)"},
    {id:"stans", title:"STANS (The Official Soundtrack)", year:2025, category:"Soundtracks", cover:"artist-media/approved-release-stans.jpg", url:"https://www.eminem.com/news/stans-soundtrack-out-now/", matchId:"MPREb_DmBhgAVua1o"},
    {id:"mourners", title:"The Death of Slim Shady: Expanded Mourner’s Edition", year:2024, category:"Editions", cover:"artist-media/approved-release-mourners.jpg", url:"https://en.wikipedia.org/wiki/The_Death_of_Slim_Shady_(Coup_de_Gr%C3%A2ce)"},
    {id:"curtain2", title:"Curtain Call 2", year:2022, category:"Compilations", cover:"artist-media/approved-release-curtain2.jpg", url:"https://en.wikipedia.org/wiki/Curtain_Call_2"},
    {id:"sideb", title:"Music to Be Murdered By: Side B", year:2020, category:"Editions", cover:"artist-media/approved-release-sideb.jpg", url:"https://en.wikipedia.org/wiki/Music_to_Be_Murdered_By"},
    {id:"southpaw", title:"Southpaw: Music from and Inspired by the Motion Picture", year:2015, category:"Soundtracks", cover:"artist-media/approved-release-southpaw.jpg", url:"https://en.wikipedia.org/wiki/Southpaw:_Music_from_and_Inspired_by_the_Motion_Picture"},
    {id:"shadyxv", title:"Shady XV", year:2014, category:"Collaborations", cover:"artist-media/approved-release-shadyxv.jpg", url:"https://en.wikipedia.org/wiki/Shady_XV"},
    {id:"mmlp2deluxe", title:"The Marshall Mathers LP 2 (Deluxe)", year:2013, category:"Editions", cover:"artist-media/approved-release-mmlp2deluxe.jpg", url:"https://en.wikipedia.org/wiki/The_Marshall_Mathers_LP_2"},
    {id:"hell", title:"Hell: The Sequel · Bad Meets Evil", year:2011, category:"Collaborations", cover:"artist-media/approved-release-hell.jpg", url:"https://en.wikipedia.org/wiki/Hell:_The_Sequel"},
    {id:"refill", title:"Relapse: Refill", year:2009, category:"Editions", cover:"artist-media/approved-release-refill.jpg", url:"https://en.wikipedia.org/wiki/Relapse_(Eminem_album)"},
    {id:"reup", title:"Eminem Presents: The Re-Up", year:2006, category:"Collaborations", cover:"artist-media/approved-release-reup.jpg", url:"https://en.wikipedia.org/wiki/Eminem_Presents:_The_Re-Up"},
    {id:"curtain1", title:"Curtain Call: The Hits", year:2005, category:"Compilations", cover:"artist-media/approved-release-curtain1.jpg", url:"https://en.wikipedia.org/wiki/Curtain_Call:_The_Hits"},
    {id:"d12world", title:"D12 World · D12", year:2004, category:"Collaborations", cover:"artist-media/approved-release-d12world.jpg", url:"https://en.wikipedia.org/wiki/D12_World"},
    {id:"eightmile", title:"8 Mile: Music from and Inspired by the Motion Picture", year:2002, category:"Soundtracks", cover:"artist-media/approved-release-eightmile.jpg", url:"https://en.wikipedia.org/wiki/8_Mile:_Music_from_and_Inspired_by_the_Motion_Picture"},
    {id:"devilsnight", title:"Devil’s Night · D12", year:2001, category:"Collaborations", cover:"artist-media/approved-release-devilsnight.jpg", url:"https://en.wikipedia.org/wiki/Devil%27s_Night_(album)"},
    {id:"slimep", title:"The Slim Shady EP", year:1997, category:"EPs", cover:"", url:"https://en.wikipedia.org/wiki/Slim_Shady_EP"}
];

var EMINEM_RELEASE_CATEGORIES = [
    {key: "all", id: "All", label: "All releases"},
    {key: "Studio", id: "Studio", label: "Studio albums"},
    {key: "Compilations", id: "Compilations", label: "Compilations"},
    {key: "Collaborations", id: "Collaborations", label: "Collaborations"},
    {key: "Soundtracks", id: "Soundtracks", label: "Soundtracks"},
    {key: "Editions", id: "Editions", label: "Editions"},
    {key: "EPs", id: "EPs", label: "EPs"}
];

var EMINEM_CURATED_SONGS = [
    ["Houdini", 2024, "The Death of Slim Shady"],
    ["Tobey", 2024, "The Death of Slim Shady"],
    ["Somebody Save Me", 2024, "The Death of Slim Shady"],
    ["Temporary", 2024, "The Death of Slim Shady"],
    ["Lace It", 2023, "Juice WRLD & Eminem"],
    ["From the D 2 the LBC", 2022, "Eminem & Snoop Dogg"],
    ["Godzilla", 2020, "Music to Be Murdered By"],
    ["Darkness", 2020, "Music to Be Murdered By"],
    ["Gnat", 2020, "Music to Be Murdered By: Side B"],
    ["Killshot", 2018, "Single"],
    ["Venom", 2018, "Kamikaze"],
    ["Lucky You", 2018, "Kamikaze"],
    ["Fall", 2018, "Kamikaze"],
    ["River", 2017, "Revival"],
    ["Walk on Water", 2017, "Revival"],
    ["Rap God", 2013, "The Marshall Mathers LP 2"],
    ["The Monster", 2013, "The Marshall Mathers LP 2"],
    ["Berzerk", 2013, "The Marshall Mathers LP 2"],
    ["Survival", 2013, "The Marshall Mathers LP 2"],
    ["Headlights", 2013, "The Marshall Mathers LP 2"],
    ["Not Afraid", 2010, "Recovery"],
    ["Love the Way You Lie", 2010, "Recovery"],
    ["No Love", 2010, "Recovery"],
    ["Beautiful", 2009, "Relapse"],
    ["We Made You", 2009, "Relapse"],
    ["3 a.m.", 2009, "Relapse"],
    ["Mockingbird", 2004, "Encore"],
    ["Like Toy Soldiers", 2004, "Encore"],
    ["Just Lose It", 2004, "Encore"],
    ["Ass Like That", 2004, "Encore"],
    ["Lose Yourself", 2002, "8 Mile"],
    ["Without Me", 2002, "The Eminem Show"],
    ["Cleanin’ Out My Closet", 2002, "The Eminem Show"],
    ["Sing for the Moment", 2002, "The Eminem Show"],
    ["Till I Collapse", 2002, "The Eminem Show"],
    ["Business", 2002, "The Eminem Show"],
    ["Stan", 2000, "The Marshall Mathers LP"],
    ["The Real Slim Shady", 2000, "The Marshall Mathers LP"],
    ["The Way I Am", 2000, "The Marshall Mathers LP"],
    ["My Name Is", 1999, "The Slim Shady LP"],
    ["Guilty Conscience", 1999, "The Slim Shady LP"],
    ["Role Model", 1999, "The Slim Shady LP"],
    ["Just Don’t Give a Fuck", 1998, "The Slim Shady EP"]
];

var EMINEM_CURATED_LIVE = [
    {
        id: "7_We5YFmS8Q",
        title: "Rock & Roll Hall of Fame",
        subtitle: "2022 induction · Full Eminem performance",
        year: "2022",
        cover: "",
        provider: "YouTube",
        sourceUrl: "https://www.youtube.com/watch?v=7_We5YFmS8Q",
        snapshotAt: "2026-10-15",
        kind: "livePerformances"
    },
    {
        id: "super-bowl-lvi",
        title: "Super Bowl LVI Halftime Show",
        subtitle: "With Dr. Dre, Snoop Dogg, Mary J. Blige & Kendrick Lamar",
        year: "2022",
        cover: "",
        provider: "NFL",
        sourceUrl: "https://www.nfl.com/videos/full-pepsi-super-bowl-lvi-halftime-show-featuring-dr-dre-snoop-dogg-eminem-mary-",
        snapshotAt: "2026-10-15",
        kind: "livePerformances"
    },
    {
        id: "Cbtgk20s6wA",
        title: "Lose Yourself at the Oscars",
        subtitle: "92nd Academy Awards",
        year: "2020",
        cover: "",
        provider: "Academy Awards",
        sourceUrl: "https://www.grammy.com/news/eminem-performs-lose-yourself-2020-oscars/",
        snapshotAt: "2026-10-15",
        kind: "livePerformances"
    }
];

// Four rotating Eminem photographs from the approved HTML design + bundled studio fallback.
// Commercial redistribution rights for TheAudioDB fanart are unverified; isolated prototype/review use.
var EMINEM_HERO_PHOTOS = [
    {
        id: "warehouse",
        name: "The warehouse",
        era: "Early 2000s · industrial light",
        cover: "artist-media/eminem-warehouse.jpg",
        fallbackCover: "artist-media/UCfM3zsQsOnfWNUppiycmBuw-hero.jpg",
        remoteUrl: "https://r2.theaudiodb.com/images/media/artist/fanart/a7qr451612948832.jpg",
        focalX: 0.50,
        focalY: 0.28,
        autoRotate: true
    },
    {
        id: "hood",
        name: "The hood",
        era: "Black & white · midnight portrait",
        cover: "artist-media/eminem-hood.jpg",
        fallbackCover: "artist-media/UCfM3zsQsOnfWNUppiycmBuw-hero.jpg",
        remoteUrl: "https://r2.theaudiodb.com/images/media/artist/fanart/p1r94k1633254171.jpg",
        focalX: 0.56,
        focalY: 0.40,
        autoRotate: true
    },
    {
        id: "early",
        name: "Early Marshall",
        era: "Turn of the century · studio portrait",
        cover: "artist-media/eminem-early.jpg",
        fallbackCover: "artist-media/UCfM3zsQsOnfWNUppiycmBuw-hero.jpg",
        remoteUrl: "https://r2.theaudiodb.com/images/media/artist/fanart/uspurv1342261484.jpg",
        focalX: 0.57,
        focalY: 0.30,
        autoRotate: true
    },
    {
        id: "signature",
        name: "The signature",
        era: "High contrast · iconic portrait",
        cover: "artist-media/eminem-signature.jpg",
        fallbackCover: "artist-media/UCfM3zsQsOnfWNUppiycmBuw-hero.jpg",
        remoteUrl: "https://r2.theaudiodb.com/images/media/artist/fanart/yfx4sq1770819459.jpg",
        focalX: 0.52,
        focalY: 0.42,
        autoRotate: true
    },
    {
        id: "studio",
        name: "Studio fallback",
        era: "Bundled photograph · offline-ready",
        cover: "artist-media/UCfM3zsQsOnfWNUppiycmBuw-hero.jpg",
        fallbackCover: "artist-media/UCfM3zsQsOnfWNUppiycmBuw-hero.jpg",
        remoteUrl: "",
        focalX: 0.53,
        focalY: 0.35,
        autoRotate: false
    }
];

function backdropsForArtist(name, media, overridePhotos) {
    var canonical = identity(name);
    if (Array.isArray(overridePhotos)) {
        return overridePhotos.map(function(item, idx) {
            return {
                id: item.id || ("photo_" + idx),
                name: item.name || item.title || (canonical + " " + (idx + 1)),
                era: item.era || item.subtitle || "Artist photograph",
                cover: item.cover || "",
                fallbackCover: item.fallbackCover || item.cover || "",
                remoteUrl: item.remoteUrl || "",
                focalX: item.focalX !== undefined ? item.focalX : 0.5,
                focalY: item.focalY !== undefined ? item.focalY : 0.35,
                autoRotate: item.autoRotate !== false
            };
        });
    }
    if (canonical === "Eminem")
        return EMINEM_HERO_PHOTOS.slice();
    if (media && media.portrait && media.portrait.cover) {
        return [{
            id: "portrait",
            name: canonical,
            era: (media.provider ? media.provider + " · " : "") + "Artist portrait",
            cover: media.portrait.cover,
            fallbackCover: media.portrait.cover,
            remoteUrl: "",
            focalX: 0.50,
            focalY: 0.35,
            autoRotate: true
        }];
    }
    return [];
}

function autoRotatePhotos(backdrops) {
    return (backdrops || []).filter(function(item) {
        return item && item.autoRotate !== false && (item.cover || item.fallbackCover || item.remoteUrl);
    });
}

function discographyCardsForArtist(record, singles, filter, newestFirst) {
    var canonical = identity(record ? record.name : "");
    var rawFilter = String(filter || "all");
    var normalizedFilter = (rawFilter === "all" || rawFilter === "All") ? "All" : rawFilter;
    if (canonical === "Eminem") {
        var playableMap = {};
        ((record && record.releases) || []).forEach(function(r) {
            if (r.album) {
                playableMap[r.album.id] = r.album;
                playableMap[String(r.album.title || "").toLocaleLowerCase()] = r.album;
            }
        });
        (singles || []).forEach(function(s) {
            var alb = s.album || s;
            if (alb) {
                playableMap[alb.id] = alb;
                playableMap[String(alb.title || "").toLocaleLowerCase()] = alb;
            }
        });
        var cards = EMINEM_CURATED_RELEASES.map(function(entry, idx) {
            var matched = (entry.matchId && playableMap[entry.matchId])
                || (entry.matchTitle && playableMap[entry.matchTitle.toLocaleLowerCase()])
                || playableMap[entry.title.toLocaleLowerCase()]
                || null;
            var coverPath = entry.cover || (matched && matched.cover) || "";
            var albumObj = matched || {
                id: entry.id,
                title: entry.title,
                artists: ["Eminem"],
                year: entry.year,
                releaseType: entry.category,
                category: entry.category,
                cover: coverPath,
                source: entry.url,
                tracks: []
            };
            return {
                id: entry.id,
                title: entry.title,
                year: Number(entry.year),
                category: entry.category,
                credit: entry.category === "Collaborations" ? "track" : "album",
                cover: coverPath,
                album: albumObj,
                playable: !!(matched && matched.tracks && matched.tracks.length > 0),
                sourceUrl: entry.url || (matched && matched.source) || "",
                fallbackTint: fallbackTintForIndex(idx),
                subtitle: String(entry.year) + " · " + entry.category
            };
        });
        var filtered = cards.filter(function(card) {
            if (normalizedFilter === "All")
                return true;
            if (normalizedFilter === "album")
                return card.credit === "album";
            if (normalizedFilter === "track")
                return card.credit === "track";
            return card.category.toLocaleLowerCase() === normalizedFilter.toLocaleLowerCase();
        });
        return filtered.sort(function(a, b) {
            var delta = Number(a.year || 0) - Number(b.year || 0);
            return (newestFirst ? -delta : delta);
        });
    }
    var list = releases(record || {releases: []}, rawFilter === "All" ? "all" : rawFilter, newestFirst);
    return releaseCards(list);
}

function releaseCategoriesForArtist(record) {
    var canonical = identity(record ? record.name : "");
    if (canonical === "Eminem") {
        return EMINEM_RELEASE_CATEGORIES.map(function(cat) {
            var count = cat.id === "All"
                ? EMINEM_CURATED_RELEASES.length
                : EMINEM_CURATED_RELEASES.filter(function(r) { return r.category === cat.id; }).length;
            return {key: cat.key, id: cat.id, label: cat.label, count: count};
        });
    }
    var rels = (record && record.releases) || [];
    var out = [{key: "all", id: "All", label: "All releases", count: rels.length}];
    var albumCount = rels.filter(function(r) { return r.credit === "album"; }).length;
    var trackCount = rels.filter(function(r) { return r.credit === "track"; }).length;
    if (albumCount > 0)
        out.push({key: "album", id: "album", label: "Album credits", count: albumCount});
    if (trackCount > 0)
        out.push({key: "track", id: "track", label: "Appears on", count: trackCount});
    return out;
}

function findCoverForEminemSong(albumTitle, discography) {
    var text = String(albumTitle || "").toLocaleLowerCase();
    var map = [
        ["the death of slim shady", "death"],
        ["music to be murdered by", "mtbmb"],
        ["kamikaze", "kamikaze"],
        ["revival", "revival"],
        ["marshall mathers lp 2", "mmlp2"],
        ["recovery", "recovery"],
        ["relapse", "relapse"],
        ["encore", "encore"],
        ["eminem show", "eminem-show"],
        ["marshall mathers lp", "mmlp"],
        ["the slim shady lp", "slim-shady"],
        ["infinite", "infinite"],
        ["curtain call", "curtain1"]
    ];
    for (var i = 0; i < map.length; ++i) {
        if (text.indexOf(map[i][0]) >= 0) {
            var targetId = map[i][1];
            var found = (discography || EMINEM_CURATED_RELEASES).find(function(r) { return r.id === targetId; });
            return found ? (found.cover || "") : "";
        }
    }
    return "";
}

function singlesRowsForArtist(record, singles, songOrderList) {
    var canonical = identity(record ? record.name : "");
    var allPlayableSongs = ((record && record.songs) || []).slice();
    (singles || []).forEach(function(singleAlbum) {
        var alb = singleAlbum.album || singleAlbum;
        (alb.tracks || []).forEach(function(t, idx) {
            allPlayableSongs.push({album: alb, track: t, index: idx});
        });
    });
    if (canonical === "Eminem") {
        var discog = discographyCardsForArtist(record, singles, "All", true);
        return EMINEM_CURATED_SONGS.map(function(entry, idx) {
            var songTitle = String(entry[0]);
            var songYear = Number(entry[1]);
            var releaseTitle = String(entry[2]);
            var lowered = songTitle.toLocaleLowerCase();
            var match = allPlayableSongs.find(function(item) {
                if (!item || !item.track) return false;
                var candidate = String(item.track.title || "").toLocaleLowerCase();
                return candidate === lowered || candidate.indexOf(lowered + " (") === 0;
            }) || null;
            var cover = (match && match.album && match.album.cover)
                ? match.album.cover
                : findCoverForEminemSong(releaseTitle, discog);
            var albumObj = (match && match.album) ? match.album : {
                id: "eminem-song-release-" + idx,
                title: releaseTitle,
                artists: ["Eminem"],
                year: songYear,
                cover: cover,
                tracks: []
            };
            var trackObj = (match && match.track) ? match.track : {
                title: songTitle,
                artists: ["Eminem"],
                seconds: 240
            };
            return {
                rowIndex: idx,
                number: (idx + 1 < 10 ? "0" : "") + String(idx + 1),
                title: songTitle,
                subtitle: releaseTitle,
                albumTitle: releaseTitle,
                year: songYear,
                cover: cover,
                playable: !!match,
                album: albumObj,
                track: trackObj,
                index: match ? match.index : 0,
                song: match || {album: albumObj, track: trackObj, index: 0}
            };
        });
    }
    return (songOrderList || []).map(function(songItem, idx) {
        var yr = Number((songItem.album && songItem.album.year) || 0);
        return {
            rowIndex: idx,
            number: (idx + 1 < 10 ? "0" : "") + String(idx + 1),
            title: songItem.track.title,
            subtitle: songItem.album.title,
            albumTitle: songItem.album.title,
            year: Number.isInteger(yr) && yr > 0 ? yr : "",
            cover: songItem.album.cover || "",
            playable: true,
            album: songItem.album,
            track: songItem.track,
            index: songItem.index,
            song: songItem
        };
    });
}

function curatedLiveForArtist(name, media) {
    if (media && Array.isArray(media.livePerformances) && media.livePerformances.length > 0)
        return media.livePerformances;
    if (identity(name) === "Eminem")
        return EMINEM_CURATED_LIVE.slice();
    return [];
}

function searchArtistCatalogue(discography, singlesRows, videos, liveItems, query) {
    var q = String(query || "").trim().toLocaleLowerCase();
    var pool = [];
    (discography || []).forEach(function(r) {
        pool.push({
            type: r.album && r.album.kind === "single" ? "Single" : "Album",
            label: r.title,
            extra: String(r.year || "") + (r.category ? " · " + r.category : ""),
            tab: "albums",
            id: r.id,
            record: r
        });
    });
    (singlesRows || []).forEach(function(s, idx) {
        pool.push({
            type: "Track",
            label: s.title,
            extra: String(s.albumTitle || "") + (s.year ? " · " + String(s.year) : ""),
            tab: "singles",
            rowIndex: idx,
            record: s
        });
    });
    (videos || []).forEach(function(v, idx) {
        pool.push({
            type: "Video",
            label: v.title,
            extra: v.subtitle || "Official music video",
            tab: "videos",
            videoIndex: idx,
            record: v
        });
    });
    (liveItems || []).forEach(function(l, idx) {
        pool.push({
            type: "Live",
            label: l.title,
            extra: (l.year ? l.year + " · " : "") + (l.subtitle || "Live performance"),
            tab: "live",
            liveIndex: idx,
            record: l
        });
    });
    return pool.filter(function(entry) {
        if (!q) return true;
        return (String(entry.label) + " " + String(entry.extra)).toLocaleLowerCase().indexOf(q) >= 0;
    }).slice(0, 70);
}
