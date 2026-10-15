"""Isolated actual-app static catalogue qualification. Windows/Lanista only.

Never connects to the daily pipe or uses the default profile. HTTP fixture data
is the real prepared publication; no canned catalogue response or QML override.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


class Pipe:
    def __init__(self, name):
        assert name.startswith("i05-static-")
        self.path, self.seq = "\\\\.\\pipe\\" + name, 0
        self.k = ctypes.WinDLL("kernel32", use_last_error=True)
        self.k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                      ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        self.k.CreateFileW.restype = wintypes.HANDLE
        for name in ["ReadFile", "WriteFile"]:
            getattr(self.k, name).argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                              ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        self.k.PeekNamedPipe.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                        ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        self.k.CloseHandle.argtypes = [wintypes.HANDLE]

    def call(self, cmd, payload=None, timeout=12):
        self.seq += 1
        handle = self.k.CreateFileW(self.path, 0xC0000000, 0, None, 3, 0, None)
        if handle == ctypes.c_void_p(-1).value:
            raise OSError(ctypes.get_last_error(), "private Lanista pipe unavailable")
        try:
            data = json.dumps({"seq":self.seq,"cmd":cmd,"payload":payload or {}}).encode()+b"\n"
            written = wintypes.DWORD()
            assert self.k.WriteFile(handle,data,len(data),ctypes.byref(written),None)
            result = bytearray(); deadline = time.monotonic()+timeout
            while time.monotonic() < deadline:
                available = wintypes.DWORD()
                if not self.k.PeekNamedPipe(handle,None,0,None,ctypes.byref(available),None):
                    raise OSError("private pipe closed before reply")
                if available.value:
                    block = ctypes.create_string_buffer(min(available.value, 1024*1024))
                    count = wintypes.DWORD()
                    assert self.k.ReadFile(handle,block,len(block),ctypes.byref(count),None)
                    result.extend(block.raw[:count.value])
                    if b"\n" in result:
                        reply = json.loads(result.split(b"\n")[0]); assert reply["seq"] == self.seq
                        if reply.get("type") == "error": raise RuntimeError(reply)
                        return reply
                    assert len(result) < 1024*1024
                time.sleep(.02)
            raise TimeoutError(cmd)
        finally:
            self.k.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path); parser.add_argument("publication", type=Path)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--inspect-only", action="store_true")
    args = parser.parse_args(); args.evidence.mkdir(parents=True,exist_ok=True)
    tag = "i05-static-" + uuid.uuid4().hex[:12]
    requests = []; mode = "cold"
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            path = self.path.split("?",1)[0]
            file = args.publication / path.lstrip("/")
            allowed = path.startswith(("/catalogue/","/assets/content/")) and ".." not in path
            raw = file.read_bytes() if allowed and file.is_file() else b""
            status = 200 if raw else 404
            etag = '"'+hashlib.sha256(raw).hexdigest()+'"'
            if status == 200 and self.headers.get("If-None-Match") == etag:
                status, raw = 304, b""
            self.send_response(status); self.send_header("Content-Length",str(len(raw)))
            self.send_header("ETag",etag)
            self.send_header("Cache-Control","max-age=60" if path.endswith("current.json") else "public,max-age=31536000,immutable")
            self.send_header("Content-Type","image/webp" if path.endswith(".webp") else "application/json")
            self.end_headers()
            try: self.wfile.write(raw)
            except (BrokenPipeError,ConnectionResetError): pass
            requests.append({"mode":mode,"path":path,"status":status,"bytes":len(raw),"at":time.monotonic()})
        def do_POST(self):
            requests.append({"mode":mode,"path":self.path,"worker":True,"bytes":0})
            self.send_response(503); self.send_header("Content-Length","0"); self.end_headers()
    server = http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    origin = f"http://127.0.0.1:{server.server_port}"
    screen = args.evidence / "offscreen.json"
    save(screen,{"screens":[{"name":"qualification","x":0,"y":0,"width":1920,"height":1080,
                             "logicalDpi":96,"logicalBaseDpi":96,"dpr":1}]})
    sessions = []
    try:
        for mode in (["cold"] if args.inspect_only else ["cold","warm","offline"]):
            if mode == "offline": server.shutdown(); server.server_close()
            folder = args.evidence / mode; folder.mkdir(exist_ok=True)
            pipe_name = tag + "-" + mode; pipe = Pipe(pipe_name)
            env = dict(os.environ, COLOSSEUM_APPDATA_TAG=tag, COLOSSEUM_LANISTA_PIPE=pipe_name,
                       COLOSSEUM_LANISTA_DRIVE="1", COLOSSEUM_AUTOMATION_FOCUS_SEAMS="0",
                       COLOSSEUM_TEST_CATALOGUE_ORIGIN=origin,
                       COLOSSEUM_TEST_CATALOGUE_OFFLINE="1" if mode=="offline" else "0",
                       QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                       QT_QUICK_CONTROLS_STYLE="Basic", QT_LOGGING_RULES="colosseum.catalogue.static.info=true")
            env.pop("COLOSSEUM_DEV",None); env.pop("COLOSSEUM_QML_ROOT",None)
            start=time.monotonic(); before=len(requests)
            with (folder/"app.log").open("wb") as log:
                # QPA arguments are colon-separated; a Windows drive letter must
                # not appear in configfile=. Use the fixture-relative path.
                proc=subprocess.Popen([str(args.exe.resolve()),"-platform","offscreen:configfile=../offscreen.json"],cwd=folder,env=env,stdout=log,stderr=log,
                                      creationflags=0x08000000)
                row={"mode":mode,"pid":proc.pid,"tag":tag,"pipe":pipe_name,"steps":[]}
                sessions.append(row)
                try:
                    def until(fn, seconds=90):
                        end=time.monotonic()+seconds; last=None
                        while time.monotonic()<end:
                            if proc.poll() is not None: raise RuntimeError("App exited: "+str(proc.returncode))
                            try:
                                last=fn()
                                if last: return last
                            except (OSError,RuntimeError): pass
                            time.sleep(.2)
                        raise TimeoutError(str(last))
                    state=until(lambda:pipe.call("get-state"),180)
                    assert tag in state["appDataRoot"] and tag in state["cacheRoot"]
                    assert state["windows"][0]["width"]==1920 and state["windows"][0]["height"]==1080
                    row["roots"]=state; row["pipeReadyMs"]=(time.monotonic()-start)*1000
                    def props(target, names): return pipe.call("qml-get",{"object":target,"props":names})["props"]
                    def snapshot(name):
                        data=pipe.call("ui-snapshot");save(folder/(name+".json"),data);return data["elements"]
                    def click(name):
                        elements=snapshot("before-"+name)
                        matches=[x for x in elements if x["objectName"]==name and 0<x["centerY"]<1060]
                        assert matches,name
                        return pipe.call("ui-click",{"target":matches[0]["handle"]})
                    def tree(root=None):
                        query={"maxDepth":35,"maxItems":3000}
                        if root: query["root"]=root
                        result=pipe.call("dump-ui",query);items=list(result["items"])
                        while result.get("truncated") and result.get("continuation"):
                            query.update(generation=result["generation"],cursor=result["continuation"]["cursor"])
                            result=pipe.call("dump-ui",query);items.extend(result["items"])
                        return items
                    def class_props(root, cls, names):
                        for item in tree(root):
                            if cls in item["class"] and item["visible"]:
                                return props(item["handle"],names)
                        return None
                    def scroll_to(name, world):
                        for _ in range(16):
                            matches=[x for x in snapshot("scroll-target") if x["objectName"]==name]
                            assert matches,name
                            y=matches[0]["centerY"]
                            if 160<y<940:return
                            pipe.call("ui-scroll",{"target":world+"WorldScroll","dy":-600 if y>940 else 600})
                            time.sleep(.25)
                        raise RuntimeError("Could not reveal "+name)
                    def record(name, data):
                        row["steps"].append({"name":name,"elapsedMs":(time.monotonic()-start)*1000,"props":data})
                        save(folder/(name+"-grab.json"),pipe.call("get-state",{"grab":{"target":"window"}}))
                        print(mode,name,round((time.monotonic()-start)*1000),flush=True)
                    until(lambda:not props("bootSplash",["visible"])["visible"],180)
                    row["shellReadyMs"]=(time.monotonic()-start)*1000
                    save(folder/"home-grab.json",pipe.call("get-state",{"grab":{"target":"window"}}))
                    click("modePill_Tankoban")
                    data=until(lambda:(lambda p:p if p.get("automationItemCount",0)>0 and not p.get("loading") else None)(props("tankobanDiscoverBrowser",["loading","automationItemCount","automationType","items"])))
                    row["steps"].append({"name":"tankoban-manga","elapsedMs":(time.monotonic()-start)*1000,"props":data})
                    save(folder/"tankoban-grab.json",pipe.call("get-state",{"grab":{"target":"window"}}))
                    save(folder/"tree.json",pipe.call("dump-ui",{"maxDepth":30,"maxItems":3000}))
                    snapshot("tankoban-ui")
                    if not args.inspect_only:
                        scroll_to("tankobanDiscoverType_comics","tankoban")
                        click("tankobanDiscoverType_comics")
                        data=until(lambda:(lambda p:p if p.get("automationType")=="comics" and p.get("automationItemCount",0)>0 and not p.get("loading") else None)(props("tankobanDiscoverBrowser",["loading","automationItemCount","automationType"])))
                        record("tankoban-comics",data)
                        click("tankobanDiscoverType_manga")
                        until(lambda:props("tankobanDiscoverBrowser",["automationType"])["automationType"]=="manga")
                        scroll_to("mangaDiscoverCard_13","tankoban");click("mangaDiscoverCard_13")
                        data=until(lambda:(lambda p:p if p.get("hasShelf") and not p.get("chapterMode") else None)(props("mangaSeriesPage",["seriesTitle","hasShelf","chapterMode","malId"])))
                        # The actual reading-room library is the canonical rendered
                        # volume model; the legacy page.volumes property is unused.
                        library=until(lambda:(lambda p:p if p.get("renderedCount")==113 else None)(props("readingRoomLibrary",["renderedCount"])))
                        data.update(library)
                        record("tankoban-series",data)
                        pipe.call("ui-keypress",{"key":"Escape"});time.sleep(.2)
                        click("topBarSearch_Tankoban")
                        pipe.call("ui-text-input",{"target":"searchSurfaceInput","text":"one piece"})
                        data=until(lambda:(lambda p:p if p.get("resultCount",0)>0 and not p.get("searching") else None)(props("tankobanSearchSurface",["searching","resultCount","searchError","lastDispatchedQuery"])))
                        assert data["searchError"] in ("","provider unavailable")
                        record("search",data)
                        pipe.call("ui-keypress",{"key":"Escape"});time.sleep(.2)
                        click("modePill_Theatre")
                        until(lambda:props("theatreWorld",["activeTab"]))
                        # The retained world's nested glass/delegate tree settles
                        # after the root exists. Wait for the real tab hit target.
                        until(lambda:(lambda p:p.get("visible") and p.get("width",0)>0)(props("theatreTab_movies",["visible","width"])))
                        scroll_to("theatreTab_movies","theatre");click("theatreTab_movies")
                        until(lambda:props("theatreWorld",["activeTab"])["activeTab"]=="movies")
                        data=until(lambda:(lambda p:p if not p.get("loading") and p.get("liveShelfCount",0)>0 else None)(props("theatreCatalogPage",["loading","liveShelfCount","errorText","pageKey"])))
                        assert not data["errorText"];record("theatre-shelves",data)
                        # Read the actual WidgetHeader label, then send a mouse click
                        # through the existing bridge. Never invoke a fabricated row.
                        opened=False
                        for _ in range(12):
                            for item in tree("theatreCatalogPage"):
                                if "WidgetHeader" not in item["class"] or not item["visible"]:continue
                                rect=item["sceneRect"]
                                if not 160<rect["y"]<950:continue
                                if not props(item["handle"],["navigable"]).get("navigable"):continue
                                children=tree(item["handle"])
                                # A nested dump changes the epoch: refresh the subtree,
                                # then keep its new handles until the mouse event.
                                for child in children:
                                    if child["visible"] and "QQuickText" in child["class"] and props(child["handle"],["text"]).get("text")=="See all":
                                        pipe.call("ui-click",{"target":child["handle"]});opened=True;break
                                if opened:break
                                break  # nested dump replaced outer handles; refresh on the next pass
                            if opened:break
                            pipe.call("ui-scroll",{"target":"theatreWorldScroll","dy":-500});time.sleep(.3)
                        assert opened,"No actual Theatre See all control reached"
                        data=until(lambda:(lambda p:p if p and not p.get("loading") and p.get("hasMore") else None)(class_props("theatreWorld","TheatreSeeAllPage",["loading","hasMore","errorText","titleText"])))
                        assert not data["errorText"];record("theatre-see-all",data)
                        pipe.call("ui-keypress",{"key":"Escape"});time.sleep(.2)
                        click("modePill_Universes")
                        data=until(lambda:(lambda p:p if p.get("universeContentGeneration",0)>0 else None)(props("universesWorld",["universeContentGeneration","facet"])))
                        count=props("universesCount",["text"]);assert "20" in count["text"]
                        record("universes",{**data,**count})
                        # The actual ledger's first portal is reached with its keyboard
                        # controller, exercising the structured universe route.
                        opened=False
                        for _ in range(18):
                            candidates=[item for item in tree("universesLedger")
                                if "UniverseBanner" in item["class"] and item["visible"]
                                and 160<item["sceneRect"]["y"]<780]
                            if candidates:
                                pipe.call("ui-click",{"target":candidates[0]["handle"]});opened=True;break
                            pipe.call("ui-scroll",{"target":"universesWorldScroll","dy":-500});time.sleep(.25)
                        assert opened,"No real universe portal reached"
                        page=until(lambda:(lambda p:p if p.get("active") and p.get("status") in (1,"Ready") else None)(props("universeLayer",["active","status","universeName","templatePage","plain"])))
                        assert page["universeName"]
                        save(folder/"universe-page-tree.json",pipe.call("dump-ui",{"maxDepth":35,"maxItems":3000}))
                        record("universe-page",page)
                    row["passed"]=True
                except Exception as error:
                    row["error"]=repr(error)
                    try:save(folder/"failure-tree.json",pipe.call("dump-ui",{"maxDepth":30,"maxItems":3000}))
                    except Exception:pass
                    raise
                finally:
                    try:pipe.call("ui-keypress",{"key":"Ctrl+Q"})
                    except Exception as error:row["quitError"]=repr(error)
                    try:row["rootExit"]=proc.wait(30)
                    except subprocess.TimeoutExpired:
                        proc.terminate();row["rootExit"]=proc.wait(15);row["forcedTermination"]=True
                    row["requests"]=requests[before:];row["durationMs"]=(time.monotonic()-start)*1000
                    app_root=Path(row.get("roots",{}).get("appDataRoot",folder))
                    if tag in str(app_root) and (app_root/"logs/colosseum.log").exists():
                        (folder/"native.log").write_bytes((app_root/"logs/colosseum.log").read_bytes())
                    row["catalogueHttpRequests"]=len(row["requests"])
                    row["catalogueHttpBytes"]=sum(x["bytes"] for x in row["requests"])
                    row["workerRequests"]=sum(x.get("worker",False) or x["path"].startswith("/v1/") for x in row["requests"])
                    # World Search intentionally also asks the deferred Comics
                    # contains service. This static-only fixture rejects that
                    # guest lease (503); prepared routes must never reach /v1/.
                    unexpected=[x for x in row["requests"] if x["path"].startswith("/v1/")
                                and x["path"]!="/v1/guest/session"]
                    row["unexpectedWorkerRoutes"]=unexpected
                    save(folder/"receipt.json",row)
                    assert not unexpected,unexpected
                assert row["rootExit"]==0 and not row.get("forcedTermination"),row
    finally:
        if mode!="offline":server.shutdown();server.server_close()
        save(args.evidence/"sessions.json",{"tag":tag,"origin":origin,"sessions":sessions,"requests":requests,"deployed":False})


if __name__=="__main__":main()
