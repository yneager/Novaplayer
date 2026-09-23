// LAMBDA Player Home page behaviour.
(() => {
  let home = null;
  let recents = [];
  let session = {available: false, name: ''};
  const q = (s, root=document) => root.querySelector(s);
  const qa = (s, root=document) => Array.from(root.querySelectorAll(s));
  const call = (name, ...args) => { if (home && typeof home[name] === 'function') home[name](...args); };
  const THEMES = ['theme-cyan', 'theme-violet', 'theme-blue'];

  const formatLeft = seconds => {
    seconds = Math.max(0, Math.round(Number(seconds) || 0));
    const h = Math.floor(seconds / 3600);
    const m = Math.max(1, Math.round((seconds % 3600) / 60));
    return h > 0 ? h + 'h ' + String(m).padStart(2, '0') + 'm left' : m + ' min left';
  };

  function makeCard({title, meta, badge, progress, theme, onOpen, tooltip}) {
    const card = document.createElement('a');
    card.className = 'wide-card ' + theme;
    card.href = '#';
    card.title = tooltip || title;
    card.innerHTML =
      '<div class="card-art"><span class="sphere"></span><span class="slash"></span></div>' +
      '<div class="card-shade"></div>' +
      '<span class="card-badge"></span>' +
      '<div class="wide-info"><div><strong></strong><span></span></div><span class="mini-play"></span></div>' +
      '<div class="progress"><i></i></div>';
    q('.card-badge', card).textContent = badge;
    q('.wide-info strong', card).textContent = title;
    q('.wide-info div span', card).textContent = meta;
    q('.progress i', card).style.width = Math.round(Math.max(0, Math.min(1, progress || 0)) * 100) + '%';
    card.addEventListener('click', e => { e.preventDefault(); onOpen(); });
    return card;
  }

  function renderRecents() {
    const rail = q('.continue-rail');
    if (!rail) return;
    rail.innerHTML = '';
    if (!recents.length) {
      rail.appendChild(makeCard({
        title: 'Open your first video', meta: 'Recently played videos appear here',
        badge: 'START', progress: 0, theme: THEMES[0], onOpen: () => call('openVideo'),
        tooltip: 'Open a video from this PC'
      }));
      rail.appendChild(makeCard({
        title: 'Play a whole folder', meta: 'Next plays the following video automatically',
        badge: 'FOLDER', progress: 0, theme: THEMES[1], onOpen: () => call('openFolder'),
        tooltip: 'Choose a folder of videos'
      }));
    } else {
      recents.forEach((item, index) => {
        const current = session.available && item.path === session.path;
        const meta = current ? 'Resume current session'
          : item.watched ? 'Watched · play again'
          : item.remaining > 0 ? formatLeft(item.remaining) : 'Not started';
        rail.appendChild(makeCard({
          title: item.name, meta, badge: current ? 'NOW' : (item.ext || 'VIDEO'),
          progress: item.watched ? 1 : item.progress, theme: THEMES[index % THEMES.length],
          onOpen: () => current ? call('resume') : call('openRecent', item.path),
          tooltip: item.path
        }));
      });
    }
    rail.classList.remove('rail-enter');
    void rail.offsetWidth;
    rail.classList.add('rail-enter');
    if (window.LambdaWindow) window.LambdaWindow.refresh();
  }

  function renderHeroCta() {
    const label = q('.primary-cta-label');
    if (label) label.textContent = session.available ? 'Resume' : (recents.length ? 'Play recent' : 'Open video');
    const cta = q('.primary-cta');
    if (cta) cta.title = session.available ? 'Resume ' + (session.name || 'current video')
      : recents.length ? 'Play ' + recents[0].name : 'Open a video from this PC';
  }

  // ---- About popover --------------------------------------------------------
  function ensureAbout() {
    let pop = q('.lambda-popover');
    if (pop) return pop;
    pop = document.createElement('div');
    pop.className = 'lambda-popover glass';
    pop.setAttribute('role', 'dialog');
    pop.setAttribute('aria-label', 'About LAMBDA Player');
    pop.innerHTML =
      '<div class="pop-head"><span class="brand-mark"><i></i><i></i></span><div><strong>LAMBDA Player</strong><small class="pop-version"></small></div></div>' +
      '<p>Local video player built on libmpv, with optional real-time RIFE frame interpolation on any Vulkan GPU.</p>' +
      '<div class="pop-keys"><span><kbd>Space</kbd> Play / pause</span><span><kbd>←</kbd><kbd>→</kbd> Seek 5 s</span><span><kbd>F</kbd> Fullscreen</span><span><kbd>M</kbd> Mute</span><span><kbd>Ctrl</kbd><kbd>O</kbd> Open</span></div>' +
      '<div class="pop-actions"><button type="button" class="pop-btn primary" data-pop="open">Open video</button><button type="button" class="pop-btn" data-pop="licenses">Third-party licenses</button></div>';
    document.body.appendChild(pop);
    q('.pop-version', pop).textContent = 'Version ' + ((home && home.version) || '');
    q('[data-pop="open"]', pop).addEventListener('click', () => { closeAbout(); call('openVideo'); });
    q('[data-pop="licenses"]', pop).addEventListener('click', () => { closeAbout(); call('openLicenses'); });
    return pop;
  }
  function openAbout() { ensureAbout().classList.add('open'); q('.profile').classList.add('open'); if (window.LambdaWindow) window.LambdaWindow.refresh(); }
  function closeAbout() {
    const pop = q('.lambda-popover');
    if (pop) pop.classList.remove('open');
    const profile = q('.profile');
    if (profile) profile.classList.remove('open');
    if (window.LambdaWindow) window.LambdaWindow.refresh();
  }
  const aboutOpen = () => !!q('.lambda-popover.open');

  // ---- Bindings -------------------------------------------------------------
  function bind() {
    qa('[data-action]').forEach(el => el.addEventListener('click', e => {
      e.preventDefault();
      const action = el.dataset.action;
      if (action === 'open') call('openVideo');
      else if (action === 'folder') call('openFolder');
      else if (action === 'resume') call('resume');
      else if (action === 'about') aboutOpen() ? closeAbout() : openAbout();
    }));

    // Catalogue tiles are a visual showcase: they open the local file picker.
    document.addEventListener('click', e => {
      const link = e.target.closest('a[href="./player.html"]');
      if (link && !link.dataset.action) { e.preventDefault(); call('openVideo'); }
    });

    const brand = q('.brand');
    if (brand) brand.addEventListener('click', e => { e.preventDefault(); window.scrollTo({top: 0, behavior: 'smooth'}); });

    // Nav links scroll smoothly; the active link follows the section in view.
    const links = qa('.nav-links a');
    links.forEach(a => a.addEventListener('click', e => {
      const id = (a.getAttribute('href') || '#').slice(1);
      e.preventDefault();
      if (!id) window.scrollTo({top: 0, behavior: 'smooth'});
      else { const target = document.getElementById(id); if (target) window.scrollTo({top: target.getBoundingClientRect().top + window.scrollY - 96, behavior: 'smooth'}); }
    }));
    const sections = [{id: '', el: q('.hero')}].concat(links.slice(1).map(a => ({id: a.getAttribute('href').slice(1), el: document.getElementById(a.getAttribute('href').slice(1))})));
    const updateActive = () => {
      const probe = window.innerHeight * 0.38;
      let active = 0;
      sections.forEach((s, i) => { if (s.el && s.el.getBoundingClientRect().top <= probe) active = i; });
      if (window.innerHeight + window.scrollY >= document.body.scrollHeight - 4) active = sections.length - 1;
      links.forEach((a, i) => a.classList.toggle('active', i === active));
      q('.nav-shell').classList.toggle('scrolled', window.scrollY > 24);
    };
    window.addEventListener('scroll', updateActive, {passive: true});
    updateActive();

    // Rail arrow buttons scroll their rail by most of a page.
    qa('.rail-controls').forEach(group => {
      const rail = q(group.dataset.rail || '.poster-rail');
      if (!rail) return;
      const buttons = qa('button', group);
      const sync = () => {
        const max = rail.scrollWidth - rail.clientWidth - 2;
        buttons.forEach(b => { b.disabled = Number(b.dataset.dir) < 0 ? rail.scrollLeft <= 2 : rail.scrollLeft >= max; });
      };
      buttons.forEach(b => b.addEventListener('click', () => rail.scrollBy({left: Number(b.dataset.dir) * rail.clientWidth * 0.8, behavior: 'smooth'})));
      rail.addEventListener('scroll', sync, {passive: true});
      window.addEventListener('resize', sync);
      sync();
    });

    document.addEventListener('pointerdown', e => {
      if (aboutOpen() && !e.target.closest('.lambda-popover,.profile')) closeAbout();
    });
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && aboutOpen()) closeAbout();
    });
  }

  window.lambdaHome = {
    setRecents(list) { recents = Array.isArray(list) ? list : []; renderRecents(); renderHeroCta(); },
    setSession(next) { session = Object.assign({available: false, name: '', path: ''}, next || {}); renderRecents(); renderHeroCta(); },
    toast: (message, kind) => window.LambdaWindow && window.LambdaWindow.toast(message, kind)
  };

  const start = () => {
    bind();
    renderRecents();
    renderHeroCta();
    new QWebChannel(qt.webChannelTransport, channel => {
      home = channel.objects.homeBridge;
      if (window.LambdaWindow && channel.objects.windowBridge) window.LambdaWindow.attach(channel.objects.windowBridge);
      call('ready');
    });
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();
