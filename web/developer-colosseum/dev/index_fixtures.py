"""Write fixtures/index.json listing every recorder file in fixtures/ (CONTRACT §6).

Copy the recorder output (COLOSSEUM_WEBUI_RECORD=<dir>) into web/developer-colosseum/fixtures/, then run:
    python web/developer-colosseum/dev/index_fixtures.py
fixtures/ is git-ignored: recordings are Hemanth's personal library data.
"""
import json
import pathlib

FIX = pathlib.Path(__file__).resolve().parent.parent / 'fixtures'
files = sorted(p.name for p in FIX.glob('*.json') if p.name not in ('index.json', 'shell.json'))
(FIX / 'index.json').write_text(json.dumps(files, indent=1), encoding='utf-8')
print(f'{len(files)} recordings indexed in {FIX / "index.json"}')
