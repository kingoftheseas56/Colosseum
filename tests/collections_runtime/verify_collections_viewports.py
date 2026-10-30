"""Verify the three integrated surfaces using actual native-DPI window resizing."""
import argparse, json, subprocess, sys, traceback
from types import SimpleNamespace
from pathlib import Path
from verify_collections_app import Acceptance, row, state, sha, require

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ("root","evidence","cli"): p.add_argument("--"+n,required=True,type=Path)
    p.add_argument("--pipe",required=True)
    a=p.parse_args(); a.evidence.mkdir(parents=True,exist_ok=True)
    t=Acceptance(SimpleNamespace(root=a.root,evidence=a.evidence,cli=a.cli,pipe=a.pipe,
        store=a.root/"profiles/local/named-collections-v2.json",mode="viewports",primary="Across worlds",timeout=45,route=None))
    def resize(width,height,label):
        cmd=[sys.executable,"-B",str(Path(__file__).with_name("candidate_window.py")),"--cli",str(a.cli),"--pipe",a.pipe,
             "--evidence",str(a.evidence/label),"--action","resize","--width",str(width),"--height",str(height)]
        subprocess.run(cmd,check=True,timeout=30)
        observed=t.client.call("get-state")["windows"][0]
        require(observed["width"]==width and observed["height"]==height,"Actual window did not take requested logical size")
    try:
        primary=row(state(t.store),"Across worlds")
        book=next(i["reference"] for i in primary["items"] if i["reference"]["route"]=="book-detail")
        curated=row(state(t.store),"Lifecycle persistence proof")
        resize(1280,672,"wide")
        t.overview(); t.screenshot("01-integrated-overview")
        t.collection(curated["id"]); t.screenshot("02-integrated-collection")
        add=t.title(primary["id"],book); t.picker(add); t.screenshot("03-integrated-picker"); t.dismiss_picker(add)
        t.overview(); resize(680,640,"narrow")
        t.screenshot("04-narrow-overview")
        t.collection(curated["id"]); t.screenshot("05-narrow-collection")
        t.click("editCollectionButton")
        t.fill("collectionNameInput","Unsaved narrow-window edit")
        t.click("cancelCollectionEdit");t.prop("collectionsPanel","pendingChanges",False)
        require(sha(t.store)==t.initial_hash,"Narrow Cancel changed persisted data")
        resize(1280,672,"wide-before-picker")
        add=t.title(primary["id"],book);t.picker(add)
        resize(680,640,"narrow-picker")
        t.screenshot("06-narrow-picker")
        t.click("pickerCancel"); t.prop("collectionsPanel","pickerActive",False)
        require(sha(t.store)==t.initial_hash,"Narrow picker Cancel changed data")
        t.check("all three real surfaces at 1280x672 and 680x640 logical pixels, native monitor DPI; actual edit/picker Cancel")
        t.finish()
    except Exception as e:
        t.report.update(status="failed",error=str(e),traceback=traceback.format_exc())
        try:t.screenshot("viewport-failure")
        except Exception as capture:t.report["captureError"]=str(capture)
    finally:
        resize(1280,672,"restored")
        (a.evidence/"viewport-acceptance.json").write_text(json.dumps(t.report,indent=2),encoding="utf-8")
    print(json.dumps({"status":t.report["status"],"report":str(a.evidence/"viewport-acceptance.json")}))
    return 0 if t.report["status"]=="passed" else 1
if __name__=="__main__":raise SystemExit(main())
