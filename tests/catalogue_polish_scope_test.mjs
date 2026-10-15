// Static scope / blast-radius guard for the Catalogue Poster & Shelf Polish arc.
// Proves the gallery profile is opted into ONLY where approved, that the shared shells default to
// classic, that Discover wrappers do NOT opt in during the Theatre pilot, and that the separate
// scroll controller was not dragged into this arc. Node's assert throws on the first violation;
// the runner gates on a clean exit + the CATALOGUE_POLISH_SCOPE_OK marker.
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import assert from 'node:assert';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const read = (p) => readFileSync(join(root, p), 'utf8');

const theatreSeeAll        = read('qml/TheatreSeeAllPage.qml');
const cataloguePosterGrid  = read('qml/CataloguePosterGrid.qml');
const discoverBrowser      = read('qml/DiscoverBrowser.qml');
const discoverPage         = read('qml/DiscoverPage.qml');
const tankobanDiscover     = read('qml/TankobanDiscoverPage.qml');
const scrollGlide          = read('qml/ScrollGlide.qml');

// Theatre See-all opts into the gallery profile.
assert.match(theatreSeeAll, /visualProfile:\s*"gallery"/,
    'Theatre See-all must select the gallery profile');

// The shared grid and browser default to classic and expose the opt-in seam wired to their cards.
assert.match(cataloguePosterGrid, /property\s+string\s+visualProfile:\s*"classic"/,
    'CataloguePosterGrid must default to the classic profile');
assert.match(discoverBrowser, /property\s+string\s+posterVisualProfile:\s*"classic"/,
    'DiscoverBrowser must default posterVisualProfile to classic');
assert.match(discoverBrowser, /visualProfile:\s*browser\.posterVisualProfile/,
    'DiscoverBrowser must wire posterVisualProfile through to its card delegate');

// After the Theatre pilot passed eyes-on (2026-08-03), the approved Discover wrappers opt into the
// gallery profile — Theatre Discover and Tankoban Discover (Manga + Comics share one shell).
assert.match(discoverPage, /posterVisualProfile:\s*"gallery"/,
    'DiscoverPage opts into the gallery profile after the Task 9 gate');
assert.match(tankobanDiscover, /posterVisualProfile:\s*"gallery"/,
    'TankobanDiscoverPage opts into the gallery profile after the Task 9 gate');

// The separate scroll-speed controller must not be dragged into this arc.
assert.doesNotMatch(scrollGlide, /CatalogueVisualMetrics|RoundedPosterImage|LazyPosterShelf/,
    'ScrollGlide must remain untouched by the poster/shelf polish arc');

console.log('CATALOGUE_POLISH_SCOPE_OK');
