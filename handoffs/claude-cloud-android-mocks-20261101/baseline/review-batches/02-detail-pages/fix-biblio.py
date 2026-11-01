from pathlib import Path
root=Path(__file__).parent/'finished-mocks'
for folder in ['06-biblio-book','17-theatre-details','18-comic-details']:
    for file in (root/folder).glob('*.html'):
        if file.name=='index.html': continue
        text=file.read_text(encoding='utf-8')
        text=text.replace('<link rel="stylesheet" href="style.css">','<link rel="stylesheet" href="style.css">\n<link rel="stylesheet" href="../shared/collections-picker.css">\n<script src="../shared/collections-picker.js" defer></script>')
        file.write_text(text,encoding='utf-8')
file=root/'06-biblio-book'/'app.js'
text=file.read_text(encoding='utf-8')
old='function openModal(kind){\n  closeModals(false);'
new='''function openModal(kind){
  closeModals(false);
  if(kind==="collections"){
    BatchCollections.open({host:"#safe-zone",trigger:$("collections-button"),key:"book-way-of-kings",title:$(".unused")?.textContent||document.querySelector(".book-title").textContent,kind:"book",kindLabel:"Book",year:"2010",cover:document.querySelector(".book-jacket img").getAttribute("src"),onSaved:()=>note("Collection membership saved in this local mock only.")});
    return;
  }'''
assert old in text
text=text.replace(old,new).replace('title:$(".unused")?.textContent||document.querySelector(".book-title").textContent','title:document.querySelector(".book-title").textContent')
text=text.replace('function applyScenario(){','function applyScenario(){\n  BatchCollections.close();')
text=text.replace('document.addEventListener("keydown",e=>{','''document.addEventListener("keydown",e=>{
  if(state.open==="read"&&["ArrowDown","ArrowUp","Home","End"].includes(e.key)){
    const rows=[...document.querySelectorAll("#read-choices .choice-row")];
    const i=rows.indexOf(document.activeElement);
    const next=e.key==="Home"?0:e.key==="End"?rows.length-1:(i+(e.key==="ArrowDown"?1:-1)+rows.length)%rows.length;
    rows[next]?.focus();e.preventDefault();return;
  }''')
file.write_text(text,encoding='utf-8')
print('Biblio shared faithful Collections picker and Read keyboard navigation authored; original outputs untouched.')

