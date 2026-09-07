'use strict';

function createRandom(input = 1) {
  if (typeof input === 'function') return input;
  if (Array.isArray(input)) {
    let index = 0;
    return () => input[index++ % input.length];
  }
  let state = Number(input) >>> 0;
  return () => {
    state = (state + 0x6D2B79F5) >>> 0;
    let value = Math.imul(state ^ state >>> 15, 1 | state);
    value ^= value + Math.imul(value ^ value >>> 7, 61 | value);
    return ((value ^ value >>> 14) >>> 0) / 4294967296;
  };
}

module.exports = { createRandom };
