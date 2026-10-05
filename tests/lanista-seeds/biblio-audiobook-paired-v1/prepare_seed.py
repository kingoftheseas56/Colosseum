"""Prepare a COPY for one known tagged app-data root. Never edits the original seed."""
from pathlib import Path
import argparse,shutil
p=argparse.ArgumentParser();p.add_argument('--app-data-root',required=True);p.add_argument('--output',required=True);a=p.parse_args()
src=Path(__file__).resolve().parent;out=Path(a.output).resolve()
if out.exists():raise SystemExit('Output must be a new directory')
shutil.copytree(src,out)
f=out/'audiobooks/index.json';f.write_text(f.read_text().replace('__APPDATA__',a.app_data_root.replace('\\','/')),encoding='utf-8')
print(out)
