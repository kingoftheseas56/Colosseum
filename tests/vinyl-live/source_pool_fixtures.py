"""Pure, synthetic V1 Home metadata. Opt-in routing belongs to fake_provider.py.

Generated channels/UUIDs simulate verified fixture bindings; none is a claim
about the real named artist. This module never resolves or fetches media.
"""
import copy
import datetime
import hashlib
import json
import re
import uuid
from urllib.parse import quote

STAMP = "2026-11-13T00:00:00.000Z"
NAMESPACE = uuid.UUID("9b7255c4-9aec-51d6-8905-e55de3fa8209")
STOREFRONTS = ("us", "gb", "jp", "kr", "in", "br", "ng", "de", "fr", "mx")
OPERATIONS = {"cataloguePage", "artistCandidates", "recommendationIdentity", "recommendationDiscography",
              "chartCandidates", "similarArtists", "songCandidates", "resolveCatalogueItem", "album"}
SEEDS = tuple({"index":i, "label":label, "name":name, "aliases":aliases,
               "channel":f"UC{i:022d}", "mbid":f"f0000000-0000-4000-8000-{i:012d}"}
              for i,label,name,aliases in (
                  (1,"Mariya Takeuchi","竹内まりや",["竹内まりや","Mariya Takeuchi","Takeuchi, Mariya"]),
                  (2,"Björk","Björk",["Björk","Bjork"]),
                  (3,"Ilaiyaraaja","இளையராஜா",["இளையராஜா","Ilaiyaraaja"]),
                  (4,"Daft Punk","Daft Punk",["Daft Punk"]),
                  (5,"SZA","SZA",["SZA"])))
NEIGHBORS = tuple({"index":100+seed["index"]*10+n, "parent":seed["index"],
                   "label":f'{seed["label"]} Neighbor {n+1}', "name":f'{seed["label"]} Neighbor {n+1}',
                   "aliases":[f'{seed["label"]} Neighbor {n+1}'],
                   "channel":f'UC{100+seed["index"]*10+n:022d}',
                   "mbid":f'f0000000-0000-4000-8000-{100+seed["index"]*10+n:012d}'}
                  for seed in SEEDS for n in range(10))
ARTISTS = SEEDS + NEIGHBORS


def artist_ref(artist):
    return {"kind":"artist", "provider":"youtube-music", "providerId":artist["channel"],
            "id":"ytm:artist:"+artist["channel"], "name":artist["label"]}


def _neighbors(artist):
    parent = artist.get("parent", artist["index"])
    rows = [row for row in NEIGHBORS if row["parent"] == parent and row != artist]
    return rows + [row for row in SEEDS if row["index"] == parent] if "parent" in artist else rows


def _identity(kind, key):
    return str(uuid.uuid5(NAMESPACE, "youtube-music:"+kind+":"+key))


def _browse(artist, index):
    return f'MPRE_v1_{artist["index"]}_{index}'


def _video(artist, index):
    return f'V{artist["index"]*1000+index:010d}'


def _album_title(artist, index):
    return f'{artist["label"]} · Album {index+1}'


def _album_count(artist):
    return 2 if "parent" in artist else 8


def _track(artist, index, title=None):
    video = _video(artist,index)
    return {"id":_identity("recording",video), "videoId":video,
            "title":title or f'{artist["label"]} · Song {index+1}', "artists":[artist["label"]],
            "artistIds":["ytm:artist:"+artist["channel"]], "seconds":180+index,
            "available":True, "explicit":False, "videoType":"MUSIC_VIDEO_TYPE_ATV"}


def _record(artist, index=None, song_index=None):
    key = "track:"+_video(artist,song_index) if song_index is not None else _browse(artist,index)
    record = {"id":_identity("release",key),
              "browseId":"ytm:track:"+_video(artist,song_index) if song_index is not None else key,
              "origin":"youtube-music", "title":f'{artist["label"]} · Song {song_index+1}' if song_index is not None else _album_title(artist,index),
              "artists":[artist["label"]], "artistIds":["ytm:artist:"+artist["channel"]],
              "year":"", "cover":"", "explicit":False, "type":"Song" if song_index is not None else "Album",
              "providerIds":{"youtube-music":_video(artist,song_index) if song_index is not None else key}}
    record["tracks"] = ([_track(artist,song_index)] if song_index is not None else
                        [_track(artist,100+index*12+n,f'{record["title"]} · Track {n+1}') for n in range(12)])
    return record


def _provider_card(artist, kind, index=0, seed=None):
    key = artist["channel"] if kind == "artist" else _browse(artist,index) if kind == "album" else _video(artist,index)
    prefix = "track" if kind == "song" else kind
    title = artist["label"] if kind == "artist" else _album_title(artist,index) if kind == "album" else f'{artist["label"]} · Song {index+1}'
    card = {"kind":kind, "provider":"youtube-music", "providerId":key, "id":f"ytm:{prefix}:"+key,
            "title":title, "name":title, "cover":"", "image":"", "artistRefs":[artist_ref(artist)],
            "relation":{"artist":"related-artist","album":"own-release","song":"top-recording"}[kind],
            "mappingStatus":"unresolved", "seedArtistRef":copy.deepcopy(seed)}
    if kind == "song":
        card.update(seconds=180+index, videoId=key, version="studio", videoType="MUSIC_VIDEO_TYPE_ATV")
    return card


def _release_groups(artist):
    return [{"id":f'a0000000-0000-4000-8000-{artist["index"]*100+i:012d}',
             "title":_album_title(artist,i), "first-release-date":
                 (f"2026-{9+i:02d}-15" if "parent" in artist else "2026-10-15" if i == 0 else f"2000-{i:02d}-01"),
             "primary-type":"Album", "secondary-types":[]} for i in range(_album_count(artist))]


def _check_fields(request, extra):
    if set(request) - {"operation","value","fetchedAt","refresh"} - set(extra):
        raise ValueError("Unknown fixture request option.")
    if "refresh" in request and type(request["refresh"]) is not bool:
        raise ValueError("Invalid fixture refresh flag.")
    if "fetchedAt" in request:
        value = request["fetchedAt"]
        if type(value) is not str or len(value)>128:
            raise ValueError("Invalid fixture timestamp.")
        datetime.datetime.fromisoformat(value.replace("Z","+00:00"))
    if "region" in request and (type(request["region"]) is not str
            or request["region"] and not re.fullmatch("[A-Z]{2}",request["region"])):
        raise ValueError("Invalid fixture region.")
    if "limit" in request and (type(request["limit"]) is not int or not 1 <= request["limit"] <= 60):
        raise ValueError("Invalid fixture limit.")


def _provider_artist(request):
    seed = request.get("seed")
    fields = {"identity","mbid","kind","provider","providerId","id","key","name","title","image","cover",
              "mappingStatus","canonicalMbid","legacyId","mapping"}
    if type(seed) is not dict or set(seed)-fields or seed.get("kind")!="artist" or seed.get("provider")!="youtube-music":
        raise ValueError("Expected a provider-bound fixture artist.")
    artist = next((row for row in ARTISTS if row["channel"]==seed.get("providerId")),None)
    if artist is None or request.get("value") != artist["channel"]:
        raise ValueError("Unknown or mismatched fixture artist.")
    native_key=json.dumps(["youtube-music","artist",artist["channel"]],separators=(",",":"))
    if "id" in seed and seed["id"] not in ("ytm:artist:"+artist["channel"],native_key):
        raise ValueError("Mismatched fixture catalogue identity.")
    if "key" in seed and seed["key"] != native_key:
        raise ValueError("Mismatched fixture native artist key.")
    if any(key in seed and seed[key] != artist["mbid"] for key in ("mbid","canonicalMbid")):
        raise ValueError("Mismatched fixture canonical identity.")
    if "identity" in seed and seed["identity"] != "provider":
        raise ValueError("Expected a provider fixture identity.")
    if "mapping" in seed:
        mapping=seed["mapping"]
        if type(mapping) is not dict or any(mapping.get(key) != expected for key,expected in (
                ("provider","youtube-music"),("providerId",artist["channel"]),("kind","artist"),("mbid",artist["mbid"]))):
            raise ValueError("Mismatched fixture seed mapping.")
    return artist


def _canonical_artist(request):
    value = request.get("value")
    artist = next((row for row in ARTISTS if row["mbid"]==value),None)
    if artist is None or request.get("seed") != {"identity":"canonical","kind":"artist","mbid":value}:
        raise ValueError("Expected an exactly bound canonical fixture artist.")
    return artist


def _home(request, source, items=None, **extra):
    items = [] if items is None else items
    return {"version":1,"seed":copy.deepcopy(request.get("seed",{})),"items":items,
            "provenance":{"source":source,"generator":"artist-catalogue" if source=="youtube-music" else "artist-evidence"},
            "fetchedAt":request.get("fetchedAt",STAMP),"complete":True,"status":"ready" if items else "empty",**extra}


def _metadata(source, kind, key, title, artist, **hints):
    return {"id":f'{source}:{kind}:'+hashlib.sha256(key.encode()).hexdigest()[:24],"kind":kind,"title":title,
            "artist":artist["label"],"subtitle":artist["label"],"cover":"","year":"","catalogueCard":True,
            "source":source,"sourceIds":{"id":key},"hints":{"artists":[artist["label"]],**hints}}


def _chart(feed):
    source = "apple-music" if feed.startswith("apple:") else "lastfm"
    parts = feed.split(":")
    if source=="apple-music":
        if len(parts)!=3 or parts[1] not in STOREFRONTS or parts[2] not in ("albums","songs"):
            raise ValueError("Unknown fixture Apple feed.")
        kind = parts[2]
    elif feed in ("lastfm:tracks","lastfm:artists"):
        kind = "songs" if parts[1]=="tracks" else "artists"
    else:
        raise ValueError("Unknown fixture chart feed.")
    rows=[]
    for i,artist in enumerate(ARTISTS[:12]):
        title = artist["label"] if kind=="artists" else _album_title(artist,0) if kind=="albums" else f'{artist["label"]} · Song 1'
        key = str(700000+i+(100 if kind=="songs" else 0)) if source=="apple-music" else "https://www.last.fm/music/"+quote(artist["label"],safe="")+("/_/Song1" if kind=="songs" else "")
        card = _metadata(source,kind,key,title,artist)
        card.update(rank=i+1,chartKind=kind)
        if source=="apple-music": card.update(storefront=parts[1])
        rows.append((card,artist,kind,0))
    return source,rows


def _similar(artist, source):
    if artist["index"]==1 and source=="listenbrainz": return []
    rows=[]
    for i,neighbor in enumerate(_neighbors(artist)):
        key = neighbor["mbid"] if source=="listenbrainz" else "https://www.last.fm/music/"+quote(neighbor["label"],safe="")
        evidence = {"artist":neighbor["mbid"]}
        if source=="listenbrainz": evidence["referenceArtist"]=artist["mbid"]
        card = _metadata(source,"artists",key,neighbor["label"],neighbor,mbidEvidence=evidence)
        card["rank"]=i+1
        if source=="lastfm" or i: card["similarity"]=0.9-i*0.01 if source=="lastfm" else 1000-i
        rows.append((card,neighbor,"artists",0))
    return rows


def _known_cards():
    for feed in [f"apple:{cc}:{kind}" for cc in STOREFRONTS for kind in ("albums","songs")]+["lastfm:tracks","lastfm:artists"]:
        yield from _chart(feed)[1]
    for artist in ARTISTS:
        for source in ("lastfm","listenbrainz"):
            yield from _similar(artist,source)


def _resolve(request):
    item=request.get("item")
    if type(item) is not dict or request.get("value") != item.get("id"):
        raise ValueError("Invalid fixture resolver binding.")
    for known,artist,kind,index in _known_cards():
        if known==item:
            result={"version":1,"id":item["id"],"kind":kind,"matched":True}
            if kind=="artists": result["artistId"]="ytm:artist:"+artist["channel"]
            else:
                record=_record(artist,index=index if kind=="albums" else None,song_index=index if kind=="songs" else None)
                result.update(albumId=record["id"],record=record,cover="")
            return result
    raise ValueError("Only exact generated fixture cards can resolve.")


def _handle(request):
    operation=request["operation"]
    if operation=="cataloguePage":
        _check_fields(request,{"kind","cursor","region"})
        if (type(request.get("value")) is not str or request["value"].strip().casefold()!="fixture"
                or request.get("kind")!="artists" or request.get("cursor") not in (None,{})
                or type(request.get("region","")) is not str
                or request.get("region","") and not re.fullmatch("[A-Z]{2}",request["region"])):
            raise ValueError("Fixture picker supports only the first 'fixture' artist query.")
        return {"version":1,"query":request["value"],"kind":"artists","source":"youtube-music",
                "region":request.get("region",""),"items":[{"id":"ytm:artist:"+a["channel"],"artistId":"ytm:artist:"+a["channel"],
                    "kind":"artists","title":a["label"],"artist":a["label"],"cover":"","round":True} for a in SEEDS],
                "records":[],"offset":0,"nextOffset":5,"nextCursor":{},"complete":True,"fetchedAt":request.get("fetchedAt",STAMP)}
    if operation in ("artistCandidates","recommendationIdentity"):
        _check_fields(request,{"seed","region","limit"})
        artist=_provider_artist(request)
        if operation=="artistCandidates":
            seed=request["seed"]
            items=[_provider_card(artist,"album",i,seed) for i in range(_album_count(artist))]
            items += [_provider_card(artist,"song",i,seed) for i in range(8)]
            items += [_provider_card(row,"artist",seed=seed) for row in _neighbors(artist)]
            return _home(request,"youtube-music",items,capabilities={key:True for key in ("albums","singles","songs","related")},
                         artist={"provider":"youtube-music","providerId":artist["channel"],"name":artist["label"],"biography":"Synthetic V1 fixture","image":""})
        stamp=datetime.datetime.fromisoformat(request.get("fetchedAt",STAMP).replace("Z","+00:00"))
        if stamp.tzinfo is None: stamp=stamp.replace(tzinfo=datetime.timezone.utc)
        mapping={"status":"verified","provider":"youtube-music","providerId":artist["channel"],"kind":"artist",
                 "mbid":artist["mbid"],"method":"external-id","evidence":["https://www.youtube.com/channel/"+artist["channel"]],
                 "fetchedAtMs":int(stamp.timestamp()*1000)}
        return _home(request,"musicbrainz",status="verified",mappingStatus="verified",mapping=mapping,mbid=artist["mbid"],
                     artist={"id":artist["mbid"],"name":artist["name"],"disambiguation":"Synthetic V1 fixture only",
                             "aliases":copy.deepcopy(artist["aliases"]),"aliasesComplete":True})
    if operation=="recommendationDiscography":
        _check_fields(request,{"seed","since","until"})
        artist=_canonical_artist(request); groups=_release_groups(artist); window=None
        if "since" in request or "until" in request:
            for key in ("since","until"):
                if type(request.get(key)) is not str or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}",request[key]):
                    raise ValueError("Fixture dates must be paired full ISO dates.")
                datetime.date.fromisoformat(request[key])
            if request["since"]>request["until"]: raise ValueError("Inverted fixture date window.")
            window={key:request[key] for key in ("since","until")}
            groups=groups[:2 if "parent" in artist else 1]
            groups=[row for row in groups if window["since"]<=row["first-release-date"]<=window["until"]]
        metadata={"id":artist["mbid"],"releaseGroups":groups,"releaseGroupsComplete":True,"releaseGroupCount":len(groups)}
        if window: metadata.update(releaseGroupScope="recent-releases",releaseGroupWindow=window)
        return _home(request,"musicbrainz",artist=metadata,status="ready" if groups else "empty")
    if operation=="chartCandidates":
        _check_fields(request,{"seed"})
        if type(request.get("seed")) is not dict or request["seed"]!={}: raise ValueError("Charts require empty seed.")
        source,rows=_chart(request.get("value",""))
        result=_home(request,source,[row[0] for row in rows]);result["provenance"].update(generator="chart-feed",**{"feed":request["value"]} if source=="apple-music" else {"method":"chart.getTopTracks" if request["value"].endswith("tracks") else "chart.getTopArtists"})
        return result
    if operation=="similarArtists":
        _check_fields(request,{"seed","source","artistName"})
        artist=_canonical_artist(request);source=request.get("source")
        if source not in ("lastfm","listenbrainz"): raise ValueError("Unknown fixture similarity source.")
        if source=="listenbrainz" and "artistName" in request: raise ValueError("Labs does not take artist spelling.")
        if source=="lastfm" and request.get("artistName") not in artist["aliases"]: raise ValueError("Unknown fixture artist spelling.")
        rows=[] if artist["index"]==1 and source=="lastfm" and request.get("artistName")!="竹内まりや" else _similar(artist,source)
        result=_home(request,source,[row[0] for row in rows]);result["provenance"].update(generator="artist-similarity",**({"algorithm":"fixture-advertised"} if source=="listenbrainz" else {"method":"artist.getSimilar","artistName":request["artistName"]}))
        return result
    if operation=="songCandidates":
        _check_fields(request,{"seed","recording","region"})
        seed=request.get("seed");recording=request.get("recording")
        if type(seed) is not dict: raise ValueError("Missing fixture song seed.")
        artist=_provider_artist(dict(request,value=seed.get("providerId")))
        if (type(recording) is not dict or recording.get("kind")!="song" or recording.get("provider")!="youtube-music"
                or recording.get("providerId")!=request.get("value") or recording.get("seedArtistRef")!=seed
                or request.get("value") not in [_video(artist,i) for i in range(8)]):
            raise ValueError("Mismatched selected fixture recording.")
        items=[]
        for i in range(8):
            if _video(artist,i)==request["value"]: continue
            card=_provider_card(artist,"song",i,seed);card.update(record=_record(artist,song_index=i),trackIndex=0,
                relation="song-radio" if i<4 else "song-related",candidateSources=["radio"] if i<4 else ["related"])
            items.append(card)
        result=_home(request,"youtube-music",items,capabilities={"songRadio":True,"songRelated":True})
        result["provenance"]={"source":"youtube-music","generator":"song-radio-related","sources":{
            key:{"status":"ready","candidateCount":sum(key in row["candidateSources"] for row in items)} for key in ("radio","related")}}
        return result
    if operation=="resolveCatalogueItem":
        _check_fields(request,{"item"});return _resolve(request)
    if operation=="album":
        _check_fields(request,set())
        for artist in ARTISTS:
            for index in range(_album_count(artist)):
                record=_record(artist,index=index)
                if request.get("value") in (_browse(artist,index),"ytm:album:"+_browse(artist,index),record["id"]): return record
            for index in range(8):
                record=_record(artist,song_index=index)
                if request.get("value") in (_video(artist,index),record["browseId"],record["id"]): return record
        raise ValueError("Unknown fixture album identity.")


def handle(request):
    """Return a normal helper envelope, or None for an unhandled operation."""
    if type(request) is not dict or type(request.get("operation")) is not str or request["operation"] not in OPERATIONS: return None
    try:
        if len(json.dumps(request,ensure_ascii=True).encode())>239*1024: raise ValueError("Oversized fixture request.")
        result={"ok":True,"data":_handle(request)}
        if len(json.dumps(result,ensure_ascii=True).encode())+1>239*1024: raise ValueError("Oversized fixture response.")
        return result
    except (ValueError,TypeError,KeyError,AttributeError,OverflowError):
        return {"ok":False,"error":"Invalid or unsupported V1 pool fixture request.","errorCode":"invalid_argument"}
