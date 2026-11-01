#!/usr/bin/python3
import sys,time,os
args=sys.argv[1:]
source=args[args.index('-i')+1]
if len(args)==2:
    if 'slow' in source: time.sleep(.15)
    sys.stderr.write("Input #0, avi, from 'fixture':\n  Duration: 00:00:07.00, start: 0.000, bitrate: 1000 kb/s\n    Stream #0:0: Video: h264, yuv420p, 320x240, 24 fps\n    Stream #0:1: Audio: aac, 48000 Hz, stereo, 128 kb/s\n")
    sys.exit(1)
if 'failed' in source: sys.exit(7)
size=8*1024*1024 if 'large' in source else 40000
for i in range(0,size,4096):
    os.write(1,b'x'*min(4096,size-i))
    if 'large' in source: time.sleep(.0001)
