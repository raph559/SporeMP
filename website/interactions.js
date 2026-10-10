document.documentElement.classList.add('enhanced');

const menuButton = document.querySelector('.menu-toggle');
const mobileNavigation = document.querySelector('#mobile-navigation');
function closeMenu() {
  menuButton?.setAttribute('aria-expanded', 'false');
  mobileNavigation?.classList.remove('is-open');
}
menuButton?.addEventListener('click', () => {
  const open = menuButton.getAttribute('aria-expanded') !== 'true';
  menuButton.setAttribute('aria-expanded', String(open));
  mobileNavigation.classList.toggle('is-open', open);
});
mobileNavigation?.addEventListener('click', event => {
  if (event.target.closest('a')) closeMenu();
});
document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && menuButton?.getAttribute('aria-expanded') === 'true') {
    closeMenu();
    menuButton.focus();
  }
});
matchMedia('(min-width: 701px)').addEventListener('change', event => {
  if (event.matches) closeMenu();
});

const tabs = [...document.querySelectorAll('[role="tab"]')];
function selectStage(tab, focus = false) {
  for (const other of tabs) {
    const selected = other === tab;
    other.setAttribute('aria-selected', String(selected));
    other.tabIndex = selected ? 0 : -1;
    document.getElementById(other.getAttribute('aria-controls')).hidden = !selected;
  }
  if (focus) {
    tab.focus();
    tab.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: 'instant' });
  }
}
tabs.forEach((tab, index) => {
  tab.addEventListener('click', event => {
    event.preventDefault();
    selectStage(tab);
    history.replaceState(null, '', '#' + tab.getAttribute('aria-controls'));
    updateLanguageLink();
  });
  tab.addEventListener('keydown', event => {
    let next;
    if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
    if (event.key === 'ArrowLeft') next = (index + tabs.length - 1) % tabs.length;
    if (event.key === 'Home') next = 0;
    if (event.key === 'End') next = tabs.length - 1;
    if (next !== undefined) {
      event.preventDefault();
      selectStage(tabs[next], true);
      history.replaceState(null, '', '#' + tabs[next].getAttribute('aria-controls'));
      updateLanguageLink();
    }
  });
});
if (tabs.length) selectStage(tabs.find(tab => tab.getAttribute('aria-selected') === 'true') || tabs[0]);

// Old bookmarks remain destinations, without retaining the previous interface.
const aliases = { project: 'universe', projet: 'universe', news: 'journal', actualites: 'journal', versions: 'updates', avenir: 'roadmap', soutenir: 'support', contenu: 'main-content' };
function applyHash() {
  const hash = location.hash.slice(1);
  const replacement = aliases[hash];
  if (replacement && document.getElementById(replacement)) {
    history.replaceState(null, '', '#' + replacement);
    document.getElementById(replacement).scrollIntoView();
  }
  const tab = tabs.find(item => item.getAttribute('aria-controls') === location.hash.slice(1));
  if (tab) {
    selectStage(tab);
    document.getElementById('stages').scrollIntoView();
  }
}
const languageLink = document.querySelector('[data-language]');
const languageDestination = languageLink?.getAttribute('href');
function updateLanguageLink() {
  if (languageLink) languageLink.href = languageDestination + location.hash;
}
applyHash();
updateLanguageLink();
window.addEventListener('hashchange', () => { applyHash(); updateLanguageLink(); });

const sections = [...document.querySelectorAll('main > section[id]')];
if ('IntersectionObserver' in window && sections.length) {
  const observer = new IntersectionObserver(entries => {
    for (const entry of entries) {
      if (!entry.isIntersecting) continue;
      document.querySelectorAll('[data-nav]').forEach(link => {
        if (link.dataset.nav === entry.target.id) link.setAttribute('aria-current', 'location');
        else link.removeAttribute('aria-current');
      });
    }
  }, { rootMargin: '-15% 0px -60% 0px', threshold: 0 });
  sections.forEach(section => observer.observe(section));
}

const motionAllowed = matchMedia('(prefers-reduced-motion: no-preference) and (pointer: fine)');
const hero = document.querySelector('.hero');
if (hero && motionAllowed.matches) {
  let frame = 0;
  hero.addEventListener('pointermove', event => {
    if (!motionAllowed.matches) return;
    cancelAnimationFrame(frame);
    frame = requestAnimationFrame(() => {
      const box = hero.getBoundingClientRect();
      hero.style.setProperty('--art-x', `${(event.clientX / box.width - .5) * 9}px`);
      hero.style.setProperty('--art-y', `${((event.clientY - box.top) / box.height - .5) * 6}px`);
    });
  }, { passive: true });
  hero.addEventListener('pointerleave', () => {
    hero.style.setProperty('--art-x', '0px');
    hero.style.setProperty('--art-y', '0px');
  });
}

if (document.body.classList.contains('page-article')) {
  let pending = false;
  const updateReadingProgress = () => {
    const range = document.documentElement.scrollHeight - innerHeight;
    document.documentElement.style.setProperty('--reading-progress', `${range > 0 ? Math.min(100, scrollY / range * 100) : 100}%`);
    pending = false;
  };
  window.addEventListener('scroll', () => {
    if (!pending) { pending = true; requestAnimationFrame(updateReadingProgress); }
  }, { passive: true });
  window.addEventListener('resize', updateReadingProgress, { passive: true });
  updateReadingProgress();
}
