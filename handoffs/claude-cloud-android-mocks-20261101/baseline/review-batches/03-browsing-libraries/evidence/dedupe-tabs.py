from pathlib import Path
p=Path("D:\\Colosseum-Android-Phone-Mocks-20261101\\review-batches\\03-browsing-libraries\\finished-mocks\\08-theatre-discover\\theatre-discover.html")
s=p.read_text(encoding="utf-8")
s=s.replace("<button class=\"tab\" type=\"button\" data-theatre-tab=\"Movies\">Movies</button><button class=\"tab\" type=\"button\" data-theatre-tab=\"Shows\">Shows</button><button class=\"tab\" type=\"button\" data-theatre-tab=\"Anime\">Anime</button>","<button class=\"tab\" type=\"button\" data-theatre-tab=\"Movies\">Movies</button>")
p.write_text(s,encoding="utf-8")
print("Removed duplicate injected tabs; original five retained")