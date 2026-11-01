from pathlib import Path
p=Path(__file__).parent/'finished-mocks'/'06-biblio-book'/'app.js'
s=p.read_text(encoding='utf-8')
old='  else if(w<=1000)mode="tablet";\n  else mode="desktop";'
new='  else if(w<=1000||h>750)mode="tablet";\n  else mode="desktop";'
assert old in s
p.write_text(s.replace(old,new),encoding='utf-8')
print('Biblio custom tablet classification corrected; original desktop preset retained.')
