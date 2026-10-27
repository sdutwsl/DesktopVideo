const { ipcRenderer } = require('electron');
let autoPlay = true;
ipcRenderer.on('auto-play', (_event, value) => { autoPlay = Boolean(value); });
document.addEventListener('click', event => {
  if (!autoPlay || !event.isTrusted || event.button !== 0 || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
  if (!['www.youtube.com', 'm.youtube.com'].includes(location.hostname)) return;
  const anchor = event.target.closest?.('a[href]');
  if (!anchor) return;
  const url = new URL(anchor.href, location.href);
  if (!['www.youtube.com', 'm.youtube.com'].includes(url.hostname)) return;
  if (url.pathname !== '/watch' && !/^\/(shorts|live)\//.test(url.pathname)) return;
  event.preventDefault();
  event.stopImmediatePropagation();
  ipcRenderer.send('youtube-play', url.href);
}, true);
