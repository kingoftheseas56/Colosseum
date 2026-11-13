"""Offline source metadata transports. All keys and responses here are synthetic."""
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import socket
import tempfile
import unittest
import urllib.error
import urllib.parse
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("source_pool_provider", ROOT / "scripts/vinyl/provider.py")
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
MBID = "b95ce3ff-3d05-4e87-9e01-c97b66af13d4"
OTHER = "11111111-1111-4111-8111-111111111111"
SEED = {"identity":"canonical", "kind":"artist", "mbid":MBID}
FORM = '<form><select name="other"><option value="wrong">wrong</option></select><select name="algorithm"><option value="">choose</option><option disabled value="disabled">disabled</option><option value="fixture_advertised" selected>Fixture</option></select></form>'

class Response:
    def __init__(self, data, status=200):
        self.body = data.encode() if isinstance(data, str) else json.dumps(data).encode()
        self.status = status
        self.headers = {}
    def read(self, limit): return self.body[:limit]
    def __enter__(self): return self
    def __exit__(self, *args): pass

class Transport:
    def __init__(self, *responses):
        self.responses = list(responses)
        self.calls = []
    def open(self, request, timeout):
        self.calls.append((request.full_url, timeout))
        if not self.responses: raise AssertionError("Unexpected transport call")
        response = self.responses.pop(0)
        if isinstance(response, Exception): raise response
        return response if isinstance(response, Response) else Response(response)


def apple_row(kind="albums", index=1):
    return {"id":str(1000+index), "kind":kind, "name":"Album 日本語" if kind == "albums" else "Song 日本語",
            "artistName":"竹内まりや", "artistId":"2000", "releaseDate":"2026-03",
            "artworkUrl100":"https://is1-ssl.mzstatic.com/image.jpg", "url":"https://music.apple.com/us/album/1001"}

def lastfm_row(artist=False):
    row = {"name":"竹内まりや" if artist else "Song", "mbid":OTHER,
           "url":"https://www.last.fm/music/Artist" if artist else "https://www.last.fm/music/Artist/_/Song",
           "match":"0.9", "image":[]}
    if not artist: row.update(artist={"name":"Artist", "mbid":MBID}, duration="180")
    return row

def lb_row(index=0):
    return {"artist_mbid":str(__import__("uuid").UUID(int=index+100)), "name":"Similar 日本語 " + str(index),
            "score":7273-index, "reference_mbid":MBID}

class SourcePoolProviderTest(unittest.TestCase):
    def setUp(self):
        self.temp_root = tempfile.gettempdir()
        self.enterContext(mock.patch.dict(os.environ, {"TEMP":self.temp_root, "TMP":self.temp_root,
            "COLOSSEUM_VINYL_LASTFM_API_KEY":format(0, "032x")}, clear=True))
        for method in ("socket.socket.connect", "socket.socket.connect_ex", "socket.getaddrinfo"):
            self.enterContext(mock.patch(method, side_effect=AssertionError("Real network prohibited")))
        self.enterContext(mock.patch.dict("sys.modules", {"requests":None, "ytmusicapi":None}))
        self.yt = self.enterContext(mock.patch.object(p, "_ytmusic_client", side_effect=AssertionError("No YT metadata fetch")))
        if hasattr(p, "_LABS_ALGORITHM_CACHE"): p._LABS_ALGORITHM_CACHE = None
        p._LASTFM_NEXT_REQUEST = 0
    def tearDown(self): self.yt.assert_not_called()
    def fetch(self, request, transport):
        result = p.handle(request, source_pool_transport=transport)
        self.assertEqual(result["version"], 1)
        self.assertLessEqual(len(json.dumps({"ok":True,"data":result}, ensure_ascii=True).encode())+1, 239*1024)
        for row in result["items"]:
            self.assertTrue(row["catalogueCard"])
            self.assertNotIn("mapping", row)
            self.assertNotIn("ref", row)
            self.assertNotIn("playbackMatch", row)
            self.assertIsNot(row.get("playable"), True)
            self.assertNotIn("audioUrl", row)
            self.assertNotEqual(row.get("provider"), "youtube-music")
            p._catalogue_input(row)
        return result
    def chart(self, value, transport, **options):
        return self.fetch(dict(operation="chartCandidates", value=value, seed={}, **options), transport)
    def similar(self, transport, source="listenbrainz", **options):
        return self.fetch(dict(operation="similarArtists", value=MBID, seed=copy.deepcopy(SEED), source=source, **options), transport)

    def test_apple_all_ten_storefronts_and_both_feed_kinds(self):
        for cc in ("us","gb","jp","kr","in","br","ng","de","fr","mx"):
            for kind in ("albums","songs"):
                with self.subTest(cc=cc,kind=kind):
                    t = Transport({"feed":{"country":cc,"updated":"2026-11-13", "results":[apple_row(kind)]}})
                    r = self.chart("apple:"+cc+":"+kind,t)
                    self.assertEqual((r["status"],r["complete"]),("ready",True))
                    self.assertEqual(r["provenance"]["source"], "apple-music")
                    item=r["items"][0]
                    self.assertEqual((item["kind"],item["rank"],item["chartKind"],item["storefront"]),(kind,1,kind,cc))
                    self.assertEqual(item["sourceIds"]["id"],"1001")
                    self.assertEqual(item["hints"]["releaseDate"],"2026-03")
                    self.assertEqual(len(t.calls),1)
                    self.assertEqual(t.calls[0],("https://rss.marketingtools.apple.com/api/v2/"+cc+"/music/most-played/50/"+kind+".json",4))
    def test_lastfm_both_charts_use_existing_key_reader(self):
        for leaf,kind,method in (("tracks","songs","chart.getTopTracks"),("artists","artists","chart.getTopArtists")):
            with self.subTest(leaf=leaf):
                t=Transport({leaf:{"track" if leaf=="tracks" else "artist":[lastfm_row(leaf=="artists")]}})
                r=self.chart("lastfm:"+leaf,t)
                self.assertEqual(r["status"],"ready")
                self.assertEqual(r["provenance"]["source"],"lastfm")
                self.assertEqual((r["items"][0]["kind"],r["items"][0]["rank"]),(kind,1))
                query=urllib.parse.parse_qs(urllib.parse.urlparse(t.calls[0][0]).query)
                self.assertEqual(query["method"],[method])
                self.assertEqual(query["limit"],["50"])
                self.assertNotIn("country",query)
                self.assertEqual(t.calls[0][1],4)
                self.assertEqual(len(t.calls),1)
    def test_missing_lastfm_key_is_unavailable_without_transport(self):
        os.environ.pop("COLOSSEUM_VINYL_LASTFM_API_KEY")
        for request in ({"operation":"chartCandidates","value":"lastfm:tracks","seed":{}},
                        {"operation":"similarArtists","value":MBID,"seed":SEED,"source":"lastfm","artistName":"竹内まりや"}):
            t=Transport()
            r=self.fetch(request,t)
            self.assertEqual(r["status"],"unavailable")
            self.assertEqual(r["items"],[])
            self.assertEqual(t.calls,[])
    def test_key_file_seam_is_existing_runtime_config_only(self):
        os.environ.pop("COLOSSEUM_VINYL_LASTFM_API_KEY")
        with tempfile.TemporaryDirectory(dir=self.temp_root) as tmp:
            path=Path(tmp)/"synthetic-key.txt"
            path.write_text(format(0,"032x"))
            os.environ["COLOSSEUM_VINYL_LASTFM_KEY_FILE"]=str(path)
            t=Transport({"artists":{"artist":[]}})
            self.assertEqual(self.chart("lastfm:artists",t)["status"],"empty")
            self.assertEqual(len(t.calls),1)
    def test_successful_empty_lists_are_empty(self):
        for value,data in (("apple:us:albums",{"feed":{"country":"us","results":[]}}),
                           ("lastfm:tracks",{"tracks":{"track":[]}})):
            r=self.chart(value,Transport(data))
            self.assertEqual((r["status"],r["complete"],r["items"]),("empty",True,[]))
        for source,data in (("listenbrainz",[]),("lastfm",{"similarartists":{"artist":[]}})):
            t=Transport(FORM,data) if source=="listenbrainz" else Transport(data)
            r=self.similar(t,source,**({"artistName":"竹内まりや"} if source=="lastfm" else {}))
            self.assertEqual((r["status"],r["complete"]),("empty",True))
    def test_transport_errors_and_401_are_not_empty(self):
        errors=[urllib.error.HTTPError("https://fixture",401,"Denied",{},None),
                urllib.error.HTTPError("https://fixture",429,"Limited",{},None),TimeoutError("fixture timeout"),
                Response({},status=401),Response("not json")]
        for error in errors:
            with self.subTest(error=type(error).__name__):
                r=self.chart("apple:us:albums",Transport(error))
                self.assertEqual(r["status"],"unavailable")
                self.assertIs(r["complete"],False)
                self.assertEqual(r["items"],[])
    def test_malformed_feed_shapes_and_ids_are_not_empty(self):
        bad_rows=[]
        for key,value in (("id","../bad"),("id",1001),("id","0"),("artistId","bad"),("name",42),("kind","songs")):
            row=apple_row();row[key]=value;bad_rows.append(row)
        values=[{}, {"feed":{"results":None}}, {"feed":{"country":"gb","results":[]}}]
        values += [{"feed":{"country":"us","results":[row]}} for row in bad_rows]
        for value in values:
            r=self.chart("apple:us:albums",Transport(value))
            self.assertEqual(r["status"],"unavailable")
        for key,value in (("mbid","not-uuid"),("url","https://evil.test/music/a"),("url","https://www.last.fm/music/"),("url","https://www.last.fm/music/Artist/_/Song")):
            row=lastfm_row(True);row[key]=value
            r=self.chart("lastfm:artists",Transport({"artists":{"artist":[row]}}))
            self.assertEqual(r["status"],"unavailable")
    def test_lastfm_json_auth_errors_are_unavailable(self):
        for code in (10,26,29):
            r=self.chart("lastfm:tracks",Transport({"error":code,"message":"Synthetic key error"}))
            self.assertEqual(r["status"],"unavailable")
    def test_chart_bound_and_order_are_source_local_not_python_merged(self):
        rows=[apple_row(index=i+1) for i in range(51)]
        rows[1]=copy.deepcopy(rows[0])
        r=self.chart("apple:us:albums",Transport({"feed":{"country":"us","results":rows}}))
        self.assertEqual(len(r["items"]),50)
        self.assertEqual(r["items"][0]["id"],r["items"][1]["id"])
        self.assertEqual([x["rank"] for x in r["items"]],list(range(1,51)))
        self.assertEqual((r["status"],r["complete"]),("partial",False))
    def test_oversized_raw_stdio_request_stops_before_metadata_dispatch(self):
        raw = b" " * (239 * 1024) + json.dumps({"operation":"chartCandidates", "value":"apple:us:albums", "seed":{}}).encode()
        with mock.patch.object(p,"source_pool_metadata") as dispatch:
            response=p._answer(raw)
        self.assertIs(response["ok"],False)
        self.assertEqual(response["errorCode"],"invalid_argument")
        dispatch.assert_not_called()
    def test_unknown_options_and_non_boolean_refresh_stop_before_transport(self):
        for options in ({"source":"lastfm"},{"limit":50},{"region":"us"},{"refresh":"yes"}):
            t=Transport()
            r=self.chart("apple:us:albums",t,**options)
            self.assertEqual(r["sourceStatus"],"invalid_argument")
            self.assertEqual(t.calls,[])
        t=Transport()
        r=self.similar(t,algorithm="unadvertised")
        self.assertEqual(r["sourceStatus"],"invalid_argument")
        self.assertEqual(t.calls,[])
    def test_invalid_requests_issue_zero_calls(self):
        requests=[{"operation":"chartCandidates","value":"apple:cn:albums","seed":{}},
                  {"operation":"chartCandidates","value":"lastfm:albums","seed":{}},
                  {"operation":"chartCandidates","value":"apple:us:songs","seed":False},
                  {"operation":"chartCandidates","value":"apple:us:songs","seed":SEED},
                  {"operation":"similarArtists","value":MBID,"seed":dict(SEED,extra=True),"source":"listenbrainz"},
                  {"operation":"similarArtists","value":"00000000-0000-0000-0000-000000000000","seed":SEED,"source":"listenbrainz"},
                  {"operation":"similarArtists","value":MBID,"seed":SEED,"source":"other"},
                  {"operation":"similarArtists","value":MBID,"seed":SEED,"source":"lastfm","artistName":[]},
                  {"operation":"similarArtists","value":MBID,"seed":SEED,"source":"lastfm","artistName":"竹内まりや","extra":"x"*(240*1024)}]
        for request in requests:
            t=Transport();r=self.fetch(request,t)
            self.assertEqual(r["status"],"unavailable")
            self.assertEqual(r["sourceStatus"],"invalid_argument")
            self.assertEqual(t.calls,[])
    def test_lastfm_one_unicode_alias_spelling_is_preserved(self):
        t=Transport({"similarartists":{"artist":[lastfm_row(True)]}})
        r=self.similar(t,"lastfm",artistName="竹内まりや")
        self.assertEqual(r["status"],"ready")
        query=urllib.parse.parse_qs(urllib.parse.urlparse(t.calls[0][0]).query)
        self.assertEqual(query["artist"],["竹内まりや"])
        self.assertEqual(query["autocorrect"],["0"])
        self.assertEqual(query["limit"],["60"])
        self.assertEqual(len(t.calls),1)
        item=r["items"][0]
        self.assertEqual(item["similarity"],0.9)
        self.assertEqual(item["hints"]["mbidEvidence"]["artist"],OTHER)
    def test_labs_advertised_algorithm_is_cached_and_get_has_no_count(self):
        t=Transport(FORM,[lb_row()])
        r=self.similar(t)
        self.assertEqual(r["status"],"ready")
        self.assertEqual(len(t.calls),2)
        query=urllib.parse.parse_qs(urllib.parse.urlparse(t.calls[-1][0]).query)
        self.assertEqual(query,{"artist_mbids":[MBID],"algorithm":["fixture_advertised"]})
        self.assertEqual([x[1] for x in t.calls],[2.5,2.5])
        self.assertEqual(r["items"][0]["similarity"],7273)
        self.assertEqual(r["items"][0]["hints"]["mbidEvidence"]["artist"],lb_row()["artist_mbid"])
        t2=Transport([lb_row(1)])
        self.assertEqual(self.similar(t2)["status"],"ready")
        self.assertEqual(len(t2.calls),1)
        self.assertEqual(r["provenance"]["algorithm"],"fixture_advertised")
    def test_labs_absent_score_preserves_absence_and_list_rank(self):
        row=lb_row()
        del row["score"]
        transport=Transport(FORM,[row])
        result=self.similar(transport)
        self.assertEqual((result["status"],result["complete"]),("ready",True))
        self.assertEqual(result["provenance"]["source"],"listenbrainz")
        self.assertEqual(len(result["items"]),1)
        item=result["items"][0]
        self.assertEqual(item["title"],row["name"])
        self.assertEqual(item["hints"]["mbidEvidence"]["artist"],row["artist_mbid"])
        self.assertEqual(item["rank"],1)
        self.assertNotIn("similarity",item)
        self.assertNotIn("score",item)
        self.assertEqual(len(transport.calls),2)

    def test_labs_bad_or_disabled_form_cannot_invent_algorithm(self):
        for form in ('<select name="other"><option value="fixture_advertised">x</option></select>',
                     '<select name="algorithm"><option disabled value="bad">bad</option></select>',
                     '<select name="algorithm"><optgroup disabled><option value="bad">bad</option></optgroup></select>',
                     '<select name="algorithm"><option value="https://evil.test">bad</option></select>'):
            p._LABS_ALGORITHM_CACHE=None
            t=Transport(form)
            r=self.similar(t)
            self.assertEqual(r["status"],"unavailable")
            self.assertEqual(len(t.calls),1)
    def test_labs_output_is_bounded_and_invalid_ids_fail(self):
        r=self.similar(Transport(FORM,[lb_row(i) for i in range(100)]))
        self.assertEqual(len(r["items"]),60)
        self.assertEqual((r["status"],r["complete"]),("partial",False))
        for key,value in (("artist_mbid","bad"),("reference_mbid",OTHER),("score",True)):
            row=lb_row();row[key]=value
            r=self.similar(Transport([row]))
            self.assertEqual(r["status"],"unavailable")
        r=self.similar(Transport([dict(lb_row(),artist_mbid=None)]))
        self.assertEqual(r["status"],"unavailable")
    def test_labs_401_is_unavailable_and_invalidates_algorithm_cache(self):
        self.assertEqual(self.similar(Transport(FORM,[]))["status"],"empty")
        r=self.similar(Transport(Response({},status=401)))
        self.assertEqual(r["status"],"unavailable")
        t=Transport(FORM,[])
        self.assertEqual(self.similar(t)["status"],"empty")
        self.assertEqual(len(t.calls),2)
    def test_utf8_names_keep_actual_stdio_response_inside_budget(self):
        row=lastfm_row(True);row["name"]="日"*600
        rows=[dict(row,url="https://www.last.fm/music/"+str(i)) for i in range(60)]
        r=self.similar(Transport({"similarartists":{"artist":rows}}),"lastfm",artistName="竹内まりや")
        self.assertEqual(r["status"],"partial")
        self.assertTrue(0<len(r["items"])<60)
    def test_advertised_algorithm_expires_without_extra_calls_in_one_request(self):
        self.similar(Transport(FORM,[]))
        p._LABS_ALGORITHM_CACHE["at"] -= 301
        new_form=FORM.replace("fixture_advertised","fixture_new")
        t=Transport(new_form,[])
        r=self.similar(t)
        self.assertEqual(r["provenance"]["algorithm"],"fixture_new")
        self.assertEqual(len(t.calls),2)

    def test_similarity_scores_cannot_be_boolean_nan_or_out_of_range(self):
        for match in (True,"NaN","1.1","-0.1",None):
            row=lastfm_row(True);row["match"]=match
            r=self.similar(Transport({"similarartists":{"artist":[row]}}),"lastfm",artistName="竹内まりや")
            self.assertEqual(r["status"],"unavailable")
            self.assertEqual(r["items"],[])

    def test_new_source_cards_resolve_only_through_existing_exact_guards(self):
        apple=self.chart("apple:us:albums",Transport({"feed":{"country":"us","results":[apple_row()]}}))["items"][0]
        client=mock.Mock()
        channel="UC"+"A"*22
        client.search.return_value=[{"title":apple["title"],"browseId":"MPRE_fixture",
                                     "artists":[{"name":"竹内まりや","id":channel}],"year":"2026","thumbnails":[]}]
        resolved=p.handle({"operation":"resolveCatalogueItem","value":apple["id"],"item":apple},client=client)
        self.assertIs(resolved["matched"],True)
        self.assertEqual(resolved["record"]["browseId"],"MPRE_fixture")
        artist=self.similar(Transport(FORM,[lb_row()]))["items"][0]
        client.search.return_value=[{"artist":artist["title"],"browseId":channel,"thumbnails":[]}]
        client.get_artist.return_value={"name":artist["title"],"songs":{"results":[{
            "artists":[{"name":artist["title"],"id":channel}]}]}}
        resolved=p.handle({"operation":"resolveCatalogueItem","value":artist["id"],"item":artist},client=client)
        self.assertIs(resolved["matched"],True)
        self.assertEqual(resolved["artistId"],"ytm:artist:"+channel)

    def test_apple_and_labs_metadata_reuse_conservative_resolver(self):
        row=self.chart("apple:us:albums",Transport({"feed":{"country":"us","results":[apple_row()]}}))["items"][0]
        parsed=p._catalogue_input(row)
        self.assertEqual(parsed["artists"],["竹内まりや"])
        client=mock.Mock()
        client.search.return_value=[{"title":row["title"],"browseId":"MPRE_fixture", "artists":[{"name":"Wrong artist"}]}]
        self.assertIsNone(p._catalogue_match(client,parsed,"albums")[0])
        client.search.return_value=[{"title":row["title"]+" (Live)","browseId":"MPRE_fixture", "artists":[{"name":"竹内まりや"}]}]
        self.assertIsNone(p._catalogue_match(client,parsed,"albums")[0])
        parsed.update(kind="songs",title="Song",duration=180)
        client.search.return_value=[{"title":"Song", "videoId":"abcdefghijk", "videoType":"MUSIC_VIDEO_TYPE_ATV",
                                     "duration_seconds":230,"artists":[{"name":"竹内まりや"}]}]
        self.assertIsNone(p._catalogue_match(client,parsed,"songs")[0])

if __name__=="__main__": unittest.main()
