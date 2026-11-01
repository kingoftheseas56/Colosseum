from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import sys,json
B=Path(r"D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries")
rev=sys.argv[1] if len(sys.argv)>1 else "before"
font=ImageFont.truetype(r"C:\Windows\Fonts\segoeui.ttf",18)
for d in sorted((B/"evidence"/rev).glob("*")):
    if not d.is_dir(): continue
    canvas=Image.new("RGB",(1540,1050),"#171921"); draw=ImageDraw.Draw(canvas)
    draw.text((20,10),d.name+" | "+rev+" | HTML capture; approved QML is source authority",font=font,fill="white")
    for tag,xy,maxsize in [("portrait",(20,75),(412,900)),("landscape",(470,75),(1050,480)),("desktop",(470,575),(1050,460))]:
        p=d/(tag+".png")
        if p.exists():
            im=Image.open(p).convert("RGB"); im.thumbnail(maxsize)
            canvas.paste(im,xy); draw.text((xy[0],xy[1]-26),tag,font=font,fill="#f0c44a")
    out=B/"contact-sheets"/(d.name+"-"+rev+".jpg"); canvas.save(out,quality=90)
print("CONTACT SHEETS",rev,len(list((B/"contact-sheets").glob("*-"+rev+".jpg"))))
