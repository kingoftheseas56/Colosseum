#!/usr/bin/env python3
"""Installed Linux app proof using its supported QML driver entry.

Uses real packaged Main/native stores/resolver/helper/player. The only fault
injection is SIGKILL/SIGSTOP to the owned helper child; no success is injected.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

PREFIX = 'LINUX_NUVIO_PROOF='

def helper_children(app_pid):
    found=[]
    for item in Path('/proc').iterdir():
        if not item.name.isdigit(): continue
        try:
            status=(item/'status').read_text()
            parent=int(next(line.split()[1] for line in status.splitlines() if line.startswith('PPid:')))
            if parent != app_pid: continue
            args=(item/'cmdline').read_bytes().split(b'\0')
            if not args or not args[0].endswith(b'/colosseum-nuvio-host') or b'--plugin' not in args: continue
            found.append({'pid':int(item.name),'exe':str((item/'exe').resolve()),
                          'startTicks':(item/'stat').read_text().split(') ',1)[1].split()[19]})
        except (FileNotFoundError, PermissionError, StopIteration, ProcessLookupError): continue
    return found

def process_identity(pid):
    """Identity fields stay stable across reparenting, but not PID reuse."""
    item = Path('/proc') / str(pid)
    return {'pid': pid, 'exe': str((item / 'exe').resolve(strict=True)),
            'startTicks': (item / 'stat').read_text().split(') ', 1)[1].split()[19]}


def kill_verified_helpers(records, expected_exe):
    """Kill only identities previously observed as this app's bundled children.

    A pidfd pins the process before rechecking /proc, closing the PID-reuse race
    between verification and signal delivery. A reparented stopped helper can
    therefore still be cleaned up without signalling an unrelated process.
    """
    outcomes = []
    for record in records:
        if record.get('exe') != expected_exe:
            continue
        pidfd = None
        try:
            pidfd = os.pidfd_open(record['pid'])
            identity = process_identity(record['pid'])
            if identity['exe'] != expected_exe or identity['startTicks'] != record['startTicks']:
                outcomes.append({'pid': record['pid'], 'outcome': 'identity_changed_not_signalled'})
                continue
            signal.pidfd_send_signal(pidfd, signal.SIGKILL)
            outcomes.append({'pid': record['pid'], 'outcome': 'SIGKILL'})
        except (FileNotFoundError, ProcessLookupError):
            outcomes.append({'pid': record['pid'], 'outcome': 'already_exited'})
        except OSError as error:
            outcomes.append({'pid': record['pid'], 'outcome': 'cleanup_error', 'error': str(error)})
        finally:
            if pidfd is not None:
                os.close(pidfd)
    return outcomes


def cleanup_owned_processes(process, tracked, expected_exe, stop_app):
    if process is None:
        return []
    # Ownership is learned while the parent is still alive. Never discover new
    # helpers by executable name after it exits: those may belong to another app.
    records = {(p['pid'], p['startTicks']): dict(p) for p in tracked
               if p.get('exe') == expected_exe}
    if expected_exe and process.poll() is None:
        for child in helper_children(process.pid):
            if child['exe'] == expected_exe:
                records[(child['pid'], child['startTicks'])] = child
    outcomes = []
    try:
        outcomes.extend(kill_verified_helpers(records.values(), expected_exe))
    finally:
        stop_app(process)
        # A SIGSTOP worker may have become an orphan during app shutdown; its
        # saved start-time/executable identity remains sufficient to clean it.
        outcomes.extend(kill_verified_helpers(records.values(), expected_exe))
    return outcomes

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--expected-sha',required=True)
    parser.add_argument('--concurrency-sha',required=True)
    parser.add_argument('--fixture-sha',default='446d50c085fc7bcaec32ec3e7822eeda7352d35c')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    spec=importlib.util.spec_from_file_location('installed_player',root/'tools/linux-release/smoke_installed_player.py')
    installed=importlib.util.module_from_spec(spec);spec.loader.exec_module(installed)
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    result={'status':'FAIL','expectedSha':args.expected_sha,'fixtureSha':args.fixture_sha,
            'driver':'temporary QML subclass of installed Main; native binary/helper/QML bundle unmodified',
            'faultInjection':'SIGKILL crash and SIGSTOP process stall, normal resolver watchdog',
            'events':[],'processes':[],'faults':[]}
    process=None
    expected_helper=None
    try:
        result['identity']=installed.installed_identity()
        if result['identity']['source_commit']!=args.expected_sha: raise RuntimeError('installed source identity mismatch')
        helper=Path('/opt/colosseum/bin/colosseum-nuvio-host')
        if not helper.is_file():
            candidates=list(Path('/opt/colosseum').rglob('colosseum-nuvio-host'))
            if len(candidates)!=1: raise RuntimeError('bundled helper missing or ambiguous')
            helper=candidates[0]
        expected_helper=str(helper.resolve())
        result['helper']={'path':str(helper),'sha256':installed.sha256_file(helper)}
        with tempfile.TemporaryDirectory(prefix='colosseum-linux-nuvio-proof-') as directory:
            profile=Path(directory)
            driver=profile/'NuvioRuntimeProof.qml'
            driver.write_bytes((Path(__file__).parent/'NuvioRuntimeProof.qml').read_bytes())
            environment=installed.clean_environment(profile)
            environment['COLOSSEUM_LANISTA_PIPE']='NuvioProof-'+str(os.getpid())
            fixture='https://raw.githubusercontent.com/kingoftheseas56/Colosseum/'+args.fixture_sha+'/tests/fixtures/nuvio-android-proof'
            concurrency='https://raw.githubusercontent.com/kingoftheseas56/Colosseum/'+args.concurrency_sha+'/tests/fixtures/nuvio-linux-concurrency'
            command=[str(installed.APP),str(driver),fixture,str(output),concurrency]
            (output/'command.json').write_text(json.dumps({'argv':command,'isolatedProfile':str(profile)},indent=2))
            started=time.monotonic();seen=set();pending=None;offset=0;buffer='';fault_times={};recovery_kind=None;concurrent=False;max_workers=0
            with (output/'app.log').open('w') as log:
                process=subprocess.Popen(command,env=environment,stdout=log,stderr=subprocess.STDOUT)
                result['appPid']=process.pid
                while process.poll() is None:
                    now=time.monotonic()
                    if now-started>380: raise TimeoutError('packaged proof timed out')
                    with (output/'app.log').open(errors='replace') as read:
                        read.seek(offset);buffer+=read.read();offset=read.tell()
                    lines=buffer.split('\n');buffer=lines.pop()
                    for line in lines:
                        if PREFIX not in line: continue
                        event=json.loads(line.split(PREFIX,1)[1]);event['elapsed']=round(now-started,3);result['events'].append(event)
                        if event['stage']=='fault_armed': pending=event['details']['kind'];recovery_kind=None
                        if event['stage']=='recovery_armed': recovery_kind=event['details']['kind']
                        if event['stage']=='concurrency_armed': concurrent=True
                        if event['stage']=='concurrency_complete': concurrent=False
                        if event['stage']=='fault_complete':
                            kind=event['details']['kind']
                            if kind not in fault_times: raise RuntimeError('fault finished without signal injection')
                            elapsed=now-fault_times[kind]
                            event['signalElapsedSeconds']=round(elapsed,3)
                            if kind=='timeout' and not 19<=elapsed<=35: raise RuntimeError('timeout outside watchdog window')
                    children=helper_children(process.pid)
                    if concurrent: max_workers=max(max_workers,len(children))
                    for child in children:
                        if child['pid'] in seen: continue
                        seen.add(child['pid']);child['elapsed']=round(now-started,3);child['recoveryAfter']=recovery_kind
                        result['processes'].append(child)
                        if child['exe']!=str(helper.resolve()): raise RuntimeError('app used a non-bundled helper')
                        if pending:
                            sig=signal.SIGKILL if pending=='crash' else signal.SIGSTOP
                            os.kill(child['pid'],sig)
                            result['faults'].append({'kind':pending,'pid':child['pid'],'signal':sig.name,'elapsed':round(now-started,3)})
                            fault_times[pending]=now;pending=None
                    time.sleep(0.01)
            # Collect records written during the final process-exit interval.
            all_events=[]
            for line in (output/'app.log').read_text(errors='replace').splitlines():
                if PREFIX in line: all_events.append(json.loads(line.split(PREFIX,1)[1]))
            result['rawEvents']=all_events;result['exitCode']=process.returncode
            result['maximumConcurrentHelpers']=max_workers
            if max_workers!=4: raise RuntimeError('did not observe exactly four concurrent bundled helpers')
            if process.returncode or not any(e['stage']=='PASS' for e in all_events): raise RuntimeError('QML proof did not pass')
            for kind in ('crash','timeout'):
                fault=next((f for f in result['faults'] if f['kind']==kind),None)
                recovery=next((p for p in result['processes'] if p['recoveryAfter']==kind),None)
                if not fault or not recovery or fault['pid']==recovery['pid']: raise RuntimeError('missing fresh helper after '+kind)
            for stage in ('welcome','sources','nuvio_playback','stremio_playback','final_requery','concurrency_complete'):
                if not any(e['stage']==stage for e in all_events): raise RuntimeError('missing '+stage)
            screenshots=list(output.glob('*.png'))
            required_images={'fresh-profile-welcome.png','sources-providers.png','nuvio-playback.png',
                             'stremio-playback.png','crash-survived.png','crash-recovered.png',
                             'timeout-survived.png','timeout-recovered.png','four-workers-complete.png'}
            missing_images=required_images-{p.name for p in screenshots}
            if missing_images: raise RuntimeError('screenshots incomplete: '+', '.join(sorted(missing_images)))
            result['screenshots']=[{'name':p.name,'sha256':installed.sha256_file(p)} for p in screenshots]
            result['status']='PASS'
    except Exception as error:
        result['error']=str(error)
    finally:
        result['cleanup']=cleanup_owned_processes(process,result['processes'],expected_helper,installed.stop_owned_process)
        if any(item['outcome']=='cleanup_error' for item in result['cleanup']):
            result['status']='FAIL'
            result['cleanupError']='Could not clean up all verified helper processes'
        (output/'proof.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'status':result['status'],'error':result.get('error'),'output':str(output)}))
    return 0 if result['status']=='PASS' else 1

if __name__=='__main__': raise SystemExit(main())
