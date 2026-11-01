#!/usr/bin/env python3
"""Prove local Player 1 playback through an installed Debian candidate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


APP = Path('/usr/bin/colosseum')
APP_ROOT = Path('/opt/colosseum')
IDENTITY_PATH = APP_ROOT / 'BUILD-IDENTITY.json'
PROOF_PREFIX = 'INSTALLED_PLAYER_PROOF='
APP_TIMEOUT_SECONDS = 110


PLAYER_DRIVER = r'''import QtQuick
import "file:///opt/colosseum/qml" as Installed

Installed.Main {
    id: app
    width: 1280
    height: 720
    property int ticks: 0
    property int stage: 0
    property real firstPosition: -1
    property var playbackEvidence: ({})
    readonly property string fixtureUrl: Qt.application.arguments.length > 2
                                         ? Qt.application.arguments[2] : ""
    readonly property string framePath: Qt.application.arguments.length > 3
                                        ? Qt.application.arguments[3] : ""

    function findItem(item, predicate) {
        if (!item) return null
        if (predicate(item)) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = findItem(children[i], predicate)
            if (found) return found
        }
        return null
    }

    function finish(code, message, evidence) {
        cycle.stop()
        console.info("INSTALLED_PLAYER_PROOF=" + JSON.stringify({
            status: code === 0 ? "PASS" : "FAIL",
            exit_code: code,
            message: message,
            ticks: ticks,
            player: evidence || playbackEvidence
        }))
        Qt.exit(code)
    }

    Timer {
        id: cycle
        interval: 200
        running: true
        repeat: true
        onTriggered: {
            app.ticks++
            if (app.ticks > 500) {
                app.finish(10, "installed Player 1 proof timed out")
                return
            }
            if (app.stage === 0) {
                var splash = app.findItem(app.contentItem,
                                          function(item) { return item.objectName === "bootSplash" })
                if (!splash || splash.visible || AccountController.busy) return
                if (!app.fixtureUrl.length || !app.framePath.length) {
                    app.finish(11, "fixture or frame path was not passed to the QML driver")
                    return
                }
                AccountController.continueWithoutAccount()
                app.showNormal()
                app.width = 1280
                app.height = 720
                var cold = { loaderActive: app.playerLayerRef.active,
                             player2Booted: app.usePlayer2 }
                console.info("INSTALLED_PLAYER_COLD=" + JSON.stringify(cold))
                if (cold.loaderActive || cold.player2Booted) {
                    app.finish(12, "candidate did not start with a cold Player 1 loader", cold)
                    return
                }
                app.openLocalVideoSession({ id: "installed-player-fixture",
                                            path: app.fixtureUrl,
                                            title: "Installed Player 1 fixture",
                                            kind: "movie", position: 0 })
                app.stage = 1
                return
            }
            if (app.stage === 1) {
                var loader = app.playerLayerRef
                if (loader.status === Loader.Error) {
                    app.finish(13, "installed player Loader.Error")
                    return
                }
                if (loader.status !== Loader.Ready || !loader.item) return
                var player = loader.item
                if (player.errored) {
                    app.finish(14, "Player 1 reported a playback error after construction")
                    return
                }
                var width = Number(player.decodedWidth || 0)
                var height = Number(player.decodedHeight || 0)
                if (!player.playerReady || !player.playbackStarted || width <= 0 || height <= 0)
                    return
                var position = Number(player.playbackPosition || 0)
                if (app.firstPosition < 0) {
                    app.firstPosition = position
                    return
                }
                if (position <= app.firstPosition + 0.2) return
                app.playbackEvidence = {
                    loaderReady: loader.status === Loader.Ready,
                    playerReady: player.playerReady === true,
                    playbackStarted: player.playbackStarted === true,
                    firstPosition: app.firstPosition,
                    position: position,
                    decodedWidth: width,
                    decodedHeight: height,
                    duration: Number(player.playbackDuration || 0)
                }
                app.stage = 2
                var accepted = app.contentItem.grabToImage(function(result) {
                    if (!result.saveToFile(app.framePath)) {
                        app.finish(15, "could not save the decoded player frame")
                        return
                    }
                    app.closePlayerSession()
                    app.stage = 3
                })
                if (!accepted) app.finish(16, "could not request the player frame capture")
                return
            }
            if (app.stage === 3 && !app.playerOpen) {
                app.finish(0, "installed Player 1 decoded an advancing local video frame",
                           app.playbackEvidence)
            }
        }
    }
}
'''


def parse_driver_proof(log_text):
    records = []
    for line in log_text.splitlines():
        index = line.find(PROOF_PREFIX)
        if index < 0:
            continue
        try:
            record = json.loads(line[index + len(PROOF_PREFIX):])
        except json.JSONDecodeError as error:
            raise RuntimeError(f'Installed-player proof marker is malformed: {error}') from error
        if not isinstance(record, dict):
            raise RuntimeError('Installed-player proof marker must contain a JSON object')
        records.append(record)
    if len(records) != 1:
        raise RuntimeError(f'Expected exactly one installed-player proof marker, found {len(records)}')
    return records[0]


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def installed_identity():
    if not APP.is_file() or not os.access(APP, os.X_OK):
        raise RuntimeError(f'Installed executable missing: {APP}')
    identity_bytes = IDENTITY_PATH.read_bytes()
    identity = json.loads(identity_bytes)
    version = str(identity.get('version', ''))
    source_commit = str(identity.get('source_commit', ''))
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise RuntimeError('Installed BUILD-IDENTITY.json has no numeric version')
    if not re.fullmatch(r'[0-9a-f]{40}', source_commit):
        raise RuntimeError('Installed BUILD-IDENTITY.json has no full source commit')
    app_version = subprocess.check_output([str(APP), '--version'], text=True,
                                          stderr=subprocess.STDOUT, timeout=10).strip()
    package = subprocess.check_output(['dpkg-query', '-W', '-f=${Version}\t${Architecture}',
                                       'colosseum'], text=True, timeout=10).strip()
    package_version, package_architecture = package.split('\t', 1)
    if app_version != f'Colosseum {version}' or package_version != version:
        raise RuntimeError(f'Installed identity mismatch: app={app_version}, package={package_version}, '
                           f'identity={version}')
    if identity.get('architecture') != 'x86_64' or package_architecture != 'amd64':
        raise RuntimeError(f'Unexpected installed architecture: {identity.get("architecture")}/'
                           f'{package_architecture}')
    return {
        'version': version,
        'source_commit': source_commit,
        'architecture': identity['architecture'],
        'package_architecture': package_architecture,
        'app_version_output': app_version,
        'build_identity_sha256': hashlib.sha256(identity_bytes).hexdigest(),
    }


def clean_environment(profile):
    environment = os.environ.copy()
    for key in list(environment):
        if key.startswith(('QT_', 'QML_', 'QTWEBENGINE', 'COLOSSEUM_')) or key in (
                'LD_LIBRARY_PATH', 'LD_PRELOAD', 'QTDIR', 'QT_HOME'):
            environment.pop(key, None)
    home = profile / 'home'
    home.mkdir(parents=True)
    environment.update({
        'HOME': str(home),
        'XDG_CONFIG_HOME': str(profile / 'config'),
        'XDG_DATA_HOME': str(profile / 'data'),
        'XDG_CACHE_HOME': str(profile / 'cache'),
        'QT_QPA_PLATFORM': 'xcb',
        'QT_FORCE_STDERR_LOGGING': '1',
        'QSG_RHI_BACKEND': 'opengl',
        'LIBGL_ALWAYS_SOFTWARE': '1',
        'COLOSSEUM_APPDATA_TAG': profile.name,
        'COLOSSEUM_DISABLE_BOOT_PREFETCH': '1',
    })
    return environment


def stop_owned_process(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--fixture', required=True, type=Path,
                        help='Absolute path to the checked-in Colosseum.mp4 fixture')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if os.getuid() == 0:
        parser.error('Run the installed application as a normal user')
    if not args.fixture.is_absolute():
        parser.error('--fixture must be an absolute path')
    fixture = args.fixture.resolve(strict=True)
    if not fixture.is_file():
        parser.error('--fixture must name a file')
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output.resolve()
    log_path = output / 'installed-player.log'
    frame_path = output / 'installed-player-frame.png'
    receipt_path = output / 'installed-player-proof.json'
    identity = None
    proof = None
    process = None
    error_message = None
    exit_code = None

    try:
        identity = installed_identity()
        fixture_identity = {'path': str(fixture), 'bytes': fixture.stat().st_size,
                            'sha256': sha256_file(fixture)}
        with tempfile.TemporaryDirectory(prefix='colosseum-installed-player-') as directory:
            profile = Path(directory)
            driver_path = profile / 'PlayerProof.qml'
            driver_path.write_text(PLAYER_DRIVER, encoding='utf-8')
            environment = clean_environment(profile)
            command = [str(APP), str(driver_path), fixture.as_uri(), str(frame_path)]
            started_at = time.monotonic()
            with log_path.open('w', encoding='utf-8') as log:
                process = subprocess.Popen(command, env=environment, stdout=log,
                                           stderr=subprocess.STDOUT)
                try:
                    while process.poll() is None:
                        if time.monotonic() - started_at >= APP_TIMEOUT_SECONDS:
                            raise TimeoutError(f'Installed-player process exceeded {APP_TIMEOUT_SECONDS}s')
                        time.sleep(0.25)
                    exit_code = process.returncode
                finally:
                    stop_owned_process(process)
            proof = parse_driver_proof(log_path.read_text(encoding='utf-8', errors='replace'))
            if exit_code != 0:
                raise RuntimeError(f'Installed-player process exited with {exit_code}: {proof}')
            if proof.get('status') != 'PASS' or proof.get('exit_code') != 0:
                raise RuntimeError(f'QML player proof did not pass: {proof}')
            player = proof.get('player')
            if not isinstance(player, dict):
                raise RuntimeError('QML player proof omitted playback evidence')
            if player.get('loaderReady') is not True or player.get('playerReady') is not True \
                    or player.get('playbackStarted') is not True:
                raise RuntimeError(f'Player readiness gates were not satisfied: {player}')
            if int(player.get('decodedWidth', 0)) <= 0 or int(player.get('decodedHeight', 0)) <= 0:
                raise RuntimeError(f'No decoded video dimensions were reported: {player}')
            if float(player.get('position', 0)) <= float(player.get('firstPosition', 0)) + 0.2:
                raise RuntimeError(f'Playback position did not advance: {player}')
            if not frame_path.is_file() or frame_path.stat().st_size <= 8 \
                    or frame_path.read_bytes()[:8] != b'\x89PNG\r\n\x1a\n':
                raise RuntimeError('Player frame capture is missing or is not a PNG')
            receipt = {
                'status': 'PASS',
                'package_identity': identity,
                'fixture': fixture_identity,
                'process': {'argv': command, 'exit_code': exit_code,
                            'natural_qt_exit': True, 'timeout_seconds': APP_TIMEOUT_SECONDS},
                'player': player,
                'frame': {'path': str(frame_path), 'bytes': frame_path.stat().st_size,
                          'sha256': sha256_file(frame_path)},
                'log': {'path': str(log_path), 'bytes': log_path.stat().st_size},
                'scope': 'Installed Debian package local-file Player 1 decode and advancing playback '
                         'through installed Main.qml under Xvfb; no GPU or live-provider qualification.',
            }
    except Exception as error:
        error_message = str(error)
        if process is not None:
            exit_code = process.poll()
        receipt = {
            'status': 'FAIL',
            'package_identity': identity,
            'process': {'exit_code': exit_code, 'natural_qt_exit': False,
                        'timeout_seconds': APP_TIMEOUT_SECONDS},
            'player': proof,
            'error': error_message,
            'log': {'path': str(log_path), 'bytes': log_path.stat().st_size if log_path.exists() else 0},
        }
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(receipt, sort_keys=True))
    if error_message:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
