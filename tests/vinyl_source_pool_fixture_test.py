"""Portable, network-fenced contracts for the opt-in runtime Home fixture."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
import uuid
from unittest import mock

ROOT=Path(__file__).resolve().parents[1]


def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


fixture=load("v1_fixture",ROOT/"tests/vinyl-live/source_pool_fixtures.py")
fake=load("v1_fake",ROOT/"tests/vinyl-live/fake_provider.py")
provider=load("v1_contracts",ROOT/"scripts/vinyl/provider.py")


class SourcePoolFixtureTest(unittest.TestCase):
    def setUp(self):
        inherited_temp=tempfile.gettempdir()
        self.enterContext(mock.patch.dict(os.environ,{"TEMP":inherited_temp,"TMP":inherited_temp},clear=True))
        for name in ("socket.socket.connect","socket.socket.connect_ex","socket.getaddrinfo","urllib.request.urlopen"):
            self.enterContext(mock.patch(name,side_effect=AssertionError("Real fixture network prohibited")))

    def utf8_subprocess(self, requests, serve):
        # Model Windows' piped cp1252 text wrapper even on a UTF-8 host. The
        # wire remains UTF-8 bytes, and -I must not depend on PYTHONIOENCODING.
        bootstrap = ("import runpy,sys;sys.stdin.reconfigure(encoding='cp1252');"
                     "sys.argv=sys.argv[1:];runpy.run_path(sys.argv[0],run_name='__main__')")
        command = [sys.executable,"-I","-B","-c",bootstrap,str(ROOT/"tests/vinyl-live/fake_provider.py")]
        if serve: command.append("--serve")
        wire = b"\n".join(json.dumps(request,ensure_ascii=False).encode("utf-8") for request in requests)+b"\n"
        environment = dict(os.environ,VINYL_TEST_V1_POOL="1")
        result = subprocess.run(command,input=wire,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                env=environment,cwd=tempfile.gettempdir(),timeout=15,check=False)
        self.assertEqual(result.returncode,0,result.stderr.decode("utf-8",errors="replace"))
        replies = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(replies),len(requests))
        for request, reply in zip(requests,replies):
            self.assertTrue(reply["ok"],reply)
            data = reply["data"]
            if request["operation"]=="artistCandidates":
                self.assertEqual(data["seed"],request["seed"])
                self.assertEqual(data["seed"]["name"],"Björk")
                self.assertEqual(len(data["items"]),26)
                self.assertTrue(all(item["seedArtistRef"]["name"]=="Björk" for item in data["items"]))
            else:
                self.assertEqual(data["provenance"]["artistName"],"竹内まりや")
                self.assertEqual(len(data["items"]),10)

    def utf8_requests(self):
        return [self.request("artistCandidates",fixture.SEEDS[1]),
                self.request("similarArtists",fixture.SEEDS[0],True,source="lastfm",artistName="竹内まりや")]

    def test_isolated_one_shot_reads_utf8_seed_and_japanese_alias_bytes(self):
        for request in self.utf8_requests():
            with self.subTest(operation=request["operation"]): self.utf8_subprocess([request],False)

    def test_isolated_server_reads_utf8_seed_and_japanese_alias_bytes(self):
        self.utf8_subprocess(self.utf8_requests(),True)

    def test_audio_download_fixture_is_opt_in_and_exact(self):
        album=self.data({"operation":"album","value":fixture._browse(fixture.SEEDS[0],0)})
        videos=[fixture._video(fixture.SEEDS[0],0),album["tracks"][3]["videoId"]]
        os.environ["VINYL_TEST_V1_POOL"]="1"
        for video in videos:
            self.assertFalse(fake.handle_fake_request({"operation":"resolve","value":video})["ok"])
        os.environ["VINYL_TEST_DOWNLOAD_AUDIO"]="1"
        for video in videos:
            result=fake.handle_fake_request({"operation":"resolve","value":video})
            self.assertTrue(result["ok"],result)
            self.assertEqual(result["data"]["videoId"],video)
            self.assertTrue(result["data"]["audioOnly"])
            self.assertEqual(result["data"]["extension"],"m4a")
            self.assertTrue(result["data"]["url"].startswith("file:///"))
        self.assertFalse(fake.handle_fake_request({"operation":"resolve","value":"unknown0000"})["ok"])
        self.assertFalse(fake.handle_fake_request({"operation":"resolve","value":videos[1][:-3]+"999"})["ok"])
        os.environ["VINYL_TEST_V1_POOL"]="0"
        for video in videos:
            self.assertFalse(fake.handle_fake_request({"operation":"resolve","value":video})["ok"])

    def request(self,operation,artist=None,canonical=False,**options):
        result={"operation":operation}
        if artist:
            result.update(value=artist["mbid"] if canonical else artist["channel"],
                seed={"identity":"canonical","kind":"artist","mbid":artist["mbid"]} if canonical else fixture.artist_ref(artist))
        result.update(options)
        return result

    def data(self,request):
        before=copy.deepcopy(request)
        result=fixture.handle(request)
        self.assertEqual(request,before)
        self.assertEqual(result["ok"],True,result)
        self.assertLessEqual(len(json.dumps(result,ensure_ascii=True).encode())+1,239*1024)
        self.assertEqual(result,fixture.handle(request))
        return result["data"]

    def invalid(self,request):
        result=fixture.handle(request)
        self.assertEqual(result["ok"],False,result)
        self.assertEqual(result["errorCode"],"invalid_argument")

    def test_flag_off_keeps_old_helper_results_and_query_contract(self):
        request={"operation":"artistCandidates","value":fixture.SEEDS[0]["channel"]}
        expected={"ok":True,"data":[{"id":request["value"],"title":request["value"],"browseId":request["value"]}]}
        for flag in (None,"0","true",""):
            if flag is None: os.environ.pop("VINYL_TEST_V1_POOL",None)
            else: os.environ["VINYL_TEST_V1_POOL"]=flag
            self.assertEqual(fake.handle_fake_request(request),expected)
            self.assertEqual(fake.handle_fake_request({"operation":"resolve","value":"anything"}),{"ok":False,"error":"Fixture unavailable"})
        old=fake.handle_fake_request({"operation":"cataloguePage","value":"fixture","kind":"artists","cursor":{}})
        self.assertTrue(old["ok"])
        self.assertEqual([row["title"] for row in old["data"]["items"]],[f"Fixture Artist {i}" for i in range(1,6)])

    def test_opt_in_helper_routes_five_spec_seed_picker_and_unhandled_fallback(self):
        os.environ["VINYL_TEST_V1_POOL"]="1"
        request={"operation":"cataloguePage","value":"fixture","kind":"artists","cursor":{}}
        result=fake.handle_fake_request(request)
        self.assertTrue(result["ok"])
        data=result["data"]
        self.assertEqual([row["title"] for row in data["items"]],["Mariya Takeuchi","Björk","Ilaiyaraaja","Daft Punk","SZA"])
        self.assertEqual(data["query"],"fixture");self.assertTrue(data["complete"]);self.assertEqual(data["nextCursor"],{})
        self.assertEqual(data["records"],[])
        for row,artist in zip(data["items"],fixture.SEEDS):
            self.assertEqual(row["artistId"],"ytm:artist:"+artist["channel"])
        self.assertIsNone(fixture.handle({"operation":"resolve","value":"any"}))
        self.assertEqual(fake.handle_fake_request({"operation":"resolve","value":"any"}),{"ok":False,"error":"Fixture unavailable"})
        for change in ({"value":"Mariya"},{"kind":"songs"},{"cursor":{"token":"next"}},{"extra":True}):
            self.invalid(dict(request,**change))

    def test_all_synthetic_ids_are_valid_and_unique(self):
        channels=[row["channel"] for row in fixture.ARTISTS]
        self.assertEqual(len(channels),len(set(channels)))
        for artist in fixture.ARTISTS:
            self.assertRegex(artist["channel"],r"^UC[A-Za-z0-9_-]{22}$")
            self.assertNotEqual(uuid.UUID(artist["mbid"]).int,0)
            for i in range(8): self.assertRegex(fixture._video(artist,i),r"^[A-Za-z0-9_-]{11}$")

    def test_seed_core_has_eight_own_albums_eight_top_songs_and_ten_neighbors(self):
        for artist in fixture.SEEDS:
            request=self.request("artistCandidates",artist)
            data=self.data(request)
            self.assertEqual(len(data["items"]),26)
            self.assertLessEqual(len(data["items"]),60)
            for kind,count in (("album",8),("song",8),("artist",10)):
                self.assertEqual(sum(row["kind"]==kind for row in data["items"]),count)
            self.assertEqual(len({row["id"] for row in data["items"]}),26)
            for row in data["items"]:
                self.assertEqual(row["seedArtistRef"],request["seed"])
                self.assertEqual(row["provider"],"youtube-music")
                self.assertNotIn("dateSource",row);self.assertNotIn("releaseDate",row)

    def test_verified_identity_is_synthetic_and_bound_with_all_aliases(self):
        for artist in fixture.SEEDS:
            data=self.data(self.request("recommendationIdentity",artist))
            self.assertEqual(data["status"],"verified")
            self.assertEqual(data["mapping"]["providerId"],artist["channel"])
            self.assertEqual(data["mapping"]["mbid"],artist["mbid"])
            self.assertEqual(data["artist"]["id"],artist["mbid"])
            self.assertEqual(data["artist"]["name"],artist["name"])
            self.assertEqual(data["artist"]["aliases"],artist["aliases"])
            self.assertIs(data["artist"]["aliasesComplete"],True)
            self.assertIn("Synthetic",data["artist"]["disambiguation"])
            self.assertGreater(data["mapping"]["fetchedAtMs"],0)
            self.assertEqual(data["items"],[])

    def test_provider_and_canonical_bindings_reject_spoofed_or_unknown_inputs(self):
        first,other=fixture.SEEDS[:2]
        for operation in ("artistCandidates","recommendationIdentity"):
            request=self.request(operation,first)
            for change in ({"value":other["channel"]},{"seed":dict(request["seed"],provider="lastfm")},
                           {"seed":dict(request["seed"],providerId=other["channel"])},
                           {"seed":dict(request["seed"],id="ytm:artist:"+other["channel"])},
                           {"seed":dict(request["seed"],canonicalMbid=other["mbid"])},
                           {"seed":dict(request["seed"],mapping={"provider":"youtube-music","providerId":first["channel"],"kind":"artist","mbid":other["mbid"]})},
                           {"extra":True}):
                self.invalid(dict(request,**change))
        request=self.request("recommendationDiscography",first,canonical=True)
        for change in ({"value":other["mbid"]},{"seed":dict(request["seed"],extra=True)},
                       {"seed":fixture.artist_ref(first)},{"value":"00000000-0000-0000-0000-000000000000"}):
            self.invalid(dict(request,**change))

    def test_native_artist_seed_projection_keeps_bound_json_key_and_echoes_seed(self):
        artist=fixture.SEEDS[0]
        # VinylRecommendationService::artist uses this compact JSON key as both
        # key and display/search id, unlike the raw picker ytm:artist id.
        key=json.dumps(["youtube-music","artist",artist["channel"]],separators=(",",":"))
        seed=dict(fixture.artist_ref(artist),id=key,key=key,title=artist["label"],image="",cover="",mappingStatus="unresolved")
        for operation in ("artistCandidates","recommendationIdentity"):
            request={"operation":operation,"value":artist["channel"],"seed":seed}
            self.assertEqual(self.data(request)["seed"],seed)
            self.invalid(dict(request,seed=dict(seed,key="wrong")))

    def test_recent_subset_and_neighbor_catalogues_force_underfill_widening(self):
        window={"since":"2025-11-13","until":"2026-11-13"}
        initial=0
        for artist in fixture.SEEDS:
            legacy=self.data(self.request("recommendationDiscography",artist,True))
            self.assertEqual(legacy["artist"]["releaseGroupCount"],8)
            self.assertNotIn("releaseGroupScope",legacy["artist"])
            recent=self.data(self.request("recommendationDiscography",artist,True,**window))
            self.assertEqual(recent["artist"]["releaseGroupCount"],1)
            self.assertEqual(recent["artist"]["releaseGroupWindow"],window)
            self.assertEqual(recent["artist"]["releaseGroupScope"],"recent-releases")
            self.assertIs(recent["artist"]["releaseGroupsComplete"],True)
            self.assertEqual(len(recent["artist"]["releaseGroups"]),1)
            initial+=len(recent["artist"]["releaseGroups"])
            for neighbor in fixture._neighbors(artist):
                data=self.data(self.request("recommendationDiscography",neighbor,True,**window))
                self.assertEqual(data["artist"]["releaseGroupCount"],2)
                core=self.data(self.request("artistCandidates",neighbor))
                self.assertEqual(sum(row["kind"]=="album" for row in core["items"]),2)
        self.assertEqual(initial,5);self.assertLess(initial,8);self.assertGreaterEqual(initial+2+2,8)
        empty=self.data(self.request("recommendationDiscography",fixture.SEEDS[0],True,since="2000-01-01",until="2000-12-31"))
        self.assertEqual(empty["status"],"empty");self.assertEqual(empty["artist"]["releaseGroupCount"],0)
        for options in ({"since":"2025-11-13"},{"since":"2026-02-30","until":"2026-11-13"},
                        {"since":"2026-11-14","until":"2026-11-13"},{"since":False,"until":"2026-11-13"}):
            self.invalid(self.request("recommendationDiscography",fixture.SEEDS[0],True,**options))

    def test_mariya_english_and_labs_empty_but_japanese_alias_has_ten(self):
        artist=fixture.SEEDS[0]
        english=self.data(self.request("similarArtists",artist,True,source="lastfm",artistName="Mariya Takeuchi"))
        labs=self.data(self.request("similarArtists",artist,True,source="listenbrainz"))
        for result in (english,labs):
            self.assertEqual(result["status"],"empty");self.assertTrue(result["complete"]);self.assertEqual(result["items"],[])
        native=self.data(self.request("similarArtists",artist,True,source="lastfm",artistName="竹内まりや"))
        self.assertEqual(len(native["items"]),10)
        self.assertEqual([row["rank"] for row in native["items"]],list(range(1,11)))
        self.invalid(self.request("similarArtists",artist,True,source="lastfm",artistName="Unrelated"))
        self.invalid(self.request("similarArtists",artist,True,source="listenbrainz",artistName=artist["name"]))

    def test_labs_absent_score_and_external_cards_never_attest_playback(self):
        artist=fixture.SEEDS[1]
        data=self.data(self.request("similarArtists",artist,True,source="listenbrainz"))
        self.assertEqual(len(data["items"]),10)
        self.assertNotIn("similarity",data["items"][0]);self.assertEqual(data["items"][0]["rank"],1)
        self.assertIn("similarity",data["items"][1])
        for row in data["items"]: self.check_external(row)

    def check_external(self,row):
        provider._catalogue_input(row,row["id"])
        for key in ("providerId","videoId","mapping","mappingStatus","playbackMatch","playable","audioUrl","ref","destination"):
            self.assertNotIn(key,row)
        self.assertTrue(row["catalogueCard"])

    def test_every_fixed_chart_feed_has_twelve_ranked_metadata_cards(self):
        for feed in [f"apple:{cc}:{kind}" for cc in fixture.STOREFRONTS for kind in ("albums","songs")]+["lastfm:tracks","lastfm:artists"]:
            request={"operation":"chartCandidates","value":feed,"seed":{}}
            data=self.data(request)
            self.assertEqual(len(data["items"]),12)
            self.assertEqual([row["rank"] for row in data["items"]],list(range(1,13)))
            self.assertEqual(data["seed"],{})
            self.assertEqual(data["provenance"]["source"],"apple-music" if feed.startswith("apple:") else "lastfm")
            for row in data["items"]:
                self.check_external(row);self.assertEqual(row["chartKind"],row["kind"])
                if feed.startswith("apple:"): self.assertEqual(row["storefront"],feed.split(":")[1])
            self.invalid(dict(request,seed={"identity":"canonical"}))
        self.invalid({"operation":"chartCandidates","value":"apple:xx:albums","seed":{}})

    def test_generated_resolver_cards_bind_exactly_and_album_records_are_known(self):
        requests=[{"operation":"chartCandidates","value":feed,"seed":{}} for feed in ("apple:us:albums","apple:jp:songs","lastfm:tracks","lastfm:artists")]
        requests += [self.request("similarArtists",fixture.SEEDS[1],True,source=source,**({"artistName":"Björk"} if source=="lastfm" else {})) for source in ("lastfm","listenbrainz")]
        for request in requests:
            card=self.data(request)["items"][0]
            result=self.data({"operation":"resolveCatalogueItem","value":card["id"],"item":card})
            self.assertTrue(result["matched"]);self.assertEqual(result["id"],card["id"]);self.assertEqual(result["kind"],card["kind"])
            if card["kind"]=="artists": self.assertRegex(result["artistId"],r"^ytm:artist:UC[A-Za-z0-9_-]{22}$")
            else:
                self.assertEqual(result["albumId"],result["record"]["id"])
                album=self.data({"operation":"album","value":result["record"]["browseId"]})
                self.assertEqual(album,result["record"])
            self.invalid({"operation":"resolveCatalogueItem","value":"wrong","item":card})
            self.invalid({"operation":"resolveCatalogueItem","value":card["id"],"item":dict(card,title="Spoofed")})
        self.invalid({"operation":"album","value":"arbitrary"})

    def test_song_candidates_match_native_bound_song_record_requirements(self):
        for artist in fixture.SEEDS:
            core=self.data(self.request("artistCandidates",artist))
            selected=next(row for row in core["items"] if row["kind"]=="song")
            request=self.request("songCandidates",artist,value=selected["providerId"],recording=selected)
            provider.song_candidate_request(request)
            result=self.data(request)
            self.assertEqual(len(result["items"]),7)
            sources=set()
            for item in result["items"]:
                self.assertEqual(item["seedArtistRef"],request["seed"])
                record=item["record"];track=record["tracks"][item["trackIndex"]]
                self.assertEqual(record["origin"],"youtube-music")
                self.assertEqual(track["videoId"],item["providerId"])
                self.assertEqual(track["title"],item["title"])
                self.assertEqual(track["seconds"],item["seconds"])
                self.assertIs(type(item["seconds"]),int);self.assertGreater(item["seconds"],0);self.assertLessEqual(item["seconds"],86400)
                self.assertIs(track["available"],True)
                expected={"ytm:artist:"+ref["providerId"] for ref in item["artistRefs"]}
                self.assertEqual(set(track["artistIds"]),expected)
                sources.update(item["candidateSources"])
                self.assertEqual(record,provider.song_record({"title":item["title"],"videoId":item["providerId"],
                    "duration_seconds":item["seconds"],"artists":[{"name":artist["label"],"id":artist["channel"]}],"videoType":"MUSIC_VIDEO_TYPE_ATV"}))
            self.assertEqual(sources,{"radio","related"})
            self.assertTrue(result["capabilities"]["songRadio"]);self.assertTrue(result["capabilities"]["songRelated"])
            self.invalid(dict(request,recording=dict(selected,seedArtistRef=fixture.artist_ref(fixture.SEEDS[-1] if artist!=fixture.SEEDS[-1] else fixture.SEEDS[0]))))

    def test_unknown_operations_are_unhandled_and_oversize_or_bad_options_reject(self):
        self.assertIsNone(fixture.handle({"operation":"unhandled"}));self.assertIsNone(fixture.handle(None))
        self.assertIsNone(fixture.handle({"operation":[]}))
        self.invalid(self.request("recommendationIdentity",fixture.SEEDS[0],extra="x"*(239*1024)))
        self.invalid(self.request("recommendationIdentity",fixture.SEEDS[0],refresh=1))
        self.invalid(self.request("recommendationIdentity",fixture.SEEDS[0],fetchedAt=False))
        self.invalid(self.request("artistCandidates",fixture.SEEDS[0],region=False))
        self.invalid(self.request("artistCandidates",fixture.SEEDS[0],limit=True))


if __name__=="__main__": unittest.main()
