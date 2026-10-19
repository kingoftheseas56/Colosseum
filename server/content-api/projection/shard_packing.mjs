// Byte-budget shard packing shared by the catalogue publisher and its tests.
// A packed chunk is measured as the exact compact JSON bytes of the final file.
export const SEARCH_SHARD_BYTES = 56 * 1024;

export function packOffsets(payloads, budget = SEARCH_SHARD_BYTES) {
    const ranges = [];
    let start = -1, bytes = 2;
    for (let i = 0; i < payloads.length; ++i) {
        const size = Buffer.byteLength(JSON.stringify(payloads[i]));
        const next = (start < 0 ? 2 : bytes + 1) + size;
        if (next > budget) {
            if (size + 2 > budget) throw Error('Single source row exceeds the search shard byte budget');
            ranges.push([start, i - 1]);
            start = i;
            bytes = size + 2;
        } else {
            if (start < 0) start = i;
            bytes = next;
        }
    }
    if (start >= 0) ranges.push([start, payloads.length - 1]);
    return ranges;
}

export function packLookupFiles(entries, budget = SEARCH_SHARD_BYTES) {
    const files = [];
    let parts = [], bytes = 2, first = '', last = '';
    const flush = () => {
        if (parts.length) files.push({first, last, body: '{' + parts.join(',') + '}'});
        parts = [];
        bytes = 2;
    };
    for (const [key, value] of entries) {
        const part = JSON.stringify(key) + ':' + JSON.stringify(value);
        const size = Buffer.byteLength(part), next = bytes + (parts.length ? 1 : 0) + size;
        if (next > budget) {
            if (size + 2 > budget) throw Error('Single lookup entry exceeds the search shard byte budget');
            flush();
        }
        if (!parts.length) first = key;
        last = key;
        bytes += (parts.length ? 1 : 0) + size;
        parts.push(part);
    }
    flush();
    return files;
}
