from pathlib import Path
import re,json
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
out=[]
for job,var in [('07-biblio-home','--font-scale'),('08-theatre-discover','--fs'),('09-catalogue-index','--font-scale'),('15-world-home','--font-scale'),('16-libraries','--s'),('20-search-browse','--font-scale')]:
 p=B/'finished-mocks'/job/'style.css';s=p.read_text(encoding='utf-8')
 s,n=re.subn(r'(font-size\s*:\s*)(\d+(?:\.\d+)?)px\b',lambda m:m[1]+'calc('+m[2]+'px * var('+var+',1))',s)
 count=[0]
 def shorthand(m):
  v=m[2]
  if 'calc(' in v or 'var(' in v and not re.search(r'\dpx',v):return m[0]
  v,k=re.subn(r'(\d+(?:\.\d+)?)px\b',lambda z:'calc('+z[1]+'px * var('+var+',1))',v);count[0]+=k
  return m[1]+v
 s=re.sub(r'(\bfont\s*:\s*)([^;{}]+)',shorthand,s)
 p.write_text(s,encoding='utf-8');out.append({'job':job,'fontSizeDeclarations':n,'shorthandDeclarations':count[0]})
(B/'evidence/font-scale-normalization.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
print(json.dumps(out))
