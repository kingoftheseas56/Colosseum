#!/usr/bin/env python3
"""Full native app, existing isolated offscreen endpoint seam; hosted runner only.
This file does not alter product code or inject UI/model state.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import time
import uuid


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def atomic_json(path, data):
    tmp = path.with_suffix('.tmp')
    tmp.write_text(json.dumps(data))
    tmp.replace(path)


def manifest(root, sha):
    return {str(p.relative_to(root)): {'bytes':p.stat().st_size, 'sha256':sha(p)}
            for p in sorted(root.rglob('*')) if p.is_file() and not p.is_symlink()}


def retry_candidate(props):
    return props.get('accessibleName')=='Try again' and props.get('enabled') is True


class RetrySelectorBlocked(RuntimeError):
    pass


def healthy_identity(props,cards,expected):
    return (sorted(c['objectName'] for c in cards)==sorted(expected['cardIds'])
            and sorted(c['properties']['capText'] for c in cards)==sorted(expected['titles'])
            and props.get('automationItemCount')==expected['itemCount']
            and props.get('currentType')=='comics' and not props.get('pageFailed')
            and not props.get('loading'))


def same_cards(before,after):
    return (sorted(c['objectName'] for c in before['cards'])==sorted(c['objectName'] for c in after['cards'])
            and before['properties'].get('automationItemCount')==after['properties'].get('automationItemCount'))


def compare_immutable_cache(before,after):
    selected={k:v for k,v in before.items() if re.fullmatch(r'[a-f0-9]{64}\.json',Path(k).name)}
    missing=[k for k in selected if k not in after]
    changed=[k for k in selected if k in after and selected[k]!=after[k]]
    return {'baselineImmutableFiles':len(selected),'missing':missing,'changed':changed,
            'passed':bool(selected) and not missing and not changed,
            'scope':'Content-addressed static JSON payloads; mutable validation timestamps excluded'}


def last_good_pairs(profile, endpoint):
    """Mirror native entry/index acceptance; do not alter persisted cache."""
    pairs=[]; errors=[]
    is_hash=lambda value:isinstance(value,str) and re.fullmatch(r'[a-f0-9]{64}',value) is not None
    for wrapper_path in sorted(profile.rglob('last-good.json')):
        relative=str(wrapper_path.relative_to(profile))
        try:
            if wrapper_path.is_symlink() or wrapper_path.stat().st_size>16384:
                raise ValueError('Wrapper is symlink/oversize')
            wrapper=json.loads(wrapper_path.read_bytes())
            if not isinstance(wrapper,dict) or wrapper.get('endpoint')!=endpoint:
                raise ValueError('Wrapper endpoint mismatch')
            entry=wrapper.get('entry'); ref=entry.get('index') if isinstance(entry,dict) else None
            if not isinstance(ref,dict): raise ValueError('Missing entry/index object')
            revision=entry.get('revision'); source=entry.get('sourceRevision'); digest=ref.get('sha256'); size=ref.get('bytes')
            if (type(entry.get('schemaVersion')) is not int or entry['schemaVersion']!=1
                or not all(is_hash(v) for v in (revision,source,digest))
                or type(size) not in (int,float) or not 0<size<=25*1024*1024 or int(size)!=size
                or ref.get('path')!='/catalogue/'+revision+'/index-'+digest+'.json'):
                raise ValueError('Invalid entry schema/reference')
            index_path=wrapper_path.parent/(digest+'.json')
            if index_path.is_symlink() or index_path.stat().st_size>25*1024*1024:
                raise ValueError('Index is symlink/oversize')
            raw=index_path.read_bytes()
            if len(raw)!=size or hashlib.sha256(raw).hexdigest()!=digest:
                raise ValueError('Index size/hash mismatch')
            index=json.loads(raw)
            if (not isinstance(index,dict) or type(index.get('schemaVersion')) is not int
                or index.get('schemaVersion')!=1 or type(index.get('version')) is not int or index.get('version')!=1
                or index.get('revision')!=revision or index.get('sourceRevision')!=source
                or any(not isinstance(index.get(k),dict) for k in ('lists','fixed','search'))):
                raise ValueError('Index publication/schema mismatch')
            pairs.append({'wrapper':relative,'endpoint':endpoint,'revision':revision,'sourceRevision':source,
                          'indexPath':ref['path'],'indexSha256':digest,'indexBytes':size})
        except (OSError,ValueError,TypeError,AttributeError) as error:
            errors.append({'wrapper':relative,'error':str(error)})
    return {'passed':bool(pairs) and not errors,'pairs':pairs,'errors':errors}


def compare_last_good(before,after):
    return {'passed':before.get('passed') is True and after.get('passed') is True and before['pairs']==after['pairs'],
            'before':before,'after':after,'excludedFields':['validatedAt','etag']}


def request_matches(row,path,mode,raw):
    body=raw if mode=='healthy' else b'' if mode=='zero' else raw[:max(1,len(raw)//2)]
    if mode not in ('healthy','zero','truncated-json'): return False
    return (row.get('path')==path and row.get('mode')==mode and row.get('writeCompleted') is True
            and row.get('status')==200 and row.get('bytes')==len(body)
            and row.get('advertisedBytes')==len(body)
            and row.get('bodySha256')==hashlib.sha256(body).hexdigest())


def selected_lanes(selection):
    if selection not in ("all", "universe-only"):
        raise ValueError("Unknown case selection")
    return ("universe",) if selection == "universe-only" else ("recovery", "universe")


def main():
    p = argparse.ArgumentParser()
    for arg in ('binary','lanista','source','provenance','publication','output','expected-catalogue'):
        p.add_argument('--'+arg, type=Path, required=True)
    p.add_argument('--publication-b', type=Path)
    p.add_argument('--universe-hook', type=Path)
    p.add_argument('--timeout', type=int, default=60)
    p.add_argument('--cases', choices=('all','universe-only'), default='all')
    a = p.parse_args()
    lanes = selected_lanes(a.cases)
    if lanes == ('universe',) and (not a.universe_hook or not a.publication_b):
        p.error('universe-only requires universe-hook and publication-b')
    if not 1 <= a.timeout <= 90:
        p.error('timeout must be 1..90 seconds')
    a.output = a.output.resolve()
    a.source = a.source.resolve()
    a.binary = a.binary.resolve()
    a.output.mkdir(parents=True, exist_ok=False)
    base = load(a.source/'tests/b1_native_catalogue_qualification.py', 'proven_driver')
    out = a.output
    expected=json.loads(a.expected_catalogue.read_text())
    summary = {'expectedCatalogue':expected,'lane':'full-native-controlled-publication-offscreen', 'syntheticPublication':True,
               'runtimeModelInjection':False, 'caseSelection':a.cases,
               'notRerun':['live-comics','catalogue-recovery'] if a.cases=='universe-only' else [], 'cases':[], 'passed':False}
    server = None
    def save_summary(): base.write(out/'summary.json',summary)
    control = out/'fixture-control.json'
    selected = 'A'
    def mode(target='index', value='healthy', publication=None):
        nonlocal selected
        if publication is not None: selected = publication
        atomic_json(control, {'target':target,'mode':value,'publication':selected})
    def set_publication(which):
        if which not in ('A','B') or (which == 'B' and not a.publication_b):
            raise ValueError('Publication must be configured A or B')
        mode(publication=which)
    def requests():
        path=out/'fixture/requests.jsonl'
        return [json.loads(x) for x in path.read_text().splitlines()] if path.exists() else []
    def close_server():
        nonlocal server
        if server is not None:
            server.terminate()
            try: server.wait(timeout=10)
            except subprocess.TimeoutExpired: server.kill(); server.wait(timeout=5)
            server=None
    try:
        provenance=json.loads(a.provenance.read_text())
        if 'compiledProvenance' in provenance:
            provenance=provenance['compiledProvenance']
        for key,path in [('binarySha256',a.binary),('lanistaSha256',a.lanista),
                         ('qmlManifestSha256',a.binary.parent/'qml-build.manifest')]:
            if not provenance.get(key) or provenance[key]!=base.sha(path):
                raise RuntimeError('Artifact provenance mismatch: '+key)
        if not provenance.get('compiledNativeSourceCommit',provenance.get('compiledSourceCommit')) or not provenance.get('runtimeQmlSourceCommit'):
            raise RuntimeError('Missing native/QML source identities')
        base.write(out/'provenance.json', {'reusedArtifact':provenance,
            'qaSourceCommit':subprocess.check_output(['git','-C',str(a.source),'rev-parse','HEAD'],text=True).strip(),
            'controllerSha256':base.sha(__file__),'driverSha256':base.sha(a.source/'tests/b1_native_catalogue_qualification.py'),
            'fixtureSha256':base.sha(Path(__file__).with_name('b1_catalogue_fault_fixture.py'))})
        for label,path in [('A',a.publication),('B',a.publication_b)]:
            if path:
                base.write(out/('publication-'+label+'.json'),manifest(path,base.sha))
        mode()
        cmd=[sys.executable,str(Path(__file__).with_name('b1_catalogue_fault_fixture.py')),
             '--publication',str(a.publication.resolve()),'--control',str(control),
             '--evidence',str(out/'fixture')]
        if a.publication_b: cmd+=['--publication-b',str(a.publication_b.resolve())]
        server_log=(out/'fixture-process.log').open('w')
        server=subprocess.Popen(cmd,stdout=server_log,stderr=server_log)
        deadline=time.monotonic()+15
        while not (out/'fixture/endpoint.json').exists():
            if server.poll() is not None or time.monotonic()>deadline:
                raise RuntimeError('ENVIRONMENT_BLOCKED: loopback fixture failed to start')
            time.sleep(.1)
        endpoint=json.loads((out/'fixture/endpoint.json').read_text())
        origin=endpoint['origin']

        def session(name, profile, tag, operation, offline=False):
            folder=out/name; (folder/'ipc').mkdir(parents=True)
            initially_empty=not any(profile.rglob('*')) if profile.exists() else True
            for directory in ('data','cache','config'): (profile/directory).mkdir(parents=True,exist_ok=True)
            base.write(folder/'cache-before.json',manifest(profile,base.sha))
            env=os.environ.copy()
            for key in list(env):
                if any(x in key.upper() for x in ('TOKEN','PASSWORD','SECRET','API_KEY')):
                    env.pop(key,None)
            for key in ('COLOSSEUM_QML_PATH','COLOSSEUM_QML_ROOT','COLOSSEUM_LANISTA_WRITE','COLOSSEUM_DEV','COLOSSEUM_CATALOG_SELFTEST'):
                env.pop(key,None)
            pipe=tag+'-'+uuid.uuid4().hex[:6]
            env.update(XDG_DATA_HOME=str(profile/'data'),XDG_CACHE_HOME=str(profile/'cache'),
                       XDG_CONFIG_HOME=str(profile/'config'),COLOSSEUM_APPDATA_TAG=tag,
                       COLOSSEUM_LANISTA_PIPE=pipe,COLOSSEUM_LANISTA_DRIVE='1',
                       COLOSSEUM_AUTOMATION_FOCUS_SEAMS='0',COLOSSEUM_CATALOGUE_TRACE='1',
                       COLOSSEUM_TEST_CATALOGUE_ORIGIN=origin,
                       COLOSSEUM_TEST_CATALOGUE_OFFLINE='1' if offline else '0',
                       QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',QT_QUICK_CONTROLS_STYLE='Basic')
            row={'case':name,'status':'UNVERIFIED','profileInitiallyEmpty':initially_empty,
                 'tag':tag,'pipe':pipe,'origin':origin,'offline':offline}
            summary['cases'].append(row); save_summary()
            raw=folder/'stderr.raw.log'; stdout=folder/'stdout.raw.log'
            proc=None; driver=None; started=time.monotonic_ns(); before=len(requests())
            try:
                with stdout.open('w') as so,raw.open('w') as se:
                    proc=subprocess.Popen([str(a.binary)],cwd=a.source,env=env,stdout=so,stderr=se)
                    driver=base.Driver(folder,proc,pipe,profile,raw)
                    driver.fixture_request_start=before
                    state=driver.ready(90)
                    base.write(folder/'initial-state.json',state)
                    if not Path(state.get('appDataRoot','')).is_relative_to(profile/'data'):
                        raise RuntimeError('Actual appDataRoot is not isolated')
                    driver.click('modePill_Tankoban')
                    driver.select_catalogue('comics',required=True)
                    result=operation(driver,folder)
                    row.update(result)
            except Exception as error:
                row.update(status='QA_SELECTOR_BLOCKED' if isinstance(error, RetrySelectorBlocked) else 'UNVERIFIED',error=str(error))
                if driver:
                    try:
                        base.write(folder/'failure-state.json',driver.props('tankobanDiscoverBrowser',FIELDS))
                        driver.grab('failure','tankobanDiscoverBrowser')
                    except Exception as capture_error: row['captureError']=str(capture_error)
            finally:
                if proc:
                    if proc.poll() is None:
                        try: driver.call('ui-keypress',{'key':'Ctrl+Q'})
                        except Exception as error: row['closeInputError']=str(error)
                        try: proc.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            row['forcedTermination']=True; proc.terminate()
                            try: proc.wait(timeout=5)
                            except subprocess.TimeoutExpired: proc.kill(); proc.wait(timeout=5)
                    row['exitCode']=proc.returncode
                    if row.get('forcedTermination') or proc.returncode!=0:
                        row['cleanExit']=False
                        if row.get('status')=='PASS': row['status']='UNVERIFIED'
                    else: row['cleanExit']=True
                row['elapsedMs']=(time.monotonic_ns()-started)/1e6
                base.write(folder/'requests.json',requests()[before:])
                base.write(folder/'catalogue-trace.json',base.traces(raw))
                for path in (raw,stdout):
                    if path.exists():
                        (folder/path.name.replace('.raw.','.sanitized.')).write_text(base.sanitize(path.read_text(errors='replace')))
                        path.unlink()
                base.write(folder/'cache-after.json',manifest(profile,base.sha))
                base.write(folder/'last-good-after.json',last_good_pairs(profile,origin))
                base.write(folder/'result.json',row); save_summary()
            return row

        FIELDS=['currentType','active','loading','pageFailed','pageRetryable','automationItemCount','bannerText','bannerVisible']
        def observe(driver,folder,label,want_failure=False):
            deadline=time.monotonic()+a.timeout; props={}; cards=[]
            while time.monotonic()<deadline:
                props=driver.props('tankobanDiscoverBrowser',FIELDS)
                if props.get('currentType')=='comics' and not props.get('loading'):
                    if want_failure and props.get('pageFailed'): break
                    if not want_failure:
                        cards=driver.cards('discoverCard_')
                        if cards and not props.get('pageFailed'): break
                time.sleep(.25)
            result={'properties':props,'cards':cards,'observedAtNs':time.monotonic_ns()}
            base.write(folder/(label+'.json'),result); driver.grab(label,'tankobanDiscoverBrowser')
            if want_failure:
                result['passed']=props.get('pageFailed') is True and props.get('pageRetryable') is True and not props.get('loading')
            else:
                result['passed']=healthy_identity(props,cards,expected)
            return result
        def healthy(driver,folder):
            obs=observe(driver,folder,'healthy')
            return {'status':'PASS' if obs['passed'] else 'FIXTURE_OR_APPLICATION_FAILURE','observation':obs}
        def retry_ui(driver):
            # Dump read-only tree, then inspect accessibleName on live KeyboardAction.
            candidates=[]
            for item in driver.tree('tankobanDiscoverBrowser'):
                if item.get('class','').startswith('KeyboardAction'):
                    props=driver.props(item['handle'],['accessibleName','enabled','visible'])
                    if retry_candidate(props):
                        geometry=driver.call('ui-query',{'object':item['handle']})
                        if base.positive_area(geometry): candidates.append((item['handle'],geometry))
            if len(candidates)!=1: raise RetrySelectorBlocked('Retry UI missing/ambiguous; no backend retry substituted')
            handle,geometry=candidates[0]
            # The action is pointerEnabled=false, but the overlapping normal MouseArea handles this physical click.
            return driver.call('ui-click',{'target':handle})
        def faulty(target,fault):
            def run(driver,folder):
                bad=observe(driver,folder,'failure',True)
                expected_raw=(a.publication/target.lstrip('/')).read_bytes()
                delivered=[r for r in requests()[driver.fixture_request_start:] if request_matches(r,target,fault,expected_raw)]
                if not delivered:
                    return {'status':'UNVERIFIED','error':'Fault was not delivered to requested path','failure':bad}
                if not bad['passed']:
                    return {'status':'APPLICATION_DEFECT','error':'Delivered fault did not settle into retryable error','failure':bad,'faultRequests':delivered}
                mode()
                before=len(requests())
                try: click=retry_ui(driver)
                except Exception as error:
                    return {'status':'QA_SELECTOR_BLOCKED','error':str(error),'failure':bad,'faultRequests':delivered,'expectedRejectionPreserved':True}
                good=observe(driver,folder,'recovered')
                fetched=[r for r in requests()[before:] if request_matches(r,target,'healthy',expected_raw)]
                return {'status':'PASS' if good['passed'] and fetched else 'APPLICATION_DEFECT',
                        'failure':bad,'recovery':good,'retryInput':click,'faultRequests':delivered,
                        'recoveryRequests':fetched,'expectedRejectionPreserved':True}
            return run
        def run_universe():
            if a.universe_hook:
                hook=load(a.universe_hook,'universe_hook')
                universe_profile=out/'profiles/universe'
                universe_tag='i05-static-'+uuid.uuid4().hex[:12]
                def phase(function):
                    def invoke(driver,folder):
                        result=function(driver,set_publication,folder)
                        return result if isinstance(result,dict) else {'status':'UNVERIFIED','error':'Hook returned no result'}
                    return invoke
                before=session('universe-before',universe_profile,universe_tag,phase(hook.run_before))
                if before['status']=='PASS':
                    # Expire the persisted 60-second current.json validation window naturally.
                    wait_start=time.monotonic_ns(); time.sleep(61)
                    base.write(out/'universe-refresh-wait.json',{'elapsedMs':(time.monotonic_ns()-wait_start)/1e6,'cacheEdited':False})
                    set_publication('B')
                    session('universe-after',universe_profile,universe_tag,phase(hook.run_after))
                else: summary['cases'].append({'case':'universe-after','status':'SKIPPED','reason':'Universe A prerequisite failed'})
                set_publication('A')
        if lanes == ('universe',):
            run_universe()
            summary['passed']=bool(summary['cases']) and all(r['status']=='PASS' for r in summary['cases'])
            return 0 if summary['passed'] else 1
        profile=out/'profiles/healthy'; tag='i05-static-'+uuid.uuid4().hex[:12]
        baseline=session('C0-healthy',profile,tag,healthy)
        if baseline['status']!='PASS':
            for case in ('C1-empty-entry','C2-truncated-json','C3-empty-index','C5-warm','C6-offline','universe-publication'):
                summary['cases'].append({'case':case,'status':'SKIPPED','reason':'Healthy controlled-publication prerequisite failed'})
        else:
            for name,target,fault in [('C1-empty-entry','/catalogue/current.json','zero'),
                                      ('C2-truncated-json','/catalogue/current.json','truncated-json'),
                                      ('C3-empty-index',endpoint['indexPath'],'zero')]:
                mode(target,fault)
                session(name,out/'profiles'/name,'i05-static-'+uuid.uuid4().hex[:12],faulty(target,fault))
            mode()
            run_universe()
            def reopen(driver,folder):
                result=healthy(driver,folder)
                same=same_cards(baseline['observation'],result['observation'])
                current=manifest(profile,base.sha)
                original=json.loads((out/'C0-healthy/cache-after.json').read_text())
                cache=compare_immutable_cache(original,current)
                last_good=compare_last_good(json.loads((out/'C0-healthy/last-good-after.json').read_text()),last_good_pairs(profile,origin))
                result.update(sameStableCards=same,immutableCacheComparison=cache,lastGoodPairComparison=last_good)
                if result['status']=='PASS' and (not same or not cache['passed'] or not last_good['passed']): result['status']='APPLICATION_DEFECT'
                return result
            warm=session('C5-warm',profile,tag,reopen)
            close_server()
            if warm['status']=='PASS': session('C6-offline',profile,tag,reopen,True)
            else: summary['cases'].append({'case':'C6-offline','status':'SKIPPED','reason':'Clean warm restart prerequisite failed'})
        summary['passed']=all(r['status']=='PASS' for r in summary['cases'])
    except Exception as error:
        summary['blocker']=str(error)
    finally:
        close_server(); save_summary()
    return 0 if summary['passed'] else 1

if __name__=='__main__':
    raise SystemExit(main())
