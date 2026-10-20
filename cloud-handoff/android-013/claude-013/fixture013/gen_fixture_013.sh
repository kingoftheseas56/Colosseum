#!/usr/bin/env bash
# Deterministic synthetic fixture for task 013. All content is generated from
# ffmpeg lavfi sources (no third-party media). Usage: gen_fixture_013.sh <out.mp4> <font.ttf>
set -euo pipefail
out="$1"; font="$2"
# 60 s, 640x360, 12 fps, IDR every 12 frames (1 s), closed GOP, moov first.
# Burned-in markers: T=<seconds.ms>, F=<frame>, W=<5 s window>, a bar whose x
# position tracks time, and a background hue that changes every 5 s window.
ffmpeg -hide_banner -v error -y \
  -f lavfi -i "color=c=black:s=640x360:r=12:d=60" \
  -f lavfi -i "aevalsrc='0.4*sin(2*PI*(330+55*floor(t))*t)':s=44100:c=mono:d=60" \
  -filter_complex "[0:v]geq=r='if(between(X\,(W-24)*T/60\,(W-24)*T/60+24)*between(Y\,300\,340)\,255\,40+16*mod(floor(T/5)\,6)*2)':g='if(between(X\,(W-24)*T/60\,(W-24)*T/60+24)*between(Y\,300\,340)\,255\,20+30*mod(floor(T/5)\,3))':b='if(between(X\,(W-24)*T/60\,(W-24)*T/60+24)*between(Y\,300\,340)\,255\,60+15*mod(floor(T/5)\,4))',\
drawtext=fontfile=${font}:fontsize=72:fontcolor=white:x=24:y=30:text='T=%{eif\:t\:d\:2}.%{eif\:mod(t*1000\,1000)\:d\:3}',\
drawtext=fontfile=${font}:fontsize=56:fontcolor=yellow:x=24:y=120:text='F=%{eif\:n\:d\:4}',\
drawtext=fontfile=${font}:fontsize=56:fontcolor=cyan:x=24:y=200:text='W=%{eif\:floor(t/5)\:d\:2}',format=yuv420p[v]" \
  -map "[v]" -map 1:a \
  -c:v libx264 -profile:v main -level 3.1 -preset medium -crf 23 -threads 1 \
  -x264-params "keyint=12:min-keyint=12:scenecut=0:open-gop=0:bframes=2" \
  -c:a aac -b:a 96k \
  -movflags +faststart -map_metadata -1 -fflags +bitexact -flags:v +bitexact -flags:a +bitexact \
  "$out"
