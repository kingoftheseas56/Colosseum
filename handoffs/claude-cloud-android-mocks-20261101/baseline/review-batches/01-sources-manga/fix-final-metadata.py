from pathlib import Path
import shutil
b=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\01-sources-manga')
shutil.copyfile(b/'evidence/manga-metadata-checks.json',b/'evidence/manga-unknown-metadata-before.json')
shutil.copyfile(b/'captures/finished-manga-sources-unknown-metadata.png',b/'captures/manga-unknown-metadata-before.png')
p=b/'finished-mocks/03-manga-sources/app.js';s=p.read_text(encoding='utf-8')
start=s.index('    meta.append(make("span","",fmtSize(row.sizeBytes)));')
end=s.index('    body.append(meta);',start)
new='''    const size=fmtSize(row.sizeBytes),raw=row.seeders,n=Number(raw),known=(typeof raw==="number"||(typeof raw==="string"&&raw.trim().length>0))&&Number.isFinite(n)&&n>=0;
    if(size)meta.append(make("span","",size));
    if(known){if(size)meta.append(make("span","meta-bullet","·"));meta.append(make("span","","👤 "+n));}
'''
s=s[:start]+new+s[end:];p.write_text(s,encoding='utf-8')
p=b/'finished-mocks/04-manga-series/app.js';s=p.read_text(encoding='utf-8');start=s.index(' const title=',s.index('function chapterRow'));end=s.index(',status=',start);s=s[:start]+' const title=(s.fixture==="long"?chapterNames[4]:(chapterNames[n-1]||""))'+s[end:];p.write_text(s,encoding='utf-8')
print('Unicode-safe metadata correction and source-derived long title fixture saved')
