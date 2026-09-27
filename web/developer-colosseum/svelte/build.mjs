// Builds every Svelte page into one self-contained IIFE bundle at ../surfaces/<name>/ (CONTRACT §16.2).
// Usage: npm run build            (all pages)
//        npm run build -- <name>  (one page)
import { build } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { readdirSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const pages = process.argv[2] ? [process.argv[2]]
  : readdirSync(here, { withFileTypes: true })
      .filter(d => d.isDirectory() && existsSync(resolve(here, d.name, 'main.js'))).map(d => d.name);

for (const name of pages) {
  const root = resolve(here, name);
  await build({
    root,
    configFile: false,
    logLevel: 'warn',
    publicDir: resolve(root, 'public'),
    plugins: [svelte()],
    build: {
      outDir: resolve(here, '..', 'surfaces', name),
      emptyOutDir: true,
      cssCodeSplit: false,
      minify: true,
      lib: { entry: resolve(root, 'main.js'), name: 'CW_' + name.replace(/\W/g, '_'), formats: ['iife'], fileName: () => 'surface.js' },
      rollupOptions: { output: { assetFileNames: a => (a.name && a.name.endsWith('.css')) ? 'surface.css' : 'assets/[name][extname]' } }
    }
  });
  console.log('built', name, '→ surfaces/' + name + '/surface.js');
}
