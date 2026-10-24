// The pinned Theatre episode chrome sits above episode rows while they scroll.
// Its background must be opaque on desktop too, so row art and text never bleed
// through the fixed header band.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function objectBody(source, anchor, type) {
  const anchorAt = source.indexOf(anchor);
  assert.notEqual(anchorAt, -1, `missing ${anchor}`);
  const openAt = source.lastIndexOf(`${type} {`, anchorAt);
  assert.notEqual(openAt, -1, `missing ${type} containing ${anchor}`);
  const braceAt = source.indexOf("{", openAt);
  let depth = 0;
  let quote = "";
  let escaped = false;
  for (let i = braceAt; i < source.length; i++) {
    const ch = source[i];
    if (quote) {
      if (escaped) escaped = false;
      else if (ch === "\\") escaped = true;
      else if (ch === quote) quote = "";
      continue;
    }
    if (ch === '"' || ch === "'") { quote = ch; continue; }
    if (ch === "{") depth++;
    else if (ch === "}" && --depth === 0) return source.slice(braceAt + 1, i);
  }
  assert.fail(`unclosed ${type} containing ${anchor}`);
}

function colorLine(body, name) {
  const match = body.match(/^\s*color:\s*(.+)$/m);
  assert.ok(match, `${name} scrim must define its background color`);
  return match[1].trim();
}

function assertOpaqueRgba(color, name) {
  const match = color.match(/Qt\.rgba\(\s*[^,]+,\s*[^,]+,\s*[^,]+,\s*([^)]+)\)/);
  assert.ok(match, `${name} desktop scrim must retain its Qt.rgba tint`);
  assert.equal(Number(match[1]), 1, `${name} desktop scrim must be fully opaque`);
}

const legacy = fs.readFileSync(path.join(repo, "qml", "TheatreSeries.qml"), "utf8");
const rebuilt = fs.readFileSync(path.join(repo, "qml", "TheatreSeriesView.qml"), "utf8");

const legacyScrim = objectBody(legacy, "id: chromeScrim", "Rectangle");
const legacyColor = colorLine(legacyScrim, "TheatreSeries");
assert.match(legacyColor, /page\.phoneNarrow\s*\?\s*"#08090d"\s*:/,
  "TheatreSeries must preserve the existing opaque phone tint");
assertOpaqueRgba(legacyColor, "TheatreSeries");
assert.match(legacyScrim, /opacity:\s*seasonChromeFloat\.pinned\s*\?\s*1\.0\s*:\s*0\.0/,
  "legacy scrim must still fade with the pinned state");

const viewOwner = objectBody(rebuilt, "id: ledgerBarFloat", "Item");
const viewBackground = objectBody(viewOwner, "anchors.fill: parent", "Rectangle");
const viewColor = colorLine(viewBackground, "TheatreSeriesView");
assertOpaqueRgba(viewColor, "TheatreSeriesView");
assert.match(viewBackground, /opacity:\s*ledgerBarFloat\.pinned\s*\?\s*1\.0\s*:\s*0\.0/,
  "rebuilt scrim must still fade with the pinned state");

console.log("theatre_pinned_header_scrim_test: PASS (legacy + rebuilt desktop scrims are opaque; pinned fades preserved)");
