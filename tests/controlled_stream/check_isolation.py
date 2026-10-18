#!/usr/bin/env python3
"""Proposed GitHub Linux isolation probe. Never launched in denied executor.
Execution creates and closes one benign loopback socket only after isolation.
--self-test tests pure validation helpers only.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


def validate_links(links):
    if not isinstance(links, list) or len(links) != 1:
        raise ValueError('Expected exactly one network interface')
    link = links[0]
    if link.get('ifname') != 'lo' or link.get('link_type') != 'loopback':
        raise ValueError('Non-loopback interface present')
    return True


def validate_routes(routes):
    if not isinstance(routes, list):
        raise ValueError('Invalid route response')
    for route in routes:
        # After lo is up Linux installs local loopback routes only. Reject
        # gateways, default routes, policy/multipath/nexthop and unknown scopes.
        if route.get('dev') != 'lo' or any(k in route for k in ('gateway', 'via', 'multipath', 'nexthops', 'nhid')):
            raise ValueError('Non-loopback routing capability present')
        dst = route.get('dst', '')
        import ipaddress
        try:
            network = ipaddress.ip_network(dst, strict=False)
        except ValueError:
            raise ValueError('Unknown/default route destination')
        allowed = ipaddress.ip_network('127.0.0.0/8') if network.version == 4 else ipaddress.ip_network('::1/128')
        if not network.subnet_of(allowed):
            raise ValueError('Route destination outside loopback')
    return True


def validate_map(value, parent_uid):
    rows = [list(map(int, row.split())) for row in value.splitlines() if row.strip()]
    if rows != [[0, parent_uid, 1]]:
        raise ValueError('User namespace mapping is not exact invoking user')
    return True


def run_json(ip, args):
    data = subprocess.run([ip, '-j', *args], check=True, capture_output=True, text=True, timeout=3)
    return json.loads(data.stdout)


def safe_diagnostic(value):
    import re
    text = str(value)
    text = re.sub(r"[a-zA-Z][a-zA-Z0-9+.-]*://[^\s]+", "[URL redacted]", text)
    text = re.sub(r"(?i)(token|password|secret|authorization|api[_-]?key)\s*[:=]\s*[^\s]+", r"\1=[redacted]", text)
    return ''.join(c if c.isprintable() else ' ' for c in text)[:600]


def validate_current_namespace(parent_net, parent_user, parent_uid, parent_gid,
                               ip_path, require_loopback_up=True):
    """Read-only gate to repeat inside the SAME live namespace as transport.
    Parent identity values must come from launcher before unshare, never inferred here.
    """
    if platform.system() != 'Linux' or os.geteuid() != 0:
        raise ValueError('Expected mapped user namespace on Linux')
    if not parent_net or not parent_user or parent_uid is None or parent_gid is None:
        raise ValueError('Original parent namespace/user identities required')
    net = os.readlink('/proc/self/ns/net')
    user = os.readlink('/proc/self/ns/user')
    if net == parent_net or user == parent_user:
        raise ValueError('Namespace isolation was not established')
    validate_map(Path('/proc/self/uid_map').read_text(), parent_uid)
    validate_map(Path('/proc/self/gid_map').read_text(), parent_gid)
    if Path('/proc/self/setgroups').read_text().strip() != 'deny':
        raise ValueError('setgroups must be denied in mapped namespace')
    links = run_json(ip_path, ['link', 'show'])
    validate_links(links)
    if require_loopback_up and 'UP' not in links[0].get('flags', []):
        raise ValueError('Loopback is not UP')
    validate_routes(run_json(ip_path, ['-4', 'route', 'show', 'table', 'all']))
    validate_routes(run_json(ip_path, ['-6', 'route', 'show', 'table', 'all']))
    return {'parentNetNamespace': parent_net, 'isolatedNetNamespace': net,
            'parentUserNamespace': parent_user, 'isolatedUserNamespace': user,
            'mappedUid': parent_uid, 'mappedGid': parent_gid,
            'interfaces': ['lo'], 'externalRoutes': False,
            'loopbackUp': 'UP' in links[0].get('flags', [])}


def child(args):
    values = (args.parent_net, args.parent_user, args.parent_uid, args.parent_gid, args.ip)
    validate_current_namespace(*values, require_loopback_up=False)
    # The ONLY setting mutation, after both namespaces and identity are checked.
    subprocess.run([args.ip, 'link', 'set', 'dev', 'lo', 'up'], check=True, capture_output=True, timeout=3)
    receipt = validate_current_namespace(*values)
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(('127.0.0.1', 0))
        bound = probe.getsockname()
    receipt.update({'status': 'PASS', 'scope': 'ephemeral isolation probe only; no transport/app tested',
                    'loopbackBind': {'status': 'PASS', 'address': bound[0], 'port': bound[1], 'closed': True}})
    print(json.dumps(receipt))


def self_test():
    validate_links([{'ifname': 'lo', 'link_type': 'loopback'}])
    validate_routes([])
    validate_routes([{'dev': 'lo', 'dst': '127.0.0.0/8'}, {'dev': 'lo', 'dst': '::1'}])
    validate_map('0 1000 1\n', 1000)
    negatives = [lambda: validate_links([]), lambda: validate_links([{'ifname': 'eth0', 'link_type': 'ether'}]),
                 lambda: validate_links([{'ifname': 'lo', 'link_type': 'loopback'}, {'ifname':'eth0'}]),
                 lambda: validate_routes([{'dev':'lo','dst':'default'}]),
                 lambda: validate_routes([{'dev':'lo','dst':'0.0.0.0/0'}]),
                 lambda: validate_routes([{'dev':'lo','dst':'::/0'}]),
                 lambda: validate_routes([{'dev':'lo','dst':'10.0.0.0/8'}]),
                 lambda: validate_routes([{'dev':'eth0','dst':'127.0.0.1'}]),
                 lambda: validate_routes([{'dev':'lo','dst':'127.0.0.1','gateway':'1.2.3.4'}]),
                 lambda: validate_routes([{'dev':'lo','dst':'::1','nhid':1}]),
                 lambda: validate_map('0 0 4294967295',1000),
                 lambda: validate_map('0 1001 1',1000)]
    for fn in negatives:
        try: fn()
        except (ValueError, TypeError): continue
        raise AssertionError('Unsafe fixture accepted')
    assert safe_diagnostic('unshare: unshare failed: Operation not permitted') == 'unshare: unshare failed: Operation not permitted'
    assert 'secret-value' not in safe_diagnostic('token=secret-value')
    assert 'user:pass' not in safe_diagnostic('http://user:pass@localhost/x')
    assert len(safe_diagnostic('x'*900)) == 600
    print('20 offline isolation validation checks passed')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('--inside', action='store_true')
    ap.add_argument('--parent-net'); ap.add_argument('--parent-user')
    ap.add_argument('--parent-uid', type=int); ap.add_argument('--parent-gid', type=int)
    ap.add_argument('--ip')
    args = ap.parse_args()
    if args.self_test:
        self_test(); return 0
    try:
        if args.inside:
            child(args); return 0
        if platform.system() != 'Linux' or os.geteuid() == 0:
            raise ValueError('Requires unprivileged Linux caller')
        if os.environ.get('GITHUB_ACTIONS') != 'true':
            raise ValueError('Proposed comparison probe is restricted to GitHub Actions')
        unshare, ip = shutil.which('unshare'), shutil.which('ip')
        if not unshare or not ip: raise ValueError('Required standard isolation tools unavailable')
        net, user = os.readlink('/proc/self/ns/net'), os.readlink('/proc/self/ns/user')
        command = [unshare, '--user', '--map-root-user', '--net', sys.executable, str(Path(__file__).resolve()),
                   '--inside', '--parent-net', net, '--parent-user', user, '--parent-uid', str(os.getuid()),
                   '--parent-gid', str(os.getgid()), '--ip', ip]
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        if result.returncode != 0:
            # Keep only bounded redacted diagnostics, never environment dumps.
            print(json.dumps({'status':'BLOCKED','stage':'namespace probe','exitCode':result.returncode,
                              'parentNetNamespace':net,'parentUserNamespace':user,'fallbackAttempted':False,
                              'diagnostic':safe_diagnostic(result.stderr or result.stdout)}))
            return 2
        receipt = json.loads(result.stdout)
        if receipt.get('status') != 'PASS': raise ValueError('Invalid child receipt')
        if os.readlink('/proc/self/ns/net') != net or os.readlink('/proc/self/ns/user') != user:
            raise ValueError('Parent namespace identity unexpectedly changed')
        receipt['parentNamespaceUnchanged'] = True
        receipt['namespaceReleasedOnExit'] = True
        print(json.dumps(receipt)); return 0
    except Exception as error:
        print(json.dumps({'status':'BLOCKED','errorType':type(error).__name__,'diagnostic':safe_diagnostic(getattr(error, 'stderr', None) or str(error)), 'fallbackAttempted':False}))
        return 2

if __name__ == '__main__':
    raise SystemExit(main())
