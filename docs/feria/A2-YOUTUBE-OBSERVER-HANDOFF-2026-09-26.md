# Feria A2 — YouTube playback observer handoff (2026-09-26)

**Status: INCOMPLETE — N1 UNTESTED.** The requested COMPLETE condition requires N1–N4 and P1–P5 all PROVEN. Stable watch identity and ad exclusion are proven, so the specified BLOCKED condition does not apply. No file outside the permitted set is needed, so NEEDS-DECISION does not apply. A2 remains open at the home inline-preview control. This is one A2-YouTube slice only; B4, C-slices, Netflix A2, Continue records, Colosseum launch, and Harness were not started or claimed.

## Implementation and message contract

`FeriaProviderHost` installs the YouTube observer with `AddScriptToExecuteOnDocumentCreated` before first navigation and receives `chrome.webview.postMessage` locally. It installs only for a YouTube initial provider and posts only from `youtube.com` hosts. The script samples at one-second intervals and on `ended`; it never clicks, seeks, plays, pauses, reads credentials/cookies/storage/streams/frames, or sends observations off the PC. The probe writes local PID-stamped JSONL events. It creates no Continue record.

Document message, version 1:

```json
{
  "kind": "feria-youtube-observation-v1",
  "href": "https://www.youtube.com/watch?v=<id>",
  "media": {"currentTime": 8.27, "duration": 252.241, "paused": false, "ended": false},
  "title": "mediaSession title or null",
  "adShowing": false,
  "adInterrupting": false
}
```

`media` is null when no `<video>`/`<audio>` exists. The probe derives `videoId` solely from `watch?v=` and adds it to `observer-sample` events. `observer-qualified` and `observer-completed` are event-log observations, not persistence or sync writes. Every event has UTC timestamp and probe PID. The probe's separate test driver may play/pause/seek media to establish a proof; the document-created observer never does.

**YouTube qualifying rule, based on these runs:** require an exact YouTube `/watch` path with nonempty `v`, nonempty `mediaSession.metadata.title`, a finite positive duration and valid position, no `ad-showing` or `ad-interrupting`, and playing, non-ended media. Require **three samples with two consecutive advancing deltas**, each 0.4–3.5 seconds, for the same ID at the one-second sampling cadence. The observed ordinary deltas were about one second; a 20-second seek, zero-delta pause, and ad samples must not supply either advance. Reset the advance count on ad, pause, invalid media, seek-sized delta, or identity change. A new autoplay ID starts from zero advances.

**Completion rule:** emit one `observer-completed` for an already qualified ID only when that same non-ad watch-page media reports `ended=true`. `duration` proximity alone is insufficient. PID 21864 reported `ended=true` at 19.021/19.021 for `jNQXAC9IVRw`, then qualified the autoplayed `RoBabfioVF8` only after its own advancing samples.

## PID-bound proof table

Evidence root: `C:/Users/Suprabha/AppData/Local/Colosseum/feria-host-probe/evidence/`. Each listed PNG was captured with `PrintWindow` for the probe's HWND; the matching JSONL `capture` event records the HWND, dimensions, path and SHA-256. No full-desktop grabs were used. The final Shorts smoke run PID 19520 was minimized at capture time (237×39), so its PNG is not used as visual proof; PID 4780 supplies the Shorts HWND image.

| Proof | State | Runtime result and evidence under the evidence root |
| --- | --- | --- |
| N1 home hover preview | **UNTESTED** | PID 19268: `a2-19268-events.jsonl`, `a2-19268-150-a2-home-inline-playing.png`. Home URL and zero qualified observations, but the advancing second video was `0×0` in both media scans: hidden media, not a visible inline preview. PID 1192/25688/4644 attempts likewise did not show advancing visible preview. No N1 pass claimed. |
| N2 Shorts playing | **PROVEN** | PID 4780: `a2-4780-events.jsonl`, `a2-4780-150-a2-shorts.png`. Probe driver started the Short because native autoplay stayed paused; 13 playing samples advanced on `/shorts/g1zbexho0zc`, ID remained empty, zero qualified. Final-QML smoke PID 19520 again advanced and emitted zero qualified. |
| N3 paused watch dwell | **PROVEN** | PID 25260: `a2-25260-events.jsonl`, `a2-25260-150-a2-dwell-5min.png`. Pause driver armed 34 ms after navigation-completed, held ad and main content at time 0; 306 samples through the 300-second dwell, zero playing and zero qualified. End read: main title, `paused=true`, `currentTime=0`. |
| N4 pre-roll ad | **PROVEN** | PID 26264: `a2-26264-events.jsonl`, `a2-26264-150-a2-watch.png`. 22 `adShowing=true` samples for the Kurlon pre-roll, including advancing ad time, yielded zero qualified observations. The HWND image visibly shows the sponsored ad. |
| P1 main-content play | **PROVEN** | PID 18192: `a2-18192-events.jsonl`, `a2-18192-150-a2-watch.png`. Three advancing non-ad samples qualified `9bZkp7q19f0`; mediaSession title `PSY - GANGNAM STYLE(강남스타일) M/V` was recorded. |
| P2 pause, seek, resume | **PROVEN** | Same PID 18192. Script reads: 8.268 s before pause, 28.268 s after 20-second seek while paused, then 37.192 s playing after resume. Observer samples retained `9bZkp7q19f0`; the seek jump did not count as an advancing sample. |
| P3 ended and autoplay identity | **PROVEN** | Final observer binary, PID 21864: `a2-21864-events.jsonl`, `a2-21864-150-a2-complete.png`. `jNQXAC9IVRw` qualified, emitted one completion at 19.021/19.021, then autoplay changed to `RoBabfioVF8`. New-ID samples began at 0, 1.043, 1.390, 2.310, 3.308 s; qualification occurred at 3.308 only after two consecutive valid advances. |
| P4 exact resume route | **PROVEN** | PID 26028: `a2-26028-events.jsonl`, `a2-26028-150-a2-resume.png`. Saved `(9bZkp7q19f0, 37.192 s)` from PID 18192. Opened exact `https://www.youtube.com/watch?v=9bZkp7q19f0&t=37s` in new `profile-a2-resume-fresh2`. After a served ad, main content read 37 s before probe play and 38.286 s playing, 1.094 s from saved position. |
| P5 identity stability | **PROVEN** | PID 18192 `9bZkp7q19f0`, PID 21864 `jNQXAC9IVRw` are distinct. Fresh-profile PID 26028 reopened `9bZkp7q19f0` with the same ID. See those three JSONL files. |

## YouTube capability row

| Capability | State | Basis |
| --- | --- | --- |
| Title identity | **PROVEN** | Stable `/watch?v=` ID plus mediaSession title in P1/P3/P5. |
| Exact resume route | **PROVEN** | P4 fresh-profile `watch?v=<id>&t=<floor(position)>s` reached 38.286 s from saved 37.192 s. This earns the route label **Resume**. |
| Provider-managed resume | **UNTESTED** | No sign-in or provider history restoration was attempted. |
| Completion signal | **PROVEN** | P3 `ended=true` on qualified content, one completion event on final binary. |

## SHA-256 (A2 new/changed source and built executable)

| File | SHA-256 |
| --- | --- |
| `native/feria/provider-host/FeriaProviderHost.cpp` | `6B55B902211FD77612112488B53B5F78DE679706CBF8D92053197A91E40AE0AE` |
| `native/feria/provider-host/FeriaProviderHost.h` | `CC9395BD1646A712DAF648E27A7AC231907D8A28785B7AC8CC29FC96EF7979A7` |
| `native/feria/provider-host/FeriaHostItem.cpp` | `FAFCA0C49C660545A16BA8D4107D095E5881C9D3E8E03CCF41229DF7E763A825` |
| `native/feria/provider-host/FeriaHostItem.h` | `DFD30400AF73C223BC763E330CC1D5AA72BBC1E5605272AFE8A55CBE1EFBF382` |
| `native/feria/provider-host/observer/YouTubeObserverScript.h` | `B8B9AD94A1E6100517240D07BA3EA0CC6AFA88578A8AB1AD7A6A75DAB77B60F4` |
| `tests/feria_host_probe/main.cpp` | `90A7FC4DD12983E180A342BBF7C015138C4090A588298A934CA721283C182DFF` |
| `tests/feria_host_probe/Main.qml` | `FFFAF9D60551761DE2511069F720FBB833A7081594BCCC210F101AEB9FA0FEAD` |
| `%LOCALAPPDATA%/Colosseum/feria-host-probe/build-a1-msvc/feria_host_probe.exe` | `F17D7B4C1AA3490C73C24C5B5D0AF4C1530DEEF24CDEE1D16D482BFFCA4D9585` |

The handoff cannot embed its own stable hash because changing that line changes the document hash. The final scoped `feria_host_probe` build succeeded. No Git mutation commands were used; the shared tree's other agents' WIP was left untouched.

## Known gap

N1 still needs a visible home-feed hover preview actually advancing in this host and zero qualified observations during that interval. YouTube showed hover tiles and sometimes a second media element, but the second element's measured rectangle was `0×0`; one driven hidden-media playback is explicitly rejected as N1 evidence. The observer currently samples the first media element; the watch-page proofs established it as the relevant player in these runs, while future multi-media layouts need new validation. Provider-managed resume remains untested without sign-in. No credentials were entered.
