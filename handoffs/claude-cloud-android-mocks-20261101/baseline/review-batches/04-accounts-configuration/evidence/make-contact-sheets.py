from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json, datetime
batch=Path(__file__).resolve().parent.parent
root=batch.parent.parent
fontpath=root/"reference/local-fonts/segoeui.ttf"
boldpath=root/"reference/local-fonts/seguisb.ttf"
font=ImageFont.truetype(str(fontpath),20)
small=ImageFont.truetype(str(fontpath),16)
title=ImageFont.truetype(str(boldpath),28)
out=batch/"contact-sheets";out.mkdir(exist_ok=True)
inspect=batch/"evidence/visual-inspection";inspect.mkdir(exist_ok=True)
pages=json.loads((batch/"INPUTS.json").read_text(encoding="utf-8-sig"))["jobs"]
entries=[]
def fitted(image,maxw,maxh):
    image=image.convert("RGB")
    ratio=min(1,maxw/image.width,maxh/image.height)
    return image.resize((round(image.width*ratio),round(image.height*ratio)),Image.Resampling.LANCZOS)
for job in pages:
 for file in job["pages"]:
    name=Path(file).stem
    before=batch/"evidence/before"/name
    after=batch/"evidence/after"/name
    if not all((x/(kind+".png")).exists() for x in [before,after] for kind in ["portrait","landscape","desktop"]):
        print("MISSING",name);continue
    sheet=Image.new("RGB",(1840,2100),"#11151c");d=ImageDraw.Draw(sheet)
    d.text((20,14),name.replace("-"," ").title(),font=title,fill="#f7f7f5")
    d.text((20,52),"LEFT  Original worker HTML",font=font,fill="#d5c6a0")
    d.text((940,52),"RIGHT  Corrected Batch 4 HTML",font=font,fill="#f0c44a")
    d.text((20,80),"Fresh local renders; frozen desktop QML is the design authority. These are not native app screenshots.",font=small,fill="#b7bdc9")
    y=120
    for kind,label,height in [("portrait","412 x 900 portrait",900),("landscape","900 x 412 landscape",412),("desktop","1280 x 700 desktop comparison",493)]:
        for i,folder in enumerate([before,after]):
            x=20+i*920;d.text((x,y),label,font=small,fill="#c9c8d0")
            im=fitted(Image.open(folder/(kind+".png")),900,height)
            sheet.paste(im,(x+(900-im.width)//2,y+25))
        y+=height+45
    dest=out/(name+"-before-after.png");sheet.save(dest,optimize=True)
    view=Image.new("RGB",(1348,1030),"#11151c");v=ImageDraw.Draw(view)
    v.text((16,10),name+" | corrected view",font=title,fill="#f7f7f5")
    v.text((16,50),"Portrait",font=small,fill="#c9c8d0")
    view.paste(Image.open(after/"portrait.png").convert("RGB"),(16,78))
    v.text((440,50),"Landscape",font=small,fill="#c9c8d0")
    view.paste(fitted(Image.open(after/"landscape.png"),900,412),(440,78))
    v.text((440,505),"Desktop 1280 x 700 (scaled)",font=small,fill="#c9c8d0")
    view.paste(fitted(Image.open(after/"desktop.png"),900,492),(440,534))
    view.save(inspect/(name+".png"),optimize=True)
    entries.append({"page":name,"sheet":dest.relative_to(batch).as_posix(),"inputs":[str((x/(kind+".png")).relative_to(batch)).replace("\\","/") for x in [before,after] for kind in ["portrait","landscape","desktop"]]})
(out/"MANIFEST.json").write_text(json.dumps({"generatedUtc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"label":"Original worker HTML versus corrected HTML; not native app captures","sheets":entries},indent=2)+"\n",encoding="utf-8")
print("Created",len(entries),"comparison sheets and",len(entries),"inspection sheets")
# Expanded-state overview sheets. Every tile names the corresponding audit case.
state_entries=[]
state_root=batch/"evidence/after/states"
for folder in sorted(state_root.iterdir()):
    audit=folder/"audit.json"
    if not audit.exists(): continue
    data=json.loads(audit.read_text(encoding="utf-8"))
    cases=data["cases"]
    ids=list(dict.fromkeys(c["id"] for c in cases))
    chosen=[]
    for ident in [ids[0], ids[-1]]:
        for kind in ["portrait","landscape","short"]:
            match=next((c for c in cases if c["id"]==ident and c["kind"]==kind),None)
            if match:chosen.append(match)
    canvas=Image.new("RGB",(1480,1240),"#11151c");draw=ImageDraw.Draw(canvas)
    draw.text((20,15),folder.name+" | expanded states",font=title,fill="#f7f7f5")
    draw.text((20,55),"Corrected HTML; short = 792 x 354, 1.3x text and 24 px top/bottom insets. Local fixtures only.",font=small,fill="#b7bdc9")
    for i,c in enumerate(chosen):
        x=20+(i%3)*490;y=100+(i//3)*565
        draw.text((x,y),c["id"]+" / "+c["kind"],font=font,fill="#f0c44a")
        im=fitted(Image.open(batch/c["screenshot"]),460,500)
        canvas.paste(im,(x+(460-im.width)//2,y+34))
    dest=out/(folder.name+"-states.png");canvas.save(dest,optimize=True)
    state_entries.append({"page":folder.name,"sheet":dest.relative_to(batch).as_posix(),"inputs":[c["screenshot"] for c in chosen]})
manifest=json.loads((out/"MANIFEST.json").read_text(encoding="utf-8"))
manifest["stateSheets"]=state_entries
(out/"MANIFEST.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
print("Created",len(state_entries),"expanded-state sheets")
