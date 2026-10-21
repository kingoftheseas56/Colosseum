"""Task014 corrected repetition evaluator (supersedes task013 eval_rep.py).

task013's eval_rep.py matched setting values with ``[0-9a-fx]+|true|false``;
the hex branch wins first, so ``false`` was recorded as ``fa``, and its
``isolation_pass`` field was inferred from the phrase ``isolation-verify PASS``
alone. The original script and its committed summaries are kept unchanged as
Cloud evidence; this version replaces only the isolation evaluation.

Isolation is accepted only when every required readback field is present
exactly once as a complete typed token and holds its safe value, the initial
torrent count is zero, and the verified session identity is present and is the
same session every admission ran against.

Usage: python -I eval_rep_014.py <rep-dir> [<rep-dir> ...]
"""
import json
import pathlib
import re
import sys

# Readback fields are ``key=value`` tokens separated by ``;``.
_KEY = re.compile(r'[a-z_]+\Z')
_VERIFY = re.compile(r'isolation-verify (PASS|FAIL); session=([^;]*);(.*)$')
_ADMISSION = re.compile(r'\[nuvio011\] admission (PASS|DENIED) (.*)$')

_BOOL = re.compile(r'(true|false)\Z')
_UINT = re.compile(r'(0|[1-9][0-9]*)\Z')
_POINTER = re.compile(r'0x[0-9a-f]+\Z')

# field -> (type, required safe value)
REQUIRED = {
    'isolation_pass': ('bool', True),
    'session_instance': ('pointer', None),
    'torrent_count': ('uint', 0),
    'enable_dht': ('bool', False),
    'enable_lsd': ('bool', False),
    'enable_upnp': ('bool', False),
    'enable_natpmp': ('bool', False),
    'listen_interfaces': ('quoted', '127.0.0.1:0'),
    'dht_bootstrap_nodes': ('quoted', ''),
    'max_pex_peers': ('uint', 0),
    'live_dht_running': ('bool', False),
    'live_listening': ('bool', True),
}


def _typed(kind, raw):
    if kind == 'bool':
        m = _BOOL.match(raw)
        return (m.group(1) == 'true') if m else None
    if kind == 'uint':
        return int(raw) if _UINT.match(raw) else None
    if kind == 'pointer':
        return raw if _POINTER.match(raw) else None
    if kind == 'quoted':
        m = re.fullmatch(r'"([^"]*)"', raw)
        return m.group(1) if m else None
    return None


def parse_readback(text):
    """Parse a session diagnostics readback into typed fields.

    Returns (fields, problems). Missing, duplicated or malformed required
    fields are problems; nothing is defaulted.
    """
    seen = {}
    problems = []
    # logcat shows qInfo strings quoted, with inner quotes escaped. Remove
    # only that outer wrapper; a quote inside a value stays and must type-check.
    body = text.strip().replace('\\"', '"')
    if re.match(r'"[a-z_]+=', body):
        body = body[1:]
    last = body.rsplit(';', 1)[-1]
    if body.endswith('"') and last.count('"') % 2:
        body = body[:-1]
    for token in body.split(';'):
        token = token.strip()
        if not token:
            continue
        key, eq, raw = token.partition('=')
        if not eq or not _KEY.match(key):
            problems.append(f'malformed token {token!r}')
            continue
        if key in seen:
            problems.append(f'duplicate field {key}')
        seen[key] = raw
    fields = {}
    for key, (kind, _) in REQUIRED.items():
        if key not in seen:
            problems.append(f'missing field {key}')
            continue
        value = _typed(kind, seen[key])
        if value is None:
            problems.append(f'malformed {kind} field {key}={seen[key]!r}')
            continue
        fields[key] = value
    return fields, problems


def _check_verify(line):
    """One live isolation-verify readback -> (session, fields, problems)."""
    m = _VERIFY.search(line)
    verdict, header_session, body = m.group(1), m.group(2).strip(), m.group(3)
    fields, problems = parse_readback(body)
    if verdict != 'PASS':
        problems.append('native verdict is FAIL')
    for key, (_, safe) in REQUIRED.items():
        if safe is not None and key in fields and fields[key] != safe:
            problems.append(f'unsafe {key}={fields[key]!r}')
    session = fields.get('session_instance')
    if not _POINTER.match(header_session):
        problems.append(f'absent/malformed verified session {header_session!r}')
    elif session is not None and header_session != session:
        problems.append(f'verified session {header_session} != readback {session}')
    return (session if not problems else None), fields, problems


def evaluate_isolation(lines):
    """Acceptance predicate over the chronological verify/admission lines.

    Every verify readback must be complete and safe. Every admission must
    itself read back an empty, safe session that is exactly the session most
    recently verified before it (a background/foreground cycle creates and
    re-verifies a new session). At least one verify and one admission must
    exist, and the first admission may not precede the first verify.
    """
    result = {'accepted': False, 'problems': [], 'verifies': [], 'admissions': 0}
    verified = None
    for line in lines:
        if _VERIFY.search(line):
            session, fields, problems = _check_verify(line)
            result['verifies'].append({'session': fields.get('session_instance'),
                                       'fields': fields, 'problems': problems})
            result['problems'].extend(problems)
            verified = session
            continue
        a = _ADMISSION.search(line)
        if not a:
            continue
        result['admissions'] += 1
        n = result['admissions']
        fields, problems = parse_readback(a.group(2))
        result['problems'].extend(f'admission {n}: {p}' for p in problems)
        if a.group(1) != 'PASS':
            result['problems'].append(f'admission {n}: DENIED')
        for key, (_, safe) in REQUIRED.items():
            if safe is not None and key in fields and fields[key] != safe:
                result['problems'].append(f'admission {n}: unsafe {key}={fields[key]!r}')
        if verified is None or fields.get('session_instance') != verified:
            result['problems'].append(
                f'admission {n}: session {fields.get("session_instance")!r} is not the verified session {verified!r}')
    if not result['verifies']:
        result['problems'].append('no isolation-verify readback line')
    if not result['admissions']:
        result['problems'].append('no fixture admission line')
    result['accepted'] = not result['problems']
    return result


def _timeline(rep):
    """Verify/admission lines from all logcat captures, deduplicated, in time order."""
    seen, lines = set(), []
    for path in sorted(rep.glob('logcat-*.txt')):
        for line in path.read_text(errors='replace').splitlines():
            if ('isolation-verify' in line or '[nuvio011] admission' in line) and line not in seen:
                seen.add(line)
                lines.append(line)
    # threadtime lines start with "MM-DD HH:MM:SS.mmm"; stable sort keeps
    # same-timestamp lines in capture order.
    return sorted(lines, key=lambda l: l[:18])


def evaluate_rep(rep):
    rep = pathlib.Path(rep)
    r = {'rep': rep.name, 'evaluator': 'task014-eval_rep_014'}
    r['isolation'] = evaluate_isolation(_timeline(rep))
    r['isolation_pass'] = r['isolation']['accepted']
    return r


if __name__ == '__main__':
    out = [evaluate_rep(a) for a in sys.argv[1:]]
    print(json.dumps(out, indent=1))
    sys.exit(0 if out and all(r['isolation_pass'] for r in out) else 1)
