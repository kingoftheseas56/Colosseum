"""Temporarily block only the matched candidate executable, run UI checks, remove rule."""
import argparse,json,subprocess,sys,uuid
from pathlib import Path
from drive_app import Candidate
from verify_collections_app import fixture_root,require

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ("root","evidence","cli"):p.add_argument("--"+n,required=True,type=Path)
    p.add_argument("--pipe",required=True)
    p.add_argument("--launch-receipt",type=Path,help="Exact receipt for the currently running candidate.")
    a=p.parse_args();fixture_root(a.root)
    a.evidence.mkdir(parents=True,exist_ok=True)
    receipt_path=a.launch_receipt or a.evidence.parent/"launch-app-final.json"
    receipt=json.loads(receipt_path.read_text(encoding="utf-8"))
    exe=(a.cli.parent/"colosseum.exe").resolve()
    require(Path(receipt["exe"]).resolve()==exe,"Only the exact candidate executable is authorized")
    require(Candidate(a.cli,a.pipe,a.evidence/"identity").call("ping")["pid"]==receipt["pid"],"Candidate process changed")
    name="Colosseum-Collections-Offline-"+uuid.uuid4().hex
    def ps(label,code):
        r=subprocess.run(["powershell.exe","-NoProfile","-Command","$ErrorActionPreference='Stop';"+code],capture_output=True,timeout=35)
        (a.evidence/(label+".stdout.txt")).write_bytes(r.stdout)
        (a.evidence/(label+".stderr.txt")).write_bytes(r.stderr)
        require(r.returncode==0,label+": "+r.stderr.decode(errors="replace"))
        return r.stdout.decode("utf-8-sig")
    code="""$profiles=@(Get-NetFirewallProfile -PolicyStore ActiveStore);
if(@($profiles|Where-Object {$_.Enabled -ne 'True'}).Count){throw 'Firewall profile disabled'};
New-NetFirewallRule -Name 'RULE' -DisplayName 'RULE' -Direction Outbound -Action Block -Program 'EXE' -Profile Any -Enabled True | Out-Null;
$r=Get-NetFirewallRule -Name 'RULE' -PolicyStore ActiveStore;
$f=$r|Get-NetFirewallApplicationFilter;
if($r.Enabled -ne 'True' -or $r.Action -ne 'Block' -or $r.Direction -ne 'Outbound' -or $f.Program -ne 'EXE'){throw 'Rule not effective'};
[ordered]@{name=$r.Name;enabled=[string]$r.Enabled;action=[string]$r.Action;direction=[string]$r.Direction;program=$f.Program;profiles=@($profiles|Select-Object Name,Enabled)}|ConvertTo-Json -Depth 5 -Compress
""".replace("RULE",name).replace("EXE",str(exe).replace("'","''"))
    result=1
    try:
        active=json.loads(ps("firewall-create",code))
        (a.evidence/"firewall-active.json").write_text(json.dumps(active,indent=2),encoding="utf-8")
        command=[sys.executable,"-B",str(Path(__file__).with_name("verify_collections_offline_ui.py")),"--root",str(a.root),"--evidence",str(a.evidence),"--cli",str(a.cli),"--pipe",a.pipe]
        result=subprocess.run(command,timeout=300).returncode
    finally:
        cleanup=ps("firewall-remove","Get-NetFirewallRule -Name '"+name+"' -ErrorAction SilentlyContinue | Remove-NetFirewallRule; if(Get-NetFirewallRule -Name '"+name+"' -ErrorAction SilentlyContinue){throw 'Temporary rule remains'}; '{\"removed\":true}'")
        (a.evidence/"firewall-removed.json").write_text(cleanup,encoding="utf-8")
    return result
if __name__=="__main__":raise SystemExit(main())
