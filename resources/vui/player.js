(() => {
  let bridge = null;
  let state = {};
  let settings = {audio:[], subtitles:[], interpolation:[], audioIndex:0, subtitleIndex:0, interpolationIndex:0};
  const SPEEDS = [0.5, 0.75, 1, 1.25, 1.5, 2];
  const player = document.querySelector('.player');
  const q = (s, root=document) => root.querySelector(s);
  const qa = (s, root=document) => Array.from(root.querySelectorAll(s));
  const button = label => q('button[aria-label="' + label + '"]');
  const clamp = v => Math.max(0, Math.min(1, v));
  const call = (name, ...args) => { if (bridge && typeof bridge[name] === 'function') bridge[name](...args); };

  const format = seconds => {
    seconds = Math.max(0, Number(seconds) || 0);
    const total = Math.floor(seconds);
    const h = Math.floor(total / 3600);
    const m = Math.floor((total % 3600) / 60);
    const s = total % 60;
    return h > 0
      ? String(h).padStart(2,'0') + ':' + String(m).padStart(2,'0') + ':' + String(s).padStart(2,'0')
      : String(m).padStart(2,'0') + ':' + String(s).padStart(2,'0');
  };

  // The native OpenGL video surface is placed exactly under the CSS .player.
  let lastRect = '';
  let lastInset = -1;
  function reportVideoRect() {
    if (!bridge || !player) return;
    const r = player.getBoundingClientRect();
    const radius = parseFloat(getComputedStyle(player).borderTopLeftRadius) || 0;
    const key = [r.x, r.y, r.width, r.height, radius].map(v => Math.round(v)).join(',');
    if (key !== lastRect) {
      lastRect = key;
      call('reportVideoRect', r.x, r.y, r.width, r.height, radius);
    }
    reportSubtitleInset();
  }

  // Distance from the bottom of the video to the top of the visible control
  // deck, so mpv can lift subtitles above the controls (0 = controls hidden).
  function reportSubtitleInset() {
    if (!bridge || !player) return;
    const deck = q('.control-deck');
    let inset = 0;
    const hidden = state.chromeVisible === false
      || (document.documentElement.classList.contains('is-mini') && !player.matches(':hover'));
    if (deck && !hidden) {
      const p = player.getBoundingClientRect();
      const d = deck.getBoundingClientRect();
      inset = Math.max(0, Math.round(p.bottom - d.top));
    }
    if (inset === lastInset) return;
    lastInset = inset;
    call('reportSubtitleInset', inset);
  }

  function setPlayIcon(paused) {
    const path = q('button[aria-label="Play"] svg path');
    if (path) path.setAttribute('d', paused ? 'M8.5 5.5v13l10-6.5z' : 'M8 5h3v14H8zm5 0h3v14h-3z');
    const railPath = q('button[aria-label="Player mode"] svg path');
    if (railPath) railPath.setAttribute('d', paused ? 'M8 5v14l11-7z' : 'M8 5h3v14H8zm5 0h3v14h-3z');
    const play = button('Play');
    if (play) play.title = paused ? 'Play (Space)' : 'Pause (Space)';
  }

  function setState(next) {
    state = Object.assign({}, state, next || {});
    if (!player) return;
    const loaded = !!state.loaded;
    const loading = !loaded && !!state.loading;
    const finished = loaded && !!state.finished;
    player.classList.toggle('media-active', loaded);
    player.classList.toggle('lambda-chrome-hidden', state.chromeVisible === false);
    reportSubtitleInset();
    player.classList.toggle('is-loading', loading);

    const title = q('.media-title strong');
    const eyebrow = q('.media-title .eyebrow');
    if (title) { title.textContent = state.title || 'Open or drop a video'; title.title = state.title || ''; }
    if (eyebrow) eyebrow.textContent = state.eyebrow || (loaded ? 'NOW PLAYING' : 'READY');

    const center = q('.center-state');
    const centerKicker = q('.center-copy span');
    const centerText = q('.center-copy strong');
    const showCenter = !loaded || !!state.paused || finished;
    if (center) center.classList.toggle('lambda-hidden', !showCenter);
    if (centerKicker) centerKicker.textContent = loading ? 'LOADING' : finished ? 'FINISHED' : loaded ? 'PAUSED' : 'READY';
    if (centerText) centerText.textContent = loading ? (state.title || 'Opening video…')
      : finished ? 'Press play to watch again'
      : loaded ? 'Press play to continue' : 'Open or drop a local video';

    setPlayIcon(!loaded || !!state.paused || finished);

    const labels = qa('.timeline-labels span');
    if (labels[0]) labels[0].textContent = format(state.position);
    if (labels[1]) labels[1].textContent = format(state.duration);
    const ratio = state.duration > 0 ? clamp(state.position / state.duration) : 0;
    const pct = (ratio * 100).toFixed(3) + '%';
    const progress = q('.timeline .progress');
    const glow = q('.timeline .timeline-glow');
    const thumb = q('.timeline .thumb');
    const buffered = q('.timeline .buffered');
    if (progress) progress.style.width = pct;
    if (glow) glow.style.width = pct;
    if (thumb) thumb.style.left = pct;
    if (buffered) buffered.style.width = (state.duration > 0 ? clamp((state.buffered || 0) / state.duration) * 100 : 0).toFixed(3) + '%';

    const timeStrong = q('.timecode strong');
    const timeRest = q('.timecode span');
    if (timeStrong) timeStrong.textContent = format(state.position);
    if (timeRest) timeRest.textContent = '/ ' + format(state.duration);

    const volumeFill = q('.volume-track span');
    if (volumeFill) volumeFill.style.width = (state.muted ? 0 : Math.max(0, Math.min(100, Number(state.volume) || 0))) + '%';
    const volumeButton = button('Volume');
    if (volumeButton) {
      volumeButton.classList.toggle('active', !!state.muted);
      volumeButton.title = state.muted ? 'Unmute (M)' : 'Mute (M)';
      const path = q('svg path', volumeButton);
      if (path) path.setAttribute('d', state.muted
        ? 'M5 9v6h4l5 4V5L9 9H5zm12 1 4 4m0-4-4 4'
        : 'M5 9v6h4l5 4V5L9 9H5zm12 1.5a3 3 0 0 1 0 3');
    }

    const speed = q('.control-cluster.right .text-button');
    if (speed) speed.textContent = String(Number(state.speed || 1).toFixed(2)).replace(/\.00$/,'').replace(/0$/,'') + '×';

    const quality = qa('.quality-badge > span:not(.status-dot)');
    if (quality[0]) quality[0].textContent = state.qualityPrimary || 'VIDEO';
    if (quality[1]) quality[1].textContent = state.qualitySecondary || 'ORIGINAL';
    const badge = q('.quality-badge');
    if (badge) badge.classList.toggle('rife-on', /RIFE/.test(state.qualitySecondary || ''));

    const chapterIndex = q('.scene-index');
    const chapterTitle = q('.scene-copy strong');
    if (chapterIndex) chapterIndex.textContent = state.chapterIndex || '--';
    if (chapterTitle) chapterTitle.textContent = state.chapterTitle || 'No chapters';

    const nextButton = button('Next');
    if (nextButton) {
      nextButton.disabled = !state.hasNext;
      nextButton.title = state.hasNext ? 'Next video in folder' : 'No next video in this folder';
    }
    const fullscreenButton = button('Fullscreen');
    if (fullscreenButton) fullscreenButton.title = document.documentElement.classList.contains('is-fullscreen') ? 'Exit fullscreen (F / Esc)' : 'Fullscreen (F)';
  }

  // ---- Settings panel ------------------------------------------------------
  function ensureSettings() {
    let panel = q('.lambda-settings');
    if (panel) return panel;
    panel = document.createElement('div');
    panel.className = 'lambda-settings glass-panel';
    panel.setAttribute('role', 'dialog');
    panel.setAttribute('aria-label', 'Playback settings');
    panel.innerHTML = '<div class="lambda-settings-head"><strong>Playback settings</strong><button class="lambda-settings-close" aria-label="Close settings" title="Close">×</button></div><div class="lambda-settings-body"></div>';
    player.appendChild(panel);
    q('.lambda-settings-close', panel).addEventListener('click', closeSettings);
    return panel;
  }

  function closeSettings() {
    const panel = q('.lambda-settings');
    if (panel) panel.classList.remove('open');
    qa('.settings-trigger').forEach(b => b.classList.remove('active'));
    if (window.LambdaWindow) window.LambdaWindow.refresh();
  }

  function renderSettings(focus='all', trigger=null) {
    const panel = ensureSettings();
    panel.dataset.focus = focus;
    const body = q('.lambda-settings-body', panel);
    const makeGroup = (name, key, items, selected, onClick) => {
      const section = document.createElement('section');
      section.className = 'lambda-group';
      section.dataset.group = key;
      const h = document.createElement('h4');
      h.textContent = name;
      section.appendChild(h);
      const options = document.createElement('div');
      options.className = 'lambda-options';
      let lastGroup = '';
      (items || []).forEach((item, index) => {
        // Subtitle sources (Embedded / External / Add-ons) get a sub-heading.
        if (item.group && item.group !== lastGroup) {
          const heading = document.createElement('div');
          heading.className = 'lambda-option-group';
          heading.textContent = item.group;
          options.appendChild(heading);
        }
        lastGroup = item.group || lastGroup;
        const b = document.createElement('button');
        b.className = 'lambda-option' + (index === selected ? ' selected' : '');
        b.textContent = item.label || String(item);
        b.disabled = item.enabled === false;
        b.addEventListener('click', () => onClick(index));
        options.appendChild(b);
      });
      section.appendChild(options);
      return section;
    };
    body.innerHTML = '';
    const audioItems = settings.audio && settings.audio.length ? settings.audio : [{label:'No audio tracks', enabled:false}];
    body.appendChild(makeGroup('Audio', 'audio', audioItems, settings.audioIndex, i => call('selectAudio', i)));
    const sub = makeGroup('Subtitles', 'subtitles', settings.subtitles, settings.subtitleIndex, i => call('selectSubtitle', i));
    const load = document.createElement('button');
    load.className = 'lambda-option lambda-action';
    load.textContent = 'Load external subtitle…';
    load.disabled = !state.loaded;
    load.addEventListener('click', () => call('action', 'load-subtitle'));
    sub.appendChild(load);
    body.appendChild(sub);
    body.appendChild(makeGroup('Frame interpolation', 'interpolation', settings.interpolation, settings.interpolationIndex, i => call('selectInterpolation', i)));
    const speeds = SPEEDS.map(v => ({label: String(v) + '×', enabled: true}));
    const selectedSpeed = Math.max(0, SPEEDS.findIndex(v => Math.abs(v - Number(state.speed || 1)) < 0.001));
    body.appendChild(makeGroup('Speed', 'speed', speeds, selectedSpeed, i => call('speed', SPEEDS[i])));
    const wasOpen = panel.classList.contains('open');
    panel.classList.add('open');
    qa('.settings-trigger').forEach(b => b.classList.toggle('active', b === trigger || (wasOpen && b.classList.contains('active') && !trigger)));
    if (window.LambdaWindow) window.LambdaWindow.refresh();
  }

  function toggleSettings(focus, trigger) {
    const panel = q('.lambda-settings');
    if (panel && panel.classList.contains('open') && panel.dataset.focus === focus) closeSettings();
    else renderSettings(focus, trigger);
  }

  function setSettings(next) {
    settings = Object.assign({}, settings, next || {});
    const panel = q('.lambda-settings.open');
    if (panel) renderSettings(panel.dataset.focus || 'all');
  }

  // ---- Bindings -------------------------------------------------------------
  function bind() {
    // Native-style tooltips for every icon button.
    qa('button[aria-label]').forEach(b => { if (!b.title) b.title = b.getAttribute('aria-label'); });

    const brand = q('.brand-chip');
    if (brand) {
      brand.addEventListener('click', () => call('action','home'));
      brand.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); call('action','home'); } });
    }
    const open = button('Open file');
    if (open) { open.title = 'Open video (Ctrl+O)'; open.addEventListener('click', () => call('action','open')); }
    const more = button('More options');
    if (more) { more.classList.add('settings-trigger'); more.addEventListener('click', () => toggleSettings('all', more)); }

    const playerMode = button('Player mode');
    if (playerMode) playerMode.addEventListener('click', () => call('action','play'));
    const audio = button('Audio');
    if (audio) { audio.title = 'Audio tracks'; audio.classList.add('settings-trigger'); audio.addEventListener('click', () => toggleSettings('audio', audio)); }
    qa('button[aria-label="Captions"]').forEach(b => {
      b.title = 'Subtitles';
      b.classList.add('settings-trigger');
      b.addEventListener('click', () => toggleSettings('subtitles', b));
    });
    const cinema = button('Cinema mode');
    if (cinema) { cinema.title = 'Fullscreen (F)'; cinema.addEventListener('click', () => call('action','fullscreen')); }

    const center = q('.play-core');
    if (center) center.addEventListener('click', () => call('action','play'));
    const play = button('Play');
    if (play) play.addEventListener('click', () => call('action','play'));
    const next = button('Next');
    if (next) next.addEventListener('click', () => call('action','next'));
    const vol = button('Volume');
    if (vol) vol.addEventListener('click', () => call('action','mute'));
    const settingsButton = button('Settings');
    if (settingsButton) { settingsButton.classList.add('settings-trigger'); settingsButton.addEventListener('click', () => toggleSettings('all', settingsButton)); }
    const fullscreen = button('Fullscreen');
    if (fullscreen) fullscreen.addEventListener('click', () => call('action','fullscreen'));
    const mini = button('Mini player');
    if (mini) { mini.title = 'Mini player (always on top)'; mini.addEventListener('click', () => call('action','mini')); }

    const speed = q('.control-cluster.right .text-button');
    if (speed) {
      speed.title = 'Playback speed';
      speed.classList.add('settings-trigger');
      speed.addEventListener('click', () => toggleSettings('speed', speed));
    }

    // Timeline: click / drag to seek, hover preview of the target time.
    const timeline = q('.timeline');
    if (timeline) {
      const tip = document.createElement('span');
      tip.className = 'timeline-tip';
      timeline.appendChild(tip);
      const ratioAt = e => { const r = timeline.getBoundingClientRect(); return clamp((e.clientX - r.left) / r.width); };
      const showTip = e => {
        if (!(state.duration > 0)) { timeline.classList.remove('tip-visible'); return; }
        const ratio = ratioAt(e);
        tip.textContent = format(ratio * state.duration);
        tip.style.left = (ratio * 100).toFixed(3) + '%';
        timeline.classList.add('tip-visible');
      };
      let dragging = false;
      timeline.addEventListener('pointerdown', e => {
        if (e.button !== 0) return;
        dragging = true;
        timeline.setPointerCapture(e.pointerId);
        timeline.classList.add('scrubbing');
        call('seek', ratioAt(e));
      });
      timeline.addEventListener('pointermove', e => { showTip(e); if (dragging) call('seek', ratioAt(e)); });
      const stop = () => { dragging = false; timeline.classList.remove('scrubbing'); };
      timeline.addEventListener('pointerup', stop);
      timeline.addEventListener('pointercancel', stop);
      timeline.addEventListener('pointerleave', () => { if (!dragging) timeline.classList.remove('tip-visible'); });
    }

    const volume = q('.volume-track');
    if (volume) {
      volume.title = 'Volume';
      const setVolume = e => {
        const r = volume.getBoundingClientRect();
        call('volume', Math.round(clamp((e.clientX - r.left) / r.width) * 100));
      };
      volume.addEventListener('pointerdown', e => { volume.setPointerCapture(e.pointerId); setVolume(e); });
      volume.addEventListener('pointermove', e => { if (e.buttons) setVolume(e); });
      volume.addEventListener('wheel', e => { e.preventDefault(); call('volume', Math.max(0, Math.min(100, (Number(state.volume) || 0) + (e.deltaY < 0 ? 5 : -5)))); }, {passive:false});
    }

    let lastActivity = 0;
    player.addEventListener('pointermove', () => {
      const now = performance.now();
      if (now - lastActivity > 220) {
        lastActivity = now;
        call('action','activity');
      }
    });
    player.addEventListener('dblclick', e => {
      if (!e.target.closest('button,.timeline,.volume-track,.lambda-settings,.topbar,.control-deck,.side-rail,[data-nodrag]')) call('action','fullscreen');
    });

    // Close the settings panel when clicking elsewhere or pressing Escape.
    document.addEventListener('pointerdown', e => {
      const panel = q('.lambda-settings.open');
      if (panel && !e.target.closest('.lambda-settings,.settings-trigger')) closeSettings();
    });
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && q('.lambda-settings.open')) { e.stopPropagation(); closeSettings(); }
    }, true);

    const ro = new ResizeObserver(reportVideoRect);
    ro.observe(player);
    const deck = q('.control-deck');
    if (deck) ro.observe(deck);
    player.addEventListener('pointerenter', reportSubtitleInset);
    player.addEventListener('pointerleave', reportSubtitleInset);
    window.addEventListener('resize', reportVideoRect);
    requestAnimationFrame(reportVideoRect);
  }

  window.lambdaUi = {
    setState,
    setSettings,
    reportVideoRect,
    closeSettings,
    settingsOpen: () => !!q('.lambda-settings.open'),
    toast: (message, kind) => window.LambdaWindow && window.LambdaWindow.toast(message, kind)
  };

  new QWebChannel(qt.webChannelTransport, channel => {
    bridge = channel.objects.lambdaBridge;
    if (window.LambdaWindow && channel.objects.windowBridge) window.LambdaWindow.attach(channel.objects.windowBridge);
    bind();
    call('ready');
    setState(state);
    setSettings(settings);
    requestAnimationFrame(reportVideoRect);
  });
})();
