"""Positive/negative fixtures for the task014 isolation evaluator.

Run: python -I test_eval_rep_014.py   (prints PASS lines and a total; exit 1 on failure)
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import eval_rep_014 as ev  # noqa: E402

SESSION = '0x77a2632dfe90'
# Real task013 emulator line shape (logcat threadtime, qInfo-quoted string).
FIELDS = ('isolation_pass=true; session_instance={s}; torrent_count=0; enable_dht=false; '
          'enable_lsd=false; enable_upnp=false; enable_natpmp=false; listen_interfaces=\\"127.0.0.1:0\\"; '
          'dht_bootstrap_nodes=\\"\\"; max_pex_peers=0; live_dht_running=false; live_listening=true; '
          'live_listen_port=46431; libtorrent=2.0.12.0')


def verify(fields=None, session=SESSION, verdict='PASS', t='19:18:37.507'):
    body = (fields or FIELDS).format(s=SESSION)
    return f'10-20 {t}  8462  8645 I default : "[nuvio011] isolation-verify {verdict}; session={session}; {body}"'


def admission(fields=None, verdict='PASS', t='19:19:00.000', s=SESSION):
    body = (fields or FIELDS).format(s=s)
    return f'10-20 {t}  8462  8645 I default : [nuvio011] admission {verdict} "{body}"'


def sub(old, new):
    assert old in FIELDS, old
    return FIELDS.replace(old, new)


def accepted(lines):
    return ev.evaluate_isolation(lines)['accepted']


CASES = []


def case(fn):
    CASES.append(fn)
    return fn


@case
def positive_verify_then_admission():
    r = ev.evaluate_isolation([verify(), admission()])
    assert r['accepted'], r['problems']
    f = r['verifies'][0]['fields']
    # Full typed tokens: false is False, never the truncated 'fa'.
    assert f['enable_dht'] is False and f['live_dht_running'] is False
    assert f['torrent_count'] == 0 and f['max_pex_peers'] == 0


@case
def positive_new_session_reverified_after_foreground():
    s2 = '0x77a26339f8c0'
    lines = [verify(), admission(),
             verify(session=s2, t='19:30:00.000').replace(SESSION, s2),
             admission(s=s2, t='19:30:01.000')]
    assert accepted(lines), ev.evaluate_isolation(lines)['problems']


@case
def negative_each_unsafe_boolean():
    for key in ('enable_dht', 'enable_lsd', 'enable_upnp', 'enable_natpmp', 'live_dht_running'):
        assert not accepted([verify(sub(f'{key}=false', f'{key}=true')), admission()]), key


@case
def negative_truncated_or_malformed_values():
    for bad in ('enable_dht=fa', 'enable_dht=False', 'enable_dht=0', 'enable_dht=', 'enable_dht=false1'):
        assert not accepted([verify(sub('enable_dht=false', bad)), admission()]), bad
    assert not accepted([verify(sub('torrent_count=0', 'torrent_count=0x0')), admission()])
    assert not accepted([verify(sub('max_pex_peers=0', 'max_pex_peers=-1')), admission()])


@case
def negative_missing_field():
    for key in ev.REQUIRED:
        fields = '; '.join(t for t in FIELDS.split('; ') if not t.startswith(key + '='))
        assert not accepted([verify(fields), admission()]), key


@case
def negative_duplicate_field():
    assert not accepted([verify(FIELDS + '; enable_dht=true'), admission()])


@case
def negative_nonzero_initial_torrent_count():
    assert not accepted([verify(sub('torrent_count=0', 'torrent_count=1')), admission()])
    assert not accepted([verify(), admission(sub('torrent_count=0', 'torrent_count=2'))])


@case
def negative_pex_listen_bootstrap():
    assert not accepted([verify(sub('max_pex_peers=0', 'max_pex_peers=50')), admission()])
    assert not accepted([verify(sub('127.0.0.1:0', '0.0.0.0:6881')), admission()])
    assert not accepted([verify(sub('dht_bootstrap_nodes=\\"\\"',
                                    'dht_bootstrap_nodes=\\"dht.example:6881\\"')), admission()])


@case
def negative_isolation_pass_false_even_with_pass_phrase():
    # The PASS phrase alone never implies the settings.
    assert not accepted([verify(sub('isolation_pass=true', 'isolation_pass=false')), admission()])


@case
def negative_pass_phrase_without_settings():
    line = f'10-20 19:18:37.507 I default : "[nuvio011] isolation-verify PASS; session={SESSION}; "'
    assert not accepted([line, admission()])


@case
def negative_fail_verdict():
    assert not accepted([verify(verdict='FAIL'), admission()])


@case
def negative_absent_or_wrong_session():
    assert not accepted([verify(session=''), admission()])
    assert not accepted([verify(session='0xdeadbeef'), admission()])
    assert not accepted([verify(), admission(s='0x1234')])
    s2 = '0x77a26339f8c0'
    # New session admitted without its own verify.
    assert not accepted([verify(), admission(), admission(s=s2, t='19:30:01.000')])


@case
def negative_admission_before_verify_or_missing():
    assert not accepted([admission(t='19:00:00.000'), verify()])
    assert not accepted([verify()])
    assert not accepted([admission()])
    assert not accepted([])


@case
def negative_denied_admission():
    assert not accepted([verify(), admission(verdict='DENIED')])


if __name__ == '__main__':
    failed = 0
    for fn in CASES:
        try:
            fn()
            print('PASS', fn.__name__)
        except AssertionError as error:
            failed += 1
            print('FAIL', fn.__name__, error)
    print(f'EXECUTED_CASES={len(CASES)} FAILED={failed} SKIPPED=0')
    sys.exit(1 if failed else 0)
