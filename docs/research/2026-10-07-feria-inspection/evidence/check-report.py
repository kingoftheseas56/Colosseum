from pathlib import Path
import re,json
root=Path(__file__).resolve().parent.parent
p=root/'report/CAPABILITY-MAP.md'
rows=[x for x in p.read_text().splitlines() if re.match(r'^\| \d+ \|',x)]
ids=[int(x.split('|')[1]) for x in rows]
assert ids==list(range(1,53)),ids
assert all(len(x.split('|'))==8 for x in rows),[(x[:50],len(x.split('|'))) for x in rows if len(x.split('|'))!=8]
for name in ['README.md','FRONTEND-INTERFACE.md','EVIDENCE.md']:
 s=(root/'report'/name).read_text()
 for link in re.findall(r'\]\(([^)]+)\)',s):
  if not link.startswith('https:'):assert (root/'report'/link).exists(),link
print('PASS: 52 ordered capability rows, six columns per row, local report links resolve')
print('This validates report structure, not browser behavior')
