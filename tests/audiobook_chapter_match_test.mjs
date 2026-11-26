import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { runInNewContext } from 'node:vm';

const moduleUrl = new URL('../qml/audiobook/ChapterMatch.js', import.meta.url);
const source = readFileSync(moduleUrl, 'utf8').replace(/^\.pragma library\r?\n/m, '');
const { match } = runInNewContext(`${source}\n;({ match })`, {}, { filename: moduleUrl.pathname });

function audio(label, index) {
    return { label, kind: 'seek', index, time: index * 90 };
}

function toc(label, index) {
    return { label, index, href: `text/ch${index}.xhtml` };
}

function check(audioRows, ebookRows, expectedPairs) {
    const result = match(audioRows, ebookRows);
    assert.equal(result.aligned, expectedPairs.length > 0);
    assert.deepEqual({ ...result.audioToEbook }, Object.fromEntries(expectedPairs));
    assert.deepEqual({ ...result.ebookToAudio }, Object.fromEntries(
        expectedPairs.map(([audioIndex, ebookIndex]) => [ebookIndex, audioIndex])
    ));
}

test('matches real rows using normalized titles and zero-based array positions', () => {
    check([
        audio('CHAPTER 01 — Stormblessed!', 100),
        audio('Interlude I–1: Ishikk', 101),
        audio('Chapter IV. A Soldier’s Duty', 102),
        audio('Chapter 5: Échos du passé', 103),
    ], [
        toc('Stormblessed', 20),
        toc('Interlude: ISHIKK', 21),
        toc("A Soldier's Duty", 22),
        toc('E\u0301chos du passe\u0301', 23),
    ], [[0, 0], [1, 1], [2, 2], [3, 3]]);
});

test('accepts strings and rows with a title property', () => {
    check(['Prologue', { title: 'Heavy Stones' }], [
        { title: 'Prologue' }, 'Chapter 5: Heavy Stones',
    ], [[0, 0], [1, 1]]);
});

test('permits a clear majority and omits unrelated or generic rows from both maps', () => {
    check(['01', 'Prologue', 'Stormblessed', 'Heavy Stones', 'Epilogue', 'Part 2'], [
        'Prologue', 'Stormblessed', 'Illustrations', 'Heavy Stones', 'Epilogue',
    ], [[1, 0], [2, 1], [3, 3], [4, 4]]);
});

test('rejects a majority that is too small in either complete sequence', () => {
    const shared = ['Foolish Questions', 'Stormblessed', 'Heavy Stones'];
    const extra = ['The Hour of Chains', 'Impossible', 'A Good Day', 'Purpose'];
    check(shared, [...shared, ...extra], []);
    check([...shared, ...extra], shared, []);
    check([...shared, ...extra.slice(0, 2)], [...shared, ...extra.slice(2)], []);
});

test('rejects crossed titles even when most matches could be kept', () => {
    check(['Prologue', 'Heavy Stones', 'Stormblessed', 'Epilogue'], [
        'Prologue', 'Stormblessed', 'Heavy Stones', 'Epilogue',
    ], []);
});

test('rejects generic markers even when both media use the same numbers', () => {
    const markers = ['01', 'Track 2', 'Part 03', 'Disc II', 'Chapter 5', 'Interlude I–1'];
    check(markers.map(audio), markers.map(toc), []);
});

test('does not match file rows or bare filenames even with meaningful words', () => {
    const chapterTitles = ['Prologue', 'Stormblessed', 'Heavy Stones'];
    check(chapterTitles.map((label, index) => ({ label, kind: 'file', index, time: 0 })),
        chapterTitles.map(toc), []);
    check(chapterTitles.map(title => `${title}.mp3`), chapterTitles.map(title => `${title}.mp3`), []);
});

test('omits repeated titles when the counterpart cannot be uniquely identified', () => {
    check(['Prologue', 'Stormblessed', 'Interlude', 'Heavy Stones', 'Epilogue', 'Interlude'], [
        'Prologue', 'Interlude', 'Stormblessed', 'Heavy Stones', 'Interlude', 'Epilogue',
    ], [[0, 0], [1, 2], [3, 3], [4, 5]]);
    check(['Prologue', 'Stormblessed', 'A Warning', 'Heavy Stones', 'Epilogue', 'A Warning'], [
        'Prologue', 'A Warning', 'Stormblessed', 'Heavy Stones', 'A Warning', 'Epilogue',
    ], [[0, 0], [1, 2], [3, 3], [4, 5]]);
});

test('aligns a single actual marker but rejects synthetic or unknown provenance', () => {
    const ebook = [toc('Stormblessed', 0)];
    check([audio('Stormblessed', 0)], ebook, [[0, 0]]);
    check([{ ...audio('Stormblessed', 0), synthetic: true }], ebook, []);
    check(['Stormblessed'], ebook, []);
    check([null, audio('Stormblessed', 1), {}], ebook, []);
    check([], ebook, []);
    check([audio('Stormblessed', 0)], null, []);
});
