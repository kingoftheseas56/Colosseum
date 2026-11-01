from pathlib import Path
p=Path("D:\\Colosseum-Android-Phone-Mocks-20261101\\review-batches\\03-browsing-libraries\\evidence\\qa-helpers.js")
s=p.read_text(encoding="utf-8").replace("e=>e.scrollTop=e.scrollHeight","e=>e.scrollTo({top:e.scrollHeight,behavior:'instant'})");p.write_text(s,encoding="utf-8")
print("Long-scroll check uses an immediate scroll command")