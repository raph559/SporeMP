import { readFile, readdir, stat } from 'node:fs/promises';
import { dirname, join, resolve, extname, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), 'dist');
const config = JSON.parse(await readFile(join(dirname(root), 'site.config.json'), 'utf8'));
const failures = [];
const files = [];
async function walk(folder) {
  for (const entry of await readdir(folder, { withFileTypes: true })) {
    const path = join(folder, entry.name);
    if (entry.isDirectory()) await walk(path); else files.push(path);
  }
}
await walk(root);
const htmlFiles = files.filter(file => extname(file) === '.html');
let checkedLinks = 0;
for (const file of htmlFiles) {
  const html = await readFile(file, 'utf8');
  const label = relative(root, file);
  if (!html.startsWith('<!doctype html>')) failures.push(`${label}: missing doctype`);
  if (!/<html lang="(en|fr)"/.test(html)) failures.push(`${label}: missing language`);
  if (!html.includes('name="viewport"')) failures.push(`${label}: missing viewport`);
  if (!html.includes('http-equiv="refresh"')) {
    if ((html.match(/<h1[> ]/g) || []).length !== 1) failures.push(`${label}: expected exactly one h1`);
    if (!html.includes('id="main-content"')) failures.push(`${label}: missing main landmark`);
    if (!html.includes('rel="canonical"')) failures.push(`${label}: missing canonical`);
    if (!html.includes('hreflang="fr"') || !html.includes('hreflang="en"')) failures.push(`${label}: missing language alternates`);
    const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
    if (ids.length !== new Set(ids).size) failures.push(`${label}: duplicate IDs`);
  }
  for (const match of html.matchAll(/\b(?:href|src)="([^"]+)"/g)) {
    const siteAbsolute = match[1].startsWith(config.siteUrl);
    const href = siteAbsolute ? match[1].slice(config.siteUrl.length) : match[1];
    if (!siteAbsolute && /^(https?:|mailto:|data:)/.test(href)) continue;
    checkedLinks++;
    const [path, fragment] = href.split('#');
    let target = siteAbsolute ? resolve(root, path.split('?')[0] || '.') : path ? resolve(dirname(file), path.split('?')[0]) : file;
    if (!target.startsWith(root + '/') && !target.startsWith(root + '\\') && target !== root) {
      failures.push(`${label}: path escapes output: ${href}`); continue;
    }
    try {
      if ((await stat(target)).isDirectory()) target = join(target, 'index.html');
      await stat(target);
      if (fragment && extname(target) === '.html') {
        const targetHtml = await readFile(target, 'utf8');
        if (!targetHtml.includes(`id="${fragment}"`)) failures.push(`${label}: missing anchor ${href}`);
      }
    } catch { failures.push(`${label}: missing resource ${href}`); }
  }
}
for (const file of files.filter(file => extname(file) === '.css')) {
  const css = await readFile(file, 'utf8');
  for (const match of css.matchAll(/url\(['"]?([^)'"\s]+)['"]?\)/g)) {
    try { await stat(resolve(dirname(file), match[1])); }
    catch { failures.push(`Missing CSS resource: ${match[1]}`); }
  }
}
if (failures.length) { console.error(failures.join('\n')); process.exit(1); }
console.log(`PASS: ${htmlFiles.length} HTML pages; ${checkedLinks} local links, anchors and assets; language pairs, headings, landmarks and font URLs.`);
