(() => {
  let bridge = null;
  let state = {};
  let settings = {audio:[], subtitles:[], interpolation:[], audioIndex:0, subtitleIndex:0, interpolationIndex:0};
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

  function reportVideoRect() {
    if (!bridge || !player) return;
    const r = player.getBoundingClientRect();
    const radius = parseFloat(getComputedStyle(player).borderTopLeftRadius) || 0;
    call('reportVideoRect', r.x, r.y, r.width, r.height, radius);
  }

  function setPlayIcon(paused) {
    const path = q('button[aria-label="Play"] svg path');
    if (path) path.setAttribute('d', paused ? 'M8.5 5.5v13l10-6.5z' : 'M8 5h3v14H8zm5 0h3v14h-3z');
  }

  function setState(next) {
    state = Object.assign({}, state, next || {});
    if (!player) return;
    player.classList.toggle('media-active', !!state.loaded);
    player.classList.toggle('lambda-chrome-hidden', state.chromeVisible === false);

    const title = q('.media-title strong');
    const eyebrow = q('.media-title .eyebrow');
    if (title) title.textContent = state.title || 'Open or drop a video';
    if (eyebrow) eyebrow.textContent = state.eyebrow || (state.loaded ? 'NOW PLAYING' : 'READY');

    const center = q('.center-state');
    const centerKicker = q('.center-copy span');
    const centerText = q('.center-copy strong');
    const showCenter = !state.loaded || !!state.paused;
    if (center) center.classList.toggle('lambda-hidden', !showCenter);
    if (centerKicker) centerKicker.textContent = state.loaded ? 'PAUSED' : 'READY';
    if (centerText) centerText.textContent = state.loaded ? 'Press play to continue' : 'Open a local video';

    setPlayIcon(!state.loaded || !!state.paused);

    const labels = qa('.timeline-labels span');
    if (labels[0]) labels[0].textContent = format(state.position);
    if (labels[1]) labels[1].textContent = format(state.duration);
    const ratio = state.duration > 0 ? clamp(state.position / state.duration) : 0;
    const pct = (ratio * 100).toFixed(3) + '%';
    const progress = q('.timeline .progress');
    const glow = q('.timeline .timeline-glow');
    const thumb = q('.timeline .thumb');
    if (progress) progress.style.width = pct;
    if (glow) glow.style.width = pct;
    if (thumb) thumb.style.left = pct;

    const timeStrong = q('.timecode strong');
    const timeRest = q('.timecode span');
    if (timeStrong) timeStrong.textContent = format(state.position);
    if (timeRest) timeRest.textContent = '/ ' + format(state.duration);

    const volumeFill = q('.volume-track span');
    if (volumeFill) volumeFill.style.width = Math.max(0, Math.min(100, Number(state.volume) || 0)) + '%';

    const speed = q('.control-cluster.right .text-button');
    if (speed) speed.textContent = String(Number(state.speed || 1).toFixed(2)).replace(/\.00$/,'').replace(/0$/,'') + '×';

    const quality = qa('.quality-badge > span:not(.status-dot)');
    if (quality[0]) quality[0].textContent = state.qualityPrimary || 'VIDEO';
    if (quality[1]) quality[1].textContent = state.qualitySecondary || 'ORIGINAL';

    const chapterIndex = q('.scene-index');
    const chapterTitle = q('.scene-copy strong');
    if (chapterIndex) chapterIndex.textContent = state.chapterIndex || '--';
    if (chapterTitle) chapterTitle.textContent = state.chapterTitle || 'No chapters';

    const volumeButton = button('Volume');
    if (volumeButton) volumeButton.classList.toggle('active', !!state.muted);
  }

  function ensureSettings() {
    let panel = q('.lambda-settings');
    if (panel) return panel;
    panel = document.createElement('div');
    panel.className = 'lambda-settings glass-panel';
    panel.innerHTML = '<div class="lambda-settings-head"><strong>Playback settings</strong><button class="lambda-settings-close" aria-label="Close settings">×</button></div><div class="lambda-settings-body"></div>';
    player.appendChild(panel);
    q('.lambda-settings-close', panel).addEventListener('click', () => panel.classList.remove('open'));
    return panel;
  }

  function renderSettings(focus='all') {
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
      (items || []).forEach((item, index) => {
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
    body.appendChild(makeGroup('Audio', 'audio', settings.audio, settings.audioIndex, i => call('selectAudio', i)));
    const sub = makeGroup('Subtitles', 'subtitles', settings.subtitles, settings.subtitleIndex, i => call('selectSubtitle', i));
    const load = document.createElement('button');
    load.className = 'lambda-option lambda-action';
    load.textContent = 'Load external subtitle';
    load.addEventListener('click', () => call('action', 'load-subtitle'));
    sub.appendChild(load);
    body.appendChild(sub);
    body.appendChild(makeGroup('Interpolation', 'interpolation', settings.interpolation, settings.interpolationIndex, i => call('selectInterpolation', i)));
    const speeds = [0.5,0.75,1,1.25,1.5,2].map(v => ({label:String(v).replace('.0','') + '×', enabled:true}));
    const selectedSpeed = Math.max(0, [0.5,0.75,1,1.25,1.5,2].findIndex(v => Math.abs(v - Number(state.speed || 1)) < 0.001));
    body.appendChild(makeGroup('Speed', 'speed', speeds, selectedSpeed, i => call('speed', [0.5,0.75,1,1.25,1.5,2][i])));
    panel.classList.add('open');
  }

  function setSettings(next) {
    settings = Object.assign({}, settings, next || {});
    const panel = q('.lambda-settings.open');
    if (panel) renderSettings(panel.dataset.focus || 'all');
  }

  function bind() {
    const brand = q('.brand-chip');
    if (brand) brand.addEventListener('click', () => call('action','home'));
    const open = button('Open file');
    if (open) open.addEventListener('click', () => call('action','open'));
    const more = button('More options');
    if (more) more.addEventListener('click', () => renderSettings('all'));

    const playerMode = button('Player mode');
    if (playerMode) playerMode.addEventListener('click', () => call('action','play'));
    const audio = button('Audio');
    if (audio) audio.addEventListener('click', () => renderSettings('audio'));
    qa('button[aria-label="Captions"]').forEach(b => b.addEventListener('click', () => renderSettings('subtitles')));
    const cinema = button('Cinema mode');
    if (cinema) cinema.addEventListener('click', () => call('action','fullscreen'));

    const center = q('.play-core');
    if (center) center.addEventListener('click', () => call('action','play'));
    const play = button('Play');
    if (play) play.addEventListener('click', () => call('action','play'));
    const next = button('Next');
    if (next) next.addEventListener('click', () => call('action','next'));
    const vol = button('Volume');
    if (vol) vol.addEventListener('click', () => call('action','mute'));
    const settingsButton = button('Settings');
    if (settingsButton) settingsButton.addEventListener('click', () => renderSettings('all'));
    const fullscreen = button('Fullscreen');
    if (fullscreen) fullscreen.addEventListener('click', () => call('action','fullscreen'));

    const speed = q('.control-cluster.right .text-button');
    if (speed) speed.addEventListener('click', () => renderSettings('all'));

    const timeline = q('.timeline');
    if (timeline) {
      const seekAt = e => {
        const r = timeline.getBoundingClientRect();
        call('seek', clamp((e.clientX - r.left) / r.width));
      };
      timeline.addEventListener('pointerdown', e => { timeline.setPointerCapture(e.pointerId); seekAt(e); });
      timeline.addEventListener('pointermove', e => { if (e.buttons) seekAt(e); });
    }

    const volume = q('.volume-track');
    if (volume) {
      const setVolume = e => {
        const r = volume.getBoundingClientRect();
        call('volume', Math.round(clamp((e.clientX - r.left) / r.width) * 100));
      };
      volume.addEventListener('pointerdown', e => { volume.setPointerCapture(e.pointerId); setVolume(e); });
      volume.addEventListener('pointermove', e => { if (e.buttons) setVolume(e); });
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
      if (!e.target.closest('button,.timeline,.volume-track,.lambda-settings')) call('action','fullscreen');
    });

    const ro = new ResizeObserver(reportVideoRect);
    ro.observe(player);
    window.addEventListener('resize', reportVideoRect);
    requestAnimationFrame(reportVideoRect);
  }

  window.lambdaUi = { setState, setSettings, reportVideoRect };

  new QWebChannel(qt.webChannelTransport, channel => {
    bridge = channel.objects.lambdaBridge;
    bind();
    call('ready');
    setState(state);
    setSettings(settings);
    requestAnimationFrame(reportVideoRect);
  });
})();