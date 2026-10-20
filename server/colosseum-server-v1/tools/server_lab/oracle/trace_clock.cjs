'use strict';

function createClock(input = {}) {
  if (typeof input === 'function') return { now: input, tick: input };
  let current = Number(input.startMs ?? input.nowMs ?? 0);
  const stepMs = Number(input.stepMs ?? 0);
  if (!Number.isFinite(current) || !Number.isFinite(stepMs)) throw new TypeError('clock values must be finite numbers');
  return {
    now() { const value = current; current += stepMs; return value; },
    tick() { current += stepMs; return current; },
    peek() { return current; },
  };
}

module.exports = { createClock };
