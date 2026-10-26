import manifest from '../../../extensions/tankoyomi/manifest.json' with { type: 'json' };
import { addonEnvelopes } from './addon-artifacts.mjs';
import weebcentral from '../../../extensions/tankoyomi/languages/en/weebcentral.js';
import zonatmo from '../../../extensions/tankoyomi/languages/es/zonatmo.js';
import niadd from '../../../extensions/tankoyomi/languages/es/niadd.js';
import mangasin from '../../../extensions/tankoyomi/languages/es/mangasin.js';
import taiyo from '../../../extensions/tankoyomi/languages/pt/taiyo.js';
import mangaOnline from '../../../extensions/tankoyomi/languages/pt/manga-online.js';
import mangaNight from '../../../extensions/tankoyomi/languages/pt/manga-night.js';
import sushiscanFr from '../../../extensions/tankoyomi/languages/fr/sushiscan-fr.js';
import lelscanVf from '../../../extensions/tankoyomi/languages/fr/lelscan-vf.js';
import lelmanga from '../../../extensions/tankoyomi/languages/fr/lelmanga.js';
import mangamoins from '../../../extensions/tankoyomi/languages/fr/mangamoins.js';
import { createWorker } from './worker.mjs';

const providers = {
  'en/weebcentral': weebcentral,
  'es/zonatmo': zonatmo,
  'es/niadd': niadd,
  'es/mangasin': mangasin,
  'pt/taiyo': taiyo,
  'pt/manga-online': mangaOnline,
  'pt/manga-night': mangaNight,
  'fr/sushiscan-fr': sushiscanFr,
  'fr/lelscan-vf': lelscanVf,
  'fr/lelmanga': lelmanga,
  'fr/mangamoins': mangamoins
};

export default createWorker({ manifest, providers, addonEnvelopes });
export { manifest, providers };
