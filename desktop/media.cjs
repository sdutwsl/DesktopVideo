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
function electronCookie(record) {
  if (!sessionDomain(record.domain)) return null;
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

async function exportCookies(filename, records) {
  const rows = ['# Netscape HTTP Cookie File'];
  for (const c of records) {
    const domain = c.domain.replace(/^\./, '').toLowerCase();
    if (domain !== 'youtube.com' && !domain.endsWith('.youtube.com')) continue;
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
    return new Error('YouTube 要求登录后才能解析这个视频。请在右侧网页登录 YouTube，或在“登录会话与播放设置”里导入浏览器登录会话，然后重试。');
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
  const data = JSON.parse(output);
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

async function resolvePlaylist(processes, extractor, source, { cookies, signal } = {}) {
  const args = ['--ignore-config', '--yes-playlist', '--flat-playlist', '--ignore-errors', '--no-warnings', '--skip-download', '--dump-single-json'];
  if (cookies) args.push('--cookies', cookies);
  args.push('--', source);
  let output;
  try { output = await processes.run(extractor, args, { signal, timeout: 60000 }); }
  catch (error) { throw extractorError(error); }
  const data = JSON.parse(output);
  const entries = (data.entries || []).flatMap((entry, index) => {
    if (!entry || ['private', 'unavailable'].includes(entry.availability)) return [];
    const url = youtubeVideo(entry.webpage_url) || youtubeVideo(entry.url)
      || (/^[\w-]{11}$/.test(entry.id || '') ? `https://www.youtube.com/watch?v=${entry.id}` : null);
    return url ? [{ source: url, title: entry.title || `视频 ${index + 1}`, index: entry.playlist_index || index + 1 }] : [];
  });
  if (!entries.length) throw new Error('合集中没有可播放的视频，请检查合集是否公开或已登录。');
  return { source, title: data.title || '视频合集', entries };
}

module.exports = { youtubeVideo, youtubeSource, youtubePlaylist, httpUrl, sessionDomain, electronCookie, exportCookies, resolveMedia, resolvePlaylist };
