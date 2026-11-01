from pathlib import Path
p=Path("D:\\Colosseum-Android-Phone-Mocks-20261101\\review-batches\\03-browsing-libraries\\finished-mocks\\20-search-browse\\app.js")
s=p.read_text(encoding="utf-8")
a="return '<div class=\"wall\"></div><div class=\"app-inner\">'+stickyHeader('Biblio')+batchWorldNav()+'<div"
b="return '<div class=\"wall\"></div>'+batchWorldNav()+'<div class=\"app-inner\">'+stickyHeader('Biblio')+'<div"
assert a in s;s=s.replace(a,b);p.write_text(s,encoding="utf-8")
print("Explore navigation anchored to frame")