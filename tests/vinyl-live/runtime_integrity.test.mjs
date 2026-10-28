import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const headerUrl = new URL('../../native/player/StreamRuntimeIntegrity.h', import.meta.url);
const header = await readFile(headerUrl, 'utf8');
const helperMatch = header.match(/R"JS\(([\s\S]*?)\)JS"/);
assert.ok(helperMatch, 'integrity header must expose its helper source as a JS raw string');

function makeHelpers() {
    const context = vm.createContext({});
    vm.runInContext(helperMatch[1], context, { filename: 'StreamRuntimeIntegrity.h' });
    return context;
}

function makeEngine() {
    const events = [];
    return { events, emit: name => events.push(name) };
}

function makeWire(peerAddress) {
    return { peerAddress, destroys: 0, destroy() { this.destroys += 1; } };
}

function failWholePiece(context, engine, piece, index = 0) {
    context.colosseumRejectCorruptSupplier(engine, piece, {
        success: false,
        start: index,
        end: index + 1,
    });
}

test('a sole corrupt whole-piece supplier is disconnected and blocked on reconnect', () => {
    const context = makeHelpers();
    const engine = makeEngine();
    const piece = { get: () => null };
    const supplier = makeWire('peer:1001');

    context.colosseumRecordPieceSupplier(piece, 0, supplier);
    failWholePiece(context, engine, piece, 3);

    assert.equal(supplier.destroys, 1);
    assert.equal(engine.events.length, 1);
    assert.equal(engine.colosseumCorruptPeers['peer:1001'], true);

    const reconnect = makeWire('peer:1001');
    assert.equal(context.colosseumRejectKnownCorruptPeer(engine, reconnect), true);
    assert.equal(reconnect.destroys, 1);
});

test('mixed suppliers and virtual subpieces are not attributed to one peer', () => {
    const context = makeHelpers();
    const engine = makeEngine();
    const firstPiece = { get: () => null };
    const first = makeWire('peer:first');
    const second = makeWire('peer:second');
    const third = makeWire('peer:third');
    context.colosseumRecordPieceSupplier(firstPiece, 0, first);
    context.colosseumRecordPieceSupplier(firstPiece, 1, second);
    context.colosseumRecordPieceSupplier(firstPiece, 2, third);
    assert.equal(firstPiece.colosseumSuppliers.length, 2, 'supplier attribution is bounded');
    failWholePiece(context, engine, firstPiece, 4);

    const virtualPiece = { get: () => null };
    const virtualSupplier = makeWire('peer:virtual');
    context.colosseumRecordPieceSupplier(virtualPiece, 0, virtualSupplier);
    context.colosseumRejectCorruptSupplier(engine, virtualPiece, {
        success: false,
        start: 5,
        end: 9,
    });

    assert.equal(first.destroys, 0);
    assert.equal(second.destroys, 0);
    assert.equal(third.destroys, 0);
    assert.equal(virtualSupplier.destroys, 0);
    assert.equal(engine.events.length, 0);
    assert.equal(engine.colosseumCorruptPeers, undefined);
});

test('duplicate and already-present block callbacks do not frame another supplier', () => {
    const context = makeHelpers();
    const engine = makeEngine();
    const presentReservation = Symbol('already-present');
    const piece = { get: reservation => reservation === presentReservation ? Buffer.from('verified') : null };
    const actualSupplier = makeWire('peer:actual');
    const lateDuplicate = makeWire('peer:late-duplicate');

    context.colosseumRecordPieceSupplier(piece, 0, actualSupplier);
    context.colosseumRecordPieceSupplier(piece, 0, actualSupplier);
    context.colosseumRecordPieceSupplier(piece, presentReservation, lateDuplicate);
    assert.equal(piece.colosseumSuppliers.length, 1);
    failWholePiece(context, engine, piece, 7);

    assert.equal(actualSupplier.destroys, 1);
    assert.equal(lateDuplicate.destroys, 0);
    assert.equal(engine.colosseumCorruptPeers['peer:actual'], true);
    assert.equal(engine.colosseumCorruptPeers['peer:late-duplicate'], undefined);
});

test('successful verification does not ban or disconnect its supplier', () => {
    const context = makeHelpers();
    const engine = makeEngine();
    const piece = { get: () => null };
    const supplier = makeWire('peer:good');
    context.colosseumRecordPieceSupplier(piece, 0, supplier);
    context.colosseumRejectCorruptSupplier(engine, piece, { success: true, start: 1, end: 2 });

    assert.equal(supplier.destroys, 0);
    assert.equal(engine.events.length, 0);
    assert.equal(engine.colosseumCorruptPeers, undefined);
});

test('retained corrupt-peer endpoint identities are capped at 256 per engine', () => {
    const context = makeHelpers();
    const engine = makeEngine();
    const suppliers = [];
    for (let i = 0; i < 257; i += 1) {
        const wire = makeWire(`peer:${i}`);
        suppliers.push(wire);
        const piece = { get: () => null };
        context.colosseumRecordPieceSupplier(piece, 0, wire);
        failWholePiece(context, engine, piece, i);
    }

    assert.equal(Object.keys(engine.colosseumCorruptPeers).length, 256);
    assert.equal(engine.colosseumCorruptPeers['peer:255'], true);
    assert.equal(engine.colosseumCorruptPeers['peer:256'], undefined);
    assert.equal(suppliers[256].destroys, 1);
    const unretainedReconnect = makeWire('peer:256');
    assert.equal(context.colosseumRejectKnownCorruptPeer(engine, unretainedReconnect), false);
    assert.equal(unretainedReconnect.destroys, 0);
});
