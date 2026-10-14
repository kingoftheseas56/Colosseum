"""Read-only Windows token observation of one owned Feria test process tree.

Does not enable/disable sandboxing, debug, inject into, or terminate a process.
Job membership is recorded, not interpreted as proof of particular job limits.
"""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import psutil

k = c.WinDLL('kernel32', use_last_error=True)
a = c.WinDLL('advapi32', use_last_error=True)
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
k.CloseHandle.argtypes = [w.HANDLE]
k.CloseHandle.restype = w.BOOL
a.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, c.POINTER(w.HANDLE)]
a.OpenProcessToken.restype = w.BOOL
a.GetTokenInformation.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)]
a.GetTokenInformation.restype = w.BOOL
a.IsTokenRestricted.argtypes = [w.HANDLE]
a.IsTokenRestricted.restype = w.BOOL
a.IsValidSid.argtypes = [c.c_void_p]
a.IsValidSid.restype = w.BOOL
a.GetSidSubAuthorityCount.argtypes = [c.c_void_p]
a.GetSidSubAuthorityCount.restype = c.POINTER(c.c_ubyte)
a.GetSidSubAuthority.argtypes = [c.c_void_p, w.DWORD]
a.GetSidSubAuthority.restype = c.POINTER(w.DWORD)
k.IsProcessInJob.argtypes = [w.HANDLE, w.HANDLE, c.POINTER(w.BOOL)]
k.IsProcessInJob.restype = w.BOOL
k.GetProcessTimes.argtypes = [w.HANDLE, c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME), c.POINTER(w.FILETIME)]
k.GetProcessTimes.restype = w.BOOL
k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
k.QueryFullProcessImageNameW.restype = w.BOOL

class SidAttributes(c.Structure):
    _fields_ = [('sid', c.c_void_p), ('attributes', w.DWORD)]

def check(ok):
    if not ok:
        raise c.WinError(c.get_last_error())

def token_info(token, kind):
    size = w.DWORD()
    c.set_last_error(0)
    a.GetTokenInformation(token, kind, None, 0, c.byref(size))
    if c.get_last_error() != 122 or not size.value:
        raise c.WinError(c.get_last_error())
    buf = c.create_string_buffer(size.value)
    check(a.GetTokenInformation(token, kind, buf, size, c.byref(size)))
    return buf

def inspect(pid, expected_created=None):
    process = k.OpenProcess(0x1000, False, pid)  # QUERY_LIMITED_INFORMATION
    check(process)
    token = w.HANDLE()
    try:
        created, exited, kernel, user = (w.FILETIME() for unused in range(4))
        check(k.GetProcessTimes(process, c.byref(created), c.byref(exited), c.byref(kernel), c.byref(user)))
        filetime = (created.dwHighDateTime << 32) | created.dwLowDateTime
        unix_created = filetime / 10000000 - 11644473600
        if expected_created is not None and abs(unix_created - expected_created) > .00001:
            raise RuntimeError('Opened process differs from enumerated creation time')
        length = w.DWORD(32768)
        image = c.create_unicode_buffer(length.value)
        check(k.QueryFullProcessImageNameW(process, 0, image, c.byref(length)))
        check(a.OpenProcessToken(process, 0x8, c.byref(token)))  # TOKEN_QUERY
        restricted_sids = c.cast(token_info(token, 11), c.POINTER(w.DWORD)).contents.value
        c.set_last_error(0)
        restricted = bool(a.IsTokenRestricted(token))
        if not restricted and c.get_last_error(): raise c.WinError(c.get_last_error())
        if restricted != bool(restricted_sids): raise RuntimeError('Restricted token API disagreement')
        label = token_info(token, 25)  # TokenIntegrityLevel
        sid = c.cast(label, c.POINTER(SidAttributes)).contents.sid
        check(a.IsValidSid(sid))
        count = a.GetSidSubAuthorityCount(sid).contents.value
        if not count:
            raise ValueError('Integrity SID has no subauthorities')
        rid = a.GetSidSubAuthority(sid, count - 1).contents.value
        in_job = w.BOOL()
        check(k.IsProcessInJob(process, None, c.byref(in_job)))
        return {'processCreationFileTime':filetime, 'processCreated':unix_created, 'processImage':image.value,
                'integrityRid': rid,
                'integrity': {0:'untrusted',4096:'low',8192:'medium',12288:'high',16384:'system'}.get(rid,'other'),
                'restricted': restricted, 'restrictingSidCount':restricted_sids,
                'hasRestrictions': bool(c.cast(token_info(token,21), c.POINTER(w.DWORD)).contents.value),
                'appContainer': bool(c.cast(token_info(token,29), c.POINTER(w.DWORD)).contents.value),
                'inJob': bool(in_job.value)}
    finally:
        if token: k.CloseHandle(token)
        k.CloseHandle(process)

def main():
    out = Path(sys.argv[1]).resolve()
    out.mkdir(exist_ok=False)
    result = {'scope':'process primary tokens; not all sandbox policy/mitigations',
              'inspectorSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'observer':inspect(os.getpid()), 'samples':[], 'errors':[]}
    command = sys.argv[2:]
    result['driverCommand'] = command
    with (out/'driver.stdout').open('wb') as stdout, (out/'driver.stderr').open('wb') as stderr:
        driver = subprocess.Popen(command, stdout=stdout, stderr=stderr)
        result['driverPid'] = driver.pid
        last = {}
        start = time.monotonic()
        while driver.poll() is None and time.monotonic()-start < 160:
            try:
                children = psutil.Process(driver.pid).children(recursive=True)
            except psutil.NoSuchProcess:
                children = []
            for child in children:
                try:
                    name = child.name().lower()
                    if name not in ('feria_foundation_runtime.exe','qtwebengineprocess.exe'): continue
                    identity = (child.pid, child.create_time())
                    if time.monotonic()-last.get(identity,0)<1: continue
                    last[identity] = time.monotonic()
                    cmdline = child.cmdline()
                    role = next((x.split('=',1)[1] for x in cmdline if x.startswith('--type=')), 'browser')
                    sample = {'pid':child.pid,'created':identity[1], 'exe':child.exe(), 'role':role,
                              'elapsed':time.monotonic()-start, 'timeNs':time.time_ns(), 'token':inspect(child.pid, identity[1])}
                    if os.path.normcase(sample['token']['processImage']) != os.path.normcase(sample['exe']):
                        raise RuntimeError('Opened process differs from enumerated image')
                    result['samples'].append(sample)
                except (psutil.NoSuchProcess, psutil.AccessDenied, OSError, RuntimeError) as error:
                    result['errors'].append({'pid':child.pid,'error':str(error)})
            time.sleep(.1)
        result['driverExit'] = driver.poll()
        result['elapsed'] = time.monotonic()-start
        (out/'tokens.json').write_text(json.dumps(result,indent=2))
        renderers = [x for x in result['samples'] if x['role']=='renderer']
        print(json.dumps({'driverExit':driver.poll(),'samples':len(result['samples']),
                          'rendererSamples':len(renderers), 'errors':len(result['errors']),
                          'observer':result['observer'],
                          'rendererStates':list({json.dumps(x['token'],sort_keys=True) for x in renderers})}))
        if driver.poll() is None: raise RuntimeError('Driver still live; preserve PID, do not duplicate')
        sys.exit(driver.returncode)

if __name__=='__main__': main()
