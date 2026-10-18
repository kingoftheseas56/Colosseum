#!/usr/bin/env python3
"""One Piece real catalogue walkthrough; mandatory full interaction probe before video."""
import argparse, hashlib, importlib.util, json, os, signal, subprocess, tempfile, time, uuid
from pathlib import Path
import capture_native_promos_v2 as base
SEAS=[('east-blue','romance','Romance Dawn','eastBlue','eastBlueCanonRow-'),('paradise','arabasta','Arabasta','paradise','paradiseIndexRow-'),('new-world','dressrosa','Dressrosa','newWorld','newWorldIndexRow-'),('final-sea','egghead','Egghead','finalSea','finalSeaIndexRow-')]
BINARY='acb86ce66202531629c137f8706d8d873c3065df8a08867cbeb8bd60eafaa677'
QML='bc48a019a12d408ce983d0f4ed8b9f0b075934c4c19090a9c2aa2c9f0fb399cf'

def ancestors(row,rows):
    by={r['handle']:r for r in rows};seen=set()
    while row and row['handle'] not in seen:
        seen.add(row['handle']);yield row;row=by.get(row.get('parentHandle'))

def has_ancestor(row,rows,predicate):return any(predicate(r) for r in ancestors(row,rows))
def loading_done(p):return all(p.get(k) is False for k in ('animeLoading','liveLoading','onePaceLoading'))
def verify_window(pid):
    ids=subprocess.run(['xdotool','search','--onlyvisible','--pid',str(pid)],check=True,capture_output=True,text=True,timeout=5).stdout.split()
    matches=[]
    for wid in ids:
        raw=subprocess.run(['xdotool','getwindowgeometry','--shell',wid],check=True,capture_output=True,text=True,timeout=5).stdout
        g=dict(line.split('=',1) for line in raw.splitlines() if '=' in line)
        if (g.get('WIDTH'),g.get('HEIGHT'))==('1280','720'):matches.append(g)
    if len(matches)!=1 or (matches[0].get('X'),matches[0].get('Y'))!=('0','0'):
        raise RuntimeError('Native X11 window must uniquely occupy 1280x720 at origin: '+str(matches))
    return matches[0]

def image_readiness(images):
    # Empty source is missing catalogue metadata, not a network loading state.
    sourced=[i for i in images if i.get('source')]
    return bool(sourced) and all(i.get('status') in (1,'Ready') for i in sourced)

def actual_card_image(row,rows):return base.is_image_class(row.get('class','')) and has_ancestor(row,rows,lambda r:r.get('class','').startswith(('OnePieceEpisodeCard','OnePieceVolumeCard')))

class DeepCapture(base.Capture):
    probing=True
    def dwell(self,seconds=4):self.hold(.2 if self.probing else seconds)
    def wait(self,fn,seconds=75):
        end=min(time.monotonic()+seconds,self.deadline-1);last=None
        while time.monotonic()<end:
            last=fn()
            if last:return last
            self.hold(.5)
        raise RuntimeError('Readiness/route deadline: '+str(last))
    def atlas(self,sea):
        wanted={'east-blue':'OnePieceEastBlueAtlas','paradise':'OnePieceParadiseAtlas','new-world':'OnePieceNewWorldAtlas','final-sea':'OnePieceFinalSeaAtlas'}[sea]
        rows=[r for r in self.tree('onePieceUniversePage') if r.get('class','').startswith(wanted) and base.bounds(self.query(r['handle']))]
        if len(rows)!=1:raise RuntimeError('Missing/ambiguous active atlas '+sea)
        return rows[0]['handle']
    def route(self,sea,title=None):
        p=self.d.props('onePieceUniversePage',['activeSeaId','arcPageOpen'])
        if p.get('activeSeaId')!=sea:raise RuntimeError('Wrong sea '+str(p))
        if title:
            if not p.get('arcPageOpen'):raise RuntimeError('Arc page is not open')
            if not any('Text' in r.get('class','') and self.d.props(r['handle'],['text']).get('text')==title for r in self.tree('onePieceArcCatalogue')):raise RuntimeError('Arc title missing: '+title)
            cat=self.d.props('onePieceArcCatalogue',['seaLabel','animeLoading','onePaceLoading','liveLoading','onePaceInstalled'])
            self.event('route',sea=sea,title=title,state=p,catalogue=cat)
        elif p.get('arcPageOpen'):raise RuntimeError('Expected map but catalogue still open')
        else:
            def map_ready():
                try:
                    h=self.atlas(sea)
                    return self.d.props(h,['captureReady']).get('captureReady') is True and self.query(h).get('enabled') is True
                except RuntimeError:return False
            self.wait(map_ready,30)
        return p
    def viewport(self,root,vertical=True):
        rows=self.tree(root);out=[]
        for r in rows:
            if 'Flickable' not in r.get('class','') and 'ListView' not in r.get('class',''):continue
            p=self.d.props(r['handle'],['contentWidth','contentHeight','width','height']);q=self.query(r['handle'])
            if not base.bounds(q):continue
            overflow=(p.get('contentHeight',0)>p.get('height',0)+1) if vertical else (p.get('contentWidth',0)>p.get('width',0)+1)
            if overflow:out.append((q['rect']['width']*q['rect']['height'],r['handle']))
        if not out:raise RuntimeError('No scrollable viewport in '+root)
        return max(out)[1]
    def vertical_step(self,root,dy=-120):
        h=self.viewport(root);q=self.query(h);before=self.d.props(h,['contentY'])
        # Full-width catalogue has a safe right gutter outside horizontal rows.
        self.d.call('ui-scroll',{'target':h,'dy':dy,'xFraction':.985 if root=='onePieceArcCatalogue' else .5,'yFraction':.5});self.hold(.35)
        after=self.d.props(h,['contentY'])
        if abs(float(after.get('contentY',0))-float(before.get('contentY',0)))<.5:raise RuntimeError('Vertical wheel did not move actual viewport in '+root)
    def reveal(self,name,root):
        for _ in range(36):
            try:
                q=self.query(name)
                if base.center_visible(q):return name
                dy=180 if q.get('rect',{}).get('y',0)<100 else -180
            except RuntimeError:dy=-180
            self.vertical_step(root,dy)
        raise RuntimeError('Control not reachable by scrolling: '+name)
    def exact_click(self,name):
        q=self.query(name)
        if not base.center_visible(q):raise RuntimeError('Control center clipped: '+name)
        self.d.call('ui-click',{'target':name});self.hold(.6)
    def catalogue_open(self,sea,arc,title,prefix,rowprefix):
        self.route(sea);self.exact_click(prefix+'IndexButton');self.dwell(3)
        self.reveal(rowprefix+arc,'onePieceUniversePage');self.exact_click(rowprefix+arc)
        preview=prefix+'ArcPreview'
        self.wait(lambda:base.bounds(self.query(preview)),10)
        a=self.d.props(self.atlas(sea),['selectedMarkerId','selectedArcId','previewVisible','indexVisible'])
        identity=a.get('selectedMarkerId') if sea=='east-blue' else a.get('selectedArcId')
        if identity!=arc or a.get('previewVisible') is not True or a.get('indexVisible') is not False:raise RuntimeError('Wrong preview/index state '+str(a))
        self.event('arc-preview',sea=sea,arc=arc,state=a);self.dwell(3)
        self.exact_click(prefix+'OpenArc')
        self.wait(lambda:self.d.props('onePieceUniversePage',['arcPageOpen']).get('arcPageOpen'),15)
        self.route(sea,title)
        self.wait(lambda:loading_done(self.d.props('onePieceArcCatalogue',['animeLoading','liveLoading','onePaceLoading'])),100)
        self.wait(lambda:self.d.props('onePieceArcHero',['status']).get('status') in (1,'Ready'),60)
        hero=self.d.props('onePieceArcHero',['status','source'])
        self.event('catalogue-entered',sea=sea,arc=arc,title=title,hero=hero);self.dwell(5)
    def lane(self,title):
        rows=self.tree('onePieceArcCatalogue')
        for r in rows:
            if not r.get('class','').startswith('WidgetHeader'):continue
            if self.d.props(r['handle'],['title']).get('title')!=title:continue
            parent=r.get('parentHandle');f=[x for x in rows if x.get('parentHandle')==parent and 'Flickable' in x.get('class','')]
            if len(f)!=1:raise RuntimeError('Ambiguous lane viewport '+title)
            if self.query(f[0]['handle']).get('visible') is False:return None,rows
            return f[0],rows
        return None,rows
    def reveal_lane(self,title):
        for _ in range(65):
            lane,rows=self.lane(title)
            if lane is None:return None
            q=self.query(lane['handle']);b=base.bounds(q);r=q.get('rect',{})
            if b and b[3]-b[1]>=min(r.get('height',0)*.75,260):return lane,rows
            self.vertical_step('onePieceArcCatalogue',150 if r.get('y',999)<95 else -120)
        raise RuntimeError('Lane not revealable '+title)
    def lane_evidence(self,title):
        lane,rows=self.lane(title)
        if not lane:return {'title':title,'exists':False}
        cards=[];images=[]
        for r in rows:
            if not has_ancestor(r,rows,lambda x:x['handle']==lane['handle']):continue
            if r.get('class','').startswith(('OnePieceEpisodeCard','OnePieceVolumeCard')):cards.append(r)
            if actual_card_image(r,rows):
                q=self.query(r['handle']);b=base.bounds(q)
                if not b:
                    card=next((x for x in ancestors(r,rows) if x.get('class','').startswith(('OnePieceEpisodeCard','OnePieceVolumeCard'))),None)
                    if card:b=base.bounds(self.query(card['handle']))
                if b:
                    p=self.d.props(r['handle'],['status','source']);images.append(p)
        return {'title':title,'exists':True,'cardCount':len(cards),'visibleImages':images,'missingSourceCount':sum(not i.get('source') for i in images),'ready':bool(cards) and image_readiness(images),'viewport':self.d.props(lane['handle'],['contentX','contentWidth','width'])}
    def drag_lane(self,title):
        lane,_=self.lane(title);before=self.d.props(lane['handle'],['contentX','contentWidth','width'])
        if before.get('contentWidth',0)<=before.get('width',0)+5:return {'applicable':False,'reason':'All cards already fit','before':before}
        q=self.query(lane['handle']);b=base.bounds(q)
        if not b:raise RuntimeError('Horizontal lane invisible')
        # Physical X11 pointer drag only; release in finally. Coordinates derive from
        # the currently visible actual viewport, not a wrapper's guessed center.
        x1=int(b[0]+(b[2]-b[0])*.80);x2=int(b[0]+(b[2]-b[0])*.25);y=int(b[1]+(b[3]-b[1])*.45)
        try:
            subprocess.run(['xdotool','mousemove','--sync',str(x1),str(y),'mousedown','1'],check=True,timeout=5)
            for step in range(1,19):
                subprocess.run(['xdotool','mousemove','--sync',str(round(x1+(x2-x1)*step/18)),str(y)],check=True,timeout=5);self.hold(.045)
        finally:subprocess.run(['xdotool','mouseup','1'],check=True,timeout=5)
        self.hold(1);lane,_=self.lane(title);after=self.d.props(lane['handle'],['contentX','contentWidth','width'])
        if after.get('contentX',0)<=before.get('contentX',0)+10:raise RuntimeError('Horizontal drag did not move '+title)
        return {'applicable':True,'before':before,'after':after}
    def show_lane(self,title,required=True):
        found=self.reveal_lane(title)
        if found is None:
            if required:raise RuntimeError('Required lane missing '+title)
            self.event('lane-not-applicable',title=title);return
        initial=self.lane_evidence(title)
        if initial['cardCount']==0:
            self.event('lane-empty',evidence=initial)
            if required:raise RuntimeError('No real cards for '+title)
            return
        self.wait(lambda:self.lane_evidence(title)['ready'],100)
        evidence=self.lane_evidence(title)
        if evidence.get('missingSourceCount'):self.art_limitations.append(evidence)
        self.event('lane-ready',evidence=evidence);self.dwell(5)
        move=self.drag_lane(title);self.event('horizontal-browse',title=title,**move)
        self.wait(lambda:self.lane_evidence(title)['ready'],60)
        evidence=self.lane_evidence(title)
        if evidence.get('missingSourceCount'):self.art_limitations.append(evidence)
        self.event('lane-after-browse',evidence=evidence);self.dwell(5)
    def map_zoom(self,prefix,sea):
        # The actual atlas owns zoom; resolve that class under the page read-only.
        def read():
            for r in self.tree('onePieceUniversePage'):
                if r.get('class','').startswith({'eastBlue':'OnePieceEastBlueAtlas','paradise':'OnePieceParadiseAtlas','newWorld':'OnePieceNewWorldAtlas','finalSea':'OnePieceFinalSeaAtlas'}[prefix]) and base.bounds(self.query(r['handle'])):return self.d.props(r['handle'],['zoom']).get('zoom')
            raise RuntimeError('Visible atlas missing')
        before=read();self.exact_click(prefix+'ZoomIn');after=read()
        if not isinstance(before,(int,float)) or not isinstance(after,(int,float)) or after<=before:raise RuntimeError('Zoom did not change')
        self.event('zoom-in',sea=sea,before=before,after=after);self.dwell(3);self.exact_click(prefix+'ZoomReset')
        if read()!=1:raise RuntimeError('Zoom reset not100%')
        self.dwell(3)
    def deep(self):
        self.art_limitations=[]
        self.tribute(0,'One Piece');self.route('east-blue')
        for idx,(sea,arc,title,prefix,rowprefix) in enumerate(SEAS):
            self.route(sea);self.event('sea-map',sea=sea);self.dwell(4);self.map_zoom(prefix,sea)
            self.catalogue_open(sea,arc,title,prefix,rowprefix)
            self.show_lane('Anime');self.show_lane('Manga');self.show_lane('Colored Manga')
            pace=self.d.props('onePieceArcCatalogue',['onePaceInstalled']).get('onePaceInstalled')
            self.event('one-pace-availability',installed=pace)
            if pace:self.show_lane('One Pace')
            self.show_lane('Live Action',required=sea=='east-blue')
            self.show_lane('Related Adaptations',required=False)
            self.route(sea,title);self.page_back('onePieceArcCatalogue');self.route(sea);self.event('back-to-map',sea=sea);self.dwell(4)
            if idx<3:
                control=['eastBlueToParadise','paradiseNewWorld','newWorldBeyondWano'][idx]
                self.exact_click(control);self.wait(lambda:self.d.props('onePieceUniversePage',['activeSeaId']).get('activeSeaId')==SEAS[idx+1][0],10)
        self.event('deep-coverage-complete',arcs=[r[1] for r in SEAS]);return True

def stop_process(p,normal_signal=signal.SIGTERM):
    if p is None:return None
    if p.poll() is None:p.send_signal(normal_signal)
    try:return p.wait(timeout=8)
    except subprocess.TimeoutExpired:
        p.terminate()
        try:return p.wait(timeout=4)
        except subprocess.TimeoutExpired:p.kill();return p.wait(timeout=4)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--record',action='store_true');args=ap.parse_args()
    def interrupt(sig,frame):raise KeyboardInterrupt('Signal '+str(sig))
    signal.signal(signal.SIGTERM,interrupt)
    source=args.source.resolve();binary=args.binary.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if any(os.environ.get(k) for k in ('COLOSSEUM_LANISTA_WRITE','COLOSSEUM_QML_PATH','COLOSSEUM_TEST_CATALOGUE_ORIGIN','COLOSSEUM_TEST_CATALOGUE_OFFLINE','COLOSSEUM_CATALOG_SELFTEST')):raise RuntimeError('Forbidden runtime override')
    if hashlib.sha256(binary.read_bytes()).hexdigest()!=BINARY:raise RuntimeError('Binary identity mismatch')
    qml=source/'qml';material=''.join(f'{p.relative_to(qml).as_posix()}\n{hashlib.sha256(p.read_bytes()).hexdigest()}\n' for p in sorted(qml.rglob('*')) if p.is_file())
    if hashlib.sha256(material.encode()).hexdigest()!=QML:raise RuntimeError('QML identity mismatch')
    subprocess.run(['xdotool','--version'],check=True,capture_output=True,timeout=5)
    spec=importlib.util.spec_from_file_location('capture_base_driver',source/'tests/b1_native_catalogue_qualification.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);Driver=base.make_driver(mod.Driver)
    probe=out/'probe';capture=out/'capture'
    for p in (probe,capture):(p/'ipc').mkdir(parents=True,exist_ok=True)
    summary={'status':'BLOCKED','probePassed':False,'humanReviewRequired':True,'arcs':[r[1] for r in SEAS]};app=rec=d=cap=None
    with tempfile.TemporaryDirectory(prefix='colosseum-deep-op-') as tmp:
        profile=Path(tmp);tag='deep-op-'+uuid.uuid4().hex[:10];env=os.environ.copy();env.update(QT_QPA_PLATFORM='xcb',XDG_DATA_HOME=str(profile/'data'),XDG_CACHE_HOME=str(profile/'cache'),XDG_CONFIG_HOME=str(profile/'config'),COLOSSEUM_APPDATA_TAG=tag,COLOSSEUM_LANISTA_PIPE=tag,COLOSSEUM_LANISTA_DRIVE='1',COLOSSEUM_AUTOMATION_FOCUS_SEAMS='0')
        for name in ('data','cache','config'):(profile/name).mkdir()
        so=(out/'app.stdout').open('w');se=(out/'app.stderr').open('w');rl=None
        try:
            app=subprocess.Popen([str(binary)],cwd=source,env=env,stdout=so,stderr=se)
            d=Driver(probe,app,tag,profile,out/'app.stderr');d.ready(90)
            window=d.call('ui-query',{'object':'modePill_Universes'}).get('rootWindow',{})
            if (window.get('width'),window.get('height'))!=(1280,720):raise RuntimeError('Require fullscreen1280x720 before physical input')
            summary['windowGeometry']=verify_window(app.pid)
            cap=DeepCapture(d,probe);cap.probing=True;cap.deadline=time.monotonic()+900
            cap.deep();summary['probeArtLimitations']=cap.art_limitations;summary['probePassed']=True;summary['status']='PROBE_PASSED';base.save(probe/'result.json',summary);d.grab('last-frame','onePieceUniversePage')
            if args.record:
                cap.world();d.output=capture;cap=DeepCapture(d,capture);cap.probing=False
                rl=(out/'ffmpeg.log').open('w');movie=capture/'onepiece-deep-raw.mp4'
                rec=subprocess.Popen(['ffmpeg','-hide_banner','-nostdin','-y','-f','x11grab','-draw_mouse','1','-video_size','1280x720','-framerate','30','-i',os.environ['DISPLAY'],'-t','780','-an','-c:v','libx264','-preset','veryfast','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(movie)],stdout=rl,stderr=subprocess.STDOUT)
                cap.start=time.monotonic();cap.deadline=cap.start+775;cap.deep();cap.dwell(4);summary['recordingElapsed']=time.monotonic()-cap.start;summary['artLimitations']=cap.art_limitations;summary['observedViewportArtworkComplete']=not bool(cap.art_limitations);summary['status']='NAVIGATION_COMPLETE_WITH_MISSING_ART' if cap.art_limitations else 'CAPTURED_REQUIRES_VIDEO_REVIEW';d.grab('last-frame','onePieceUniversePage')
        except BaseException as e:
            summary['status']='BLOCKED';summary['error']=str(e);summary['failedPhase']='record' if rec else 'probe'
            if d and cap:
                try:d.grab('failure','onePieceUniversePage')
                except Exception:pass
        finally:
            if rec:
                summary['recorderExit']=stop_process(rec,signal.SIGINT)
                try:
                    p=subprocess.run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(movie)],capture_output=True,text=True,timeout=8);data=json.loads(p.stdout);base.save(capture/'ffprobe.json',data)
                    streams=[s for s in data.get('streams',[]) if s.get('codec_type')=='video'];duration=float(data.get('format',{}).get('duration',0));elapsed=summary.get('recordingElapsed',time.monotonic()-cap.start)
                    if not streams or not all(s.get('codec_name')=='h264' and s.get('width')==1280 and s.get('height')==720 and s.get('r_frame_rate')=='30/1' for s in streams) or duration<elapsed-3 or duration>781 or summary['recorderExit'] not in (0,255):raise RuntimeError('Video validation failed')
                    summary['video']='capture/onepiece-deep-raw.mp4'
                except Exception as e:summary['status']='BLOCKED';summary['videoError']=str(e)
            if app:
                if app.poll() is None and d:
                    try:d.call('ui-keypress',{'key':'Ctrl+Q'},timeout=3)
                    except Exception:pass
                try:summary['nativeExit']=app.wait(timeout=8)
                except subprocess.TimeoutExpired:summary['nativeExit']=stop_process(app);summary['forcedTermination']=True
                if summary['nativeExit']!=0 or summary.get('forcedTermination'):summary['status']='BLOCKED'
            if cap:base.save(cap.out/'result.json',summary)
            base.save(out/'summary.json',summary);so.close();se.close()
            if rl:rl.close()
    return 0 if summary['status'] in ('PROBE_PASSED','CAPTURED_REQUIRES_VIDEO_REVIEW','NAVIGATION_COMPLETE_WITH_MISSING_ART') else 2
if __name__=='__main__':raise SystemExit(main())
