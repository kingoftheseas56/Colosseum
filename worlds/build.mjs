// Builds each world into ../resources/worlds/<name>/: index.html + world.js (classic IIFE, works over file://)
// + world.css + assets/. Usage: npm run build [-- <name>]
import { build } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { readdirSync, existsSync, copyFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const worlds = process.argv[2] ? [process.argv[2]]
  : readdirSync(here, { withFileTypes: true })
      .filter(d => d.isDirectory() && existsSync(resolve(here, d.name, 'main.js'))).map(d => d.name);

for (const name of worlds) {
  const root = resolve(here, name);
  const outDir = resolve(here, '..', 'resources', 'worlds', name);
  await build({
    root, configFile: false, logLevel: 'warn', publicDir: resolve(root, 'public'),
    plugins: [svelte()],
    build: {
      outDir, emptyOutDir: true, cssCodeSplit: false, minify: true,
      lib: { entry: resolve(root, 'main.js'), name: 'World', formats: ['iife'], fileName: () => 'world.js' },
      rollupOptions: { output: { assetFileNames: a => (a.name && a.name.endsWith('.css')) ? 'world.css' : 'assets/[name][extname]' } }
    }
  });
  copyFileSync(resolve(root, 'index.html'), resolve(outDir, 'index.html'));
  console.log('built world', name, '→ resources/worlds/' + name + '/');
}
