from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
font=ImageFont.truetype(r'C:\Windows\Fonts\segoeui.ttf',17)
for d in sorted((B/'evidence/after').iterdir()):
 if not d.is_dir():continue
 canvas=Image.new('RGB',(1580,1170),'#171921');dr=ImageDraw.Draw(canvas);dr.text((18,10),d.name+' | stress + tablet captures | 1.3x text, 24px top/bottom for stress',font=font,fill='white')
 for tag,xy,size in [('stress-portrait',(18,65),(354,792)),('stress-landscape',(400,65),(792,354)),('stress-wide-landscape',(400,465),(900,412)),('tablet-portrait',(1315,65),(240,384)),('tablet-landscape',(1030,920),(520,230))]:
  p=d/(tag+'.png')
  if p.exists():
   im=Image.open(p).convert('RGB');im.thumbnail(size);canvas.paste(im,xy);dr.text((xy[0],xy[1]-24),tag,font=font,fill='#f0c44a')
 canvas.save(B/'contact-sheets'/(d.name+'-stress.jpg'),quality=90)
print('16 stress/tablet sheets saved.')
