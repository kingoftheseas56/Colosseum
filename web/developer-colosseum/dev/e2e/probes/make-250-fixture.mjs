// Derive a paging stress fixture from a recorded series. Output stays in ignored fixtures/.
import fs from 'node:fs';
import path from 'node:path';

const dir = path.resolve(import.meta.dirname, '../../../fixtures');
const indexPath = path.join(dir, 'index.json');
const files = JSON.parse(fs.readFileSync(indexPath, 'utf8'));
const rec = files.map(file => JSON.parse(fs.readFileSync(path.join(dir, file), 'utf8')))
  .find(value => value.feed === 'detail.theatre' && value.params?.id === 'tt0944947');
if (!rec) throw new Error('Record Game of Thrones in a tagged session first.');
const sections = new Map();
for (const value of rec.events) {
  const ev = value.event;
  if (ev.type === 'reset') { sections.clear(); for (const section of ev.sections) sections.set(section.id, section); }
  if (ev.type === 'section') sections.set(ev.section.id, ev.section);
}
const clone = value => structuredClone(value);
const template = sections.get('episodes').data.rows[0];
const rows = Array.from({ length: 250 }, (_, i) => ({ ...clone(template),
  id: `tt-slice4-paging:1:${i + 1}`, season: 1, number: i + 1,
  displayNumber: i + 1, title: `Episode ${i + 1}`, progress: 0, watched: false }));
const titleId = 'tt-slice4-paging';
const hero = clone(sections.get('hero'));
hero.data.id = titleId;
hero.data.title = 'Paging Test';
hero.data.primaryTargetId = rows[0].id;
const seasons = clone(sections.get('seasons'));
seasons.data = { ...seasons.data, selected: 1, order: 'seasons', absoluteAvailable: false,
  rows: [{ number: 1, label: 'Season 1', count: 250 }] };
const episode = (start, end, more) => ({ ...clone(sections.get('episodes')),
  hasMore: more, data: { ...sections.get('episodes').data, season: 1,
    nextUpId: rows[0].id, windowStart: start, rows: rows.slice(start, end) } });
const first = episode(0, 100, true);
const rest = [episode(100, 200, true), episode(200, 250, false)];
const cast = clone(sections.get('cast'));
cast.state = 'empty'; cast.data.people = [];
const related = clone(sections.get('related'));
related.state = 'empty'; related.items = [];
const sources = clone(sections.get('sources'));
sources.state = 'empty'; sources.data.targetId = ''; sources.data.rows = [];
const recOut = { feed: 'detail.theatre', params: { id: titleId, type: 'series', title: 'Paging Test' },
  events: [{ t: 0, generation: 1, event: { type: 'reset',
    sections: [hero, sections.get('facts'), seasons, first, cast, sources, related] } }],
  pages: { episodes: rest } };
const name = 'detail.theatre__slice4-250.json';
fs.writeFileSync(path.join(dir, name), JSON.stringify(recOut, null, 2));
if (!files.includes(name)) { files.push(name); fs.writeFileSync(indexPath, JSON.stringify(files.sort(), null, 1)); }
console.log(path.join(dir, name));
