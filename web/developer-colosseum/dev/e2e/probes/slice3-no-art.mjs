// Derive a degraded presentation fixture from a real recorded movie feed.
// The original native recording remains untouched; fixture files stay ignored.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const fixtures = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../fixtures');
const source = fs.readdirSync(fixtures).filter(name => name.startsWith('detail.theatre__') && name.endsWith('.json'))
  .map(name => JSON.parse(fs.readFileSync(path.join(fixtures, name), 'utf8')))
  .find(record => record.params?.id === 'tt0133093' && record.params?.title === 'The Matrix');
if (!source) throw new Error('Record the tagged Matrix feed before deriving the no-art fixture.');
const record = structuredClone(source);
record.params.title = 'No art fixture';
for (const entry of record.events) {
  const event = entry.event || {};
  const hero = event.section?.id === 'hero' ? event.section
    : (event.sections || []).find(section => section.id === 'hero');
  if (!hero?.data) continue;
  Object.assign(hero.data, { banner: '', cover: '', logo: '', scores: [], rating: '' });
}
fs.writeFileSync(path.join(fixtures, 'detail.theatre__slice3-no-art.json'), JSON.stringify(record, null, 2));
const names = fs.readdirSync(fixtures).filter(name => name.endsWith('.json') &&
  name !== 'index.json' && name !== 'shell.json').sort();
fs.writeFileSync(path.join(fixtures, 'index.json'), JSON.stringify(names, null, 2));
console.log('no-art fixture: detail.theatre__slice3-no-art.json');
