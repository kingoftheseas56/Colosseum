import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App
TestCase {
 id: suite
 name: "AndroidBatch02Headers"
 when: windowShown
 Window { id:host; width:1192; height:533; visible:true; property bool androidHost:true }
 FontLoader { source:"../../assets/fonts/Inter-Regular.otf" }
 FontLoader { source:"../../assets/fonts/Fraunces-Regular.ttf" }
 Item { id:testBackdrop; parent:host.contentItem; width:host.width; height:host.height }
 QtObject { id:account; property string mode:"localOnly"; property string username:""; property bool accountFeaturesAvailable:true }
 QtObject {
  id:testAdapter
  property string title:"Popular"
  property string provider:"Apple Books"
  function types() { return [{key:"manga",label:"Manga"},{key:"manhwa",label:"Manhwa"},{key:"comics",label:"Comics"}] }
  function catalogs(type) { return [{key:"popular",title:title,sourceName:provider},{key:"new",title:"New",sourceName:provider}] }
  function filters(type,catalog) { return [] }
 }
 Component { id:barComponent; App.TopBar { backdrop:testBackdrop; androidHost:true; trackersEnabled:true; accountController:account; function refreshClock() {} clock:"12:59"; ampm:"PM"; date:"Wednesday, September 30" } }
 Component { id:browserComponent; App.DiscoverBrowser { androidHost:true; adapter:testAdapter; active:false; currentType:"manga"; currentCatalogKey:"popular"; automationPrefix:"fixture" } }
 Component { id:tabsComponent; App.WorldTabBar { backdrop:testBackdrop; androidHost:true; tabModel:[{key:"discover",label:"Discover"},{key:"manga",label:"Manga"},{key:"comics",label:"Comics"},{key:"library",label:"Library"}] } }
 Component { id:worldComponent; App.WorldPage { backdrop:testBackdrop; androidHost:true; medium:"Tankoban" } }
 function runtime(android,systemScale) {
  if(typeof FontTestFixture!=="undefined") {
   FontTestFixture.useDesktopRuntime(!android)
   FontTestFixture.setScale(systemScale)
  }
 }
 function init() { runtime(true,1) }
 function cleanupTestCase() { runtime(true,1) }
 function make(component,properties) {
  const item=createTemporaryObject(component,host.contentItem,properties||{})
  verify(item!==null)
  return item
 }
 function child(item,name) { const found=findChild(item,name); verify(found!==null,name); return found }
 function scenarios() {
  var rows=[]
  for (var w of [1192,1024,1500]) for(var scale of [1,1.3]) rows.push({tag:w+"-font"+scale,w:w,scale:scale})
  return rows
 }
 function scale(item,name,value) { child(item,name).fontScale=value; wait(50) }
 function within(item,root) {
  const pos=item.mapToItem(root,0,0)
  verify(pos.x>=-0.1 && pos.y>=-0.1,"contained origin "+item.objectName)
  verify(pos.x+item.width<=root.width+0.1 && pos.y+item.height<=root.height+0.1,"contained extent "+item.objectName)
 }
 function test_system_icon_label_placement_data() {
  return [{tag:"desktop",android:false,scale:1},{tag:"android",android:true,scale:1},{tag:"android-large-text",android:true,scale:1.3}]
 }
 function test_system_icon_label_placement(data) {
  runtime(data.android,data.scale)
  const bar=make(barComponent,{width:1500,activeMedium:"Theatre",androidHost:data.android})
  scale(bar,"topBarTypography",data.scale)
  const action=child(bar,"topBarWallpaperButton")
  var glyph=null,label=null
  for(var node of action.children) {
   if(typeof node.sourceSize!=="undefined") glyph=node
   if(typeof node.text==="string") label=node
  }
  verify(glyph!==null && label!==null)
  compare(action.width,data.android ? 48 : 22)
  fuzzyCompare(glyph.x+glyph.width/2,action.width/2,0.01)
  action.showLabel=true
  wait(30)
  compare(glyph.x,0)
  compare(label.x,data.android ? 36 : 30)
  verify(label.x+0.01>=glyph.x+glyph.width+8,"label clears glyph: "+label.x+" / "+glyph.x+" / "+glyph.width)
  if(data.android) within(label,action)
  else compare(action.width,82)
  action.showLabel=false
  wait(30)
  compare(action.width,data.android ? 48 : 22)
  fuzzyCompare(glyph.x+glyph.width/2,action.width/2,0.01)
 }
 function test_topbar_data() { return scenarios().concat([{tag:"midnight-date",w:1192,scale:1.3,date:"Wednesday, November 4"}]) }
 function test_topbar(data) {
  runtime(true,data.scale)
  host.width=data.w
  for (var medium of ["","Tankoban","Theatre","Universes"]) {
   const bar=make(barComponent,{width:data.w-32,activeMedium:medium,date:data.date || "Wednesday, September 30"})
   scale(bar,"topBarTypography",data.scale)
   const left=child(bar,"topBarLeftChrome"),picker=child(bar,"topBarWorldPicker"),right=child(bar,"topBarSystemChrome")
   if(typeof FontTestFixture!=="undefined") {
    var backControl=null
    for(var control of left.children) if(typeof control._chev==="number") backControl=control
    verify(backControl!==null)
    compare(backControl._chev,28,"BackAction uses actual Android runtime")
    compare(backControl.height>=52,true)
   }
   console.log("BUDGET",data.w,data.scale,medium,"left",left.width,"fullLeft",left.fullWidth,"picker",picker.width,"systems",right.width,"compact",bar.compactChrome,"height",bar.height)
   verify(picker.x>=left.width+8)
   verify(picker.x+picker.width+8<=right.x,"groups overlap")
   within(left,bar);within(picker,bar);within(right,bar)
   var previousRight=0,actionCount=0
   for (var node of right.children) {
    if (!node.visible || typeof node.width!=="number") continue
    actionCount++
    verify(node.width>=48 && node.height>=48,"independent target "+node.objectName)
    fuzzyCompare(node.y+node.height/2,right.height/2,0.01,"aligned centre")
    verify(node.x>=previousRight,"disjoint system targets")
    previousRight=node.x+node.width
   }
   compare(actionCount,medium==="Theatre" ? 5 : (medium==="Universes" ? 3 : 4))
   for (var label of ["Tankoban","Biblio","Theatre","Universes","Feria","Vinyl"]) {
    const pill=child(bar,"modePill_"+label)
    within(pill,picker)
    for(var content of pill.children) if(typeof content.spacing==="number") within(content,pill)
   }
   if (data.w===1192) compare(child(bar,"topBarClock").visible,true)
  }
 }
 function test_discover_data() {
  var rows=[]
  for(var scenario of scenarios().concat([{tag:"stacked-font1.3",w:600,scale:1.3}]))
   for(var collapsed of [true,false]) rows.push({tag:scenario.tag+"-collapsed"+collapsed,w:scenario.w,scale:scenario.scale,collapsed:collapsed})
  return rows
 }
 function test_discover(data) {
  runtime(true,data.scale)
  host.width=data.w
  testAdapter.title="A very long selected catalogue title that needs its available width and must not overlap the type tabs even on the widest tablet"
  testAdapter.provider="An unusually long catalogue provider attribution that must stay inside the heading"
  const browser=make(browserComponent,{width:data.w,height:533})
  child(browser,"fixtureDiscoverSidebar").collapsed=data.collapsed
  scale(browser,"discoverTypography",data.scale)
  wait(350)
  const group=child(browser,"fixtureDiscoverHeadingGroup"),summary=child(browser,"fixtureDiscoverSummary"),provider=child(browser,"fixtureDiscoverHeadingProvider")
  const last=child(browser,"fixtureDiscoverType_comics")
  const lastPos=last.mapToItem(browser,last.width,last.height)
  const groupPos=group.mapToItem(browser,0,0)
  verify(browser.stackedTypeHeader || groupPos.x>=lastPos.x+16,"separate type and heading groups")
  within(group,browser);within(summary,group);within(provider,group);within(child(browser,"fixtureDiscoverHeadingKicker"),group)
  const title=child(browser,"fixtureDiscoverHeadingTitle")
  verify(title.width<title.implicitWidth)
  fuzzyCompare(group.mapToItem(browser,group.width,0).x,browser.width-16,0.01)
  browser.catalogMenuOpen=true
  wait(30)
  const menu=child(browser,"discoverCatalogMenu")
  within(menu,browser)
  compare(child(browser,"fixtureDiscoverType_manga").height>=48,true)
 }
 function test_world_tabs_data() { return scenarios() }
 function test_world_tabs(data) {
  runtime(true,data.scale)
  host.width=data.w
  const tabs=make(tabsComponent,{width:data.w})
  scale(tabs,"worldTabsTypography",data.scale)
  for (var key of ["discover","manga","comics","library"]) {
   const pill=child(tabs,"worldTab_"+key)
   within(pill,tabs)
   verify(pill.height>=48)
   for (var label of pill.children) if(typeof label.text==="string") within(label,pill)
  }
 }
 function test_world_content_follows_header() {
  const world=make(worldComponent,{width:1192,height:533})
  wait(50)
  const scroll=child(world,"tankobanWorldScroll"),chrome=child(world,"topBarWorldPicker")
  const bottom=chrome.parent.y+chrome.parent.height
  verify(scroll.y>=bottom+10)
 }
}
