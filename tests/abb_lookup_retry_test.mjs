import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const script = fs.readFileSync(new URL('../qml/AbbApi.js', import.meta.url), 'utf8')
    .replace(/^\.pragma library\s*$/m, '');
const requests = [];
const timers = [];
let time = 0;
class FakeRequest {
    static DONE = 4;

    open(method, url) {
        this.method = method;
        this.url = url;
    }

    setRequestHeader() {}
    send(payload) {
        this.payload = payload;
        requests.push(this);
    }

    respond(status, body) {
        this.readyState = FakeRequest.DONE;
        this.status = status;
        this.responseText = JSON.stringify(body);
        this.onreadystatechange();
    }
}
const adapter = vm.createContext({
    XMLHttpRequest: FakeRequest,
    Date: { now: () => time },
    Qt: {
        application: {},
        createQmlObject() {
            const timer = { triggered: { connect() {} }, start() {}, stop() {}, destroy() {} };
            timers.push(timer);
            return timer;
        }
    },
    console: { warn() {} }
});
vm.runInContext(script, adapter);

function search() {
    requests.length = 0;
    timers.length = 0;
    time = 0;
    const replies = [];
    adapter.resolveAudiobook('Joe Country', 'Mick Herron', (...args) => replies.push(args));
    assert.equal(requests.length, 1);
    return replies;
}

const row = { slug: 'joe-country', title: 'Joe Country', author: 'Mick Herron' };
const success = rows => ({ contractVersion: 1, sourceId: 'audiobookbay', result: { page: 1, rows } });
const recovered = search();
time = 1500;
requests[0].respond(502, {});
assert.equal(requests.length, 2);
assert.equal(requests[1].payload, requests[0].payload);
assert.equal(requests[1].url, requests[0].url);
assert.equal(timers[1].interval, timers[0].interval - time);
assert.equal(recovered.length, 0);
requests[1].respond(200, success([row]));
assert.equal(recovered.length, 1);
assert.equal(recovered[0][0].rows[0].slug, row.slug);

const unavailable = search();
requests[0].respond(502, {});
requests[1].respond(502, {});
assert.equal(requests.length, 2);
assert.deepEqual(unavailable, [[null, 'http-502']]);

const late = search();
time = 21000;
requests[0].respond(503, {});
assert.equal(requests.length, 1);
assert.deepEqual(late, [[null, 'http-503']]);

const rejected = search();
requests[0].respond(400, {});
assert.equal(requests.length, 1);
assert.deepEqual(rejected, [[null, 'http-400']]);

const empty = search();
requests[0].respond(200, success([]));
assert.equal(requests.length, 1);
assert.equal(empty.length, 1);
assert.equal(empty[0][0].rows.length, 0);
assert.equal(empty[0][1], undefined);

console.log('PASS AudioBookBay retry, deadline, and empty results');
