from pathlib import Path
p=Path("D:\\Colosseum-Android-Phone-Mocks-20261101\\review-batches\\03-browsing-libraries\\finished-mocks\\07-biblio-home\\app.js")
s=p.read_text(encoding="utf-8")
s=s.replace('\'<div class="poster-wall">\'+displayBooks','\'<div class="poster-wall">\'+(displayBooks')
s=s.replace('\'<p class="empty-filter">No books match this filter.</p>\'+\'</div>\'','\'<p class="empty-filter">No books match this filter.</p>\')+\'</div>\'')
p.write_text(s,encoding="utf-8")
print("07 filter expression grouped")