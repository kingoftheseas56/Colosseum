# M04 source-led media primitives

Implemented and compiled in isolated build-m04. 606 exact factory oracle cases pass plus native split-box, truncation, overflow, missing timing, unmatched track, inclusive VTT bucket checks. Final-newline comparator mutation fails (exit 1), clean corpus passes (exit 0). Initial test-first RED was missing M04 headers. Oracle identity is locked 405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f.

Production files: include/server1/media/{HlsPlaylist,SegmentLayout,Mp4Timeline,VttTimeline}.h and src/media counterparts under native/colosseum_server_v1. Test: tests/test_segment_timeline.cpp, tests/run_segment_timeline_oracle.cjs, tests/fixtures/segment-timeline.json. Dependency: M03 groupKeyframeSegments in KeyframeIndex.cpp. Existing Boost headers supply exact IEEE754 rational-to-decimal rounding; no extra linked library.

Coverage: source 467 predict; 914 master; 915 media; 918 grouping via M03 implementation; 922 in-place 64-bit decode-time edits; 924 format; 925/927 used cue-field parsing, including malformed cues and recovery; 880 timescale and 895/896 inspection. Actual FFmpeg-generated H264 fragmented MP4 supplies three mdat fragments, sidx, version-1 tfdt, moov timescale. FFmpeg-generated VTT supplies actual cues crossing the 10-second boundary. Oracle VTT exposes a Region header constructor error and drops a pending arrow cue when its remaining text lacks a newline; both source quirks preserved.

Golden corpus generator and original source hashes: oracle.json; raw clean output green.log; rejected mutation mutation.log; configure/build logs preserved. Native box reader tested with every split position of compact normal and extended boxes and every incomplete prefix at EOF. Both size=1 extended and size=0 EOF boxes handled; the source version-before-error mutation order is preserved for failed decode-time updates.

Exactness limits requiring receiving attention, not a full M04 gate claim:
- M468 exact asynchronous factory compared 40 cases on Node 24.19: 16 normal-size successful cases agree byte-for-byte; 24 source cases stalled at the bounded 1 s observation (all extended-size cases and 1/2-byte Readable.from chunks). Those stalls are explicitly not counted as passes. reader-goldens.json/reader-oracle.log preserve them. Native incremental reader handles these sizes without stalled reads. Exact source lifecycle compatibility remains a receiving issue.
- M922 version-0 overwrite semantics now match the source against containing Buffer bounds, including writing version before conversion error and clamping negative fractional decode times. Added exact differential cases for version-0 overwrites with trailing mdat; no native out-of-buffer writes.
- Source predictor can allocate huge JS arrays; native explicitly rejects over 1,000,000 root segments (resource admission boundary). Invalid/native-overflow timing rejects rather than supplies an invented fallback.
- VTT API returns only startTime/endTime/text fields used by M916 and formatting; DOM cue metadata/rendering is not ported or claimed.
- Full converter/process/router integration belongs to M05/M06. No player runtime, full packet predecessor suite, Windows or final server parity claim.

No commits, pushes, shared build edits, or broad-suite execution performed.
