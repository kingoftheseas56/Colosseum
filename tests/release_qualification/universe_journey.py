"""QA-only real-UI A/B assertions. Controller owns process/profile/fixture lifecycle."""
import json,time,math,re
from pathlib import Path
NAME='Colosseum QA Publication 1.1.9'
TITLE='Star Wars: Episode IV - A New Hope'
def rect_bounds(r):
 try:
  x,y,w,h=(float(r[k]) for k in ('x','y','width','height'))
 except (KeyError,TypeError,ValueError):return None
 if not all(math.isfinite(v) for v in (x,y,w,h)) or w<=0 or h<=0:return None
 return (x,y,x+w,y+h)
def intersection(*rects):
 if not rects or any(r is None for r in rects):return None
 r=(max(v[0] for v in rects),max(v[1] for v in rects),min(v[2] for v in rects),min(v[3] for v in rects))
 return r if r[2]>r[0] and r[3]>r[1] else None
def visible_bounds(q):
 if not q.get('visible') or q.get('opacity',1)<=0:return None
 window=q.get('rootWindow',{})
 rects=[rect_bounds(q.get('rect',{})),rect_bounds({'x':0,'y':0,'width':window.get('width',0),'height':window.get('height',0)})]
 rects += [rect_bounds(c.get('sceneRect',{})) for c in q.get('clipChain',[])]
 return intersection(*rects)
def center_in_view(target,viewport):
 # Lanista ui-click always clicks the full item's center, never the clipped center.
 r=rect_bounds(target.get('rect',{}));visible=intersection(visible_bounds(target),visible_bounds(viewport))
 if r is None or visible is None or target.get('enabled') is False:return False
 cx,cy=(r[0]+r[2])/2,(r[1]+r[3])/2
 return visible[0]<cx<visible[2] and visible[1]<cy<visible[3]
def scroll_toward(driver,viewport_name,target=None):
 viewport=driver.call('ui-query',{'object':viewport_name});visible=visible_bounds(viewport);r=rect_bounds(viewport.get('rect',{}))
 if visible is None or r is None:raise RuntimeError('Scroll viewport not visible: '+viewport_name)
 x,y=(visible[0]+visible[2])/2,(visible[1]+visible[3])/2
 target_rect=rect_bounds((target or {}).get('rect',{}))
 # Existing Driver: negative wheel dy moves down content, positive returns up.
 dy=400 if target_rect and (target_rect[1]+target_rect[3])/2<y else -400
 return driver.call('ui-scroll',{'target':viewport_name,'dy':dy,'xFraction':(x-r[0])/(r[2]-r[0]),'yFraction':(y-r[1])/(r[3]-r[1])})
def save(output,name,value):
 p=Path(output);p.mkdir(parents=True,exist_ok=True);(p/name).write_text(json.dumps(value,indent=2))
def until(fn,seconds=90):
 end=time.monotonic()+seconds;last=None
 while time.monotonic()<end:
  try:
   last=fn()
   if last:return last
  except (RuntimeError,KeyError) as e:last=str(e)
  time.sleep(.3)
 raise TimeoutError(str(last))
class UniverseObservationError(RuntimeError):
 def __init__(self,message,observation):
  self.observation=observation;super().__init__(message+'; observation='+json.dumps(observation,default=str))
def observe_ledger(driver):
 # QML var arrays/objects arrive as null through this binary's qml-get bridge.
 # Enumerate every Repeater delegate (including offscreen ones) from a complete tree,
 # then read scalar title Text properties. Do not equate null with an empty model.
 state=driver.props('universesWorld',['universeContentGeneration','facet'])
 count=driver.props('universesCount',['text']);match=re.fullmatch(r'(\d+) universes?',str(count.get('text','')))
 rows=driver.tree('universesLedger');observation={'state':state,'count':count,'treeRows':len(rows) if isinstance(rows,list) else None,'entries':[]}
 if not isinstance(rows,list):raise UniverseObservationError('Non-array complete UI tree',observation)
 banners=[r for r in rows if 'UniverseBanner' in r.get('class','')]
 observation['bannerCount']=len(banners)
 for banner in banners:
  copies=[r for r in rows if r.get('parentHandle')==banner['handle'] and r.get('objectName')=='universeBannerCopy']
  if len(copies)!=1:raise UniverseObservationError('Banner copy structure missing/ambiguous',observation)
  # UniverseBanner.qml: first direct Text child of Copy is the name, then blurb/holds.
  texts=[r for r in rows if r.get('parentHandle')==copies[0]['handle'] and 'Text' in r.get('class','')]
  if not texts:raise UniverseObservationError('Banner scalar title missing',observation)
  props=driver.props(texts[0]['handle'],['text']);name=props.get('text')
  if not isinstance(name,str) or not name:raise UniverseObservationError('Banner title is not a nonempty scalar string',dict(observation,badTitle=props))
  observation['entries'].append({'name':name,'handle':banner['handle'],'titleHandle':texts[0]['handle']})
 names=[e['name'] for e in observation['entries']]
 if not match or int(match[1])!=len(banners) or len(names)!=len(set(names)) or not names or state.get('facet')!='' or not isinstance(state.get('universeContentGeneration'),int) or state['universeContentGeneration']<=0:
  raise UniverseObservationError('Ledger completeness/count/generation check failed',observation)
 observation['declaredCount']=int(match[1]);return observation
def ledger_when(driver,output,label,predicate):
 def sample():
  try:state=observe_ledger(driver)
  except UniverseObservationError as error:
   save(output,label+'-last-observation.json',{'error':str(error),'observation':error.observation});raise
  save(output,label+'-last-observation.json',state)
  return state if predicate(state) else None
 return until(sample)
def run_before(driver,set_publication,output):
 driver.ensure_world('Universes')
 state=ledger_when(driver,output,'universe-A',lambda x:True)
 assert all(x['name']!=NAME for x in state['entries']), 'Test universe unexpectedly in A'
 save(output,'universe-A-roster.json',state);driver.grab('universe-A-absent','universesWorld')
 return {'status':'PASS','phase':'A','nameAbsent':NAME,'generation':state['state']['universeContentGeneration'],'rosterCount':state['declaredCount'],'identityProof':'All actual ledger delegates enumerated; count equals rendered universe count'}
def run_after(driver,set_publication,output):
 driver.ensure_world('Universes')
 state=ledger_when(driver,output,'universe-B',lambda x:any(r['name']==NAME for r in x['entries']))
 assert sum(x['name']==NAME for x in state['entries'])==1
 save(output,'universe-B-roster.json',state)
 # Only ordinary wheel/click input. Read entry from fresh actual banner handles.
 activated=False;receipts=[]
 for attempt in range(24):
  target_query=None
  current=observe_ledger(driver)
  save(output,'universe-B-click-observation.json',current)
  for row in current['entries']:
   if row['name']!=NAME:continue
   q=driver.call('ui-query',{'object':row['handle']});target_query=q
   viewport=driver.call('ui-query',{'object':'universesWorldScroll'})
   if center_in_view(q,viewport):
    receipts.append({'name':row['name'],'query':q,'viewport':viewport,'input':driver.call('ui-click',{'target':row['handle']})});activated=True;break
  if activated:break
  scroll_toward(driver,'universesWorldScroll',target_query);time.sleep(.25)
 save(output,'universe-B-input.json',receipts);assert activated,'Specific published Universe banner not reached through ordinary UI'
 page=until(lambda:(lambda p:p if p.get('active') and p.get('universeName')==NAME and p.get('templatePage')=='shelves' and p.get('status') in (1,'Ready') else None)(driver.props('universeLayer',['active','universeName','templatePage','status'])))
 medium=until(lambda:(lambda p:p if p.get('title')=='QA Films' and p.get('visible') and p.get('total')==1 and p.get('kind')=='movie' else None)(driver.props('shelvesMedium_qa-films',['title','total','kind','key','visible'])))
 save(output,'universe-B-medium.json',medium)
 def caption():
  for row in driver.tree('shelvesMedium_qa-films'):
   if 'Text' in row.get('class',''):
    p=driver.props(row['handle'],['text','visible'])
    if p.get('text')==TITLE and p.get('visible'):
     q=driver.call('ui-query',{'object':row['handle']})
     if visible_bounds(q):return {'text':p['text'],'query':q}
     scroll_toward(driver,'shelvesScroll',q)
  return None
 def rendered():
  for row in driver.tree('universeLayer'):
   if row.get('objectName')=='shelvesCover_qa-films:0':
    p=driver.props(row['handle'],['source','status','visible','width','height'])
    if p.get('visible') and p.get('width',0)>0 and p.get('height',0)>0 and p.get('status') in (1,'Ready') and '/assets/content/' in str(p.get('source','')):
     q=driver.call('ui-query',{'object':row['handle']});viewport=driver.call('ui-query',{'object':'shelvesScroll'})
     if center_in_view(q,viewport):return {'objectName':row['objectName'],'props':p,'query':q,'viewport':viewport}
     scroll_toward(driver,'shelvesScroll',q)
  return None
 art=until(rendered)
 title=until(caption)
 art=until(rendered)  # Re-query after any caption scroll before the final grab.
 save(output,'universe-B-title.json',title)
 driver.grab('universe-B-rendered','universeLayer')
 result={'status':'PASS','phase':'B','page':page,'medium':medium,'renderedTitle':title,'renderedOwnedArtwork':art,'identityScope':'Rendered title and slot matched; IMDb identity verified in published fixture, not read as native QML var','claim':'Same runtime content-only discovery/navigation/render; controlled fixture, not live publication coverage'}
 save(output,'universe-B-render.json',result);return result
