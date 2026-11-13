#!/usr/bin/env python3
"""Generate original synthetic profiling inputs; no input asset files are used."""
import argparse, hashlib, json, pathlib, subprocess
p=argparse.ArgumentParser();p.add_argument('--ffmpeg',default='/usr/bin/ffmpeg');p.add_argument('--ffprobe',default='/usr/bin/ffprobe');p.add_argument('--output',type=pathlib.Path,default=pathlib.Path(__file__).parent/'assets');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
common=['-hide_banner','-loglevel','error','-y','-fflags','+bitexact']
recipes={
 'hevc.mkv':(['-f','lavfi','-i','color=c=black:s=1920x960:r=25:d=0.2','-an','-c:v','libx265','-preset','ultrafast','-pix_fmt','yuv420p','-threads','1','-x265-params','pools=none:frame-threads=1:wpp=0:log-level=error:info=0','-flags:v','+bitexact','-map_metadata','-1','-fflags','+bitexact','-f','matroska'],'video/x-matroska'),
 'ac3-2chan.wav':(['-f','lavfi','-i','anullsrc=r=44100:cl=stereo','-t','0.25','-c:a','ac3','-b:a','192k','-threads','1','-flags:a','+bitexact','-map_metadata','-1','-fflags','+bitexact','-f','wav'],'audio/x-wav'),
 'aac-6chan.wav':(['-f','lavfi','-i','anullsrc=r=44100:cl=5.1','-t','0.25','-c:a','aac','-b:a','256k','-threads','1','-flags:a','+bitexact','-map_metadata','-1','-fflags','+bitexact','-f','wav'],'audio/x-wav')}
rows=[]
for name,(args,mime) in recipes.items():
 path=a.output/name;subprocess.run([a.ffmpeg,*common,*args,str(path)],check=True)
 probe=json.loads(subprocess.check_output([a.ffprobe,'-v','error','-show_format','-show_streams','-of','json',str(path)]))
 subprocess.run([a.ffmpeg,'-hide_banner','-loglevel','error','-xerror','-i',str(path),'-f','null','-'],check=True)
 rows.append({'file':name,'route':'/samples/'+name,'mime':mime,'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'recipe':common+args+['{output}/'+name],'probe':probe})
version=subprocess.check_output([a.ffmpeg,'-version'],text=True).splitlines()[0]
# Remove machine-local paths and file-dependent nondeterministic presentation.
for r in rows:r['probe']['format']['filename']=r['file']
manifest={'schema':1,'origin':'Original synthetic black frames and digital silence; generated without source asset files.','parity':'Intentional M934 payload-byte deviation. M805 codec/container/channel assumptions retained.','generator':version,'samples':rows,'license':'MIT, to the extent applicable rights exist; see tools/server_lab/profiler_samples/LICENSE and docs/server1/GENERATED-PROFILER-SAMPLES.md. No tool binary or upstream sample payload is included.'}
(a.output.parent/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps([{'file':r['file'],'bytes':r['bytes'],'sha256':r['sha256']} for r in rows],indent=2))
