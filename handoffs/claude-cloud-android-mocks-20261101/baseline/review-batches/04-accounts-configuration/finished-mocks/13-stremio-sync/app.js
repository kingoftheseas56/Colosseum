"use strict";
// Local-only state rehearsal. No network, native accounts or provider operations.
const presets = Object.freeze({
  portrait: [412, 900], landscape: [900, 412],
  smallPortrait: [354, 792], smallLandscape: [792, 354],
  desktop: [1280, 700], tabletPortrait: [800, 1280],
  tabletLandscape: [1280, 800]
});
const ids = Object.fromEntries([
  "preset","vw","vh","state","confirm","fontScale","imeSize",
  "insetTop","insetBottom","insetLeft","insetRight","keyboardPreview",
  "longFixture","panelOpen","reset","metrics","frame","overlay","panel",
  "panelScroll","panelHeading","panelDetail","accountValue","lastSyncValue",
  "result","confirmationText","primaryAction","switchAction",
  "disconnectAction","cancelAction","headerClose","openSettings",
  "backdropStatus"
].map(id => [id, document.getElementById(id)]));
const headers = Object.freeze({
  connecting: "Connecting to Stremio",
  syncing: "Syncing with Stremio",
  reconnectRequired: "Reconnect Stremio",
  syncFailed: "Stremio sync needs attention",
  paused: "Stremio sync needs attention",
  unavailable: "Stremio sync needs attention",
  synced: "Stremio Sync",
  notConnected: "Connect Stremio"
});
const details = Object.freeze({
  connecting: "Finish signing in through the browser window.",
  syncing: "Combining your library, progress and addons safely.",
  reconnectRequired: "This profile uses Stremio, but this device needs its own secure connection.",
  syncFailed: "Nothing was deleted. Try again when the connection is available.",
  paused: "Nothing was deleted. Try again when the connection is available.",
  unavailable: "Stremio Sync is unavailable while this profile is sealed.",
  synced: "Your newest activity wins. First merge never deletes either library.",
  notConnected: "Bring your Stremio library, progress, History and addon collection into this profile."
});
const confDetails = Object.freeze({
  switch: "Switching accounts keeps everything already merged, then connects a replacement Stremio account.",
  disconnect: "Disconnecting stops sync and removes this device's credential. Everything already merged stays in Colosseum."
});
const connectedStates = new Set([
  "synced","syncing","reconnectRequired","syncFailed","paused","unavailable"
]);
const state = {
  status: "synced", confirmation: "", opened: true,
  viewport: "portrait", longFixture: false,
  linkedAccount: true
};
const url = new URL(window.location.href);
const requestedCapture = url.searchParams.get("capture");
if(presets[url.searchParams.get("preset")])ids.preset.value=url.searchParams.get("preset");
if (requestedCapture) {
  document.body.classList.add("capture");
  if (presets[requestedCapture]) ids.preset.value = requestedCapture;
}
const initialState = url.searchParams.get("state");
if (initialState && Object.prototype.hasOwnProperty.call(headers, initialState))
  ids.state.value = initialState;
if (url.searchParams.get("confirm") === "switch" || url.searchParams.get("confirm") === "disconnect")
  ids.confirm.value = url.searchParams.get("confirm");
if (url.searchParams.get("keyboard") === "1") ids.keyboardPreview.checked = true;
if (url.searchParams.get("long") === "1") ids.longFixture.checked = true;
if (url.searchParams.get("closed") === "1") ids.panelOpen.checked = false;
if (url.searchParams.get("font")) ids.fontScale.value = url.searchParams.get("font");
if (url.searchParams.get("ime")) ids.imeSize.value = url.searchParams.get("ime");
if (url.searchParams.get("inset") === "24") {
  ids.insetTop.value = 24;
  ids.insetBottom.value = 24;
}
for(const [key,id] of [["top","insetTop"],["bottom","insetBottom"],["left","insetLeft"],["right","insetRight"]])if(url.searchParams.has(key))ids[id].value=url.searchParams.get(key);
function numberValue(element, fallback, min, max) {
  const parsed = Number(element.value);
  return Number.isFinite(parsed) ? Math.min(max, Math.max(min, parsed)) : fallback;
}
function connected() { return state.linkedAccount; }
function setFixtureStatus(status) {
  state.status = status;
  state.linkedAccount = connectedStates.has(status);
  ids.state.value = status;
  if (!connected()) { state.confirmation = ""; ids.confirm.value = ""; }
}
function statusBadge(status) {
  if (["reconnectRequired","syncFailed","paused"].includes(status)) return "Needs attention";
  if (status === "connecting") return "Connecting";
  if (status === "syncing") return "Updating";
  return connected() ? "Connected" : "Not connected";
}
function primaryLabel(status) {
  if (status === "notConnected") return "Connect Stremio";
  if (status === "reconnectRequired") return "Reconnect Stremio";
  return "Sync now";
}
function revealFocusedControl(element) {
  if (!element || !state.opened) return;
  const outer = ids.panelScroll.getBoundingClientRect();
  const inner = element.getBoundingClientRect();
  if (inner.top < outer.top || inner.bottom > outer.bottom)
    element.scrollIntoView({block:"nearest",inline:"nearest"});
}
function setOpened(opened, focus = false) {
  state.opened = Boolean(opened);
  ids.panelOpen.checked = state.opened;
  ids.overlay.hidden = !state.opened;
  document.querySelector(".connections-page").inert=state.opened;
  ids.panel.setAttribute("aria-hidden", String(!state.opened));
  if (state.opened) {
    ids.panelScroll.scrollTop = 0;
    if (focus) {
      (ids.primaryAction.disabled?ids.cancelAction:ids.primaryAction).focus({preventScroll:true});
      revealFocusedControl(ids.primaryAction);
    }
  } else if (focus) {
    ids.openSettings.focus({preventScroll:false});
  }
  updateMetrics();
}
function updateMetrics() {
  const w = numberValue(ids.vw,412,320,2560);
  const h = numberValue(ids.vh,900,240,1800);
  const it = numberValue(ids.insetTop,0,0,120);
  const ib = numberValue(ids.insetBottom,0,0,120);
  const il = numberValue(ids.insetLeft,0,0,120);
  const ir = numberValue(ids.insetRight,0,0,120);
  const font = numberValue(ids.fontScale,1,.8,2);
  const imeWanted = numberValue(ids.imeSize,260,40,500);
  const ime = ids.keyboardPreview.checked
    ? Math.min(imeWanted, Math.max(0,h-it-ib-160)) : 0;
  const phone = Math.min(w,h) < 600;
  ids.frame.style.width = w + "px";
  ids.frame.style.height = h + "px";
  ids.frame.style.setProperty("--inset-top",it+"px");
  ids.frame.style.setProperty("--inset-bottom",ib+"px");
  ids.frame.style.setProperty("--inset-left",il+"px");
  ids.frame.style.setProperty("--inset-right",ir+"px");
  ids.frame.style.setProperty("--font-scale",String(font));
  ids.frame.style.setProperty("--ime-height",ime+"px");
  ids.frame.dataset.imeRequested=ids.keyboardPreview.checked?imeWanted:0;ids.frame.dataset.imeApplied=ime;
  ids.frame.style.setProperty("--panel-target-height",(state.confirmation ? 470 : 440)+"px");
  ids.frame.classList.toggle("phone",phone);
  ids.frame.classList.toggle("keyboard-open",ids.keyboardPreview.checked);
  const stageWidth = Math.max(0,w-il-ir);
  const stageHeight = Math.max(0,h-it-ib-ime);
  ids.metrics.textContent = w+" × "+h+" logical px · "+
    (phone?"phone reflow":"desktop/tablet layout")+
    " · available "+stageWidth+" × "+stageHeight+
    " · "+(state.opened?"centred panel":"panel closed")+
    (ids.keyboardPreview.checked?" · IME geometry only: requested "+imeWanted+"px, reserved "+ime+"px":"");
}
function render() {
  state.confirmation = ids.confirm.value;
  state.longFixture = ids.longFixture.checked;
  if (!connected() && state.confirmation) {
    state.confirmation = "";
    ids.confirm.value = "";
  }
  const status = state.status;
  const busy = status === "connecting" || status === "syncing";
  ids.panelHeading.textContent = headers[status] || headers.notConnected;
  ids.panelDetail.textContent = details[status] || details.notConnected;
  ids.accountValue.textContent = connected()
    ? (state.longFixture
        ? "illustrative.extra.long.stremio.profile.identifier.for.mobile-layout@example.invalid"
        : "Stremio demo profile")
    : "Not connected";
  ids.lastSyncValue.textContent = connected() ? "Oct 31, 8:42 PM" : "Never";
  const result = connected()
    ? (state.longFixture
        ? "Illustrative result: library, progress, History, and addon collection were reconciled; a long provider summary wraps here without changing any Colosseum control."
        : "Illustrative sync result: library and progress updated.")
    : "";
  ids.result.textContent = result;
  ids.result.hidden = !result;
  ids.confirmationText.textContent = confDetails[state.confirmation] || "";
  ids.confirmationText.hidden = !state.confirmation;
  const label = state.confirmation
    ? (state.confirmation === "switch" ? "Confirm switch" : "Confirm disconnect")
    : primaryLabel(status);
  ids.primaryAction.textContent = label;
  ids.primaryAction.disabled = busy || status === "unavailable" || status === "paused";
  ids.switchAction.hidden = !connected() || Boolean(state.confirmation);
  ids.disconnectAction.hidden = !connected() || Boolean(state.confirmation);
  const cancelLabel = state.confirmation ? "Cancel" : "Close";
  ids.cancelAction.textContent = cancelLabel;
  ids.headerClose.textContent = cancelLabel;
  ids.backdropStatus.textContent = statusBadge(status);
  updateMetrics();
}
function selectConfirmation(value) {
  if (!connected()) return;
  ids.confirm.value = value;
  render();
  ids.panelScroll.scrollTop = 0;
  (ids.primaryAction.disabled?ids.cancelAction:ids.primaryAction).focus({preventScroll:true});
  revealFocusedControl(ids.primaryAction);
}
function closeOrCancel() {
  if (state.confirmation) {
    ids.confirm.value = "";
    render();
    (ids.primaryAction.disabled?ids.cancelAction:ids.primaryAction).focus({preventScroll:true});
    revealFocusedControl(ids.primaryAction);
  } else {
    setOpened(false,true);
  }
}
function primaryClick() {
  if (ids.primaryAction.disabled) return;
  Batch4.emit(state.confirmation?state.confirmation+"Account":state.status==="notConnected"||state.status==="reconnectRequired"?"connectAccount":"syncStremioNow",{fixture:true});
  if (state.confirmation === "switch") {
    ids.confirm.value = "";
    setFixtureStatus("connecting"); // represents switchAccount, no call performed
  } else if (state.confirmation === "disconnect") {
    ids.confirm.value = "";
    setFixtureStatus("notConnected"); // represents disconnectCurrentProfile
  } else if (state.status === "notConnected" || state.status === "reconnectRequired") {
    setFixtureStatus("connecting"); // represents connectAccount
  } else {
    setFixtureStatus("syncing"); // represents syncStremioNow
  }
  render();
  ids.panelScroll.scrollTop = 0;
  if(ids.primaryAction.disabled)ids.cancelAction.focus();
}
function choosePreset(preset) {
  if (!presets[preset]) return;
  ids.vw.value = presets[preset][0];
  ids.vh.value = presets[preset][1];
  updateMetrics();
}
ids.preset.addEventListener("change",()=>choosePreset(ids.preset.value));
for (const id of ["vw","vh"]) ids[id].addEventListener("input",()=>{
  const [w,h] = presets[ids.preset.value] || [];
  if (Number(ids.vw.value)!==w || Number(ids.vh.value)!==h)
    ids.preset.value = "custom";
  updateMetrics();
});
for (const id of ["fontScale","imeSize","insetTop","insetBottom","insetLeft","insetRight","keyboardPreview"])
  ids[id].addEventListener("input",updateMetrics);
ids.state.addEventListener("change",()=>{
  setFixtureStatus(ids.state.value);
  render();
});
ids.confirm.addEventListener("change",()=>{
  render();
  ids.panelScroll.scrollTop = 0;
});
ids.longFixture.addEventListener("change",render);
ids.panelOpen.addEventListener("change",()=>setOpened(ids.panelOpen.checked,true));
ids.openSettings.addEventListener("click",()=>setOpened(true,true));
ids.primaryAction.addEventListener("click",primaryClick);
ids.switchAction.addEventListener("click",()=>selectConfirmation("switch"));
ids.disconnectAction.addEventListener("click",()=>selectConfirmation("disconnect"));
ids.cancelAction.addEventListener("click",closeOrCancel);
ids.headerClose.addEventListener("click",closeOrCancel);
for (const id of ["primaryAction","switchAction","disconnectAction","cancelAction","headerClose"])
  ids[id].addEventListener("focus",()=>revealFocusedControl(ids[id]));
document.addEventListener("keydown",event=>{
  if(!state.opened)return;
  if(event.key==="Tab"){Batch4.contain(event,ids.panel);return;}
  if (event.key !== "Escape") return;
  event.preventDefault();
  closeOrCancel();
});
ids.reset.addEventListener("click",()=>{
  ids.preset.value="portrait";choosePreset("portrait");
  ids.state.value="synced";setFixtureStatus("synced");
  ids.confirm.value="";ids.longFixture.checked=false;
  ids.keyboardPreview.checked=false;ids.panelOpen.checked=true;
  ids.fontScale.value=1;ids.imeSize.value=260;
  for (const id of ["insetTop","insetBottom","insetLeft","insetRight"]) ids[id].value=0;
  render();setOpened(true,true);
});
state.viewport=ids.preset.value;
choosePreset(ids.preset.value);
setFixtureStatus(ids.state.value);
render();
setOpened(ids.panelOpen.checked,true);
// Observe snapshot geometry from an isolated headless browser without touching the app.
window.__stremioMockAudit = () => {
  const frame = ids.frame.getBoundingClientRect();
  const panel = ids.panel.getBoundingClientRect();
  const scroll = ids.panelScroll;
  const actions = ["primaryAction","switchAction","disconnectAction","cancelAction","headerClose"]
    .filter(id=>!ids[id].hidden && getComputedStyle(ids[id]).display!=="none")
    .map(id=>{ const r=ids[id].getBoundingClientRect(); return {id,w:r.width,h:r.height,insidePanel:r.left>=panel.left && r.right<=panel.right}; });
  return {frame:{w:frame.width,h:frame.height},panel:{x:panel.x,y:panel.y,w:panel.width,h:panel.height},
    availableScroll:scroll.scrollHeight-scroll.clientHeight,
    horizontalOverflow:scroll.scrollWidth>scroll.clientWidth+1,
    actions,keyboardOpen:ids.keyboardPreview.checked};
};