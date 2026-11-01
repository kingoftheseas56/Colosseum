from pathlib import Path
p=Path(r"D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\02-detail-pages\finished-mocks\19-generic-universes\app.js")
s=p.read_text(encoding="utf-8").replace('+(Number($("insetR").value)||0;','+(Number($("insetR").value)||0);')
p.write_text(s,encoding="utf-8")
