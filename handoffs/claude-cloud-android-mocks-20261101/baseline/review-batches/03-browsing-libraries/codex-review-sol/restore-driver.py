from pathlib import Path
p=Path(__file__).with_name('test-search-correction.cjs')
s=p.read_text(encoding='utf-8')
a=s.index(',utf8OpenLabel:');z=s.index("document.getElementById('appFrame').innerText",a)+len("document.getElementById('appFrame').innerText")
p.write_text(s[:a]+s[z:],encoding='utf-8')
