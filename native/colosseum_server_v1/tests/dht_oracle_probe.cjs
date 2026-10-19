// Executes the pinned webpack DHT module with a deterministic datagram/timer
// seam. This is source execution, not a reimplementation of its behavior.
const fs = require('fs');
const crypto = require('crypto');
const { EventEmitter } = require('events');
const assert = require('assert');
const oracle = fs.readFileSync(process.argv[2], 'utf8');
const expectedHash = '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f';
assert.strictEqual(crypto.createHash('sha256').update(oracle).digest('hex'), expectedHash, 'pinned source hash');
const entry = '__webpack_require__(__webpack_require__.s = 564)';
assert.strictEqual(oracle.split(entry).length, 2, 'single exact webpack entry seam');
const source = oracle.replace(entry, '__capture.require = __webpack_require__');
const hash = Buffer.from('0123456789abcdef0123456789abcdef01234567', 'hex');
const output = {};

function world(bootstrap = false) {
    const nextTicks = [];
    const timers = new Map();
    let timerId = 0;
    let socket;
    class Datagram extends EventEmitter {
        constructor() { super(); this.sent = []; socket = this; }
        send(bytes, offset, length, port, host, callback) {
            this.sent.push({ bytes: Buffer.from(bytes.slice(offset, offset + length)), port, host });
            if (callback) callback(null);
        }
        bind() { this.emit('listening'); }
        address() { return { address: '127.0.0.1', port: 49000 }; }
        close() { this.emit('close'); }
    }
    const timer = (callback, interval) => { const id = ++timerId; timers.set(id, { callback, interval }); return id; };
    const fakeProcess = Object.create(process);
    fakeProcess.nextTick = callback => nextTicks.push(callback);
    const sandbox = {
        Buffer, console, process: fakeProcess,
        require: name => name === 'dgram' ? { createSocket: () => new Datagram() } : require(name),
        setInterval: timer, clearInterval: id => timers.delete(id),
        setTimeout: timer, clearTimeout: id => timers.delete(id),
    };
    const capture = {};
    new Function('require', 'process', 'setInterval', 'clearInterval', 'setTimeout', 'clearTimeout', '__capture', source)(
        sandbox.require, sandbox.process, sandbox.setInterval, sandbox.clearInterval, sandbox.setTimeout, sandbox.clearTimeout, capture);
    const DHT = capture.require(290);
    const bencode = capture.require(181);
    const dht = new DHT({ concurrency: 10, nodeId: Buffer.alloc(20, 1), bootstrap });
    function flush() { while (nextTicks.length) nextTicks.shift()(); }
    function sent() { return socket.sent.splice(0).map(packet => ({ ...packet, message: bencode.decode(packet.bytes) })); }
    function inject(message, port = 60000, host = '127.0.0.1') {
        socket.emit('message', bencode.encode(message), { address: host, port });
    }
    function query(method, args = {}, port = 60001, id = Buffer.alloc(20, 'z')) {
        inject({ t: Buffer.from('qq'), y: 'q', q: method, a: { id, ...args } }, port);
        return sent().filter(packet => packet.message.y.toString() !== 'q').at(-1)?.message;
    }
    function reply(packet, r, port = packet.port) { inject({ t: packet.message.t, y: 'r', r }, port); }
    function tick(id, times = 1) { for (let i = 0; i < times; ++i) timers.get(id).callback(); }
    function addNode(id, port) { dht.addNode({ id, host: '127.0.0.1', port }); }
    flush();
    return { dht, socket, flush, sent, inject, query, reply, tick, addNode, timers };
}
function compact(port) { const data = Buffer.from([127, 0, 0, 1, 0, 0]); data.writeUInt16BE(port, 4); return data; }

{
    const w = world([{ host: '127.0.0.1', port: 60000 }]);
    const bootstrap = w.sent()[0];
    output.bootstrapMethod = bootstrap.message.q.toString();
    w.reply(bootstrap, { id: Buffer.alloc(20, 'a') }); w.flush(); w.sent();
    const peers = []; w.dht.on('peer', peer => peers.push(peer));
    const abort = w.dht.lookup(hash); const first = w.sent()[0];
    w.dht.lookup(hash); const second = w.sent()[0];
    output.persistentNode = first.message.a.id.equals(second.message.a.id);
    abort();
    w.reply(first, { id: Buffer.alloc(20, 'a'), values: [compact(6000)] });
    w.reply(second, { id: Buffer.alloc(20, 'a'), values: [compact(6001)] });
    w.flush(); output.overlapAbortPeers = peers.length;
}
{
    const w = world();
    const token = w.query('get_peers', { info_hash: hash }).r.token;
    w.query('announce_peer', { info_hash: hash, port: 6000, token });
    const peers = []; w.dht.on('peer', peer => peers.push(peer));
    const abort = w.dht.lookup(hash); abort(); w.flush();
    output.cachedPeerAfterAbort = peers.length;
}
{
    const w = world();
    const token = w.query('get_peers', { info_hash: hash }).r.token;
    w.tick(w.dht._interval);
    output.tokenFirstRotationAccepted = w.query('announce_peer', { info_hash: hash, port: 6000, token }).y.toString() === 'r';
    w.tick(w.dht._interval);
    output.tokenSecondRotationRejected = w.query('announce_peer', { info_hash: hash, port: 6000, token }).e[1].toString().includes('bad token');
    const currentToken = w.query('get_peers', { info_hash: hash }).r.token;
    for (let port = 6001; port <= 6104; ++port) w.query('announce_peer', { info_hash: hash, port, token: currentToken });
    const sample = w.query('get_peers', { info_hash: hash }).r.values;
    output.peerSampleSize = sample.length;
    output.peerSampleUnique = new Set(sample.map(value => value.toString('hex'))).size;
}
{
    const w = world();
    w.addNode(Buffer.alloc(20, 'a'), 60000);
    let finished = false;
    w.dht.lookup(hash, () => { finished = true; }); w.flush(); w.sent();
    output.timeoutActiveAfterTicks = [];
    for (let i = 0; i < 5; ++i) {
        w.tick(w.dht._rpc.socket._timer); w.flush();
        output.timeoutActiveAfterTicks.push(!finished);
    }
}
{
    const w = world();
    const value = Buffer.from('payload');
    const target = crypto.createHash('sha1').update(Buffer.concat([Buffer.from('7:'), value])).digest();
    const token = w.query('get', { target }).r.token;
    w.query('put', { v: value, token });
    output.immutableValue = w.query('get', { target }).r.v.toString();
    const rejected = w.query('put', { v: value, token, k: Buffer.alloc(32, 'k'), sig: Buffer.alloc(64, 's') });
    output.mutableVerificationUnsupported = rejected.e[1].toString() === 'verification not supported';
}
{
    const w = world();
    const ids = [];
    for (let i = 0; i < 21; ++i) {
        const id = Buffer.alloc(20, 1); id[0] ^= 0x80; id[18] = i >> 8; id[19] = i;
        ids.push(id); w.addNode(id, 6000 + i);
    }
    const initial = w.sent()[0];
    assert.strictEqual(initial.port, 6009, 'source probes oldest ten in reverse order');
    w.tick(w.dht._rpc.socket._timer, 5);
    const retry = w.sent()[0];
    assert.strictEqual(retry.port, 6009, 'source retries failed contact once');
    w.reply(retry, { id: ids[9] });
    output.routingEvictsAfterSuccessfulRetry = !w.dht.nodes.get(ids[9]);
    output.routingKeepsNewCandidate = !!w.dht.nodes.get(ids[20]);
}
{
    const w = world();
    w.addNode(Buffer.alloc(20, 'a'), 60000);
    w.inject({ t: Buffer.from('zz'), y: 'r', r: { id: Buffer.alloc(20, 'u'), values: [compact(7000)] } }, 60002);
    output.unexpectedResponseLearnsNode = !!w.dht.nodes.get(Buffer.alloc(20, 'u'));
    const peers = []; w.dht.on('peer', peer => peers.push(peer));
    w.dht.lookup(hash); w.flush();
    const requests = w.sent();
    const toRouter = requests.find(packet => packet.port === 60000);
    const toLearned = requests.find(packet => packet.port === 60002);
    w.reply(toRouter, { id: Buffer.alloc(20, 'a'), values: [compact(7001)] }, 60003);
    output.sameHostDifferentPortEmits = peers.length;
    w.reply(toLearned, { values: [compact(7002)] });
    output.missingNodeIdReplyEmits = peers.length;
}
{
    const w = world();
    const token = w.query('get_peers', { info_hash: Buffer.alloc(20, 'h') }).r.token;
    const value = i => Buffer.from(`value-${i}`);
    const valueKey = i => crypto.createHash('sha1').update(Buffer.concat([Buffer.from(`${value(i).length}:`), value(i)])).digest();
    const getValue = i => w.query('get', { target: valueKey(i) }).r.v;
    for (let i = 0; i < 1000; ++i) w.query('put', { token, v: value(i) });
    getValue(0);
    w.query('put', { token, v: value(1000) });
    output.valueLruTouchedSurvives = !!getValue(0);
    output.valueLruOldestEvicted = !getValue(1);
    output.valueLruNewestPresent = !!getValue(1000);
    const peerKey = i => { const key = Buffer.alloc(20); key.writeUInt32BE(i, 16); return key; };
    const getPeers = i => w.query('get_peers', { info_hash: peerKey(i) }).r.values;
    for (let i = 0; i < 10000; ++i) w.query('announce_peer', { token, info_hash: peerKey(i), port: 6000 });
    getPeers(0);
    w.query('announce_peer', { token, info_hash: peerKey(10000), port: 6000 });
    output.peerLruTouchedSurvives = !!getPeers(0);
    output.peerLruOldestEvicted = !getPeers(1);
    output.peerLruNewestPresent = !!getPeers(10000);
}

console.log(JSON.stringify(output));
