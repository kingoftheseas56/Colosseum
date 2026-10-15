const esc=s=>String(s).replace(/[&<>"']/g,m=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[m]));
const poster=id=>`https://images.metahub.space/poster/small/${id}/img`;

function screenCard(x,i){
  return `<a class="tile" href="#" aria-label="${esc(x.t)}"><div class="plate"><img class="poster" src="${poster(x.id)}" alt="" loading="lazy"><span class="index">${i+1}</span></div><div class="caption"><div class="title">${esc(x.t)}</div></div></a>`;
}

function comicCard(x,i){
  const linked=!!x.u;
  return `<a class="tile ${linked?"":"pending"}" href="${linked?esc(x.u):"#"}" ${linked?'target="_blank" rel="noopener"':''} aria-label="${esc(x.t)}"><div class="plate"><div class="fallback">${esc(x.t)}</div><span class="index">${i+1}</span>${linked?'':'<span class="note">PENDING</span>'}</div><div class="caption"><div class="title">${esc(x.t)}</div></div></a>`;
}

function rail(title,items,card){
  if(!items.length) return "";
  return `<section class="section"><h2>${esc(title)}</h2><div class="count">${items.length} ${items.length===1?"work":"works"}</div><div class="rule"></div><div class="rail-wrap"><div class="rail">${items.map(card).join("")}</div></div></section>`;
}

function isTv(x){ return String(x.b||"").startsWith("SERIES") || String(x.b||"").includes("ANIMATION"); }
function isMovie(x){ return String(x.b||"") === "MOVIE"; }
function isSpecial(x){ return !isMovie(x) && !isTv(x); }

const main=document.querySelector("#universe-sections");
for(const p of MCU_PHASES){
  const movies=p.screen.filter(isMovie);
  const tv=p.screen.filter(isTv);
  const specials=p.screen.filter(isSpecial);
  main.insertAdjacentHTML("beforeend",rail(`Phase ${p.roman} · Movies`,movies,screenCard));
  main.insertAdjacentHTML("beforeend",rail(`Phase ${p.roman} · TV Shows`,tv,screenCard));
  main.insertAdjacentHTML("beforeend",rail(`Phase ${p.roman} · Specials`,specials,screenCard));
  if(p.adjacent && p.adjacent.length) main.insertAdjacentHTML("beforeend",rail(`Phase ${p.roman} Era · Adjacent Releases`,p.adjacent,screenCard));
  main.insertAdjacentHTML("beforeend",rail(`Phase ${p.roman} · Comics`,p.comics,comicCard));
}
