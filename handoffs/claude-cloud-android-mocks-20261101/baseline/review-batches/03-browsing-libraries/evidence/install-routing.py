from pathlib import Path
p=Path("D:\\Colosseum-Android-Phone-Mocks-20261101\\review-batches\\03-browsing-libraries\\finished-mocks")
for f in p.glob("*/*.html"):
 s=f.read_text(encoding="utf-8");s=s.replace("</body>",'<script src="../batch-runtime.js"></script></body>');f.write_text(s,encoding="utf-8")
print("Installed local batch routing")