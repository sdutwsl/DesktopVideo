// Shared by the main process and the browser preload (sent over private IPC).
const sites = [
  { id: 'youtube', name: 'YouTube', home: 'https://www.youtube.com/', domains: ['youtube.com', 'youtu.be'], loginDomains: ['google.com'], paths: ['^/watch\\?', '^/playlist\\?', '^/(shorts|live)/[\\w-]{11}'] },
  { id: 'bilibili', name: '哔哩哔哩', home: 'https://www.bilibili.com/', domains: ['bilibili.com', 'b23.tv'], paths: ['^/video/(BV[\\w]+|av\\d+)', '^/bangumi/play/(ep|ss)\\d+'] },
  { id: 'douyin', name: '抖音', home: 'https://www.douyin.com/', domains: ['douyin.com'], paths: ['^/video/\\d+', '[?&]modal_id=\\d+'] },
  { id: 'tiktok', name: 'TikTok', home: 'https://www.tiktok.com/', domains: ['tiktok.com'], paths: ['^/@[^/]+/video/\\d+', '^/(t/)?[A-Z][\\w]+/?(?:\\?|$)'] },
  { id: 'vimeo', name: 'Vimeo', home: 'https://vimeo.com/', domains: ['vimeo.com'], paths: ['^/\\d+(?:/|\\?|$)', '^/(channels/[^/]+|groups/[^/]+/videos|video)/\\d+'] },
  { id: 'dailymotion', name: 'Dailymotion', home: 'https://www.dailymotion.com/', domains: ['dailymotion.com', 'dai.ly'], paths: ['^/video/[a-zA-Z0-9]+'] },
  { id: 'twitch', name: 'Twitch', home: 'https://www.twitch.tv/', domains: ['twitch.tv'], paths: ['^/videos/\\d+', '^/[^/]+/clip/[\\w-]+'] },
];
const matchesDomain = (host, domain) => host === domain || host.endsWith('.' + domain);
function videoSite(value) {
  try {
    const url = new URL(value);
    if (url.protocol !== 'https:' || url.username || url.password || url.port) return null;
    return sites.find(site => site.domains.some(domain => matchesDomain(url.hostname, domain))) || null;
  } catch { return null; }
}
function isSiteVideo(value) {
  const site = videoSite(value);
  if (!site) return false;
  const url = new URL(value);
  // Short-link hosts accept opaque IDs; main websites need a video route.
  if (['youtu.be', 'b23.tv', 'v.douyin.com', 'vm.tiktok.com', 'vt.tiktok.com', 'dai.ly', 'clips.twitch.tv'].includes(url.hostname))
    return /^\/[\w-]+\/?$/.test(url.pathname);
  return site.paths.some(pattern => new RegExp(pattern).test(url.pathname + url.search));
}
function videoSource(value) {
  if (!isSiteVideo(value)) return null;
  const url = new URL(value);
  if (videoSite(value).id === 'douyin' && /^\d+$/.test(url.searchParams.get('modal_id') || ''))
    return `https://www.douyin.com/video/${url.searchParams.get('modal_id')}`;
  return url.href;
}
module.exports = { sites, videoSite, isSiteVideo, videoSource };
