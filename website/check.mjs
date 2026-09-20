import { readFile, readdir, stat } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.join(path.dirname(fileURLToPath(import.meta.url)), 'dist');
async function walk(dir) { const result=[]; for(const e of await readdir(dir,{withFileTypes:true})){const p=path.join(dir,e.name); if(e.isDirectory())result.push(...await walk(p));else result.push(p);}return result; }
const files=await walk(root); let references=0;
for(const file of files.filter(p=>p.endsWith('.html'))){
  const html=await readFile(file,'utf8');
  if((html.match(/<h1[ >]/g)||[]).length!==1)throw new Error('Expected one h1: '+file);
  const language = path.relative(root,file).startsWith('fr'+path.sep) ? 'fr' : 'en';
  if(!html.includes('<html lang="'+language+'"'))throw new Error('Wrong document language: '+file);
  if(!html.includes('hreflang="x-default"'))throw new Error('Missing default language metadata: '+file);
  for(const match of html.matchAll(/(?:href|src)="([^"]*)"/g)){
    const value=match[1]; if(!value)throw new Error('Empty URL: '+file);
    if(/^(https:|data:)/.test(value))continue;
    const [url,anchor]=value.split('#'); let target=path.resolve(path.dirname(file),url||path.basename(file));
    if(!target.startsWith(root+path.sep)&&target!==root)throw new Error('Outside output: '+target);
    if((await stat(target)).isDirectory())target=path.join(target,'index.html');
    const content=await readFile(target);
    if(anchor&&!content.toString().includes('id="'+anchor+'"'))throw new Error('Missing anchor '+value+' in '+file);
    references++;
  }
}
console.log(`Validated ${files.length} public files, ${references} local references, page headings, language and anchors.`);
