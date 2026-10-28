"""Set 2 restart proof through the full application.

Two launches of the real app over ONE disposable profile, headless, driven by
Lanista. The first launch browses in a regular and a private tab, saves a
bookmark and changes a preference, then quits through the app's own Quit
command. The second launch checks what came back.

    python tests/feria_history_probe/run_app_restart.py --exe native/build-msvc/colosseum.exe \
        --lanista native/build-msvc/lanista.exe --qt-bin C:/Qt/6.11.1/msvc2022_64/bin

Pass: every step of both scenarios passes and both launches leave by themselves
with exit code 0. The page is the public MANGA Plus site, opened anonymously.
"""
import argparse, json, os, re, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REGULAR = 'https://mangaplus.shueisha.co.jp/updates?visit=regular'
PRIVATE = 'https://mangaplus.shueisha.co.jp/updates?visit=private'
MATCHED = [{'path': 'matched', 'op': '==', 'value': 'true'}]


def wait_for(label, obj, prop, value, timeout=10000):
    return {'cmd': 'ui-wait-for', 'label': label,
            'payload': {'object': obj, 'prop': prop, 'value': value, 'timeout_ms': timeout}, 'expect': MATCHED}


def click(label, target):
    return {'cmd': 'ui-click', 'label': label, 'payload': {'target': target}}


def key(label, sequence):
    return {'cmd': 'ui-keypress', 'label': label, 'payload': {'key': sequence}}


def pause(label, ms):
    return {'presentation_wait_ms': ms, 'label': label}


def get(label, obj, prop, op, value):
    return {'cmd': 'qml-get', 'label': label, 'payload': {'object': obj, 'props': [prop]},
            'expect': [{'path': 'props.' + prop, 'op': op, 'value': value}]}


def typed(label, target, text):
    return [key(label + ': select what is there', 'Ctrl+A'),
            {'cmd': 'ui-text-input', 'label': label, 'payload': {'target': target, 'text': text}}]


def boot(first):
    steps = [{'cmd': 'ping', 'label': 'isolated app answers',
              'expect': [{'path': 'gates.drive', 'op': '==', 'value': 'true'}]},
             wait_for('boot splash leaves', 'bootSplash', 'visible', False, 60000)]
    if first:
        steps += [wait_for('local entry is available', 'accountWelcomeContinueLocal', 'visible', True),
                  click('continue locally', 'accountWelcomeContinueLocal')]
    return steps + [
        wait_for('shell is ready', 'accountHost', 'visible', False),
        click('open Feria', 'modePill_Feria'),
        wait_for('Feria is active', 'feriaAppPage', 'lifecycleActive', True, 15000),
        key('to tile 2', 'Right'), key('to tile 3', 'Right'), key('to tile 6', 'Down'),
        key('to tile 9', 'Down'), key('to tile 12', 'Down'), key('to tile 11, MANGA Plus', 'Left'),
        pause('focus scrolling settles', 350),
        click('open the MANGA Plus tile', 'feriaApp_mangaplus'),
        wait_for('browser host view is showing', 'feriaAppPage', 'viewState', 'host', 15000),
        wait_for('the page finished loading', 'feriaBrowserHost', 'loadSucceeded', True, 60000)]


def navigate(label, url, marker):
    return [click(label + ': open the address bar', 'feriaBrowserAddressToggle'), pause('address bar opens', 400),
            *typed(label + ': type the address', 'feriaBrowserAddress', url),
            key(label + ': go', 'Return'), pause('navigation starts', 1500),
            wait_for(label + ': page finished loading', 'feriaBrowserHost', 'loadSucceeded', True, 60000),
            get(label + ': address is the one typed', 'feriaBrowserHost', 'currentUrl', 'contains', marker)]


def history_search(label, text, count):
    return [*typed(label, 'feriaBrowserHistorySearch', text),
            wait_for(label + ': rows', 'feriaBrowserHistoryList', 'count', count, 5000)]


QUIT = [key("quit through the app's own Quit command", 'Ctrl+Q'), pause('the application leaves on its own', 12000)]

FIRST = boot(True) + navigate('regular tab', REGULAR, 'visit=regular') + [
    key('open Bookmarks from the keyboard', 'Ctrl+D'),
    wait_for('Bookmarks list is showing', 'feriaBrowserBookmarksList', 'visible', True),
    click('bookmark the current page', 'feriaBrowserBookmarkCurrent'),
    wait_for('the bookmark is listed at once', 'feriaBrowserBookmarksList', 'count', 1, 5000),
    click('open Settings', 'feriaBrowserSettingsSection'),
    get('freezing inactive tabs starts on', 'feriaBrowserFreezeBackgroundPreference', 'checked', '==', 'true'),
    click('turn freezing inactive tabs off', 'feriaBrowserFreezeBackgroundPreference'),
    get('the setting shows off', 'feriaBrowserFreezeBackgroundPreference', 'checked', '==', 'false'),
    click('close the browser menu', 'feriaBrowserMenuDismiss'), pause('menu closes', 400),
    key('open a private tab', 'Ctrl+Shift+N'), pause('private tab opens', 2000),
    wait_for('private tab finished loading', 'feriaBrowserHost', 'loadSucceeded', True, 60000),
] + navigate('private tab', PRIVATE, 'visit=private') + [
    key('open History from the keyboard', 'Ctrl+H'),
    wait_for('History list is showing', 'feriaBrowserHistoryList', 'visible', True),
    get('the menu says this is a private tab', 'feriaBrowserPrivacy', 'text', 'contains', 'Private'),
    *history_search('private page is not in history', 'visit=private', 0),
    *history_search('regular page is in history', 'visit=regular', 1),
    click('close the browser menu again', 'feriaBrowserMenuDismiss'), pause('menu closes', 400),
] + QUIT

SECOND = boot(False) + [
    key('open History from the keyboard', 'Ctrl+H'),
    wait_for('History list is showing', 'feriaBrowserHistoryList', 'visible', True),
    *history_search('regular page survived the restart', 'visit=regular', 1),
    *history_search('private page left nothing', 'visit=private', 0),
    click('open Bookmarks', 'feriaBrowserBookmarksSection'),
    wait_for('the bookmark survived the restart', 'feriaBrowserBookmarksList', 'count', 1, 5000),
    get('it is the page that was saved', 'feriaBrowserBookmark_0', 'text', 'contains', 'MANGA'),
    click('open Settings', 'feriaBrowserSettingsSection'),
    get('the setting is still off', 'feriaBrowserFreezeBackgroundPreference', 'checked', '==', 'false'),
    get('one previous tab is offered: the regular one', 'feriaBrowserRestorePrevious', 'text', '==',
        'Restore previous tabs (1)'),
    click('restore the previous tab', 'feriaBrowserRestorePrevious'), pause('the tab is restored', 2500),
    wait_for('restored page finished loading', 'feriaBrowserHost', 'loadSucceeded', True, 60000),
    get('the restored tab is the regular page', 'feriaBrowserHost', 'currentUrl', 'contains', 'visit=regular'),
    key('open the browser menu', 'Ctrl+H'),
    wait_for('History list is showing again', 'feriaBrowserHistoryList', 'visible', True),
    get('the restored tab is a regular tab', 'feriaBrowserPrivacy', 'text', 'contains', 'Regular'),
    click('open Settings again', 'feriaBrowserSettingsSection'),
    get('nothing is left to restore', 'feriaBrowserRestorePrevious', 'text', '==', 'Restore previous tabs (0)'),
    click('close the browser menu', 'feriaBrowserMenuDismiss'), pause('menu closes', 400),
] + QUIT


def run(args, name, steps, tag, out_dir):
    scenario = out_dir / (name + '.json')
    scenario.write_text(json.dumps({'name': name, 'steps': steps}, indent=1))
    env = dict(os.environ)
    env['PATH'] = str(args.qt_bin) + os.pathsep + env['PATH']
    env['QT_QPA_PLATFORM'] = 'offscreen'   # no window on the desktop, no focus taken
    result = subprocess.run([str(args.lanista), 'session', 'run', str(scenario), '--exe', str(args.exe),
                             '--qml', 'qml/Main.qml', '--drive', '--keep-going', '--tag', tag],
                            cwd=ROOT, env=env, capture_output=True, text=True)
    (out_dir / (name + '.out')).write_text(result.stdout + result.stderr)
    summary = re.search(r'(\d+) steps, (\d+) failed', result.stdout)
    manifest = re.search(r'manifest: (.*)\)', result.stdout)
    state = json.loads((ROOT / manifest.group(1)).read_text()) if manifest else {}
    failed = [line for line in result.stdout.splitlines() if line.startswith(('FAIL', 'INFRA'))]
    clean = state.get('exitCode') == 0 and state.get('crashed') is False and state.get('killReason') == 'graceful'
    passed = bool(summary) and summary.group(2) == '0' and clean
    print('%s %s: %s | exit code %s, crashed %s, %s' % (
        'PASS' if passed else 'FAIL', name, summary.group(0) if summary else 'no summary',
        state.get('exitCode'), state.get('crashed'), state.get('killReason')))
    for line in failed:
        print('   ' + line[:260])
    return passed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--lanista', required=True, type=Path)
    parser.add_argument('--qt-bin', required=True, type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'artifacts' / 'feria-set2-restart')
    args = parser.parse_args()
    args.exe, args.lanista = args.exe.resolve(), args.lanista.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    tag = 'feria-set2-' + time.strftime('%Y%m%d-%H%M%S')
    first = run(args, 'restart-first-launch', FIRST, tag, args.out)
    second = first and run(args, 'restart-second-launch', SECOND, tag, args.out)
    print('RESTART PROOF', 'PASS' if first and second else 'FAIL', '| profile tag', tag)
    return 0 if first and second else 1


if __name__ == '__main__':
    sys.exit(main())
