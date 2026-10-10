import { readFile, writeFile, mkdir, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';
import { createHash } from 'node:crypto';
import en from './src/locales/en.mjs';
import fr from './src/locales/fr.mjs';
import { render } from './src/render.mjs';

const directory = dirname(fileURLToPath(import.meta.url));
const output = resolve(directory, 'dist');
if (output !== join(directory, 'dist')) throw new Error('Unexpected build destination');
const config = JSON.parse(await readFile(join(directory, 'site.config.json'), 'utf8'));
if (!config.siteUrl.endsWith('/') || new URL(config.siteUrl).protocol !== 'https:') throw new Error('An HTTPS site URL ending in / is required');
if (config.leetchiUrl && new URL(config.leetchiUrl).hostname !== 'www.leetchi.com') throw new Error('Unexpected fundraiser host');
if (en.news.map(n => n.slug).join() !== fr.news.map(n => n.slug).join()) throw new Error('Translated articles must have matching routes');
await rm(output, { recursive: true, force: true });
await mkdir(join(output, 'assets/fonts'), { recursive: true });
const hash = data => createHash('sha256').update(data).digest('hex').slice(0, 12);
const sourceText = async path => (await readFile(join(directory, path), 'utf8')).replace(/\r\n?/g, '\n');
// Third-party font files are pinned build dependencies, never checked-in assets.
const fontSource = 'https://raw.githubusercontent.com/google/fonts/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/outfit/';
async function pinnedDependency(name, expectedHash) {
  const response = await fetch(fontSource + name);
  if (!response.ok) throw new Error(`Pinned font dependency failed: ${response.status}`);
  const bytes = Buffer.from(await response.arrayBuffer());
  if (createHash('sha256').update(bytes).digest('hex') !== expectedHash) throw new Error(`Pinned font dependency changed: ${name}`);
  return bytes;
}
const [font, fontLicense] = await Promise.all([
  pinnedDependency('Outfit%5Bwght%5D.ttf', 'fc7287273e66929776e2ba54f144fe699080bec29f61bf649d70d871468aeade'),
  pinnedDependency('OFL.txt', 'c676351bf8576b9aba743cd5eaa8c0e7ee0d51f805d720447b4df4ddb6a2e416')
]);
const planet = await readFile(join(directory, 'assets/shared-planet.png'));
const stages = await readFile(join(directory, 'assets/evolution-worlds.png'));
const js = await sourceText('interactions.js');
const fontName = `outfit-${hash(font)}.ttf`;
const css = (await sourceText('design.css')).replace('./fonts/outfit-variable.ttf', `./fonts/${fontName}`);
const assets = { css: `assets/design-${hash(css)}.css`, js: `assets/interactions-${hash(js)}.js`, planet: `assets/shared-planet-${hash(planet)}.png`, stages: `assets/evolution-worlds-${hash(stages)}.png`, font: `assets/fonts/${fontName}` };
await Promise.all([
  writeFile(join(output, assets.css), css), writeFile(join(output, assets.js), js),
  writeFile(join(output, assets.planet), planet), writeFile(join(output, assets.font), font),
  writeFile(join(output, assets.stages), stages),
  writeFile(join(output, 'assets/fonts/OFL.txt'), fontLicense.toString('utf8').replace(/\r\n?/g, '\n')),
  ...['assets/favicon.svg', 'assets/credits.txt'].map(async path => writeFile(join(output, path), await sourceText(path)))
]);
const pages = [];
async function page(path, html, index = true) {
  await mkdir(dirname(join(output, path)), { recursive: true });
  await writeFile(join(output, path), html.replace(/\r\n?/g, '\n'));
  if (index) pages.push(path.replace(/index\.html$/, ''));
}
for (const content of [en, fr]) {
  const prefix = content.lang === 'fr' ? 'fr/' : '';
  await page(prefix + 'index.html', render(content, config, assets));
  await page(prefix + 'news/index.html', render(content, config, assets, 'archive'));
  await page(prefix + '404.html', render(content, config, assets, '404'), false);
  for (const article of content.news) await page(`${prefix}news/${article.slug}.html`, render(content, config, assets, 'article', article));
}
const historic = {
  'premiere-scene-partagee': 'first-shared-scene',
  'recompenses-sans-doublon': 'rewards-counted-once',
  'monde-independant-du-joueur': 'world-beyond-the-player'
};
for (const prefix of ['', 'fr/']) {
  for (const [oldSlug, slug] of Object.entries(historic)) {
    const target = prefix ? `../news/${slug}.html` : `../fr/news/${slug}.html`;
    await page(`${prefix}actualites/${oldSlug}.html`, `<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>SporeMP — Journal</title><meta http-equiv="refresh" content="0;url=${target}"><meta name="robots" content="noindex"><link rel="canonical" href="${new URL('fr/news/' + slug + '.html', config.siteUrl).href}"></head><body><a href="${target}">Lire l’article dans le nouveau journal SporeMP</a></body></html>`, false);
  }
}
await writeFile(join(output, 'sitemap.xml'), `<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">${pages.map(p => `<url><loc>${new URL(p, config.siteUrl).href}</loc></url>`).join('')}</urlset>`);
await writeFile(join(output, 'robots.txt'), `User-agent: *\nAllow: /\nSitemap: ${new URL('sitemap.xml', config.siteUrl).href}\n`);
await writeFile(join(output, 'build.json'), JSON.stringify({ design: 'shared-universe-v2', locales: ['en', 'fr'], pages: pages.length, assets }, null, 2));
console.log(`Built SporeMP v2: ${pages.length} indexable pages, 2 not-found pages, 6 historical redirects. All assets are local and content-versioned.`);
