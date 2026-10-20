#!/usr/bin/env bash
# Three pinned-candidate repetitions; each owns its fixture services and logs.
set -u
A="/root/colosseum-android/android-sdk/platform-tools/adb -s emulator-5554"
F=/home/user/Colosseum/cloud-handoff/android-013/claude-013/fixture013
kill $(cat /root/c013/fixture-logs/run7/owned-pids.txt) 2>/dev/null
for n in 1 2 3; do
  L=/root/c013/fixture-logs/rep$n; mkdir -p $L
  { echo "## before $(date -u +%FT%TZ)"; $A reverse --list; } > $L/ownership-before.txt
  (cd $F && nohup python3 -I fixture013.py track fixture013.torrent 51413 127.0.0.1 51412 $L/tracker.jsonl > $L/tracker.out 2>&1 &)
  (nohup python3 -I $F/fixture013.py seed $F/fixture013.torrent $F/fixture013.mp4 51412 $L/seed.jsonl > $L/seed.out 2>&1 &)
  until [ -s $L/seed.jsonl ] && [ -s $L/tracker.jsonl ]; do sleep 1; done
  pgrep -f '^python3 -I .*fixture013.py (seed|track)' > $L/owned-pids.txt
  bash /tmp/claude-0/-home-user-Colosseum/a2b8b6f7-4189-50ea-a5f4-52a65770c52f/scratchpad/c013_cycle.sh $L all > $L/cycle.out 2>&1
  sleep 30   # observe post-close transfer before stopping owned services
  { echo "## after $(date -u +%FT%TZ)"; $A reverse --list; ps -o pid=,args= -p $(tr '\n' ',' < $L/owned-pids.txt | sed 's/,$//') | cut -c1-90; } > $L/ownership-after.txt
  kill $(cat $L/owned-pids.txt) 2>/dev/null
  echo "rep$n done $(date -u +%T)"
done
