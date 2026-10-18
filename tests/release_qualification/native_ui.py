#!/usr/bin/env python3
"""Targeted genuine Comics zero/single native UI qualification; never a fixture lane."""
import argparse,importlib.util,json,os,pathlib,subprocess,tempfile,time,uuid
P=pathlib.Path
NATIVE='cb2db25904dfe9f86660efebffa498aa3f06e5e8'
QML='6199235afcd6768010bd87816ce2d14a00ae77a4'
PIN={'binarySha256':'acb86ce66202531629c137f8706d8d873c3065df8a08867cbeb8bd60eafaa677','lanistaSha256':'9577113c1fe48521755d964431e7c76dece2d8d82b3cd30c83ddf1a29cc88aad','qmlManifestSha256':'d52158f306a2f5588ad5370ea1aa95fb72074af49a4cea27e76eccc5b48b0dd5'}
MULTI_HASH='1c11e21aec6abd4f115022dd40fc62cb2bc3a4ae1a9d5f35780539230d64c8d5'
def load_module(path,name):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def choose(report):
 selected=[]
 for cls,n in [('zero',0),('single',1)]:
  for c in report.get('candidates',[]):
   r=c.get('result',{});ident=c.get('locgId');route=c.get('normalUiRoute',{})
   if c.get('status')=='LIVE_DATA_ESTABLISHED' and c.get('countClass')==cls and c.get('count')==n and isinstance(r.get('editions'),list) and len(r['editions'])==n and r.get('locgId')==ident and r.get('title')==c.get('title') and route.get('firstPageCard')=='discoverCard_'+str(ident) and c.get('lookup',{}).get('publication')==report.get('entry',{}).get('revision'):
    selected.append(c);break
 return selected
def compare(c,r,owner,pub,expected_pub):
 return (r.get('status')=='PASS' and r.get('page',{}).get('locgId')==c['locgId'] and r.get('page',{}).get('seriesTitle')==c['title'] and not r.get('page',{}).get('notAvailable') and owner.get('locgId')==c['locgId'] and owner.get('title')==c['title'] and isinstance(owner.get('editions'),list) and len(owner['editions'])==c['count'] and r.get('nativeReturnedCount')==c['count'] and r.get('displayedCount')==c['count'] and r.get('countClass')==c['countClass'] and pub==expected_pub and r.get('publication')==expected_pub)
def selftest():
 c={'status':'LIVE_DATA_ESTABLISHED','countClass':'zero','count':0,'locgId':'id','title':'Title','result':{'locgId':'id','title':'Title','editions':[]},'normalUiRoute':{'firstPageCard':'discoverCard_id'},'lookup':{'publication':'pub'}}
 assert choose({'entry':{'revision':'pub'},'candidates':[c]})==[c]
 assert not choose({'entry':{'revision':'other'},'candidates':[c]})
 r={'status':'PASS','page':{'locgId':'id','seriesTitle':'Title'},'nativeReturnedCount':0,'displayedCount':0,'countClass':'zero','publication':'pub'}
 assert compare(c,r,c['result'],'pub','pub')
 assert not compare(c,r,{},'pub','pub')
 assert not compare(c,r,c['result'],'drift','pub')
 print('PASS: offline selection/identity/publication guards; no native/live claim')
def main(a):
 d=load_module(a.driver,'prior_driver');report=json.loads(a.probe.read_text());targets=choose(report)
 if a.output.exists():raise RuntimeError('Fresh output required')
 a.output.mkdir(parents=True);d.write(a.output/'target-selection.json',{'probeSha256':d.sha(a.probe),'publication':report.get('entry',{}).get('revision'),'selected':[{'locgId':c['locgId'],'title':c['title'],'countClass':c['countClass']} for c in targets]})
 summary={'status':'INCOMPLETE','scope':'Live zero/single native UI; retained multi evidence reused separately','cases':[],'missingClasses':['zero','single'],'appLaunched':False}
 process=driver=None;private=None;error=None;forced=False
 try:
  if not targets:
   summary['reason']='No explicit zero/single record with a verified first-page ordinary UI route; no app launch or replacement fixture'
   return 2
  compiled=json.loads(a.compiled_provenance.read_text());head=subprocess.check_output(['git','-C',str(a.source),'rev-parse','HEAD'],text=True).strip()
  for k,path in [('binarySha256',a.binary),('lanistaSha256',a.lanista),('qmlManifestSha256',a.binary.parent/'qml-build.manifest')]:
   if d.sha(path)!=PIN[k] or compiled.get(k)!=PIN[k]:raise RuntimeError('Pinned artifact mismatch: '+k)
  if compiled.get('compiledNativeSourceCommit')!=NATIVE or compiled.get('runtimeQmlSourceCommit')!=QML or compiled.get('qualificationSourceCommit')!=head:raise RuntimeError('Source provenance mismatch')
  if d.sha(a.prior_multi)!=MULTI_HASH:raise RuntimeError('Retained multi receipt mismatch')
  multi=json.loads(a.prior_multi.read_text())
  if multi.get('status')!='PASS' or multi.get('countClass')!='multi':raise RuntimeError('Retained multi invalid')
  summary['reusedMultiValidForCurrentPublication']=multi.get('publication')==report.get('entry',{}).get('revision')
  if not summary['reusedMultiValidForCurrentPublication']:summary['multiReuseBlocker']='Publication drift; retained multi cannot close current combined class gate'
  if compiled.get('configuredContentEndpoint')!='https://colosseum-content-api-staging.colosseum-watchparty-relay.workers.dev':raise RuntimeError('Endpoint provenance mismatch')
  d.write(a.output/'reused-multi.json',{'runId':39129013913,'sourceReceiptSha256':MULTI_HASH,'compiledNativeSourceCommit':NATIVE,'runtimeQmlSourceCommit':QML,'result':multi})
  d.preflight(a.source,a.binary)
  private=P(tempfile.mkdtemp(prefix='b1-comics-private-',dir=a.output.parent.parent));profile=private/'profile'
  for name in ['data','cache','config']:(profile/name).mkdir(parents=True)
  (a.output/'ipc').mkdir()
  tag='b1-comics-'+uuid.uuid4().hex[:12];env=os.environ.copy()
  for k in list(env):
   if any(p in k.upper() for p in ['TOKEN','PASSWORD','SECRET','API_KEY']):del env[k]
  env.update(XDG_DATA_HOME=str(profile/'data'),XDG_CACHE_HOME=str(profile/'cache'),XDG_CONFIG_HOME=str(profile/'config'),QT_QPA_PLATFORM='xcb',COLOSSEUM_APPDATA_TAG=tag,COLOSSEUM_LANISTA_PIPE=tag,COLOSSEUM_LANISTA_DRIVE='1',COLOSSEUM_AUTOMATION_FOCUS_SEAMS='0',COLOSSEUM_CATALOGUE_TRACE='1',COLOSSEUM_POSTER_TIMING='1')
  raw=private/'stderr.log'
  with open(private/'stdout.log','w') as out,open(raw,'w') as err:
   process=subprocess.Popen([str(a.binary)],cwd=a.source,env=env,stdout=out,stderr=err);summary['appLaunched']=True
   d.write(a.output/'launch.json',{'compiledProvenance':compiled,'driverSha256':d.sha(a.driver),'controllerSha256':d.sha(__file__),'qualificationSourceCommit':head,'artifactRunId':39129013913,'pid':process.pid,'isolatedProfile':True,'rendering':'actual full native app/X11/xcb; caller supplies Xvfb1600x1000'})
   driver=d.Driver(a.output,process,tag,profile,raw);state=driver.ready(90)
   if not P(state.get('appDataRoot','')).is_relative_to(profile/'data'):raise RuntimeError('Profile isolation failed')
   d.write(a.output/'initial-state.json',state)
   driver.click('modePill_Tankoban');driver.select_catalogue('comics')
   landing=driver.journey('comics','comics','discoverPageFiltered','tankobanDiscoverBrowser',{'currentType':'comics'},'discoverCard_',60)
   if not landing.get('passed'):raise RuntimeError('Comics landing did not settle successfully')
   for c in targets:
    label='comics-series-'+c['countClass']
    def observe(c=c,label=label):
     driver.ensure_world('Tankoban');driver.select_catalogue('comics')
     # Restore first-page origin using only bounded ordinary input.
     for _ in range(6):
      if float(driver.props('tankobanWorldScroll',['contentY']).get('contentY') or 0)<=1:break
      driver.call('ui-scroll',{'target':driver.visible_ref('tankobanWorldScroll'),'dy':720,'xFraction':0.92,'yFraction':0.8});time.sleep(.2)
     # At most eight ordinary wheel steps; only exact known first-page card, never a search/crawl.
     found=None
     for step in range(9):
      try:found=driver.visible_ref('discoverCard_'+c['locgId']);break
      except RuntimeError:
       if step==8:break
       driver.call('ui-scroll',{'target':driver.visible_ref('tankobanWorldScroll'),'dy':-260,'xFraction':0.92,'yFraction':0.8});time.sleep(.2)
     if not found:return {'status':'INCOMPLETE','reason':'Known first-page target not reachable using bounded ordinary scrolling','expectedLocgId':c['locgId']}
     driver.call('ui-click',{'target':found})
     try:
      r=driver.comics_series(label)
      k,e,pub=driver.cache_entry('comics_api_v1.json',lambda e:e.get('method')=='curatedSeries' and e.get('result',{}).get('locgId')==c['locgId'],seconds=3)
      owner=e.get('result',{}) if e else {}
      r['expectedIdentity']={'locgId':c['locgId'],'title':c['title'],'count':c['count'],'publication':report['entry']['revision']}
      r['exactOwnerRecord']=owner;r['nativeOwnerQueryKey']=k
      exact=compare(c,r,owner,pub,report['entry']['revision'])
      root=driver.visible_ref(class_prefix='ComicSeriesPage_');nodes=driver.tree(root)
      ledger=next((x for x in nodes if x.get('class','').startswith('ComicDbLedger_')),None)
      texts=[]
      if ledger:
       for x in driver.tree(ledger['handle']):
        if 'Text' in x.get('class',''):
         value=driver.props(x['handle'],['text']).get('text')
         if value and 'collected edition' in str(value):texts.append(str(value))
      import re
      count_text=any(re.search(r'(?<!\d)'+str(c['count'])+r' collected edition'+('s' if c['count']!=1 else r'(?!s)'),t) for t in texts)
      r['ledgerPresent']=bool(ledger);r['countTexts']=texts;r['exactIdentityAndPublication']=exact
      r['status']='PASS' if exact and ledger and count_text else 'INCOMPLETE'
      return r
     finally:driver.back()
    summary['cases'].append(driver.case(label,observe))
   passed={r.get('countClass') for r in summary['cases'] if r.get('status')=='PASS'}
   summary['missingClasses']=sorted({'zero','single'}-passed)
   summary['status']='PASS' if not summary['missingClasses'] and summary.get('reusedMultiValidForCurrentPublication') else 'INCOMPLETE'
 except Exception as e:error=str(e);summary['error']=error
 finally:
  if process and process.poll() is None:
   try:
    if driver and driver.path:driver.call('ui-keypress',{'key':'Ctrl+Q'},timeout=3)
    process.wait(timeout=15)
   except Exception:
    forced=True;process.terminate()
    try:process.wait(timeout=5)
    except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
  if process:
   summary['numericExitStatus']=process.returncode;summary['forcedTermination']=forced
   if process.returncode!=0 or forced:summary['status']='INCOMPLETE'
  if private:
   # Retain the first failure and all diagnostics; never upload the raw profile/logs.
   for stream in ('stdout','stderr'):
    raw_log=private/(stream+'.log')
    if raw_log.exists():
     (a.output/(stream+'.sanitized.log')).write_text(d.sanitize(raw_log.read_text(errors='replace')))
   d.write(a.output/'catalogue-trace.json',d.traces(private/'stderr.log'))
   d.write(a.output/'profile-databases-after.json',[str(x.relative_to(profile)) for x in profile.rglob('*.db')])
  d.write(a.output/'summary.json',summary);print(json.dumps({'status':summary['status'],'missingClasses':summary['missingClasses'],'appLaunched':summary['appLaunched'],'error':error}))
 return 0 if summary['status']=='PASS' else 2
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--self-test',action='store_true')
 for name in ['driver','probe','source','binary','lanista','compiled-provenance','prior-multi','output']:ap.add_argument('--'+name,type=P)
 a=ap.parse_args()
 if a.self_test:selftest()
 else:
  if any(getattr(a,k.replace('-','_')) is None for k in ['driver','probe','source','binary','lanista','compiled-provenance','prior-multi','output']):ap.error('All path arguments required')
  for k in ['driver','probe','source','binary','lanista','compiled_provenance','prior_multi','output']:setattr(a,k,getattr(a,k).resolve())
  raise SystemExit(main(a))
