from pathlib import Path
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
for p,prefix in [(B/'index.html',''),(B/'finished-mocks/index.html','../')]:
 s=p.read_text(encoding='utf-8-sig')
 marker='<a href="'+prefix+'REPORT.json">Report</a>'
 s=s.replace(marker,marker+'<a href="'+prefix+'decisions.html">P1 artwork decision</a>')
 import re
 s=re.sub(r'(<a href="([^"]+)/([^/"]+)-comparison.jpg">Before / after sheet</a>)',lambda m:m[1]+'<a href="'+m[2]+'/'+m[3]+'-stress.jpg">Stress / tablet sheet</a>',s)
 p.write_text(s,encoding='utf-8')
print('Both handoff indexes link P1 and all 16 stress sheets.')
