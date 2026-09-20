import { readFile, writeFile, mkdir, copyFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { copy } from './i18n.mjs';

const root = path.dirname(fileURLToPath(import.meta.url));
const output = path.join(root, 'dist');
const french = JSON.parse(await readFile(path.join(root, 'locales/fr/content.json'), 'utf8'));
const english = JSON.parse(await readFile(path.join(root, 'content.json'), 'utf8'));
const config = JSON.parse(await readFile(path.join(root, 'site.config.json'), 'utf8'));
const legacyRoutes = JSON.parse(await readFile(path.join(root, 'legacy-routes.json'), 'utf8'));
const legacyAnchors = JSON.parse(await readFile(path.join(root, 'legacy-anchors.json'), 'utf8'));
const donationUrl = config.leetchiUrl; // Shared destination across translations.
if (donationUrl) {
  const url = new URL(donationUrl);
  if (url.protocol !== 'https:' || !['leetchi.com','www.leetchi.com'].includes(url.hostname) || url.pathname === '/') throw new Error('A specific HTTPS Leetchi fundraiser URL is required.');
}
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="18" fill="#091321"/><circle cx="28" cy="34" r="16" fill="none" stroke="#81efe0" stroke-width="5"/><circle cx="46" cy="16" r="6" fill="#b9a1ff"/></svg>';
const favicon = 'data:image/svg+xml,' + encodeURIComponent(svg);
const arrow = '<span aria-hidden="true">↗</span>';
const base = config.siteUrl;
const articlePath = n => 'news/' + n.slug + '.html';
const slugs = data => data.news.map(n=>n.slug).sort().join(',');
if (slugs(french) !== slugs(english)) throw new Error('Each article must have both English and French translations.');

for (const [lang, data] of [['en',english],['fr',french]]) {
  const t = Object.fromEntries(Object.entries(copy[lang]).map(([key,value]) => [key, typeof value === 'string' ? value.replaceAll('<br>', '<br> ') : value]));
  const out = lang === 'en' ? output : path.join(output,'fr');
  const langBase = base + (lang === 'fr' ? 'fr/' : '');
  await mkdir(path.join(out,'news'),{recursive:true});
  const date = s => new Intl.DateTimeFormat(t.locale,{day:'numeric',month:'long',year:'numeric',timeZone:'UTC'}).format(new Date(s+'T12:00:00Z'));
  for(const n of data.news) if(!/^[a-z0-9-]+$/.test(n.slug)) throw new Error('Invalid article slug');
  data.news.sort((a,b)=>b.date.localeCompare(a.date));
  const context = (pagePath='',absolute=false) => {
    const prefix = absolute ? langBase : pagePath.startsWith('news/') ? '../' : './';
    const global = absolute ? base : prefix + (lang === 'fr' ? '../' : '');
    return {prefix,global,pagePath};
  };
  const nav = c => `<header class="header"><a class="brand" href="${c.prefix}" aria-label="SporeMP, ${t.home}"><span class="brand-icon" aria-hidden="true">◌</span>spore<span>mp</span></a><nav aria-label="${t.navigation}"><a href="${c.prefix}#project">${t.project}</a><a href="${c.prefix}#news" ${c.pagePath.startsWith('news/')?'aria-current="page"':''}>${t.news}</a><a href="${c.prefix}#updates">${t.updates}</a><a href="${c.prefix}#roadmap">${t.future}</a></nav><div class="header-tools"><div class="language-switch" role="group" aria-label="${t.language}"><a href="${c.global}${c.pagePath}" lang="en" hreflang="en" aria-label="${copy.en.languageName}" ${lang==='en'?'aria-current="true"':''}>EN</a><span aria-hidden="true">/</span><a href="${c.global}fr/${c.pagePath}" lang="fr" hreflang="fr" aria-label="${copy.fr.languageName}" ${lang==='fr'?'aria-current="true"':''}>FR</a></div><a class="support-link" href="${c.prefix}#support">${t.support} <span aria-hidden="true">♡</span></a></div></header>`;
  const footer = c => `<footer class="footer wrap"><a class="brand" href="${c.prefix}">spore<span>mp</span></a><p>${t.footer}</p><a href="${c.prefix}#project">${t.backProject}</a><div class="fineprint">${t.rights}<br>${t.artNotice}<br><a href="https://github.com/raph559/SporeMP" target="_blank" rel="noopener noreferrer">${t.sourceCode} ${arrow}</a></div></footer>`;
  const shell = ({title,description,body,c}) => `<!doctype html>
<html lang="${lang}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><meta name="color-scheme" content="dark"><meta name="theme-color" content="#070c15"><title>${esc(title)}</title><meta name="description" content="${esc(description)}"><link rel="canonical" href="${esc(langBase+c.pagePath)}"><link rel="alternate" hreflang="en" href="${esc(base+c.pagePath)}"><link rel="alternate" hreflang="fr" href="${esc(base+'fr/'+c.pagePath)}"><link rel="alternate" hreflang="x-default" href="${esc(base+c.pagePath)}"><meta property="og:type" content="${c.pagePath.startsWith('news/')?'article':'website'}"><meta property="og:locale" content="${t.ogLocale}"><meta property="og:title" content="${esc(title)}"><meta property="og:description" content="${esc(description)}"><meta property="og:url" content="${esc(langBase+c.pagePath)}"><link rel="icon" type="image/svg+xml" href="${favicon}"><link rel="stylesheet" href="${c.global}styles.css"></head><body><a class="skip-link" href="#main-content">${t.skip}</a>${body}<script src="${c.global}site.js" defer></script></body></html>`;
  const c = context();
  const newsCards = data.news.map((n,i)=>`<article class="news-card"><div class="news-meta"><span>${esc(n.category)}</span><time datetime="${n.date}">${date(n.date)}</time></div><span class="editorial-number" aria-hidden="true">0${i+1}</span><h3><a href="./${articlePath(n)}">${esc(n.title)}</a></h3><p>${esc(n.summary)}</p><a class="text-link" href="./${articlePath(n)}" aria-label="${t.readArticle} ${esc(n.title)}">${t.readJournal} ${arrow}</a></article>`).join('');
  const releases = data.releases.map((r,i)=>`<details class="release" ${i===0?'open':''}><summary><span class="version">v${esc(r.version)}</span><span class="release-title">${esc(r.title)}<time datetime="${r.date}">${date(r.date)}</time></span><span class="expand" aria-hidden="true">+</span></summary><div class="release-body"><ul>${r.changes.map(s=>`<li>${esc(s)}</li>`).join('')}</ul><p class="release-note">${esc(r.note)}</p></div></details>`).join('');
  const roadmap = data.roadmap.map((r,i)=>`<li class="roadmap-step"><span class="step-number">0${i+1}</span><div><span class="tag ${i===0?'next':''}">${esc(r.status)}</span><h3>${esc(r.title)}</h3><p>${esc(r.description)}</p></div></li>`).join('');
  const support = donationUrl ? `<a class="button primary" href="${esc(donationUrl)}" target="_blank" rel="noopener noreferrer">${t.donate} ${arrow}</a><p class="support-note">${t.donateNote}</p>` : `<p class="donation-pending">${t.pending}</p>`;
  const body = `${nav(c)}<main id="main-content">
<section class="hero" aria-labelledby="hero-title"><img class="hero-art" src="${c.global}assets/universe.png" width="1536" height="1024" alt="${t.alt}" fetchpriority="high"><div class="hero-shade"></div><div class="hero-content wrap"><p class="eyebrow"><span class="status-dot"></span>${t.heroEyebrow}</p><h1 id="hero-title">${t.heroTitle}</h1><p class="hero-description">${t.heroDescription}</p><div class="hero-actions"><a class="button primary" href="#project">${t.discover} <span aria-hidden="true">↓</span></a><a class="button ghost" href="#news">${t.latest} ${arrow}</a></div><p class="hero-note">${t.heroNote}</p></div><div class="hero-caption">${t.illustration}</div></section>
<div class="development-strip wrap"><span class="tag">${t.strip}</span><p>${t.stripText}</p><a href="./news/first-shared-scene.html">${t.stripLink} ${arrow}</a></div>
<section class="section wrap project" id="project" aria-labelledby="project-title"><div><p class="eyebrow muted">${t.idea}</p><h2 id="project-title">${t.projectTitle}</h2></div><div class="project-copy"><p class="large-copy">${t.projectLead}</p><p>${t.projectP1}</p><p>${t.projectP2}</p><div class="current-status"><span class="tag next">${t.where}</span><p>${t.current}</p></div></div><div class="evolution" aria-label="${t.stagesLabel}">${t.stages.map((s,i)=>`<span>0${i+1} <b>${s}</b>${i===1?`<small>${t.firstTests}</small>`:''}</span>`).join('')}</div></section>
<section class="section news-section" id="news" aria-labelledby="news-title"><div class="wrap"><div class="section-heading"><div><p class="eyebrow muted">${t.journal}</p><h2 id="news-title">${t.newsTitle}</h2></div><p>${t.newsIntro}</p></div><div class="news-grid">${newsCards}</div></div></section>
<section class="section wrap releases-section" id="updates" aria-labelledby="release-title"><div><p class="eyebrow muted">${t.versionEyebrow}</p><h2 id="release-title">${t.versionTitle}</h2><p class="section-intro">${t.versionIntro}</p><p class="small-note">${t.versionNote}</p></div><div class="releases">${releases}</div></section>
<section class="section wrap" id="roadmap" aria-labelledby="future-title"><div class="section-heading"><div><p class="eyebrow muted">${t.futureEyebrow}</p><h2 id="future-title">${t.futureTitle}</h2></div><p>${t.futureIntro}</p></div><ol class="roadmap">${roadmap}</ol><p class="roadmap-note">${t.roadmapNote}</p></section>
<section class="support-section wrap" id="support" aria-labelledby="support-title"><div class="support-inner"><div><p class="eyebrow">${t.supportEyebrow}</p><h2 id="support-title">${t.supportTitle}</h2></div><div><p>${t.supportP1}</p><p>${t.supportP2}</p>${support}</div></div></section></main>${footer(c)}`;
  // Preserve previously shared fragment links without using them in new navigation.
  const compatibleBody = body.replace(/(<(?:section|main)\b[^>]*\bid="([^"]+)"[^>]*>)/g, (tag,full,id) => legacyAnchors[id] ? `${tag}<span id="${legacyAnchors[id]}" class="legacy-anchor" aria-hidden="true"></span>` : tag);
  await writeFile(path.join(out,'index.html'),shell({title:t.title,description:t.description,body:compatibleBody,c}));
  for(const n of data.news){
    const c = context(articlePath(n));
    const body = `${nav(c)}<main id="main-content" class="article wrap"><a class="text-link" href="../#news">${t.allNews}</a><div class="article-heading"><p class="eyebrow">${esc(n.category)} <span class="separator">/</span> <time datetime="${n.date}">${date(n.date)}</time></p><h1>${esc(n.title)}</h1><p class="article-lead">${esc(n.summary)}</p></div><div class="article-body">${n.paragraphs.map(p=>`<p>${esc(p)}</p>`).join('')}<aside>${t.articleAside}</aside><a class="button ghost" href="../#roadmap">${t.nextSteps} ${arrow}</a></div></main>${footer(c)}`;
    await writeFile(path.join(out,articlePath(n)),shell({title:n.title+' — SporeMP',description:n.summary,body,c}));
  }
  // Keep previously published article links working after the English URL migration.
  for (const route of legacyRoutes) {
    const destination = '../' + route.to;
    const c = context(route.to);
    const redirect = shell({title:t.title,description:t.description,c,body:`${nav(c)}<main id="main-content" class="article wrap"><h1>${t.project}</h1><a class="button primary" href="${esc(destination)}">${t.readJournal} ${arrow}</a></main>${footer(c)}`});
    await mkdir(path.dirname(path.join(out,route.from)),{recursive:true});
    await writeFile(path.join(out,route.from),redirect.replace('</head>',`<meta http-equiv="refresh" content="0;url=${esc(destination)}"></head>`));
  }
  const errorContext = context('404.html',true);
  await writeFile(path.join(out,'404.html'),shell({title:t.notFoundTitle,description:t.description,c:errorContext,body:`${nav(errorContext)}<main id="main-content" class="article wrap"><p class="eyebrow">${t.notFoundLabel}</p><h1>${t.notFoundHeading}</h1><p>${t.notFoundText}</p><a class="button primary" href="${langBase}">${t.backHome}</a></main>${footer(errorContext)}`}));
}
await mkdir(path.join(output,'assets'),{recursive:true});
for(const f of ['styles.css','site.js'])await copyFile(path.join(root,f),path.join(output,f));
await copyFile(path.join(root,'assets','universe.png'),path.join(output,'assets','universe.png'));
await writeFile(path.join(output,'.nojekyll'),'');
const paths=['',...french.news.map(articlePath)];
await writeFile(path.join(output,'sitemap.xml'),`<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">${['','fr/'].flatMap(lang=>paths.map(p=>`<url><loc>${esc(base+lang+p)}</loc></url>`)).join('')}</urlset>`);
console.log(`Built EN default + FR: 2 homepages, ${french.news.length*2} articles, localized 404s, sitemap and shared assets. Donation: ${donationUrl?'configured':'pending'}.`);
