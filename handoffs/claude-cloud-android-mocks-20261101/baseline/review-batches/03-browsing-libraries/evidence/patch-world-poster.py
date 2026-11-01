from pathlib import Path
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
p=B/'finished-mocks/15-world-home/app.js';s=p.read_text(encoding='utf-8')
s=s.replace('var kind=slide.kind==="poster" && !phone?" inset":"";','var kind=slide.kind==="poster"?" inset":"";var cw=s.w-(wideNav()?92:0),m=cw<=430?18:cw<600?24:cw<840?32:54,narrow=cw-2*m<600;')
s=s.replace("'<section class=\"section featured\" tabindex=\"0\"","'<section class=\"section featured '+(narrow?'hero-narrow ':'')+(qp.get('posterPhone')==='bleed'?'proposal-phone-bleed':'')+'\" data-art-kind=\"'+(slide.kind||'banner')+'\" tabindex=\"0\"")
p.write_text(s,encoding='utf-8')
p=B/'finished-mocks/15-world-home/style.css';s=p.read_text(encoding='utf-8');s+='\n/* Native poster branch: preserve the crisp inset box and avoid a bleed duplication. */\n.featured .hero-art.inset{width:calc((var(--hero-h) - 44px)*.66);height:calc(100% - 44px);right:56px;left:auto;top:22px;max-width:none;object-fit:contain;border-radius:0}\n.featured.hero-narrow .hero-art.inset{display:none}.featured.hero-narrow.proposal-phone-bleed .hero-art.inset{display:block;inset:0;width:100%;height:100%;object-fit:cover}\n';p.write_text(s,encoding='utf-8')
print('Applied approved poster-only inset branch to Tankoban carousel as well.')
