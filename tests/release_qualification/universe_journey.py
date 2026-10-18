"""QA-only real-UI A/B assertions. Controller owns process/profile/fixture lifecycle."""
import json,time,math
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
def roster(driver):return driver.props('universesWorld',['roster','universeContentGeneration','shown'])
def run_before(driver,set_publication,output):
 driver.ensure_world('Universes')
 state=until(lambda: (lambda x:x if x.get('universeContentGeneration',0)>0 and len(x.get('roster',[]))>0 else None)(roster(driver)))
 assert all(x.get('name')!=NAME for x in state['roster']), 'Test universe unexpectedly in A'
 save(output,'universe-A-roster.json',state);driver.grab('universe-A-absent','universesWorld')
 return {'status':'PASS','phase':'A','nameAbsent':NAME,'generation':state['universeContentGeneration'],'rosterCount':len(state['roster'])}
def run_after(driver,set_publication,output):
 driver.ensure_world('Universes')
 state=until(lambda: (lambda x:x if any(r.get('name')==NAME for r in x.get('roster',[])) else None)(roster(driver)))
 entry=[x for x in state['roster'] if x.get('name')==NAME];assert len(entry)==1 and entry[0].get('page')=='shelves'
 save(output,'universe-B-roster.json',state)
 # Only ordinary wheel/click input. Read entry from fresh actual banner handles.
 activated=False;receipts=[]
 for attempt in range(24):
  target_query=None
  rows=driver.tree('universesLedger')
  for row in rows:
   if 'UniverseBanner' not in row.get('class',''):continue
   p=driver.props(row['handle'],['entry','visible'])
   if p.get('entry',{}).get('name')!=NAME:continue
   q=driver.call('ui-query',{'object':row['handle']});target_query=q
   viewport=driver.call('ui-query',{'object':'universesWorldScroll'})
   if p.get('visible') and center_in_view(q,viewport):
    receipts.append({'entry':p['entry'],'query':q,'viewport':viewport,'input':driver.call('ui-click',{'target':row['handle']})});activated=True;break
  if activated:break
  scroll_toward(driver,'universesWorldScroll',target_query);time.sleep(.25)
 save(output,'universe-B-input.json',receipts);assert activated,'Specific published Universe banner not reached through ordinary UI'
 page=until(lambda:(lambda p:p if p.get('active') and p.get('universeName')==NAME and p.get('templatePage')=='shelves' and p.get('status') in (1,'Ready') else None)(driver.props('universeLayer',['active','universeName','templatePage','status'])))
 medium=until(lambda:(lambda p:p if p.get('title')=='QA Films' and p.get('visible') and len(p.get('runs',[]))==1 else None)(driver.props('shelvesMedium_qa-films',['title','runs','visible'])))
 items=medium['runs'][0].get('items',[]);assert len(items)==1 and items[0].get('title')==TITLE and items[0].get('id')=='tt0076759',items
 def rendered():
  for row in driver.tree('universeLayer'):
   if row.get('objectName','').startswith('shelvesCover_'):
    p=driver.props(row['handle'],['source','status','visible','width','height'])
    if p.get('visible') and p.get('width',0)>0 and p.get('height',0)>0 and p.get('status') in (1,'Ready') and '/assets/content/' in str(p.get('source','')):
     q=driver.call('ui-query',{'object':row['handle']});viewport=driver.call('ui-query',{'object':'shelvesScroll'})
     if center_in_view(q,viewport):return {'objectName':row['objectName'],'props':p,'query':q,'viewport':viewport}
     scroll_toward(driver,'shelvesScroll',q)
  return None
 art=until(rendered)
 driver.grab('universe-B-rendered','universeLayer')
 result={'status':'PASS','phase':'B','page':page,'medium':medium,'renderedOwnedArtwork':art,'claim':'Same runtime content-only discovery/navigation/render; controlled fixture, not live publication coverage'}
 save(output,'universe-B-render.json',result);return result
