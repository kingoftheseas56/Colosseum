#!/usr/bin/env python3
"""Controlled ffprobe/ffmpeg boundary; no media or network access."""
import json, os, sys, struct, time
if '-show_entries' in sys.argv:
    print(json.dumps({'format': {'format_name': 'fixture', 'duration': '12'},
                     'streams': [
                         {'index': 0, 'codec_type': 'video', 'codec_name': 'h264', 'width': 1920, 'height': 1080, 'bit_rate': '6000000', 'r_frame_rate': '24/1', 'time_base': '1/1000'},
                         *[{'index': i+1, 'codec_type': 'audio', 'codec_name': 'aac', 'channels': 2, 'sample_rate': '48000', 'bit_rate': '128000', 'time_base': '1/48000'} for i in range(3)] ]}))
else:
    with open(os.environ['SERVER1_MEDIA_SETTINGS_LOG'], 'a') as f:
        f.write(json.dumps(sys.argv[1:])+'\n')
    def box(kind, payload=b''):
        return struct.pack('>I', len(payload)+8)+kind+payload
    scale = int(sys.argv[sys.argv.index('-ar:a')+1]) if '-ar:a' in sys.argv else 1000
    tk = bytes(12)+struct.pack('>I', 1)
    md = bytes(12)+struct.pack('>I', scale)
    os.write(1, box(b'ftyp')+box(b'moov', box(b'trak', box(b'tkhd', tk)+box(b'mdia', box(b'mdhd', md)))))
    while True:
        time.sleep(1)
