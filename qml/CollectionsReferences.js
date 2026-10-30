.pragma library

// Snapshot adapters for the actual title owners. Identity is finalized by
// CollectionsIdentity; no title/name matching or provider request belongs here.
function value(v) { return v === undefined || v === null ? "" : String(v); }
function base(world, kind, source, sourceId, route, title, cover, creator, year) {
    return {world:world, kind:kind, source:source, sourceId:value(sourceId),
        editionId:"", title:value(title), cover:value(cover), creator:value(creator),
        year:value(year), route:route, payload:{}};
}
function theatre(page) {
    var entry = page.collectionEntry();
    var sourceId = value(entry.id), resolvedId = value(page.resolvedId);
    // loading=false also occurs after a timeout/error. Only the exact metadata
    // identity, or an already-explicit IMDb door, makes this ready to save.
    var ready = /^tt[0-9]+$/.test(sourceId) || resolvedId.length > 0;
    var result = base("theatre", page.mediaType, "theatre", ready ? sourceId : "", "theatre-detail",
        page.title, page.cover, "", page.year);
    result.payload = {canonicalSourceId:resolvedId, art:value(page.banner)};
    return result;
}
function manga(entry, author, year) {
    entry = entry || {};
    var result = base("tankoban", "manga", "mal", entry.id, "manga-detail",
        entry.title, entry.cover, author, year);
    result.editionId = /:color$/.test(result.sourceId) ? "color" : "";
    var p = entry.payload || {};
    result.payload = {sourceSearchTitle:value(p.sourceSearchTitle),
        sourceSearchAliases:p.sourceSearchAliases || [], sourceRequiredMarkers:p.sourceRequiredMarkers || []};
    return result;
}
function weebcentral(page) {
    var result = base("tankoban", "manga", "weebcentral", page.seriesId, "manga-weebcentral-detail",
        page.seriesTitle, page.cover, page.author, page.year);
    result.payload = {seriesUrl:value(page.seriesUrl)};
    return result;
}
function locg(entry) {
    entry = entry || {};
    var result = base("tankoban", "comic", "locg", entry.id, "comic-locg-detail",
        entry.title, entry.cover, "", "");
    result.payload = {locgMeta:(entry.payload || {}).locgMeta || {}};
    return result;
}
function comic(page) {
    var id = value(page.seriesId), source = "", route = "", payload = {};
    if (id.indexOf("gc:posts:") === 0) {
        source = "getcomics"; route = "comic-posts-detail";
    } else if (value(page.packSeriesId).length) {
        source = "comic-packs"; route = "comic-pack-detail";
    } else if (/^gcd:[1-9][0-9]*$/.test(id)) {
        source = "gcd"; route = "comic-gcd-detail";
    } else if (value(page.tagSlug).length) {
        source = "getcomics"; route = "comic-tag-detail";
        payload = {tag:value(page.tagSlug), tagId:value(page.tagId)};
    }
    if (route === "comic-posts-detail" || route === "comic-gcd-detail") {
        var releases = page.bakedReleases || [], posts = [], seen = {};
        for (var i = 0; i < releases.length; ++i) {
            var post = value(releases[i].id);
            if (/^[1-9][0-9]*$/.test(post) && !seen[post]) { seen[post] = true; posts.push(post); }
        }
        if (posts.length) payload.posts = posts;
    }
    var result = base("tankoban", "comic", source, id, route, page.seriesTitle, page.poster, "", "");
    result.payload = payload;
    return result;
}
function book(record) {
    record = record || {};
    var result = base("biblio", "book", "apple-books", record.id, "book-detail",
        record.title, record.cover, record.author, record.year);
    result.editionId = record.isAudiobook === true ? "audiobook" : "ebook";
    result.payload = {book:record};
    return result;
}
function vault(detail, mediaKind) {
    detail = detail || {};
    var result = base("vault", "film", "imdb",
        ((mediaKind === "film" || mediaKind === "video") && !detail.isFileDetail && detail.identitySource === "IMDB") ? detail.identitySourceId : "",
        "vault-film-detail", detail.displayTitle, detail.coverRef, "", detail.year);
    result.payload = {vaultKey:value(detail.key)};
    return result;
}
function vinylSource(edition) {
    var origin = value(edition.origin || (edition.sourceAlbum || {}).origin);
    return origin === "local" ? "vinyl-local" : origin === "youtube-music" ? "youtube-music" : "";
}
function album(data) {
    var edition = data.shownEdition || {};
    var result = base("vinyl", "album", vinylSource(edition), edition.albumId,
        "vinyl-album-detail", edition.name, edition.cover, edition.artist, edition.year);
    result.editionId = value(edition.id);
    result.payload = {albumId:value(edition.albumId), browseId:value(edition.browseId)};
    return result;
}
function song(data) {
    var edition = data.playingEdition || {}, track = data.playingTrack || {};
    var result = base("vinyl", "song", vinylSource(edition), track.id,
        "vinyl-song-detail", track.title, edition.cover, track.artist || edition.artist, edition.year);
    result.editionId = value(edition.id);
    result.payload = {albumId:value(edition.albumId), browseId:value(edition.browseId), trackId:value(track.id)};
    return result;
}
function artist(data) {
    var record = data.artist || {};
    // The online record supplies the resolved exact channel. A name-only search
    // cannot become a collection identity merely because the page is visible.
    var id = data.online ? value(record.id) : value(data.artistId || record.id);
    return base("vinyl", "artist", data.online ? "youtube-music" : "vinyl-local", id,
        "vinyl-artist-detail", record.name || data.name, record.cover || record.thumbnail, "", "");
}

// Reuse native Vault projections. A Windows rescan may recase the same path;
// never use a display title or choose another copy to repair a saved location.
function vaultDetail(owner, reference, windowsPaths) {
    var key = String((reference.payload || {}).vaultKey || "")
    if (!key || !reference.sourceId) return null
    var detail = owner.browseDetail(key)
    if (!detail || !detail.found) {
        if (!windowsPaths || key.indexOf("content://") === 0) return null
        var groups = owner.series("video") || [], matching = []
        for (var i = 0; i < groups.length; ++i) {
            var candidate = String(groups[i].key || "")
            if (candidate.toLowerCase() === key.toLowerCase()) matching.push(candidate)
        }
        if (matching.length !== 1) return null
        detail = owner.browseDetail(matching[0])
    }
    return detail && detail.found && !detail.isFileDetail && detail.identitySource === "IMDB"
            && detail.identitySourceId === reference.sourceId ? detail : null
}

// Reconstruct only existing recipient-owned routes from the public identity
// allowlist. Never persist these local details back into the received edition.
function sharedReopenReference(saved, vaultOwner, identityOwner) {
    if (!saved || saved.route) return saved || {};
    var ref = Object.assign({}, saved), p = saved.payload || {};
    ref.payload = {};
    ref.route = "";
    // A source-only imported key is not a canonical registry key. The registry
    // derives it at this open boundary; an existing canonical ID remains checked.
    if (!ref.mediaId) ref.key = "";
    var id = String(ref.sourceId || ""), edition = String(ref.editionId || "");
    var token = /^[A-Za-z0-9][A-Za-z0-9._:+-]{0,255}$/;
    if (!token.test(id) || (edition && !token.test(edition))) return ref;
    if (ref.world === "theatre" && ref.source === "theatre"
            && (ref.kind === "movie" || ref.kind === "series") && !edition) {
        var canonical = String(p.canonicalSourceId || "");
        // A sender's alias assertion is untrusted. Never let opening a gift
        // teach or overwrite the recipient's persistent provider→IMDb mapping.
        // Only an already known recipient anchor can authorize a provider pivot.
        if (/^tt[0-9]+$/.test(id)) {
            if (canonical && canonical !== id) return ref;
            ref.route = "theatre-detail";
            if (canonical) ref.payload.canonicalSourceId = canonical;
        } else if (/^tt[0-9]+$/.test(canonical) && identityOwner
                && typeof identityOwner.knownTheatreAnchor === "function"
                && identityOwner.knownTheatreAnchor(id) === canonical) {
            ref.route = "theatre-detail";
            ref.payload.canonicalSourceId = canonical;
        }
    } else if (ref.world === "tankoban" && ref.kind === "manga" && ref.source === "mal") {
        var match = /^mal:([1-9][0-9]*)(:color)?$/.exec(id);
        if (match && edition === (match[2] ? "color" : "") && (!match[2] || id === "mal:13:color")) {
            ref.route = "manga-detail";
            ref.payload.malId = match[1];
            if (edition === "color") {
                ref.payload.sourceSearchTitle = "One Piece Colored";
                ref.payload.sourceSearchAliases = ["One Piece Digital Colored Comics"];
                ref.payload.sourceRequiredMarkers = ["colored", "full color", "full colour"];
            }
        }
    } else if (ref.world === "tankoban" && ref.kind === "manga" && ref.source === "weebcentral" && !edition) {
        ref.route = "manga-weebcentral-detail";
    } else if (ref.world === "biblio" && ref.kind === "book" && ref.source === "apple-books"
            && /^[1-9][0-9]*$/.test(id) && (edition === "ebook" || edition === "audiobook")) {
        ref.route = "book-detail";
        ref.payload.book = {id:id, title:ref.title, author:ref.creator, year:ref.year,
            cover:"", isAudiobook:edition === "audiobook"};
    } else if (ref.world === "tankoban" && ref.kind === "comic" && !edition) {
        if (ref.source === "locg" && /^locg:[1-9][0-9]*$/.test(id)) {
            ref.route = "comic-locg-detail";
            ref.payload.locgMeta = {name:ref.title};
        } else if (ref.source === "gcd" && /^gcd:[1-9][0-9]*$/.test(id)) {
            ref.route = "comic-gcd-detail";
            if (p.posts && p.posts.length) ref.payload.posts = p.posts.slice();
        } else if (ref.source === "getcomics" && /^gc:posts:[1-9][0-9]*$/.test(id)
                && p.posts && p.posts.length && String(p.posts[0]) === id.substring(9)) {
            ref.route = "comic-posts-detail";
            ref.payload.posts = p.posts.slice();
        } else if (ref.source === "getcomics" && /^gc:[A-Za-z0-9][A-Za-z0-9_-]*$/.test(id)) {
            ref.route = "comic-tag-detail";
            ref.payload.tag = id.substring(3);
        } else if (ref.source === "comic-packs") ref.route = "comic-pack-detail";
    } else if (ref.world === "vinyl" && (ref.source === "youtube-music" || ref.source === "vinyl-local")) {
        if (ref.kind === "artist" && !edition) ref.route = "vinyl-artist-detail";
        else if ((ref.kind === "album" || ref.kind === "song") && edition
                && token.test(String(p.albumId || "")) && token.test(String(p.browseId || ""))
                && (ref.kind !== "album" || p.albumId === id)) {
            ref.route = ref.kind === "album" ? "vinyl-album-detail" : "vinyl-song-detail";
            ref.payload = {albumId:p.albumId, browseId:p.browseId};
            if (ref.kind === "song") ref.payload.trackId = id;
        }
    } else if (ref.world === "vault" && ref.kind === "film" && ref.source === "imdb"
            && /^imdb:tt[0-9]+$/.test(id) && !edition && vaultOwner) {
        var groups = vaultOwner.series("video") || [], matches = [];
        for (var i = 0; i < groups.length; i++) {
            var detail = vaultOwner.browseDetail(groups[i].key);
            if (detail && detail.found && !detail.isFileDetail && detail.identitySource === "IMDB"
                    && detail.identitySourceId === id) matches.push(String(groups[i].key));
        }
        if (matches.length === 1) {
            ref.route = "vault-film-detail";
            ref.payload.vaultKey = matches[0];
        }
    }
    return ref;
}
