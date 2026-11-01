from pathlib import Path
p=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries\finished-mocks\20-search-browse\app.js')
s=p.read_text(encoding='utf-8').replace("Math.min(260,Math.max(100,S.height*.42))+'px'","Math.min(260,Math.max(100,Math.min(S.height*.42,S.height-S.top-S.bottom-(S.width>S.height?76:148)-110)))+'px'")
p.write_text(s,encoding='utf-8')
print('Bounded illustrative keyboard reservation with minimum readable content; compacted only short landscape IME header.')
