import fs from 'node:fs';
import assert from 'node:assert/strict';
const root = process.argv[2] || '.';
const account = fs.readFileSync(`${root}/qml/account/AccountCenter.qml`, 'utf8');
const scopes = account.match(/privacySearchHistoryScopes:\s*\[([^\]]+)\]/);
assert.ok(scopes, 'existing account search-history scope list exists');
for (const scope of ['biblio', 'tankoban', 'theatre', 'vinyl'])
    assert.ok(scopes[1].includes(`"${scope}"`), `shared clear includes ${scope}`);
assert.match(account, /searchHistoryStore\.clearAllScopes\(privacySearchHistoryScopes\)/);
console.log('PASS Vinyl shared search privacy contract: 6 checks');
