from pathlib import Path
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
if not (B/'evidence/README-ORIGINAL.md').exists():(B/'evidence/README-ORIGINAL.md').write_bytes((B/'README.md').read_bytes())
for p,href in [(B/'index.html','decisions.html'),(B/'finished-mocks/index.html','../decisions.html')]:
 s=p.read_text(encoding='utf-8')
 if 'Poster artwork decision' not in s:s=s.replace('</body>','<section style="padding:24px;border-top:1px solid #39404c"><h2>Poster artwork decision</h2><p>The default follows the approved poster branch. A phone artwork alternative is isolated for Hemanth’s review.</p><a href="'+href+'">P1 · Original source treatment and proposed phone alternative</a></section></body>')
 p.write_text(s,encoding='utf-8')
print('Added explicit optional decision links; preserved original batch README.')
