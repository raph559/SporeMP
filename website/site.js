// Native links and disclosures keep the entire site usable without JavaScript.
// Only add a current-section hint; never intercept navigation or modify history.
const sections = [...document.querySelectorAll('main > section[id]')];
// Preserve an anchored section when changing language. The links also work without JS.
function updateLanguageLinks() {
  for (const link of document.querySelectorAll('.language-switch a')) {
    const url = new URL(link.href);
    url.hash = location.hash;
    link.href = url.href;
  }
}
updateLanguageLinks();
window.addEventListener('hashchange', updateLanguageLinks);
if ('IntersectionObserver' in window && sections.length) {
  const links = [...document.querySelectorAll('.header nav a')];
  const observer = new IntersectionObserver(entries => {
    const active = entries.find(entry => entry.isIntersecting);
    if (!active) return;
    for (const link of links) {
      if (link.hash === '#' + active.target.id) link.setAttribute('aria-current', 'location');
      else link.removeAttribute('aria-current');
    }
  }, { rootMargin: '-10% 0px -65% 0px' });
  for (const section of sections) observer.observe(section);
}
