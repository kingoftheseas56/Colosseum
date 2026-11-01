from pathlib import Path
p=Path("D:\\Colosseum-Android-Phone-Mocks-20261101\\review-batches\\03-browsing-libraries\\finished-mocks")
f=p/"07-biblio-home/app.js";s=f.read_text(encoding="utf-8");s=s.replace("icon('book-library')+'\" alt=\"\" style=\"width:21px;height:21px\"","icon('books')+'\" alt=\"\" style=\"width:21px;height:21px\"");f.write_text(s,encoding="utf-8")
f=p/"20-search-browse/app.js";s=f.read_text(encoding="utf-8");s=s.replace("item.kind==='video'?'▶':img('icons/book-library.svg','','Read')","img('icons/'+(item.kind==='video'?'play':item.kind==='book'?'books':'manga')+'.svg','','Resume')");f.write_text(s,encoding="utf-8")
print("Resume icon artwork matches ContinueTile")