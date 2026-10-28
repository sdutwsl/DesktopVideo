const $ = id => document.getElementById(id);
const setText = (id, value) => {
  value = String(value ?? '');
  if ($(id).textContent !== value) $(id).textContent = value;
};
const invoke = (action, value) => window.muralop.invoke(action, value).catch(error => {
  setText('message', error.message);
});
let state = {}, initialized = false, playlistKey = '', lastPhase = '', phaseAnimation;
const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
const steps = [...document.querySelectorAll('#load-steps li')];
const labels = { connecting:'连接中', ready:'就绪', resolving:'解析中', loading:'加载中', playing:'播放中', paused:'已暂停', cancelled:'已取消', error:'失败' };
window.muralop.onState(next => {
  state = { ...state, ...next };
  document.body.dataset.hidden = state.uiVisible === false;
  const busy = ['connecting', 'resolving', 'loading'].includes(state.phase);
  $('feedback').dataset.phase = state.phase;
  $('feedback').setAttribute('aria-busy', busy);
  $('progress').setAttribute('aria-hidden', !busy);
  setText('phase', labels[state.phase] || state.phase);
  if (lastPhase && state.phase !== lastPhase && state.uiVisible !== false && !reduceMotion.matches) {
    phaseAnimation?.cancel();
    phaseAnimation = $('phase').animate([{ opacity: .35, transform: 'translateY(3px)' }, { opacity: 1, transform: 'translateY(0)' }], { duration: 220, easing: 'ease-out' });
  }
  lastPhase = state.phase;
  if (state.diagnostics) {
    const info = state.diagnostics.application;
    setText('app-version', info.version);
    setText('app-fingerprint', info.fingerprint || '—');
    setText('app-location', info.location);
    setText('app-plugin', info.plugin);
    const errorKey = JSON.stringify(state.diagnostics.errors);
    if ($('recent-errors').dataset.key !== errorKey) {
      $('recent-errors').dataset.key = errorKey;
      $('recent-errors').replaceChildren(...state.diagnostics.errors.slice().reverse().map(entry => {
        const item = document.createElement('li');
        item.textContent = `${new Date(entry.at).toLocaleString()} · ${entry.category}\n${entry.message}${entry.count > 1 ? `（${entry.count} 次）` : ''}`;
        return item;
      }));
    }
  }
  const stage = ['resolving', 'loading', 'playing'].indexOf(state.phase === 'paused' ? 'playing' : state.phase);
  steps.forEach((step, index) => {
    step.dataset.state = stage < 0 ? 'pending' : index < stage || stage === 2 ? 'done' : index === stage ? 'active' : 'pending';
    if (index === stage) step.setAttribute('aria-current', 'step');
    else step.removeAttribute('aria-current');
  });
  setText('message', state.message);
  $('message').title = state.message || '';
  setText('playback-summary', `${labels[state.phase] || state.phase}${state.title ? ' · ' + state.title : ''}`);
  $('playback-summary').title = state.title || state.message || '';
  $('playback-summary').dataset.phase = state.phase;
  document.querySelector('.now-playing').dataset.phase = state.phase;
  $('cancel').hidden = !state.canCancel && !state.playlistLoading;
  setText('cancel', state.canCancel ? '取消' : '停止读取');
  $('play').disabled = !state.playerReady;
  setText('pause', state.paused ? '继续' : '暂停');
  setText('mute', state.mute ? '取消静音' : '静音');
  $('pause').setAttribute('aria-pressed', Boolean(state.paused));
  $('mute').setAttribute('aria-pressed', Boolean(state.mute));
  for (const id of ['pause','mute','volume']) $(id).disabled = !state.playerReady;
  $('clear').disabled = !state.playerReady || !$('monitor').value;
  $('connect').disabled = state.phase === 'connecting';
  $('connect').hidden = Boolean(state.playerReady);
  setText('connect', state.phase === 'connecting' ? '连接中…' : '重启播放');
  setText('player-status', state.playerReady ? '播放已就绪' : state.phase === 'connecting' ? '正在启动播放…' : '播放未就绪');
  $('player-status').dataset.online = Boolean(state.playerReady);
  for (const id of ['import-browser','import-file','clear-session']) $(id).disabled = state.importing;
  setText('import-browser', state.importing ? '正在导入全部平台会话…' : '一键导入全部平台会话');
  if (document.activeElement !== $('volume')) { $('volume').value = state.volume; setText('volume-value', state.volume); }
  const monitors = state.monitors || [];
  const keys = monitors.map(m => m.id).join('\n');
  if ($('monitor').dataset.keys !== keys) {
    const selected = $('monitor').value;
    $('monitor').replaceChildren(new Option('所有显示器（默认视频）', ''), ...monitors.map(m => new Option(`${m.id} · ${m.width}×${m.height}`, m.id)));
    $('monitor').value = [...$('monitor').options].some(o => o.value === selected) ? selected : '';
    $('monitor').dataset.keys = keys;
  }
  $('clear').disabled = !state.playerReady || !$('monitor').value;
  if (!initialized && state.settings) {
    for (const id of ['quality','extractor','browser','profile']) $(id).value = state.settings[id] || '';
    $('extractor').placeholder = state.defaultExtractor || '使用内置解析器';
    initialized = true;
  }
  setText('browser-status', state.browserError || (state.browserSlow ? '网页加载较慢，可继续使用左侧控制' : state.browserLoading || !state.browserHasContent ? '正在加载网页…' : '网页已就绪'));
  $('browser-status').title = $('browser-status').textContent;
  $('browser-status').dataset.error = Boolean(state.browserError);
  const browserState = state.browserHasContent ? 'ready' : state.browserError ? 'error' : 'loading';
  $('browser-slot').dataset.state = browserState;
  $('browser-slot').dataset.loading = Boolean(state.browserLoading);
  $('browser-slot').setAttribute('aria-busy', browserState === 'loading');
  const browserMeta = document.querySelector('.browser-meta');
  browserMeta.dataset.loading = Boolean(state.browserLoading);
  browserMeta.dataset.error = Boolean(state.browserError);
  browserMeta.dataset.ready = Boolean(state.browserHasContent && !state.browserLoading && !state.browserError);
  browserMeta.title = $('browser-status').textContent;
  $('retry-browser').hidden = !state.browserError && !state.browserSlow;
  $('browser-loader').hidden = browserState === 'error';
  const siteName = state.sites?.find(site => site.id === state.settings?.site)?.name || '视频站点';
  setText('site-name', siteName);
  $('site-switch').title = `切换平台 · 当前 ${siteName}`;
  setText('window-maximize', state.maximized ? '❐' : '□');
  $('window-maximize').title = state.maximized ? '还原窗口' : '最大化';
  $('window-maximize').setAttribute('aria-label', $('window-maximize').title);
  setText('placeholder-title', browserState === 'error' ? '网页暂时无法显示' : state.browserSlow ? '网页还在加载，请稍候' : `正在打开 ${siteName}`);
  setText('placeholder-message', state.browserError || '网页就绪后会自动显示，左侧控制可继续使用。');
  const playlist = state.playlist;
  const key = playlist?.source || '';
  const reset = key !== playlistKey || $('playlist-entry').options.length > (playlist?.entries.length || 0);
  $('playlist-panel').hidden = !playlist && !state.playlistLoading && !state.playlistError;
  $('playlist-panel').dataset.loading = Boolean(state.playlistLoading);
  setText('playlist-status', state.playlistError ? `列表读取未完成：${state.playlistError}`
    : state.playlistLoading ? `已获取 ${playlist?.entries.length || 0} 个视频，列表仍在补齐，可以先选先播。` : '');
  if (reset) $('playlist-entry').replaceChildren();
  if (playlist) {
    setText('playlist-title', `${playlist.title} · ${playlist.entries.length} 个视频${state.playlistLoading ? '（读取中）' : playlist.complete === false ? '（未完整）' : ''}`);
    const fragment = document.createDocumentFragment();
    for (let index = $('playlist-entry').options.length; index < playlist.entries.length; index++) {
      const entry = playlist.entries[index];
      fragment.append(new Option(`${entry.index}. ${entry.title}`, String(index)));
    }
    $('playlist-entry').append(fragment);
  } else {
    setText('playlist-title', '视频合集');
  }
  playlistKey = key;
  if (playlist && (reset || $('playlist-entry').dataset.playing !== String(state.playlistIndex))) {
    $('playlist-entry').value = String(state.playlistIndex);
    $('playlist-entry').dataset.playing = String(state.playlistIndex);
  }
  updatePlaylistControls();
});
function updatePlaylistControls() {
  const index = Number($('playlist-entry').value);
  const selected = state.playerReady && state.playlist && $('playlist-entry').selectedIndex >= 0;
  $('play-entry').disabled = !selected;
  $('previous-entry').disabled = !selected || index <= 0;
  $('next-entry').disabled = !selected || index >= state.playlist.entries.length - 1;
}
setInterval(() => {
  setText('elapsed', state.canCancel && state.startedAt ? `${Math.floor((Date.now() - state.startedAt) / 1000)} 秒` : '');
}, 1000);
$('playlist-entry').addEventListener('change', updatePlaylistControls);
$('play-entry').addEventListener('click', () => invoke('play-entry', Number($('playlist-entry').value)));
for (const [id, offset] of [['previous-entry', -1], ['next-entry', 1]]) $(id).addEventListener('click', () => {
  $('playlist-entry').selectedIndex += offset;
  updatePlaylistControls();
  invoke('play-entry', Number($('playlist-entry').value));
});
$('play-form').addEventListener('submit', event => {
  event.preventDefault();
  if (!$('source').value.trim()) {
    $('source').setCustomValidity('请先粘贴视频网址，也可以直接点击右侧网页中的视频。');
    $('source').reportValidity();
    return;
  }
  invoke('play', $('source').value);
});
$('source').addEventListener('input', () => $('source').setCustomValidity(''));
for (const action of ['cancel','pause','mute','file','clear','connect','retry-browser','import-browser','import-file','clear-session','window-minimize','window-maximize','window-hide']) $(action).addEventListener('click', () => invoke(action));
$('monitor').addEventListener('change', () => { $('clear').disabled = !state.playerReady || !$('monitor').value; invoke('monitor', $('monitor').value); });
$('site-switch').addEventListener('click', () => {
  const bounds = $('site-switch').getBoundingClientRect();
  invoke('site-menu', { x: bounds.x, y: bounds.bottom + 4 });
});
$('volume').addEventListener('input', () => { $('volume-value').textContent = $('volume').value; });
$('volume').addEventListener('change', () => invoke('volume', Number($('volume').value)));
for (const id of ['quality','extractor','browser','profile']) $(id).addEventListener('change', () => invoke('settings', {
  quality: $('quality').value, extractor: $('extractor').value.trim(), browser: $('browser').value, profile: $('profile').value,
}));
let layoutFrame;
const resize = () => {
  cancelAnimationFrame(layoutFrame);
  layoutFrame = requestAnimationFrame(() => {
    const bounds = $('browser-slot').getBoundingClientRect();
    window.muralop.layout({ x: bounds.x, y: bounds.y + 3, width: bounds.width, height: bounds.height - 3 });
  });
};
new ResizeObserver(resize).observe($('browser-slot'));
window.addEventListener('resize', resize);
document.addEventListener('visibilitychange', () => { if (!document.hidden) resize(); });
invoke('state');
for (const action of ['copy-diagnostics', 'clear-errors']) $(action).addEventListener('click', async () => {
  const result = await invoke(action);
  if (result) setText('diagnostics-message', result);
});
