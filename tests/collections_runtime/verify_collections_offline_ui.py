"""Run only inside the candidate-only outbound-block wrapper; never changes network settings."""
import argparse,json,traceback
from pathlib import Path
from types import SimpleNamespace
from verify_collections_app import Acceptance,row,state,sha,require

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ("root","evidence","cli"):p.add_argument("--"+n,required=True,type=Path)
    p.add_argument("--pipe",required=True);a=p.parse_args()
    require((a.evidence/"firewall-active.json").is_file(),"Outbound-block receipt is required")
    t=Acceptance(SimpleNamespace(root=a.root,evidence=a.evidence,cli=a.cli,pipe=a.pipe,
        store=a.root/"profiles/local/named-collections-v2.json",mode="outbound-blocked",primary="Across worlds",timeout=45,route=None))
    try:
        primary=row(state(t.store),"Across worlds");ref=next(i["reference"] for i in primary["items"] if i["reference"]["route"]=="book-detail")
        t.overview();cid=t.create_empty("Offline local proof")
        add=t.title(primary["id"],ref);t.picker(add);t.toggle_membership(cid);t.click("pickerDone");t.prop("collectionsPanel","pickerActive",False)
        t.collection(cid);t.click("editCollectionButton");t.fill("collectionNameInput","Offline renamed proof")
        t.fill("collectionDescriptionInput","Saved while this candidate executable could not make outbound network connections.")
        t.fill("noteInput_"+ref["key"],"Offline note persisted by the real native owner.")
        t.click("saveCollectionButton");t.prop("collectionsPanel","pendingChanges",False)
        saved=row(state(t.store),cid);require(saved["name"]=="Offline renamed proof" and saved["items"][0]["note"]=="Offline note persisted by the real native owner.","Offline edit did not persist")
        t.screenshot("offline-saved-collection");t.delete(cid);require(sha(t.store)==t.initial_hash,"Offline cleanup did not restore original collection bytes")
        t.check("candidate outbound blocked: create, exact cached-title membership, rename/description/note Save, browse and delete")
        t.finish()
    except Exception as e:
        t.report.update(status="failed",error=str(e),traceback=traceback.format_exc())
        try:t.screenshot("offline-failure")
        except Exception as capture:t.report["captureError"]=str(capture)
    finally:(a.evidence/"offline-acceptance.json").write_text(json.dumps(t.report,indent=2),encoding="utf-8")
    print(json.dumps({"status":t.report["status"],"report":str(a.evidence/"offline-acceptance.json")}))
    return 0 if t.report["status"]=="passed" else 1
if __name__=="__main__":raise SystemExit(main())
