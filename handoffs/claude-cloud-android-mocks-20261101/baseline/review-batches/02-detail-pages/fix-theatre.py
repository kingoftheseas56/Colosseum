POPUP="function closePopup(returnFocus = true) {\n  const old = state.popup, focus = state.popupFocus;\n  state.popup = \"\"; state.popupFocus = null;\n  $(\"overlay\").innerHTML = \"\"; $(\"overlay\").dataset.which = \"\"; $(\"overlay\").classList.remove(\"modal-open\");\n  $(\"page-scroll\").inert = false; $(\"chrome\").inert = false;\n  for (const id of [\"season-trigger\",\"jump-trigger\"]) $(id)?.setAttribute(\"aria-expanded\",\"false\");\n  if (returnFocus && old && focus?.isConnected) focus.focus({preventScroll:true});\n}\nfunction showPopup(which, episode) {\n  if (!which || state.popup === which) { closePopup(); return; }\n  closePopup(false); state.popup = which;\n  if (episode) state.contextEpisode = episode;\n  state.popupFocus = which === \"season\" ? $(\"season-trigger\") : which === \"jump\" ? $(\"jump-trigger\") :\n    which === \"removal\" ? content.querySelector('[data-action=\"library\"]') :\n    content.querySelector('.episode-row[data-episode=\"' + state.contextEpisode + '\"]');\n  paintPopup();\n}\nfunction paintPopup() {\n  const target = $(\"overlay\"), which = state.popup;\n  if (!which) return;\n  let anchor = null, html = \"\", className = \"\";\n  if (which === \"season\") {\n    anchor = $(\"season-trigger\"); className = \"popup-season\";\n    html = seasons.map(season => '<button type=\"button\" role=\"option\" aria-selected=\"' + (season === state.activeSeason) +\n      '\" data-action=\"season-select\" data-season=\"' + season + '\">' + (season === 0 ? \"Specials\" : \"Season \" + season) + '</button>').join(\"\");\n  } else if (which === \"jump\") {\n    anchor = $(\"jump-trigger\"); className = \"popup-jump\";\n    const max = activeEpisodes().length;\n    html = '<div class=\"jump-input-row\"><input id=\"jump-input\" type=\"number\" inputmode=\"numeric\" min=\"1\" max=\"' + max +\n      '\" placeholder=\"1 - ' + max + '\" aria-label=\"Episode number\"><button type=\"button\" data-action=\"jump-submit\" class=\"jump-submit\">Jump</button></div>' +\n      '<div class=\"jump-ranges\">' + Array.from({length:Math.ceil(max/50)},(_,i)=>\n      '<button type=\"button\" data-action=\"jump-range\" data-episode=\"' + (i*50+1) + '\">' + (i*50+1) + '-' + Math.min((i+1)*50,max) + '</button>').join(\"\") + '</div>';\n  } else if (which === \"context\") {\n    anchor = content.querySelector('.episode-row[data-episode=\"' + state.contextEpisode + '\"]'); className = \"context-menu\";\n    html = '<button type=\"button\" data-action=\"episode-play\" data-episode=\"' + state.contextEpisode +\n      '\">Play</button><button type=\"button\" data-action=\"episode-download\" data-episode=\"' + state.contextEpisode + '\">Download</button>';\n  } else if (which === \"removal\") {\n    anchor = content.querySelector('[data-action=\"library\"]'); className = \"popup-removal\";\n    html = '<h2>Remove from your library?</h2><p>' + escapeHtml(data.title) + '</p><button data-action=\"remove-confirm\">Remove from Colosseum</button>' +\n      (params.get(\"linked\") === \"1\" ? '<button data-action=\"remove-linked\">Remove from Colosseum &amp; Stremio</button>' : '') +\n      '<button class=\"quiet\" data-action=\"remove-cancel\">Cancel</button>';\n  }\n  if (!anchor) {closePopup();return;}\n  let popup = target.querySelector(\".popup\"), fresh = !popup || target.dataset.which !== which;\n  if (fresh) {\n    target.innerHTML = \"\"; popup = document.createElement(\"div\"); popup.className = \"popup \" + className;\n    popup.setAttribute(\"role\",which === \"season\" ? \"listbox\" : which === \"context\" ? \"menu\" : \"dialog\");\n    popup.setAttribute(\"aria-label\",which === \"season\" ? \"Choose season\" : which === \"jump\" ? \"Go to episode\" : which === \"removal\" ? \"Remove from your library?\" : \"Episode actions\");\n    popup.innerHTML = html; target.appendChild(popup); target.dataset.which = which;\n  }\n  const bounds = app.getBoundingClientRect(), ref = anchor.getBoundingClientRect(), styles = getComputedStyle(app);\n  const top = (parseFloat(styles.getPropertyValue(\"--safe-top\")) || 0) + 10;\n  const bottom = app.clientHeight - (parseFloat(styles.getPropertyValue(\"--safe-bottom\")) || 0) - 10;\n  const below = ref.bottom - bounds.top + 8, above = ref.top - bounds.top - 8;\n  popup.style.maxWidth = Math.max(120,app.clientWidth - (which === \"removal\" ? 48 : 20)) + \"px\";\n  popup.style.maxHeight = Math.max(60,Math.min(which === \"season\" ? 304 : bottom-top,\n    which === \"removal\" ? bottom-top : Math.max(bottom - below, above - top))) + \"px\";\n  const w = popup.offsetWidth, h = popup.offsetHeight;\n  popup.style.left = (which === \"removal\" ? (app.clientWidth-w)/2 : Math.max(10,Math.min(ref.left-bounds.left,app.clientWidth-w-10))) + \"px\";\n  popup.style.top = (which === \"removal\" ? top+(bottom-top-h)/2 : below+h<=bottom ? below : above-h>=top ? above-h : Math.max(top,bottom-h)) + \"px\";\n  if (which === \"season\" || which === \"jump\") anchor.setAttribute(\"aria-expanded\",\"true\");\n  if (which === \"removal\") {\n    target.classList.add(\"modal-open\"); popup.setAttribute(\"aria-modal\",\"true\");\n    $(\"page-scroll\").inert = true; $(\"chrome\").inert = true;\n  }\n  if (fresh) {\n    const focus = which === \"season\" ? popup.querySelector('[aria-selected=\"true\"]') :\n      which === \"jump\" ? popup.querySelector(\"input\") : popup.querySelector(\"button\");\n    focus?.focus({preventScroll:true}); focus?.scrollIntoView({block:\"nearest\"});\n  }\n}\n"
from pathlib import Path
root=Path(r"D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\02-detail-pages\finished-mocks")
p=root/"shared/collections-picker.js"
s=p.read_text(encoding="utf-8")
s=s.replace("host.append(overlay);","host.append(overlay);overlay.querySelector('.cp-dialog').style.padding=Math.min(560,host.clientWidth-32)<480?'20px':'26px';")
p.write_text(s,encoding="utf-8")
p=root/"shared/collections-picker.css"
s=p.read_text(encoding="utf-8")
s+="""
/* Preserve the shared component inside worker pages with ID-scoped button resets. */
#app .cp-overlay .cp-button{font:600 calc(14px * var(--cp-scale,1)) BatchSegoe,sans-serif;color:#f7f7f5;background:transparent;border:1px solid transparent}
#app .cp-overlay .cp-button:hover{background:#ffffff18}
#app .cp-overlay .cp-primary{background:#f0c44a;color:#17140a}
#app .cp-overlay .cp-primary:hover{background:#f7d67c}
#app .cp-overlay .cp-membership{background:transparent;border:1px solid transparent;color:#f7f7f5;font:inherit}
#app .cp-overlay .cp-membership[aria-checked=true]{background:#f0c44a15;border-color:#f0c44a40}
"""
p.write_text(s,encoding="utf-8")
p=root/"17-theatre-details/app.js"
s=p.read_text(encoding="utf-8")
a=s.index("function closePopup()");b=s.index("function render(",a)
s=s[:a]+POPUP+s[b:]
s=s.replace('''      state.saved = !state.saved;
      render();''','''      if(state.saved){showPopup("removal");break;}
      state.saved = true;
      render();content.querySelector('[data-action="library"]')?.focus({preventScroll:true});''')
s=s.replace('''      status("Add to Collections invokes the existing picker in the app; no account or picker operation is attempted here."); break;''','''      closePopup(false);
      BatchCollections.open({host:app,trigger:el,key:"theatre-"+data.type+":"+data.title,title:data.title,kind:film?"film":"series",kindLabel:film?"Film":"Series",year:data.year,cover:fullArt(data.art),onSaved:()=>status("Collection membership saved in this local page fixture.")});break;
    case "remove-cancel":closePopup();break;
    case "remove-confirm":case "remove-linked":
      closePopup(false);state.saved=false;render();content.querySelector('[data-action="library"]')?.focus({preventScroll:true});
      status("Removed from this local library fixture"+(action==="remove-linked"?" with the linked-account choice":"")+". No service operation.");break;''')
s=s.replace('''    case "watch": case "episode-play": case "episode-download": case "season-download":
      status''','''    case "watch": case "episode-play": case "episode-download": case "season-download":
      closePopup();
      status''')
s=s.replace('''      state.activeSeason = +el.dataset.season; state.popup = ""; render(); scrollToEpisodes(); break;''','''      closePopup(false);state.activeSeason = +el.dataset.season; render(); scrollToEpisodes();$("season-trigger")?.focus({preventScroll:true});break;''')
s=s.replace('''if(row && (event.key==="Enter" || event.key===" "))''','''if(row && event.target===row && (event.key==="Enter" || event.key===" "))''')
s=s.replace('''document.addEventListener("keydown",(event)=>{
  if(event.key==="Escape"){''','''document.addEventListener("keydown",(event)=>{
  const popup=$("overlay").querySelector(".popup");
  if(popup && ["ArrowDown","ArrowUp","Home","End","Tab"].includes(event.key) && event.target.tagName!=="INPUT"){
    const items=[...popup.querySelectorAll("button")],i=items.indexOf(document.activeElement);
    if(state.popup==="season"&&event.key==="Tab"){event.preventDefault();return;}
    let n=event.key==="Home"?0:event.key==="End"?items.length-1:event.key==="ArrowUp"||(event.key==="Tab"&&event.shiftKey)?(i+items.length-1)%items.length:(i+1)%items.length;
    event.preventDefault();items[n]?.focus();return;
  }
  if(popup && event.key==="Tab" && state.popup!=="season"){
    const items=[...popup.querySelectorAll("button,input")],i=items.indexOf(document.activeElement);
    event.preventDefault();items[(i+items.length+(event.shiftKey?-1:1))%items.length]?.focus();return;
  }
  if(event.key==="Escape"){''')
s+='''
document.addEventListener("pointerdown",event=>{
  if(state.popup&&state.popup!=="removal"&&!event.target.closest("#overlay")&&!event.target.closest("#season-trigger,#jump-trigger"))closePopup(false);
});
if(params.get("state")==="removal-open"){state.saved=true;render();showPopup("removal");}
'''
p.write_text(s,encoding="utf-8")
p=root/"17-theatre-details/style.css"
s=p.read_text(encoding="utf-8")+"""
/* Source TheatreSeriesView: phone synopsis is complete, not clamped behind a hidden More. */
#app.is-handset .synopsis.clamped p{display:block;-webkit-line-clamp:unset;overflow:visible}
#app .popup{overflow:auto;overscroll-behavior:contain}
#app .popup button:focus-visible{outline:2px solid var(--gold);outline-offset:-2px}
#app #overlay.modal-open{background:rgba(3,4,6,.78);pointer-events:auto}
#app .popup-removal{width:520px;min-height:278px;padding:30px;border-radius:20px;background:#171a22;display:flex;flex-direction:column;gap:16px}
#app .popup-removal>*{flex-shrink:0;margin:0}
#app .popup-removal h2{font:calc(28px * var(--font-scale))/1.15 "Fraunces Review",serif;overflow-wrap:anywhere}
#app .popup-removal p{font-size:calc(14px * var(--font-scale));color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#app .popup-removal button{width:100%;min-height:42px;padding:10px;border-radius:10px;border:1px solid var(--edge);background:#ffffff0f;color:var(--ink);font-size:calc(14px * var(--font-scale));font-weight:600}
#app .popup-removal button.quiet{border:0;background:transparent;color:var(--muted)}
"""
p.write_text(s,encoding="utf-8")
print("Finished-only shared picker and Theatre corrections written.")
