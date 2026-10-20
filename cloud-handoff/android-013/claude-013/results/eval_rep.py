import json, re, sys, pathlib
L = pathlib.Path(sys.argv[1]); r = {"rep": L.name}
acts = (L/"actions.txt").read_text()
r["timeouts"] = re.findall(r"TIMEOUT waiting for (.*)", acts)
pids = dict(re.findall(r"pid (before background|after foreground|after close) (\d*)", acts))
r["pid_before_bg"], r["pid_after_fg"], r["pid_after_close"] = (pids.get(k, "") for k in ("before background","after foreground","after close"))
logs = "".join(p.read_text(errors="replace") for p in sorted(L.glob("logcat-*.txt")))
iso = (L/"isolation.txt").read_text() if (L/"isolation.txt").exists() else ""
r["isolation"] = {k: v for k, v in re.findall(r"(session|torrent_count|enable_dht|enable_lsd|enable_upnp|enable_natpmp|max_pex_peers|live_dht_running)=([0-9a-fx]+|true|false)", iso)}
r["isolation_pass"] = "isolation-verify PASS" in iso
r["admission_pass"] = logs.count("admission PASS")
r["stream_ready"] = len(set(re.findall(r'streamReady url: "http://127.0.0.1:(\d+)/stream/([0-9a-f]{8})', logs)))
r["re_demand"] = logs.count("fixture re-demand")
r["stop_released"] = len(re.findall(r'released torrent "7f13e4df[0-9a-f]+" status "ok"', logs))
r["fatal"] = len(re.findall(r"Fatal signal|FATAL EXCEPTION|DeadSystemException", logs))
r["playback_errors"] = re.findall(r"ExoPlaybackException: ([^,\n]*)", logs)
r["http_404"] = logs.count("Response code: 404")
r["http_opens"] = re.findall(r"open gen=(\d+) route=\S+ position=(\d+)", logs)
seed = [json.loads(l) for l in open(L/"seed.jsonl")]
pieces = [e for e in seed if e["event"] == "piece"]
r["seed_pieces"] = len(pieces); r["seed_bytes"] = pieces[-1]["served_bytes_total"] if pieces else 0
close_t = re.search(r"(\d\d:\d\d:\d\d)\.\d+ back \(close player\)", acts)
r["post_close_pieces"] = sum(1 for e in pieces if close_t and e["at"][11:19] > close_t.group(1))
trk = [json.loads(l) for l in open(L/"tracker.jsonl")][1:]
r["tracker_events"] = [e.get("event_param") for e in trk]; r["uploaded_nonzero"] = [e for e in trk if e.get("uploaded") not in ("0", None)]
r["captures"] = sorted(p.name for p in L.glob("*.png") if not p.name.startswith("contact"))
print(json.dumps(r))
