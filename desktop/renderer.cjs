const $ = id => document.getElementById(id);
const invoke = (action, value) => window.muralop.invoke(action, value).catch(error => {
  $('message').textContent = error.message;
});
let state = {}, initialized = false, playlistKey = '';
const labels = { connecting:'连接中', ready:'就绪', resolving:'解析中', loading:'加载中', playing:'播放中', paused:'已暂停', cancelled:'已取消', error:'失败' };
window.muralop.onState(next => {
  state = { ...state, ...next };
  $('feedback').dataset.phase = state.phase;
  $('phase').textContent = labels[state.phase] || state.phase;
  $('message').textContent = state.message;
  $('playback-summary').textContent = `${labels[state.phase] || state.phase}${state.title ? ' · ' + state.title : ''}`;
  $('playback-summary').dataset.phase = state.phase;
  $('cancel').hidden = !state.canCancel;
  $('play').disabled = !state.backendOnline;
  $('pause').textContent = state.paused ? '继续' : '暂停';
  $('mute').textContent = state.mute ? '取消静音' : '静音';
  for (const id of ['pause','mute','volume','clear']) $(id).disabled = !state.backendOnline;
  for (const id of ['import-browser','import-file','clear-session']) $(id).disabled = state.importing;
  $('import-browser').textContent = state.importing ? '正在导入会话…' : '导入浏览器登录会话';
  if (document.activeElement !== $('volume')) { $('volume').value = state.volume; $('volume-value').textContent = state.volume; }
  const monitors = state.monitors || [];
  const keys = monitors.map(m => m.id).join('\n');
  if ($('monitor').dataset.keys !== keys) {
    const selected = $('monitor').value;
    $('monitor').replaceChildren(new Option('所有显示器（默认视频）', ''), ...monitors.map(m => new Option(`${m.id} · ${m.width}×${m.height}`, m.id)));
    $('monitor').value = [...$('monitor').options].some(o => o.value === selected) ? selected : '';
    $('monitor').dataset.keys = keys;
  }
  if (!initialized && state.settings) {
    for (const id of ['quality','extractor','browser','profile']) $(id).value = state.settings[id] || '';
    $('extractor').placeholder = state.defaultExtractor || '使用内置解析器';
    initialized = true;
  }
  $('browser-status').textContent = state.browserError || (state.browserLoading ? '正在加载网页…' : '网页已加载');
  $('browser-status').dataset.error = Boolean(state.browserError);
  $('retry-browser').hidden = !state.browserError;
  if (state.sites && !$('site').options.length) $('site').replaceChildren(...state.sites.map(site => new Option(site.name, site.id)));
  if (state.settings) $('site').value = state.settings.site || 'youtube';
  const playlist = state.playlist;
  const key = playlist ? `${playlist.source}\n${playlist.entries.length}` : '';
  $('playlist-panel').hidden = !playlist;
  if (playlist && key !== playlistKey) {
    $('playlist-title').textContent = `${playlist.title} · ${playlist.entries.length} 个视频`;
    $('playlist-entry').replaceChildren(...playlist.entries.map((entry, index) => new Option(`${entry.index}. ${entry.title}`, String(index))));
  }
  playlistKey = key;
  if (playlist && (next.playlist !== undefined || next.playlistIndex !== undefined && $('playlist-entry').dataset.playing !== String(state.playlistIndex))) {
    $('playlist-entry').value = String(state.playlistIndex);
    $('playlist-entry').dataset.playing = String(state.playlistIndex);
  }
  updatePlaylistControls();
});
function updatePlaylistControls() {
  const index = Number($('playlist-entry').value);
  $('play-entry').disabled = !state.backendOnline || !state.playlist;
  $('previous-entry').disabled = !state.backendOnline || !state.playlist || index <= 0;
  $('next-entry').disabled = !state.backendOnline || !state.playlist || index >= state.playlist.entries.length - 1;
}
setInterval(() => {
  $('elapsed').textContent = state.canCancel && state.startedAt ? `${Math.floor((Date.now() - state.startedAt) / 1000)} 秒` : '';
}, 250);
$('playlist-entry').addEventListener('change', updatePlaylistControls);
$('play-entry').addEventListener('click', () => invoke('play-entry', Number($('playlist-entry').value)));
for (const [id, offset] of [['previous-entry', -1], ['next-entry', 1]]) $(id).addEventListener('click', () => {
  $('playlist-entry').selectedIndex += offset;
  updatePlaylistControls();
  invoke('play-entry', Number($('playlist-entry').value));
});
$('play-form').addEventListener('submit', event => { event.preventDefault(); invoke('play', $('source').value); });
for (const action of ['cancel','pause','mute','file','clear','connect','quit-ui','retry-browser','import-browser','import-file','clear-session']) $(action).addEventListener('click', () => invoke(action));
$('monitor').addEventListener('change', () => invoke('monitor', $('monitor').value));
$('site').addEventListener('change', () => invoke('site', $('site').value));
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
    window.muralop.layout({ x: bounds.x + 1, y: bounds.y + 1, width: bounds.width - 2, height: bounds.height - 2 });
  });
};
new ResizeObserver(resize).observe($('browser-slot'));
window.addEventListener('resize', resize);
document.addEventListener('visibilitychange', () => { if (!document.hidden) resize(); });
invoke('state');
