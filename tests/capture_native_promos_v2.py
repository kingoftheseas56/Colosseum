#!/usr/bin/env python3
"""Actual native UI capture only. No product writes, fixtures or content substitution."""
import argparse, hashlib, importlib.util, json, os, signal, subprocess, sys, tempfile, time, uuid
from pathlib import Path
ALLOWED = {'get-state','qml-get','ui-query','ui-snapshot','dump-ui','ui-click','ui-keypress','ui-scroll'}
NAMES = ['Attack on Titan','Avatar: The Last Airbender','DC Animated Universe','Dragon Ball','Dune','Harry Potter','James Bond','Jurassic Park','Lord of the Rings','Marvel Cinematic Universe','Naruto','One Piece','Percy Jackson','Sherlock Holmes','A Song of Ice and Fire','Star Trek','Star Wars','Studio Ghibli','Weekly Shonen Jump','The Witcher']

def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, default=str)+'\n')

def bounds(q):
    if not q.get('visible') or q.get('opacity',1)<=0:return None
    r=q.get('rect',{}); w=q.get('rootWindow',{})
    if r.get('width',0)<=0 or r.get('height',0)<=0:return None
    box=[r['x'],r['y'],r['x']+r['width'],r['y']+r['height']]
    for c in [dict(x=0,y=0,width=w.get('width',1280),height=w.get('height',720))]+[x.get('sceneRect',{}) for x in q.get('clipChain',[])]:
        if not all(k in c for k in ('x','y','width','height')):continue
        box=[max(box[0],c['x']),max(box[1],c['y']),min(box[2],c['x']+c['width']),min(box[3],c['y']+c['height'])]
    return box if box[2]>box[0] and box[3]>box[1] else None

def center_visible(q):
    b=bounds(q);r=q.get('rect',{})
    return bool(b and q.get('enabled',True) and b[0]<(r['x']+r['width']/2)<b[2] and b[1]<(r['y']+r['height']/2)<b[3])

def is_image_class(cls):
    return cls == 'QQuickImage' or cls.startswith('QQuickImage_')

def substantive(row, by_handle):
    seen=set()
    while row and row.get('handle') not in seen:
        seen.add(row.get('handle'));name=row.get('objectName','');cls=row.get('class','')
        if name.startswith(('dcauVideo_','dcauComic_','shelvesCover_','dcauPortal_','eastBlueBadge-')) or 'UniverseGalleryCard' in cls:return True
        if name in ('universeBannerArt','eastBlueArcPreviewPoster','paradisePreviewHero','newWorldPreviewHero','finalSeaPreviewHero','finalSeaMapArtwork'):return True
        row=by_handle.get(row.get('parentHandle'))
    return False

def art_ok(rows):
    art=[r for r in rows if r.get('substantive') and r.get('area',0)>=400]
    return bool(art) and all(r.get('source') and r.get('status') in (1,'Ready') for r in art)

def make_driver(base):
    class CaptureDriver(base):
        def call(self,cmd,payload=None,timeout=10):
            if cmd not in ALLOWED:raise RuntimeError('Capture forbids command: '+cmd)
            return super().call(cmd,payload,timeout)
    return CaptureDriver

class Capture:
    def __init__(self,d,out): self.d=d;self.out=out;self.events=[];self.start=time.monotonic();self.record=None;self.deadline=float('inf')
    def event(self,scene,**kw):self.events.append(dict(scene=scene,seconds=round(time.monotonic()-self.start,3),**kw));save(self.out/'scenes.json',self.events)
    def hold(self,n=2):
        if time.monotonic()+n>self.deadline:raise TimeoutError('Recording duration cap reached')
        time.sleep(n)
    def click(self,name):self.d.click(name);self.hold(.8)
    def query(self,target):return self.d.call('ui-query',{'object':target})
    def tree(self,root):return self.d.tree(root)
    def scroll(self,target,dy=-120):
        q=self.query(target);b=bounds(q);r=q.get('rect',{})
        if not b:raise RuntimeError('Invisible scroll target '+target)
        self.d.call('ui-scroll',{'target':target,'dy':dy,'xFraction':((b[0]+b[2])/2-r['x'])/r['width'],'yFraction':((b[1]+b[3])/2-r['y'])/r['height']});self.hold(.35)
    def observe(self,root):
        rows=self.tree(root);by_handle={r['handle']:r for r in rows};images=[];texts=[];content=[];structures=[];charts=[]
        for row in rows:
            h=row['handle'];cls=row.get('class','')
            if row.get('objectName','').startswith(('starWarsGalaxyGate_','dcauPortal_','eastBlueBadge-')) and bounds(self.query(h)):structures.append(row.get('objectName'))
            if 'UniverseGalleryCard' in cls and bounds(self.query(h)):content.append(cls)
            if row.get('objectName','').startswith(('dcauVideo_','dcauComic_','shelvesCover_')) and bounds(self.query(h)):content.append(row.get('objectName'))
            if 'Loader' in cls and bounds(self.query(h)):
                p=self.d.props(h,['status','source'])
                if any(chart in str(p.get('source','')) for chart in ('EastBlueChart.qml','ParadiseChart.qml','NewWorldChart.qml')):charts.append(p)
            if not is_image_class(cls) and 'Text' not in cls:continue
            q=self.query(h);b=bounds(q)
            if row.get('objectName')=='universeBannerArt':
                parent=by_handle.get(row.get('parentHandle'))
                if parent and 'UniverseBanner' in parent.get('class',''):
                    pb=bounds(self.query(parent['handle']))
                    effects=[r for r in rows if r.get('parentHandle')==parent['handle'] and 'MultiEffect' in r.get('class','')]
                    # The image is intentionally hidden; its actual visible compositor is evidence.
                    if pb and any(bounds(self.query(e['handle'])) for e in effects):b=pb
                    elif pb:
                        images.append(dict(name='universeBannerArt',substantive=True,source='',status=0,area=(pb[2]-pb[0])*(pb[3]-pb[1])))
            elif not b and is_image_class(cls) and substantive(row,by_handle):
                # Unready images may intentionally have opacity zero; retain their failure.
                parent=by_handle.get(row.get('parentHandle'))
                if parent:b=bounds(self.query(parent['handle']))
            if not b:continue
            if is_image_class(cls):
                p=self.d.props(h,['status','source']);images.append(dict(name=row.get('objectName'),substantive=substantive(row,by_handle),status=p.get('status'),source=str(p.get('source','')),area=(b[2]-b[0])*(b[3]-b[1])))
            elif 'Text' in cls:
                p=self.d.props(h,['text']);t=p.get('text')
                if isinstance(t,str) and t:texts.append(t)
        return dict(images=images,texts=texts,visibleContent=content,structures=structures,charts=charts,artReady=art_ok(images))
    def ready(self,root,label,seconds=60):
        end=min(time.monotonic()+seconds,self.deadline-1);last={};attempt=0
        while time.monotonic()<end:
            attempt+=1;last=self.observe(root)
            bad=any(any(word in t.upper() for word in ('ART UNAVAILABLE','POSTER UNAVAILABLE','CHART UNAVAILABLE','PREPARING PARADISE')) for t in last['texts'])
            structural=(root=='galaxyUniversePage' and self.d.props(root,['selectedDestinationId']).get('selectedDestinationId')=='' and len(last['structures'])>=4)
            if label=='east-blue-preview':last['artReady']=art_ok([i for i in last['images'] if i.get('name')=='eastBlueArcPreviewPoster'])
            chartReady=root=='onePieceUniversePage' and bool(last['charts']) and all(p.get('status') in (1,'Ready') for p in last['charts']) and 'preview' not in label
            if (last['artReady'] or structural or chartReady) and not bad:
                self.event(label,readiness='visible-art-ready',observation=last);self.hold(2);return True
            self.hold(2)
        self.event(label,readiness='BLOCKED',observation=last);return False
    def state(self,**expected):
        p=self.d.props('universeLayer',['active','universeName','templatePage','status'])
        if not p.get('active') or p.get('status') not in (1,'Ready') or any(p.get(k)!=v for k,v in expected.items()):raise RuntimeError('Unexpected page '+str(p))
    def page_back(self,root):
        candidates=[r for r in self.tree(root) if r.get('class','').startswith('BackAction') and center_visible(self.query(r['handle']))]
        if len(candidates)!=1:raise RuntimeError('Missing or ambiguous page BackAction in '+root)
        self.d.call('ui-click',{'target':candidates[0]['handle']});self.hold(.8)
    def world(self):
        # World chrome can be geometrically visible behind an overlay. Close the actual
        # page first; do not click through it or assume Escape means page-local Back.
        for _ in range(6):
            p=self.d.props('universeLayer',['active','universeName'])
            if not p.get('active'):
                self.d.ensure_world('Universes');self.hold(1);return
            self.page_back('universeLayer')
        raise RuntimeError('Cannot leave active Universe page through its Back control')
    def tribute(self,index,name):
        self.world()
        for _ in range(35):
            try:
                q=self.query('universesTributeCarouselView')
                if center_visible(q):break
            except RuntimeError:pass
            self.scroll('universesWorldScroll',240)
        # Click the carousel's actual dot delegate; no focus injection or index setter.
        for _ in range(4):
            current=self.d.props('universesTributeCarouselView',['currentIndex']).get('currentIndex')
            if current==index:break
            chosen=None
            for row in self.tree('universesTributeCarousel'):
                if 'Rectangle' not in row.get('class',''):continue
                q=self.query(row['handle']);r=q.get('rect',{})
                if not center_visible(q) or not (0<r.get('width',0)<=24 and r.get('height')==7):continue
                if self.d.props(row['handle'],['index']).get('index')==index:chosen=row['handle'];break
            if not chosen:raise RuntimeError('Visible carousel dot not found')
            self.d.call('ui-click',{'target':chosen});self.hold(.5)
        if self.d.props('universesTributeCarouselView',['currentIndex']).get('currentIndex')!=index:raise RuntimeError('Carousel did not reach requested slide')
        self.click('featuredPrimaryAction_'+str(index));self.state(universeName=name)
    def first_named(self,root,prefix):
        for row in self.tree(root):
            if row.get('objectName','').startswith(prefix) and center_visible(self.query(row['handle'])):return row['objectName']
        raise RuntimeError('No visible '+prefix)
    def flick(self,root):
        candidates=[]
        for row in self.tree(root):
            if 'Flickable' not in row.get('class',''):continue
            q=self.query(row['handle']);b=bounds(q)
            if not b:continue
            p=self.d.props(row['handle'],['contentHeight','height'])
            if (p.get('contentHeight') or 0)>(p.get('height') or 0):candidates.append(((b[2]-b[0])*(b[3]-b[1]),row['handle']))
        if not candidates:return None
        return max(candidates)[1]
    def overview_scroll(self,root,steps=8):
        for _ in range(steps):
            f=self.flick(root)
            if not f:break
            self.scroll(f,-100)
        self.hold(2)
    def onepiece(self):
        self.tribute(0,'One Piece');good=self.ready('onePieceUniversePage','east-blue',90)
        if not good:return False
        self.click('eastBlueZoomIn');self.hold(2);self.click('eastBlueZoomReset')
        self.click('eastBlueIndexButton')
        row=self.first_named('onePieceUniversePage','eastBlueCanonRow-');self.click(row)
        if not bounds(self.query('eastBlueArcPreview')):raise RuntimeError('Index selection did not open East Blue preview')
        good=self.ready('onePieceUniversePage','east-blue-preview',20) and good
        self.click('eastBlueClosePreview')
        for control,label in [('eastBlueToParadise','paradise'),('paradiseNewWorld','new-world'),('newWorldBeyondWano','final-sea')]:
            self.click(control);good=self.ready('onePieceUniversePage',label,25) and good;self.hold(3)
        self.click('finalSeaIndexButton');self.event('final-sea-index');self.hold(4)
        return good
    def starwars(self):
        self.tribute(3,'Star Wars');good=self.ready('galaxyUniversePage','galaxy',90)
        if not good:return False
        for control,label in [('starWarsGalaxySkywalker','skywalker'),('starWarsGalaxyGate_fall','fall'),('starWarsGalaxyGate_rebellion','rebellion'),('starWarsGalaxyGate_newrep','newrep')]:
            self.click(control)
            actual=self.d.props('galaxyUniversePage',['selectedDestinationId']).get('selectedDestinationId')
            if actual!=label:raise RuntimeError('Wrong galaxy destination '+str(actual))
            self.overview_scroll('galaxyUniversePage',5)
            good=self.ready('galaxyUniversePage',label,20) and good
            content=self.observe('galaxyUniversePage');self.event(label+'-content',observation=content)
            good=bool(content['visibleContent']) and good
            self.page_back('galaxyUniversePage')
            if self.d.props('galaxyUniversePage',['selectedDestinationId']).get('selectedDestinationId')!='':raise RuntimeError('Galaxy Back did not return overview')
        return good
    def dcau(self):
        self.tribute(2,'DC Animated Universe');good=self.ready('dcauUniversePage','dcau-landing',90)
        if not good:return False
        for label,hub in [('gotham','gotham'),('metropolis','metropolis'),('watch-tower','justice'),('future-gotham','future')]:
            # Horizontal portal strip: normal arrows can reveal offscreen portals.
            target='dcauPortal_'+label
            for _ in range(5):
                try:self.click(target);break
                except RuntimeError:self.d.call('ui-keypress',{'key':'Right'});self.hold(.5)
            if self.d.props('dcauUniversePage',['currentHubId']).get('currentHubId')!=hub:raise RuntimeError('DCAU hub not activated '+hub)
            good=self.ready('dcauWorldPage',hub,20) and good;self.overview_scroll('dcauWorldPage',5)
            content=self.observe('dcauWorldPage');self.event(hub+'-content',observation=content)
            good=bool(content['visibleContent']) and good
            self.page_back('dcauUniversePage')
            if self.d.props('dcauUniversePage',['currentHubId']).get('currentHubId')!='':raise RuntimeError('DCAU Back did not return landing')
        return good
    def ledger_names(self):
        o=self.observe('universesLedger');return set(o['texts']) & set(NAMES),o
    def open_standard(self,name):
        self.world()
        # Return top normally, then locate text within each actual banner subtree.
        for _ in range(45):
            p=self.d.props('universesWorldScroll',['contentY'])
            if (p.get('contentY') or 0)<=1:break
            self.scroll('universesWorldScroll',400)
        for _ in range(65):
            banners=[r for r in self.tree('universesLedger') if 'UniverseBanner' in r.get('class','')]
            target=None
            # entry is sometimes null. Visible text descendants provide fallback identity.
            for r in banners:
                if not center_visible(self.query(r['handle'])):continue
                entry=self.d.props(r['handle'],['entry']).get('entry')
                if isinstance(entry,dict) and entry.get('name')==name:target=r['handle'];break
            if target:
                self.d.call('ui-click',{'target':target});self.hold(1);self.state(universeName=name,templatePage='shelves');return
            # Find title text and click its center: clicks naturally propagate to banner.
            for r in self.tree('universesLedger'):
                if 'Text' in r.get('class','') and center_visible(self.query(r['handle'])) and self.d.props(r['handle'],['text']).get('text')==name:
                    self.d.call('ui-click',{'target':r['handle']});self.hold(1);self.state(universeName=name,templatePage='shelves');return
            self.scroll('universesWorldScroll',-180)
        raise RuntimeError('Standard banner not reached '+name)
    def universes(self):
        self.world()
        for _ in range(45):
            if (self.d.props('universesWorldScroll',['contentY']).get('contentY') or 0)<=1:break
            self.scroll('universesWorldScroll',400)
        seen=set();good=True
        start=self.d.props('universesWorldScroll',['contentY','contentHeight','height','atYEnd'])
        count=self.d.props('universesCount',['text']).get('text','')
        self.event('ledger-start',geometry=start,declaredCount=count)
        good=bool(str(count).startswith('20 ')) and (start.get('contentY') or 0)<=1
        for step in range(75):
            names,o=self.ledger_names();seen|=names
            self.event('ledger',visibleNames=sorted(names),covered=len(seen),artReady=o['artReady'])
            good=good and o['artReady'];self.hold(.6)
            geometry=self.d.props('universesWorldScroll',['contentY','contentHeight','height','atYEnd'])
            if seen==set(NAMES) and geometry.get('atYEnd') is True:break
            self.scroll('universesWorldScroll',-150)
        end=self.d.props('universesWorldScroll',['contentY','contentHeight','height','atYEnd'])
        self.event('ledger-coverage',seen=sorted(seen),missing=sorted(set(NAMES)-seen),endGeometry=end)
        good=good and seen==set(NAMES) and end.get('atYEnd') is True
        for name in ['Marvel Cinematic Universe','One Piece']:
            self.open_standard(name)
            self.scroll('shelvesScroll',-240)
            good=self.ready('universeLayer',name,25) and good
            content=self.observe('universeLayer');self.event(name+'-content',observation=content)
            good=bool(content['visibleContent']) and good
            for _ in range(70):
                geometry=self.d.props('shelvesScroll',['contentY','contentHeight','height','atYEnd'])
                if geometry.get('atYEnd') is True:break
                self.scroll('shelvesScroll',-120)
            self.event(name+'-scroll-end',geometry=geometry)
            good=good and geometry.get('atYEnd') is True
            self.hold(2)
            self.page_back('universeLayer')
        return good

def main():
    def interrupted(signum,frame):raise KeyboardInterrupt('capture interrupted by signal '+str(signum))
    signal.signal(signal.SIGTERM,interrupted)
    ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,required=True);ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--clips',nargs='+',choices=['onepiece','starwars','dcau','universes'],default=['onepiece','starwars','dcau']);args=ap.parse_args()
    source=args.source.resolve();binary=args.binary.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    forbidden=['COLOSSEUM_LANISTA_WRITE','COLOSSEUM_QML_PATH','COLOSSEUM_TEST_CATALOGUE_ORIGIN','COLOSSEUM_TEST_CATALOGUE_OFFLINE','COLOSSEUM_CATALOG_SELFTEST']
    if any(os.environ.get(k) for k in forbidden):raise RuntimeError('Forbidden runtime override inherited')
    if not os.environ.get('DISPLAY'):raise RuntimeError('Caller must supply Xvfb DISPLAY')
    if hashlib.sha256(binary.read_bytes()).hexdigest()!='acb86ce66202531629c137f8706d8d873c3065df8a08867cbeb8bd60eafaa677':raise RuntimeError('Unexpected native binary')
    qml=source/'qml';material=''.join(f'{p.relative_to(qml).as_posix()}\n{hashlib.sha256(p.read_bytes()).hexdigest()}\n' for p in sorted(qml.rglob('*')) if p.is_file())
    if hashlib.sha256(material.encode()).hexdigest()!='bc48a019a12d408ce983d0f4ed8b9f0b075934c4c19090a9c2aa2c9f0fb399cf':raise RuntimeError('Unexpected runtime QML')
    spec=importlib.util.spec_from_file_location('native_capture_base',source/'tests/b1_native_catalogue_qualification.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);Driver=make_driver(module.Driver)
    summary={'status':'REQUIRES_HUMAN_VIDEO_REVIEW','clips':[],'dimensions':[1280,720],'fps':30,'privateProfilesUploaded':False}
    for label in dict.fromkeys(args.clips):
        limit=240 if label=='universes' else 150
        clip=out/label;clip.mkdir(exist_ok=True);(clip/'ipc').mkdir(exist_ok=True);result={'clip':label,'status':'BLOCKED','humanReviewRequired':True};app=None;rec=None;d=None;cap=None
        # Private runtime files live outside artifact output, and are deleted afterwards.
        with tempfile.TemporaryDirectory(prefix='colosseum-promo-') as tmp:
            profile=Path(tmp);tag='promo-'+uuid.uuid4().hex[:12];env=os.environ.copy();env.update(XDG_DATA_HOME=str(profile/'data'),XDG_CACHE_HOME=str(profile/'cache'),XDG_CONFIG_HOME=str(profile/'config'),COLOSSEUM_APPDATA_TAG=tag,COLOSSEUM_LANISTA_PIPE=tag,COLOSSEUM_LANISTA_DRIVE='1',COLOSSEUM_AUTOMATION_FOCUS_SEAMS='0',QT_QPA_PLATFORM='xcb')
            for folder in ('data','cache','config'):(profile/folder).mkdir()
            try:
                with (clip/'app.stdout').open('w') as so,(clip/'app.stderr').open('w') as se:
                    app=subprocess.Popen([str(binary)],cwd=source,env=env,stdout=so,stderr=se)
                    d=Driver(clip,app,tag,profile,clip/'app.stderr');d.ready(90)
                    q=d.call('ui-query',{'object':'modePill_Universes'});window=q.get('rootWindow',{})
                    if (window.get('width'),window.get('height'))!=(1280,720):
                        d.call('ui-keypress',{'key':'F11'});time.sleep(2);window=d.call('ui-query',{'object':'modePill_Universes'}).get('rootWindow',{})
                    if (window.get('width'),window.get('height'))!=(1280,720):raise RuntimeError('Native window does not cover 1280x720 display: '+str(window))
                    result['window']=window
                    cap=Capture(d,clip)
                    # Initial readiness is outside recording. The journey still checks every new scene.
                    if label!='universes':
                        idx,name,root={'onepiece':(0,'One Piece','onePieceUniversePage'),'starwars':(3,'Star Wars','galaxyUniversePage'),'dcau':(2,'DC Animated Universe','dcauUniversePage')}[label]
                        cap.tribute(idx,name);result['preflightReady']=cap.ready(root,'preflight',90)
                        d.grab('preflight',root)
                        cap.world()
                    else:cap.world();result['preflightReady']=True
                    log=(clip/'ffmpeg.log').open('w')
                    rec=subprocess.Popen(['ffmpeg','-hide_banner','-nostdin','-y','-f','x11grab','-draw_mouse','1','-video_size','1280x720','-framerate','30','-i',os.environ['DISPLAY'],'-t',str(limit),'-an','-c:v','libx264','-preset','veryfast','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(clip/(label+'-raw.mp4'))],stdout=log,stderr=subprocess.STDOUT)
                    cap.start=time.monotonic();cap.events=[];cap.deadline=cap.start+limit-3;cap.hold(1)
                    ready=getattr(cap,label)();result['status']='CAPTURED_REQUIRES_REVIEW' if ready and result['preflightReady'] else 'DIAGNOSTIC_MISSING_ART';cap.hold(2)
                    d.grab('last-frame','universeLayer' if label!='universes' else 'universesWorld')
            except KeyboardInterrupt:
                result['error']='Capture interrupted';result['status']='BLOCKED_INTERRUPTED';raise
            except Exception as e:result['error']=str(e);result['status']='BLOCKED_EXCEPTION'
            finally:
                if rec is not None:
                    result['recordingElapsed']=time.monotonic()-cap.start
                    if rec.poll() is None:rec.send_signal(signal.SIGINT)
                    try:result['recorderExit']=rec.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        rec.terminate()
                        try:result['recorderExit']=rec.wait(timeout=5)
                        except subprocess.TimeoutExpired:rec.kill();result['recorderExit']=rec.wait(timeout=5)
                        result['status']='BLOCKED_RECORDER_EXIT'
                    log.close()
                    movie=clip/(label+'-raw.mp4')
                    try:
                        if movie.exists():
                            probe=subprocess.run(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(movie)],capture_output=True,text=True,timeout=8)
                            if probe.returncode==0:
                                data=json.loads(probe.stdout);save(clip/'ffprobe.json',data)
                                streams=[x for x in data.get('streams',[]) if x.get('codec_type')=='video']
                                valid=bool(streams) and all(x.get('codec_name')=='h264' and x.get('width')==1280 and x.get('height')==720 and x.get('r_frame_rate') in ('30/1','30') for x in streams) and 1<float(data.get('format',{}).get('duration',0))<=limit+1 and float(data.get('format',{}).get('duration',0))>=min(result['recordingElapsed'],limit)-3
                                if valid:result['video']=str(movie.relative_to(out))
                                else:result['status']='BLOCKED_VIDEO_VALIDATION'
                            else:result['videoError']='ffprobe failed';result['status']='BLOCKED_VIDEO_VALIDATION'
                        else:result['status']='BLOCKED_VIDEO_MISSING'
                    except Exception as error:
                        result['status']='BLOCKED_VIDEO_VALIDATION';result['videoError']=str(error)
                    if result.get('recorderExit') not in (0,255):result['status']='BLOCKED_RECORDER_EXIT'
                if app is not None:
                    if app.poll() is None and d is not None:
                        try:d.call('ui-keypress',{'key':'Ctrl+Q'},timeout=3)
                        except Exception as e:result['quitError']=str(e)
                    try:result['nativeExit']=app.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        app.terminate();result['forcedTermination']=True
                        try:result['nativeExit']=app.wait(timeout=5)
                        except subprocess.TimeoutExpired:app.kill();result['nativeExit']=app.wait(timeout=5)
                    if result.get('nativeExit')!=0 or result.get('forcedTermination'):result['status']='BLOCKED_NATIVE_EXIT'
                if cap is not None:save(clip/'scenes.json',cap.events)
                save(clip/'result.json',result);summary['clips'].append(result);save(out/'summary.json',summary)
    # Capture completion is not promo approval. Review all movies visually before delivery.
    return 0 if all(x['status']=='CAPTURED_REQUIRES_REVIEW' for x in summary['clips']) else 2

if __name__=='__main__':sys.exit(main())
