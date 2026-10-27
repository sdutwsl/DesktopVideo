const $ = id => document.getElementById(id);
const invoke = (action, value) => window.muralop.invoke(action, value).catch(error => {
  $('message').textContent = error.message;
});
let state = {}, initialized = false, phaseAt = Date.now(), lastPhase = '';
const labels = { connecting:'连接中', ready:'就绪', resolving:'解析中', loading:'加载中', playing:'播放中', paused:'已暂停', cancelled:'已取消', error:'失败' };
window.muralop.onState(next => {
  state = { ...state, ...next };
  if (state.phase !== lastPhase) { lastPhase = state.phase; phaseAt = Date.now(); }
  $('feedback').dataset.phase = state.phase;
  $('phase').textContent = labels[state.phase] || state.phase;
  $('message').textContent = state.message;
  $('cancel').hidden = !state.canCancel;
  $('play').disabled = !state.backendOnline;
  $('pause').textContent = state.paused ? '继续' : '暂停';
  $('mute').textContent = state.mute ? '取消静音' : '静音';
  for (const id of ['pause','mute','volume','clear','play-current']) $(id).disabled = !state.backendOnline;
  for (const id of ['import-browser','import-file']) $(id).disabled = state.importing;
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
    $('auto').checked = state.settings.autoPlay;
    $('extractor').placeholder = state.defaultExtractor || '使用内置解析器';
    initialized = true;
  }
  if (state.browserUrl && document.activeElement !== $('address')) $('address').value = state.browserUrl;
  $('reload').textContent = state.browserLoading ? '···' : '↻';
});
setInterval(() => {
  $('elapsed').textContent = state.canCancel ? `${Math.floor((Date.now() - phaseAt) / 1000)} 秒` : '';
}, 250);
$('play-form').addEventListener('submit', event => { event.preventDefault(); invoke('play', $('source').value); });
$('navigation').addEventListener('submit', event => { event.preventDefault(); invoke('navigate', $('address').value); });
for (const action of ['cancel','pause','mute','file','clear','connect','back','forward','reload','play-current','import-browser','import-file']) $(action).addEventListener('click', () => invoke(action));
$('monitor').addEventListener('change', () => invoke('monitor', $('monitor').value));
$('volume').addEventListener('input', () => { $('volume-value').textContent = $('volume').value; });
$('volume').addEventListener('change', () => invoke('volume', Number($('volume').value)));
for (const id of ['auto','quality','extractor','browser','profile']) $(id).addEventListener('change', () => invoke('settings', {
  autoPlay: $('auto').checked, quality: $('quality').value, extractor: $('extractor').value.trim(), browser: $('browser').value, profile: $('profile').value,
}));
const resize = () => {
  const bounds = $('browser-slot').getBoundingClientRect();
  window.muralop.layout({ x: bounds.x + 1, y: bounds.y + 1, width: bounds.width - 2, height: bounds.height - 2 });
};
new ResizeObserver(resize).observe($('browser-slot'));
window.addEventListener('resize', resize);
invoke('state');
