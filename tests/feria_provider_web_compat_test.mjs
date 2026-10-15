import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../native/feria/provider-host/FeriaProviderHost.cpp', import.meta.url), 'utf8');
const match = source.match(/kProviderWebCompatibilityScript\[\]\s*=\s*LR"JS\(([\s\S]*?)\)JS"/);
assert.ok(match, 'WebView2 must install provider compatibility before site scripts run');

function fixture(host, withBridge = true) {
    const bridge = {hostObjects: {sync: new Proxy({}, {get() { throw new Error('Element not found. (0x80070490)'); }})}};
    const chrome = withBridge ? {webview: bridge} : {};
    const context = vm.createContext({location: {hostname: host}, chrome});
    context.window = context;
    return {context, chrome, bridge};
}
for (const host of ['primevideo.com', 'www.primevideo.com', 'atv-ps-eu.primevideo.com', 'WWW.PRIMEVIDEO.COM']) {
    const page = fixture(host);
    assert.throws(() => page.chrome.webview.hostObjects.sync.PrimeVideoWinRTComponent, /0x80070490/);
    vm.runInContext(match[1], page.context);
    assert.equal(page.chrome.webview, undefined, `${host} must use the web player instead of probing the absent native Prime component`);
}
for (const host of ['youtube.com', 'www.netflix.com', 'www.hotstar.com', 'www.amazon.in', 'evilprimevideo.com', 'primevideo.com.example.org']) {
    const page = fixture(host);
    vm.runInContext(match[1], page.context);
    assert.equal(page.chrome.webview, page.bridge, `${host} must retain its WebView2 bridge`);
}
const ordinaryBrowser = fixture('www.primevideo.com', false);
vm.runInContext(match[1], ordinaryBrowser.context);
assert.equal(ordinaryBrowser.chrome.webview, undefined);
const noChrome = vm.createContext({location: {hostname: 'www.primevideo.com'}});
noChrome.window = noChrome;
vm.runInContext(match[1], noChrome);
console.log('Feria provider compatibility: Prime native-component failure avoided; other providers and ordinary browsers preserved');
