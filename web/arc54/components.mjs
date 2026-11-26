export function el(tag, attrs={}, ...children) {
  const e=document.createElement(tag);
  for(const [key,val] of Object.entries(attrs)) if(val!=null) e.setAttribute(key,String(val));
  for(const child of children.flat()) if(child!=null) e.append(child instanceof Node?child:String(child));
  return e;
}
export const link=(url,text,attrs={})=>el('a',{href:url,...attrs},text);
export function title(kicker,name){return [el('p',{class:'eyebrow'},kicker),el('h1',{},name)];}
export function action(text,{href,primary=false,disabled=false,focusId,...attrs}={}){
  const props={class:'action'+(primary?' primary':''),'data-focus-id':focusId,...attrs};
  return href&&!disabled?link(href,text,props):el('button',{type:'button',...props,disabled:disabled?'':null},text);
}
export function status(kind,text){return el('div',{class:'status','data-kind':kind,role:kind==='error'?'alert':'status'},text);}
export function sample() {
 const current=location.hash.endsWith('/alt')?'alternate':'discover';
 const tab=(id,text,url)=>link(url,text,{class:'tab','aria-current':current===id?'page':null,'data-focus-id':'sample-'+id});
 return [...title('Shared presentation','Component catalogue'),
 el('p',{},'A browser translation of the pinned Colosseum theme. The controls here are examples, not media actions.'),
 el('div',{class:'component'},
 el('section',{class:'panel'},el('p',{class:'meta'},'Navigation'),el('h2',{},'Pill group'),el('p',{},'Selected tab, neutral tab and a separate keyboard focus ring.'),el('nav',{class:'pills','aria-label':'Sample pages'},tab('discover','Discover','#/components'),tab('alternate','Alternate','#/components/alt'))),
 el('section',{class:'panel'},el('p',{class:'meta'},'Controls'),el('h2',{},'Actions'),el('p',{},'Primary, secondary and unavailable.'),el('div',{class:'flow'},action('View Theatre',{href:'#/theatre',primary:true,focusId:'sample-primary'}),action('Back to Home',{href:'#/',focusId:'sample-secondary'}),action('Play',{disabled:true}))),
 el('section',{class:'panel'},el('p',{class:'meta'},'Status'),el('h2',{},'Data states'),el('p',{},'Explicit loading, empty, and error states.'),el('div',{class:'flow'},status('loading','Loading catalogue…'),status('empty','Nothing here yet.'),status('error','Couldn’t load this section.'))))];
}
