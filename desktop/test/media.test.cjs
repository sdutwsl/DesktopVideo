const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { youtubeVideo, electronCookie, exportCookies, resolveMedia } = require('../media.cjs');

test('navigation accepts only actual YouTube video addresses', () => {
  assert.equal(youtubeVideo('https://youtu.be/abcdefghijk?t=5'), 'https://www.youtube.com/watch?v=abcdefghijk');
  for (const url of ['https://youtube.com.evil.test/watch?v=abcdefghijk','file:///secret','https://user@youtube.com/watch?v=abcdefghijk','https://youtu.be/abcdefghijk/other']) assert.equal(youtubeVideo(url), null);
});

test('resolver selects separate streams without passing signed URLs to the UI state', async () => {
  let command;
  const processes = { run: async (_exe, args) => { command = args; return JSON.stringify({ title: 'Demo', requested_formats: [
    { vcodec:'vp9', acodec:'none', url:'https://media.example/video', http_headers: { 'User-Agent':'Example' } },
    { vcodec:'none', acodec:'opus', url:'https://media.example/audio' },
  ] }); } };
  const media = await resolveMedia(processes, 'extractor', 'https://www.youtube.com/watch?v=abcdefghijk', { quality:'best', cookies:'cookies.txt' });
  assert.equal(media.audio, 'https://media.example/audio');
  assert.equal(media.userAgent, 'Example');
  assert.ok(command.includes('--cookies'));
  assert.ok(command.includes('--format'));
});

test('cookie export keeps only YouTube domains and private file permissions', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'muralop-cookie-test-'));
  try {
    const file = path.join(directory, 'cookies.txt');
    await exportCookies(file, [
      { domain:'.youtube.com', name:'SID', value:'youtube-session', httpOnly:true },
      { domain:'.google.com', name:'SID', value:'google-session' },
      { domain:'.youtube.com.evil.test', name:'private', value:'other-site' },
    ]);
    const text = await fs.readFile(file, 'utf8');
    assert.match(text, /#HttpOnly_\.youtube.com/);
    assert.ok(!text.includes('google-session') && !text.includes('other-site'));
    if (process.platform !== 'win32') assert.equal((await fs.stat(file)).mode & 0o777, 0o600);
  } finally { await fs.rm(directory, { recursive:true, force:true }); }
});

test('extractor failures become actionable messages', async () => {
  const failing = message => ({ run: async () => { throw new Error(message); } });
  await assert.rejects(resolveMedia(failing('ERROR: [youtube] abcdefghijk: Sign in to confirm you’re not a bot.'), 'x', 'https://www.youtube.com/watch?v=abcdefghijk'),
    /要求登录/);
  await assert.rejects(resolveMedia(failing('ERROR: [youtube] abcdefghijk: Video unavailable'), 'x', 'https://www.youtube.com/watch?v=abcdefghijk'),
    { message: '解析失败：Video unavailable' });
  const abort = Object.assign(new Error('操作已取消'), { name: 'AbortError' });
  await assert.rejects(resolveMedia({ run: async () => { throw abort; } }, 'x', 'https://example.com/v'), { name: 'AbortError' });
});

test('imported cookies follow Chromium prefix rules', () => {
  const hostOnly = electronCookie({ name: '__Host-GAPS', value: 'v', domain: 'accounts.google.com', path: '/a', secure: false, expires: 2e9 });
  assert.equal(hostOnly.domain, undefined);
  assert.equal(hostOnly.path, '/');
  assert.equal(hostOnly.secure, true);
  assert.equal(hostOnly.url, 'https://accounts.google.com/');
  const secure = electronCookie({ name: '__Secure-3PSID', value: 'v', domain: '.youtube.com', path: '/', secure: true, http_only: true });
  assert.equal(secure.domain, '.youtube.com');
  assert.equal(secure.httpOnly, true);
  assert.equal(electronCookie({ name: '__Host-x', value: 'v', domain: '.youtube.com' }), null);
  assert.equal(electronCookie({ name: 'SID', value: 'v', domain: '.example.com' }), null);
});
