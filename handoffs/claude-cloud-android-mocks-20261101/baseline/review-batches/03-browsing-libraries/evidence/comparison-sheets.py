from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
M=json.loads((B/'INPUTS.json').read_text(encoding='utf-8-sig'));font=ImageFont.truetype(r'C:\Windows\Fonts\segoeui.ttf',20);small=ImageFont.truetype(r'C:\Windows\Fonts\segoeui.ttf',17)
def paste(canvas,dr,p,xy,size,label):
 if not p.exists():return
 im=Image.open(p).convert('RGB');im.thumbnail(size);canvas.paste(im,xy);dr.text((xy[0],xy[1]-27),label,font=font,fill='#f0c44a')
for job in M['jobs']:
 for name in job['pages']:
  stem=Path(name).stem;canvas=Image.new('RGB',(2080,1600),'#171921');dr=ImageDraw.Draw(canvas)
  dr.text((20,8),stem+' | preserved worker HTML vs finished HTML | approved QML is the separate source authority',font=font,fill='white')
  for rev,x in [('before',20),('after',454)]:paste(canvas,dr,B/'evidence'/rev/stem/'portrait.png',(x,72),(412,900),('Original' if rev=='before' else 'Finished')+' · 412x900')
  for rev,y in [('before',72),('after',536)]:paste(canvas,dr,B/'evidence'/rev/stem/'landscape.png',(930,y),(1120,412),('Original' if rev=='before' else 'Finished')+' · 900x412')
  for rev,x in [('before',20),('after',1060)]:paste(canvas,dr,B/'evidence'/rev/stem/'desktop.png',(x,1030),(1000,547),('Original' if rev=='before' else 'Finished')+' · 1280x700')
  canvas.save(B/'contact-sheets'/(stem+'-comparison.jpg'),quality=92)
pairs=[('biblio-index-toggle','biblio-home/index-open.png','biblio-home/index-open.png'),('library-confirmation','theatre-library/confirm-short-stress.png','theatre-library/confirm-short-stress.png'),('search-keyboard','search/keyboard-short-stress.png','search/keyboard-short-stress.png'),('theatre-complete-context','theatre-discover/complete-world-context.png','theatre-discover/complete-world-context.png')]
for label,before,after in pairs:
 p=B/'evidence/before'/before;q=B/'evidence/after'/after
 if not p.exists() or not q.exists():continue
 a=Image.open(p);b=Image.open(q);wide=max(a.width,b.width)>600;W=1900 if wide else 1000;H=700 if wide else 1050
 c=Image.new('RGB',(W,H),'#171921');d=ImageDraw.Draw(c);d.text((20,12),label+' | HTML evidence; actual sizes shown below',font=font,fill='white')
 for rev,img,file,x in [('Original',a,p,20),('Finished',b,q,W//2+10)]:paste(c,d,file,(x,82),(W//2-40,H-102),rev+' · '+str(img.width)+'x'+str(img.height))
 c.save(B/'contact-sheets'/('issue-'+label+'.jpg'),quality=92)
print('Before/after comparison sheets saved.')
