const fs = require('node:fs/promises');
const path = require('node:path');
const { randomUUID } = require('node:crypto');

function youtubeVideo(value) {
  try {
    const url = new URL(value);
    if (url.protocol !== 'https:' || url.username || url.password || url.port) return null;
    let id;
    if (url.hostname === 'youtu.be' && url.pathname.split('/').filter(Boolean).length === 1) id = url.pathname.slice(1);
    else if (['youtube.com', 'www.youtube.com', 'm.youtube.com', 'music.youtube.com'].includes(url.hostname)) {
      if (url.pathname === '/watch') id = url.searchParams.get('v');
      else id = url.pathname.match(/^\/(?:shorts|live)\/([\w-]{11})(?:\/|$)/)?.[1];
    }
    return /^[\w-]{11}$/.test(id || '') ? `https://www.youtube.com/watch?v=${id}` : null;
  } catch { return null; }
}

function httpUrl(value) {
  try { return ['https:', 'http:'].includes(new URL(value).protocol); } catch { return false; }
}

// A watch URL can also identify a collection. Keep that context until the user
// selects a specific entry; stream resolution still uses a single video URL.
function youtubeSource(value) {
  const video = youtubeVideo(value);
  try {
    const url = new URL(value);
    if (url.protocol !== 'https:' || url.username || url.password || url.port) return video;
    if (!['youtube.com', 'www.youtube.com', 'm.youtube.com', 'music.youtube.com', 'youtu.be'].includes(url.hostname)) return video;
    if (!video && url.pathname !== '/playlist') return null;
    const list = url.searchParams.get('list');
    if (!/^[\w-]{1,200}$/.test(list || '')) return video;
    const source = new URL(video || 'https://www.youtube.com/playlist');
    source.searchParams.set('list', list);
    const index = url.searchParams.get('index');
    if (/^[1-9]\d{0,8}$/.test(index || '')) source.searchParams.set('index', index);
    return source.href;
  } catch { return video; }
}

function youtubePlaylist(value) {
  const source = youtubeSource(value);
  return source && new URL(source).searchParams.has('list') ? source : null;
}

function sessionDomain(domain) {
  const value = domain.replace(/^\./, '').toLowerCase();
  return ['youtube.com', 'google.com', 'accounts.google.com'].includes(value) || value.endsWith('.youtube.com');
}

// Converts an imported cookie record into Electron's cookies.set() details.
// A leading dot marks a domain cookie; without it the cookie is host-only, and
// Chromium rejects __Host- cookies that carry a domain or a path other than "/".
function electronCookie(record, domains) {
  const domain = record.domain.replace(/^\./, '').toLowerCase();
  const allowed = domains ? domains.some(root => domain === root || domain.endsWith('.' + root)) : sessionDomain(record.domain);
  if (!allowed) return null;
  const host = record.domain.replace(/^\./, '');
  const hostPrefix = record.name.startsWith('__Host-');
  const path = hostPrefix ? '/' : record.path || '/';
  const cookie = { url: `https://${host}${path}`, name: record.name, value: record.value, path,
    secure: Boolean(record.secure) || hostPrefix || record.name.startsWith('__Secure-'),
    httpOnly: Boolean(record.http_only) };
  if (record.domain.startsWith('.')) {
    if (hostPrefix) return null;
    cookie.domain = record.domain;
  }
  if (record.expires > 0) cookie.expirationDate = record.expires;
  return cookie;
}

async function exportCookies(filename, records, domains = ['youtube.com']) {
  const rows = ['# Netscape HTTP Cookie File'];
  for (const c of records) {
    const domain = c.domain.replace(/^\./, '').toLowerCase();
    if (!domains.some(root => domain === root || domain.endsWith('.' + root))) continue;
    const fields = [c.domain, c.domain.startsWith('.') ? 'TRUE' : 'FALSE', c.path || '/', c.secure ? 'TRUE' : 'FALSE',
      String(Math.floor(c.expirationDate || 0)), c.name, c.value];
    if (fields.some(value => /[\r\n\t\0]/.test(value))) continue;
    if (c.httpOnly) fields[0] = '#HttpOnly_' + fields[0];
    rows.push(fields.join('\t'));
  }
  await fs.mkdir(path.dirname(filename), { recursive: true });
  const temporary = filename + '.' + randomUUID() + '.tmp';
  try {
    await fs.writeFile(temporary, rows.join('\n') + '\n', { mode: 0o600 });
    await fs.chmod(temporary, 0o600);
    await fs.rename(temporary, filename);
  } finally { await fs.rm(temporary, { force: true }); }
}

function extractorError(error) {
  if (error.name === 'AbortError') return error;
  const text = String(error.message);
  if (/not a bot|confirm your age|Sign in to confirm|LOGIN_REQUIRED/i.test(text))
    return new Error('该站点要求登录后才能解析这个视频。请在右侧网页中登录，或在“登录会话与播放设置”里导入浏览器登录会话，然后重试。');
  return new Error(text.replace(/^ERROR:\s*(\[\w+\]\s*[\w-]+:\s*)?/, '解析失败：'));
}

async function resolveMedia(processes, extractor, source, { cookies, quality = '', signal } = {}) {
  const args = ['--ignore-config', '--no-playlist', '--no-warnings', '--skip-download', '--dump-single-json'];
  if (cookies) args.push('--cookies', cookies);
  if (quality) args.push('--format', quality);
  args.push('--', source);
  let output;
  try { output = await processes.run(extractor, args, { signal, timeout: 60000 }); }
  catch (error) { throw extractorError(error); }
  let data = JSON.parse(output);
  // Some extractors (e.g. multi-part Bilibili videos) return a collection even
  // with --no-playlist. Resolve one actual entry instead of treating its cover as media.
  if (data.entries) data = data.entries.find(entry => entry && (entry.url || entry.requested_formats || entry.requested_downloads)) || {};
  const formats = data.requested_formats || data.requested_downloads || [data];
  const video = formats.find(item => item.vcodec && item.vcodec !== 'none') || formats.find(item => item.url);
  const audio = formats.find(item => item.acodec && item.acodec !== 'none' && item !== video);
  if (!video || !httpUrl(video.url) || (audio && !httpUrl(audio.url))) throw new Error('没有找到可直接播放的音视频流');
  const headers = video.http_headers || data.http_headers || {};
  const userAgent = headers['User-Agent'] || '';
  const referrer = headers.Referer || headers.Referrer || '';
  if (/[\r\n\0]/.test(userAgent + referrer)) throw new Error('视频返回的请求头无效');
  return { source, video: video.url, audio: audio?.url || '', userAgent, referrer, title: data.title || '在线视频' };
}

async function resolvePlaylist(processes, extractor, source, { cookies, signal, onUpdate } = {}) {
  // Flat, lazy extraction produces usable entries before later pages arrive.
  // Print only list metadata, omitting thumbnails and other large per-video data.
  const args = ['--ignore-config', '--yes-playlist', '--flat-playlist', '--lazy-playlist', '--ignore-errors', '--no-warnings', '--skip-download',
    '--print', '%(.{id,title,url,webpage_url,playlist_index,playlist_title,availability})j'];
  if (cookies) args.push('--cookies', cookies);
  args.push('--', source);
  const entries = [];
  let title = '视频合集', timer, received = false;
  const snapshot = complete => ({ source, title, entries: entries.slice(), complete });
  const publish = () => { timer = null; onUpdate?.(snapshot(false)); };
  const append = entry => {
    if (!entry || ['private', 'unavailable'].includes(entry.availability)) return;
    const url = youtubeVideo(entry.webpage_url) || youtubeVideo(entry.url)
      || (/^[\w-]{11}$/.test(entry.id || '') ? `https://www.youtube.com/watch?v=${entry.id}` : null);
    if (!url) return;
    if (entry.playlist_title) title = entry.playlist_title;
    entries.push({ source: url, title: entry.title || `视频 ${entries.length + 1}`, index: entry.playlist_index || entries.length + 1 });
    if (entries.length === 1) publish();
    else if (!timer) timer = setTimeout(publish, 250);
  };
  const consume = line => {
    received = true;
    const data = JSON.parse(line);
    if (Array.isArray(data.entries)) { title = data.title || title; data.entries.forEach(append); }
    else append(data);
  };
  try {
    const output = await processes.run(extractor, args, { signal, onLine: consume, idleTimeout: true, timeout: 60000 });
    if (!received && output.trim()) output.trim().split('\n').forEach(consume);
  } catch (error) { throw extractorError(error); }
  finally { clearTimeout(timer); }
  if (!entries.length) throw new Error('合集中没有可播放的视频，请检查合集是否公开或已登录。');
  return snapshot(true);
}

module.exports = { youtubeVideo, youtubeSource, youtubePlaylist, httpUrl, sessionDomain, electronCookie, exportCookies, resolveMedia, resolvePlaylist };
